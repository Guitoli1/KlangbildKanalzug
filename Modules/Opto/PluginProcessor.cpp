#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr pct()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout OptoAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "pr", 1 }, "Peak Reduction", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f, pct()));
    l.add (std::make_unique<APF> (juce::ParameterID { "gain", 1 }, "Gain", juce::NormalisableRange<float> (-10.0f, 30.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "limit", 1 }, "Limit", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "emphasis", 1 }, "Emphasis", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pct()));
    l.add (std::make_unique<APF> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "tube", 1 }, juce::String (juce::CharPointer_UTF8 ("R\xc3\xb6hre")), true));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "meter", 1 }, "Anzeige", juce::StringArray { "Pegelreduktion", "Ausgang" }, 0));
    return l;
}

const std::vector<OptoAudioProcessor::Preset>& OptoAudioProcessor::presets()
{
    static const std::vector<Preset> p = {
        { "Standard",            { { "pr", 40 }, { "gain", 0 },  { "limit", 0 }, { "emphasis", 0 },  { "mix", 100 }, { "tube", 1 } } },
        { "Clean-Gitarre sanft", { { "pr", 45 }, { "gain", 3 },  { "limit", 0 }, { "emphasis", 20 }, { "mix", 100 }, { "tube", 1 } } },
        { "Funk-Rhythmus",       { { "pr", 62 }, { "gain", 6 },  { "limit", 0 }, { "emphasis", 35 }, { "mix", 85 },  { "tube", 1 } } },
        { "Bass gleichmäßig",    { { "pr", 55 }, { "gain", 5 },  { "limit", 0 }, { "emphasis", 0 },  { "mix", 100 }, { "tube", 1 } } },
        { "Gesang",              { { "pr", 50 }, { "gain", 4 },  { "limit", 0 }, { "emphasis", 50 }, { "mix", 100 }, { "tube", 1 } } },
        { "Parallel dicht",      { { "pr", 80 }, { "gain", 12 }, { "limit", 1 }, { "emphasis", 10 }, { "mix", 40 },  { "tube", 1 } } },
        { "Limiter",             { { "pr", 60 }, { "gain", 6 },  { "limit", 1 }, { "emphasis", 0 },  { "mix", 100 }, { "tube", 0 } } },
    };
    return p;
}

void OptoAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

const juce::String OptoAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size())
               ? juce::String (juce::CharPointer_UTF8 (presets()[(size_t) index].name)) : juce::String();
}

OptoAudioProcessor::OptoAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildOpto", createLayout())
{
    pPR = apvts.getRawParameterValue ("pr");      pGain = apvts.getRawParameterValue ("gain");
    pLimit = apvts.getRawParameterValue ("limit"); pEmph = apvts.getRawParameterValue ("emphasis");
    pMix = apvts.getRawParameterValue ("mix");    pTube = apvts.getRawParameterValue ("tube");
}

bool OptoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && in == out;
}

void OptoAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    comp.prepare (sr);
    for (auto& t : tube) t.prepare (sr);
    lastEmph = -1.0f;
    smGain.reset (sr, 0.03); smGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    smMix.reset (sr, 0.03);  smMix.setCurrentAndTargetValue (pMix->load() * 0.01f);
    smTube.reset (sr, 0.03); smTube.setCurrentAndTargetValue (pTube->load());
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuState = 0;
}

void OptoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    if (numCh == 0)
        return;

    comp.setParams (pPR->load(), pLimit->load() > 0.5f);
    if (! juce::exactlyEqual (pEmph->load(), lastEmph))
    {
        lastEmph = pEmph->load();
        comp.setEmphasis (lastEmph * 0.01f);
    }
    smGain.setTargetValue (juce::Decibels::decibelsToGain (pGain->load()));
    smMix.setTargetValue (pMix->load() * 0.01f);
    smTube.setTargetValue (pTube->load());

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float maxGr = 0, pk = 0;

    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i], dr = R ? R[i] : dl;
        const float gr = (float) comp.process (0.5f * (dl + dr));   // gemeinsame Regelung (Stereo gekoppelt)
        maxGr = juce::jmax (maxGr, gr);
        const float g = juce::Decibels::decibelsToGain (-gr) * smGain.getNextValue();
        const float mix = smMix.getNextValue(), tb = smTube.getNextValue();

        auto stage = [&] (float dry, opto::Tube& t)
        {
            float wet = dry * g;
            if (tb > 0.001f)
                wet = wet + tb * (t.process (wet) - wet);
            return dry + mix * (wet - dry);
        };
        L[i] = stage (dl, tube[0]);
        if (R) R[i] = stage (dr, tube[1]);

        const float o = juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
        vuState += (o - vuState) * vuCoef;
        pk = juce::jmax (pk, o);
    }

    grDb.store (maxGr);
    vuOut.store (vuState);
    peakOut.store (juce::jmax (pk, peakOut.load()));
}

void OptoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OptoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
}

juce::AudioProcessorEditor* OptoAudioProcessor::createEditor() { return new OptoAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OptoAudioProcessor(); }
#endif

