#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ScaledEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr dial()   // Anzeige 0 … 10 wie auf einem Pedal
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v / 10.0f, 1); })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() * 10.0f; });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout DriveAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "drive", 1 }, "Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, dial()));
    l.add (std::make_unique<APF> (juce::ParameterID { "tone", 1 }, "Ton", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, dial()));
    l.add (std::make_unique<APF> (juce::ParameterID { "level", 1 }, "Pegel", juce::NormalisableRange<float> (-20.0f, 12.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Charakter",
        juce::StringArray { juce::String (juce::CharPointer_UTF8 ("Gr\xc3\xbcn (Mitten)")), "Klar (transparent)" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "An", true));
    return l;
}

DriveAudioProcessor::DriveAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildDrive", createLayout())
{
    pDrive = apvts.getRawParameterValue ("drive"); pTone = apvts.getRawParameterValue ("tone");
    pLevel = apvts.getRawParameterValue ("level"); pMode = apvts.getRawParameterValue ("mode");
    pOn = apvts.getRawParameterValue ("on");
}

bool DriveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && in == out;
}

void DriveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    oversampling.initProcessing ((size_t) maxBlock);
    oversampling.reset();
    for (auto& c : channels) c.prepare (sr * 4.0);
    latency = juce::roundToInt (oversampling.getLatencyInSamples());
    setLatencySamples (latency);
    dryBuffer.setSize (2, maxBlock);
    for (auto& d : dryDelay) d.assign ((size_t) juce::jmax (1, latency), 0.0f);
    dryPos = 0;
    smLevel.reset (sr, 0.03); smLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pLevel->load()));
    smOn.reset (sr, 0.015);   smOn.setCurrentAndTargetValue (pOn->load());
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void DriveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), 2);
    const int total = buffer.getNumSamples();
    if (numCh == 0)
        return;

    const bool clear = pMode->load() > 0.5f;
    for (auto& c : channels)
        c.setParams (pDrive->load() * 0.01f, pTone->load() * 0.01f, clear);
    smLevel.setTargetValue (juce::Decibels::decibelsToGain (pLevel->load()));
    smOn.setTargetValue (pOn->load() > 0.5f ? 1.0f : 0.0f);

    float pkL = 0, pkR = 0;
    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);
        for (int ch = 0; ch < numCh; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, pos, n);

        float* chans[2] = { buffer.getWritePointer (0, pos), numCh > 1 ? buffer.getWritePointer (1, pos) : nullptr };
        juce::dsp::AudioBlock<float> block (chans, (size_t) numCh, (size_t) n);
        auto up = oversampling.processSamplesUp (block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            float* d = up.getChannelPointer (ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = channels[ch].process (d[i]);
        }
        oversampling.processSamplesDown (block);

        // Fußschalter: weich zwischen Effekt und unbearbeitetem Signal überblenden
        for (int i = 0; i < n; ++i)
        {
            const float lvl = smLevel.getNextValue(), on = smOn.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch, pos);
                float dry = dryBuffer.getSample (ch, i);
                if (latency > 0)
                {
                    auto& dl = dryDelay[(size_t) ch];
                    std::swap (dry, dl[(size_t) dryPos]);   // liefert das Sample von vor 'latency' Samples
                }
                d[i] = dry + on * (d[i] * lvl - dry);
            }
            if (latency > 0) dryPos = (dryPos + 1) % latency;
            const float l = std::abs (buffer.getSample (0, pos + i));
            const float r = numCh > 1 ? std::abs (buffer.getSample (1, pos + i)) : l;
            vuStateL += (l - vuStateL) * vuCoef;
            vuStateR += (r - vuStateR) * vuCoef;
            pkL = juce::jmax (pkL, l);
            pkR = juce::jmax (pkR, r);
        }
    }
    vuL.store (vuStateL); vuR.store (vuStateR);
    meterL.store (juce::jmax (pkL, meterL.load())); meterR.store (juce::jmax (pkR, meterR.load()));
}

void DriveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DriveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
}

juce::AudioProcessorEditor* DriveAudioProcessor::createEditor() { 
#ifdef KB_EMBEDDED
    return new DriveAudioProcessorEditor (*this);
#else
    return new kb::ScaledEditor (*this, new DriveAudioProcessorEditor (*this));
#endif
 }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DriveAudioProcessor(); }
#endif
