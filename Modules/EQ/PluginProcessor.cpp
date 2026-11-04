#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float defaultFreqs[eq::numBands] = { 30.0f, 80.0f, 200.0f, 500.0f, 1500.0f, 5000.0f, 10000.0f, 18000.0f };

juce::String freqToString (float f, int)
{
    if (f >= 1000.0f)
        return juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String (f, f < 100.0f ? 1 : 0) + " Hz";
}

float stringToFreq (const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    auto v = t.getFloatValue();
    if (t.containsChar ('k'))
        v *= 1000.0f;
    return v;
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
}
} // namespace

juce::StringArray KlangbildAudioProcessor::bandNames()
{
    return { "Low Cut", "Low Shelf", "Bell 1", "Bell 2", "Bell 3", "Bell 4", "High Shelf", "High Cut" };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout KlangbildAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto names = bandNames();

    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto type = eq::typeForBand (b);
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1),
                                                                           names[b], " | ");

        group->addChild (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { paramID (b, "on"), 1 }, names[b] + " An", ! eq::isCut (type)));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID (b, "freq"), 1 }, names[b] + " Freq", logRange (20.0f, 20000.0f), defaultFreqs[b],
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (freqToString)
                                                 .withValueFromStringFunction (stringToFreq)));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID (b, "gain"), 1 }, names[b] + " Gain",
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
                .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

        group->addChild (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramID (b, "q"), 1 }, names[b] + " Q", logRange (0.1f, 18.0f), 0.707f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })
                .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));

        if (eq::isCut (type))
            group->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { paramID (b, "slope"), 1 }, names[b] + " Flanke",
                juce::StringArray { "12 dB/Okt", "24 dB/Okt", "36 dB/Okt", "48 dB/Okt" }, 1));

        layout.add (std::move (group));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "analyzer", 1 }, "Analyzer",
        juce::StringArray { "Aus", "Pre", "Post", "Pre + Post" }, (int) analyzerBoth));

    return layout;
}

//==============================================================================
KlangbildAudioProcessor::KlangbildAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildEQ", createLayout())
{
    for (int b = 0; b < eq::numBands; ++b)
    {
        auto& p = ptrs[(size_t) b];
        p.freq = apvts.getRawParameterValue (paramID (b, "freq"));
        p.gain = apvts.getRawParameterValue (paramID (b, "gain"));
        p.q    = apvts.getRawParameterValue (paramID (b, "q"));
        p.on   = apvts.getRawParameterValue (paramID (b, "on"));
        p.slope = apvts.getRawParameterValue (paramID (b, "slope")); // nullptr bei Bell/Shelf
    }
    outGainPtr  = apvts.getRawParameterValue ("output");
    analyzerPtr = apvts.getRawParameterValue ("analyzer");
}

KlangbildAudioProcessor::BandSettings KlangbildAudioProcessor::getBandSettings (int band) const
{
    const auto& p = ptrs[(size_t) band];
    return { p.freq->load(), p.gain->load(), p.q->load(), p.on->load() > 0.5f,
             p.slope != nullptr ? (int) p.slope->load() : 0 };
}

bool KlangbildAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void KlangbildAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    currentSampleRate.store (sampleRate);

    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto s = getBandSettings (b);
        smFreq[(size_t) b].reset (sampleRate, 0.03);  smFreq[(size_t) b].setCurrentAndTargetValue (s.freq);
        smQ[(size_t) b].reset (sampleRate, 0.03);     smQ[(size_t) b].setCurrentAndTargetValue (s.q);
        smGain[(size_t) b].reset (sampleRate, 0.03);  smGain[(size_t) b].setCurrentAndTargetValue (s.gain);
        lastSlope[(size_t) b] = s.slope;
        lastOn[(size_t) b] = s.on;
        coeffs[(size_t) b] = eq::makeBand (eq::typeForBand (b), sr, s.freq, s.gain, s.q, s.slope);
        dirty[(size_t) b] = false;
    }

    smOut.reset (sampleRate, 0.03);
    smOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outGainPtr->load()));

    for (auto& ch : state)
        for (auto& band : ch)
            for (auto& st : band)
                st.reset();

    preFifo.reset();
    postFifo.reset();
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sampleRate)));
    vuStateL = vuStateR = 0;
}

void KlangbildAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (getTotalNumInputChannels(), 2);
    const int numSamples = buffer.getNumSamples();
    for (int ch = numCh; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const int mode = (int) analyzerPtr->load();

    auto pushToFifo = [&] (SpectrumFifo& fifo)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
        for (int i = 0; i < numSamples; ++i)
            fifo.push (0.5f * (l[i] + r[i]));
    };

    if (numCh > 0 && (mode == analyzerPre || mode == analyzerBoth))
        pushToFifo (preFifo);

    // Zielwerte setzen
    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto s = getBandSettings (b);
        const auto i = (size_t) b;
        smFreq[i].setTargetValue (s.freq);
        smGain[i].setTargetValue (s.gain);
        smQ[i].setTargetValue (s.q);

        if (s.slope != lastSlope[i] || (s.on && ! lastOn[i]))
        {
            for (auto& ch : state)
                for (auto& st : ch[b])
                    st.reset();
            dirty[i] = true;
        }
        lastSlope[i] = s.slope;
        lastOn[i] = s.on;
    }
    smOut.setTargetValue (juce::Decibels::decibelsToGain (outGainPtr->load()));

    // In kleinen Blöcken verarbeiten, damit Reglerbewegungen weich klingen
    constexpr int subBlock = 32;
    for (int pos = 0; pos < numSamples; pos += subBlock)
    {
        const int len = juce::jmin (subBlock, numSamples - pos);

        for (int b = 0; b < eq::numBands; ++b)
        {
            const auto i = (size_t) b;
            const bool smoothing = smFreq[i].isSmoothing() || smGain[i].isSmoothing() || smQ[i].isSmoothing();
            const float f = smFreq[i].skip (len), g = smGain[i].skip (len), q = smQ[i].skip (len);

            if (smoothing || dirty[i])
            {
                coeffs[i] = eq::makeBand (eq::typeForBand (b), sr, f, g, q, lastSlope[i]);
                dirty[i] = false;
            }
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer (ch, pos);
            for (int b = 0; b < eq::numBands; ++b)
            {
                if (! lastOn[(size_t) b])
                    continue;
                const auto& bc = coeffs[(size_t) b];
                for (int s = 0; s < bc.numStages; ++s)
                {
                    auto& st = state[ch][b][s];
                    const auto& c = bc.stage[(size_t) s];
                    for (int n = 0; n < len; ++n)
                        data[n] = st.process (c, data[n]);
                }
            }
        }

        const float g0 = smOut.getCurrentValue();
        const float g1 = smOut.skip (len);
        for (int ch = 0; ch < numCh; ++ch)
            buffer.applyGainRamp (ch, pos, len, g0, g1);
    }

    if (numCh > 0 && (mode == analyzerPost || mode == analyzerBoth))
        pushToFifo (postFifo);

    // Pegel für die VU-Meter (gleichgerichteter Mittelwert + Spitze)
    if (numCh > 0)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = numCh > 1 ? buffer.getReadPointer (1) : l;
        float pl = 0, pr = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            vuStateL += (std::abs (l[i]) - vuStateL) * vuCoef;
            vuStateR += (std::abs (r[i]) - vuStateR) * vuCoef;
            pl = juce::jmax (pl, std::abs (l[i]));
            pr = juce::jmax (pr, std::abs (r[i]));
        }
        vuL.store (vuStateL);
        vuR.store (vuStateR);
        meterL.store (juce::jmax (pl, meterL.load()));
        meterR.store (juce::jmax (pr, meterR.load()));
    }
}

//==============================================================================
void KlangbildAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KlangbildAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            // Werte einrasten lassen (z. B. Schalter exakt auf 0/1)
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
        }
}

juce::AudioProcessorEditor* KlangbildAudioProcessor::createEditor()
{
    return new KlangbildAudioProcessorEditor (*this);
}

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KlangbildAudioProcessor();
}
#endif

