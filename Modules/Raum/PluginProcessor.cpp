#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
}

Attr percent() { return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
                              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); }); }

std::unique_ptr<APF> pct (const char* id, const char* name, float def)
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), def, percent());
}

constexpr float syncBeats[] = { 0.0f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 2.0f / 3.0f, 1.0f, 1.5f, 2.0f };
}

juce::StringArray RaumAudioProcessor::syncNames()
{
    return { "Frei (ms)", "1/16", "1/8 T", "1/8", "1/8 punkt.", "1/4 T", "1/4", "1/4 punkt.", "1/2" };
}

juce::AudioProcessorValueTreeState::ParameterLayout RaumAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto doubler = std::make_unique<juce::AudioProcessorParameterGroup> ("doubler", "Doubler", " | ");
    doubler->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "d_on", 1 }, "Doubler An", true));
    doubler->addChild (pct ("d_mix",   "Doubler Mix",   50.0f));
    doubler->addChild (pct ("d_width", "Doubler Breite", 80.0f));
    doubler->addChild (pct ("d_depth", "Doubler Tiefe", 40.0f));
    l.add (std::move (doubler));

    auto echo = std::make_unique<juce::AudioProcessorParameterGroup> ("echo", "Bandecho", " | ");
    echo->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "e_on", 1 }, "Echo An", true));
    echo->addChild (std::make_unique<APF> (juce::ParameterID { "e_time", 1 }, "Echo Zeit", logRange (30.0f, 1500.0f), 380.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    echo->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "e_sync", 1 }, "Echo Sync", syncNames(), 0));
    echo->addChild (std::make_unique<APF> (juce::ParameterID { "e_fb", 1 }, "Echo Feedback",
        juce::NormalisableRange<float> (0.0f, 95.0f, 0.1f), 35.0f, percent()));
    echo->addChild (pct ("e_tone", "Echo Ton", 45.0f));
    echo->addChild (pct ("e_wow",  "Echo Wow & Flutter", 30.0f));
    echo->addChild (pct ("e_mix",  "Echo Mix", 30.0f));
    echo->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "e_pp", 1 }, "Echo Ping-Pong", false));
    l.add (std::move (echo));

    auto spring = std::make_unique<juce::AudioProcessorParameterGroup> ("spring", "Federhall", " | ");
    spring->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "s_on", 1 }, "Hall An", true));
    spring->addChild (pct ("s_dwell", "Hall Dwell", 30.0f));
    spring->addChild (std::make_unique<APF> (juce::ParameterID { "s_decay", 1 }, juce::String (juce::CharPointer_UTF8 ("Hall L\xc3\xa4nge")), logRange (0.5f, 5.0f), 2.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " s"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    spring->addChild (pct ("s_tone", "Hall Ton", 50.0f));
    spring->addChild (pct ("s_mix",  "Hall Mix", 25.0f));
    l.add (std::move (spring));

    l.add (std::make_unique<APF> (juce::ParameterID { "out", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

const std::vector<RaumAudioProcessor::Preset>& RaumAudioProcessor::presets()
{
    static const std::vector<Preset> p = {
        { "Standard", { { "d_on", 1 }, { "d_mix", 50 }, { "d_width", 80 }, { "d_depth", 40 },
                        { "e_on", 1 }, { "e_time", 380 }, { "e_sync", 0 }, { "e_fb", 35 }, { "e_tone", 45 }, { "e_wow", 30 }, { "e_mix", 30 }, { "e_pp", 0 },
                        { "s_on", 1 }, { "s_dwell", 30 }, { "s_decay", 2 }, { "s_tone", 50 }, { "s_mix", 25 }, { "out", 0 } } },
        { "Slapback Rockabilly", { { "d_on", 0 }, { "e_on", 1 }, { "e_time", 110 }, { "e_sync", 0 }, { "e_fb", 12 }, { "e_tone", 55 }, { "e_wow", 20 },
                                   { "e_mix", 40 }, { "e_pp", 0 }, { "s_on", 1 }, { "s_dwell", 25 }, { "s_decay", 1.4f }, { "s_tone", 55 }, { "s_mix", 18 }, { "out", 0 } } },
        { "Surf", { { "d_on", 0 }, { "e_on", 0 }, { "s_on", 1 }, { "s_dwell", 85 }, { "s_decay", 3.2f }, { "s_tone", 75 }, { "s_mix", 60 }, { "out", -1.5f } } },
        { "Breiter Rhythmus", { { "d_on", 1 }, { "d_mix", 75 }, { "d_width", 100 }, { "d_depth", 30 }, { "e_on", 0 },
                                { "s_on", 1 }, { "s_dwell", 20 }, { "s_decay", 1.2f }, { "s_tone", 45 }, { "s_mix", 12 }, { "out", 0 } } },
        { "Ambient Lead", { { "d_on", 1 }, { "d_mix", 45 }, { "d_width", 100 }, { "d_depth", 60 },
                            { "e_on", 1 }, { "e_sync", 4 }, { "e_fb", 48 }, { "e_tone", 40 }, { "e_wow", 35 }, { "e_mix", 32 }, { "e_pp", 1 },
                            { "s_on", 1 }, { "s_dwell", 35 }, { "s_decay", 4.5f }, { "s_tone", 35 }, { "s_mix", 35 }, { "out", -1 } } },
        { "Dub Echo", { { "d_on", 0 }, { "e_on", 1 }, { "e_sync", 6 }, { "e_fb", 72 }, { "e_tone", 20 }, { "e_wow", 60 }, { "e_mix", 45 }, { "e_pp", 1 },
                        { "s_on", 1 }, { "s_dwell", 40 }, { "s_decay", 2.2f }, { "s_tone", 40 }, { "s_mix", 20 }, { "out", -1 } } },
        { "Clean Ballade", { { "d_on", 1 }, { "d_mix", 35 }, { "d_width", 70 }, { "d_depth", 35 },
                             { "e_on", 1 }, { "e_sync", 6 }, { "e_fb", 25 }, { "e_tone", 50 }, { "e_wow", 20 }, { "e_mix", 20 }, { "e_pp", 0 },
                             { "s_on", 1 }, { "s_dwell", 25 }, { "s_decay", 2.6f }, { "s_tone", 50 }, { "s_mix", 25 }, { "out", 0 } } },
        { "Nur Doubler", { { "d_on", 1 }, { "d_mix", 65 }, { "d_width", 90 }, { "d_depth", 45 }, { "e_on", 0 }, { "s_on", 0 }, { "out", 0 } } },
    };
    return p;
}

void RaumAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

const juce::String RaumAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size()) ? juce::String (presets()[(size_t) index].name) : juce::String();
}

//==============================================================================
RaumAudioProcessor::RaumAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildRaum", createLayout())
{
    auto bind = [this] (P& p, const char* id) { p.v = apvts.getRawParameterValue (id); jassert (p.v != nullptr); };
    bind (dOn, "d_on"); bind (dMix, "d_mix"); bind (dWidth, "d_width"); bind (dDepth, "d_depth");
    bind (eOn, "e_on"); bind (eTime, "e_time"); bind (eSync, "e_sync"); bind (eFb, "e_fb");
    bind (eTone, "e_tone"); bind (eWow, "e_wow"); bind (eMix, "e_mix"); bind (ePP, "e_pp");
    bind (sOn, "s_on"); bind (sDwell, "s_dwell"); bind (sDecay, "s_decay"); bind (sTone, "s_tone"); bind (sMix, "s_mix");
    bind (outGain, "out");
}

bool RaumAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

float RaumAudioProcessor::echoTimeMs() const
{
    const int sync = juce::jlimit (0, 8, (int) eSync.get());
    if (sync == 0)
        return eTime.get();

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;
    return (float) juce::jlimit (30.0, 2000.0, 60000.0 / juce::jmax (20.0, bpm) * syncBeats[sync]);
}

void RaumAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    doubler.prepare (sr);
    echo.prepare (sr);
    spring.prepare (sr);
    echo.setTimeMs (echoTimeMs(), true);

    auto init = [this] (juce::SmoothedValue<float>& s, float v) { s.reset (sr, 0.04); s.setCurrentAndTargetValue (v); };
    init (smDOn, dOn.get()); init (smDMix, dMix.get() * 0.01f); init (smDWidth, dWidth.get() * 0.01f); init (smDDepth, dDepth.get() * 0.01f);
    init (smEOn, eOn.get()); init (smEFb, eFb.get() * 0.01f); init (smEWow, eWow.get() * 0.01f); init (smEMix, eMix.get() * 0.01f);
    init (smSOn, sOn.get()); init (smSDwell, sDwell.get() * 0.01f); init (smSMix, sMix.get() * 0.01f);
    init (smOut, juce::Decibels::decibelsToGain (outGain.get()));
    lastETone = lastSDecay = lastSTone = -1;
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void RaumAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2 || inCh == 0)
        return;

    // Zielwerte
    smDOn.setTargetValue (dOn.get()); smDMix.setTargetValue (dMix.get() * 0.01f);
    smDWidth.setTargetValue (dWidth.get() * 0.01f); smDDepth.setTargetValue (dDepth.get() * 0.01f);
    smEOn.setTargetValue (eOn.get()); smEFb.setTargetValue (eFb.get() * 0.01f);
    smEWow.setTargetValue (eWow.get() * 0.01f); smEMix.setTargetValue (eMix.get() * 0.01f);
    smSOn.setTargetValue (sOn.get()); smSDwell.setTargetValue (sDwell.get() * 0.01f); smSMix.setTargetValue (sMix.get() * 0.01f);
    smOut.setTargetValue (juce::Decibels::decibelsToGain (outGain.get()));

    echo.setTimeMs (echoTimeMs());
    if (! juce::exactlyEqual (eTone.get(), lastETone))  { lastETone = eTone.get(); echo.setTone (lastETone * 0.01f); }
    if (! juce::exactlyEqual (sDecay.get(), lastSDecay) || ! juce::exactlyEqual (sTone.get(), lastSTone))
    {
        lastSDecay = sDecay.get(); lastSTone = sTone.get();
        spring.setParams (lastSDecay, lastSTone * 0.01f);
    }
    const bool pingPong = ePP.get() > 0.5f;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const bool monoIn = inCh == 1;
    float peakL = 0, peakR = 0;

    for (int i = 0; i < n; ++i)
    {
        const float in = monoIn ? L[i] : 0.5f * (L[i] + R[i]);

        // 1) Doubler: macht aus Mono ein breites Stereosignal
        float l, r;
        const float dEn = smDOn.getNextValue();
        doubler.process (in, l, r, smDMix.getNextValue(), smDWidth.getNextValue(), smDDepth.getNextValue(), dEn);
        if (! monoIn)
        {
            // bei Stereo-Eingang das Original-Stereobild erhalten, wenn der Doubler aus ist
            l += (1.0f - dEn) * (L[i] - in);
            r += (1.0f - dEn) * (R[i] - in);
        }

        // 2) Bandecho (beim Ausschalten klingen die Wiederholungen aus)
        float el, er;
        echo.process (l, r, el, er, smEFb.getNextValue(), smEWow.getNextValue(), smEMix.getNextValue(), pingPong, smEOn.getNextValue());

        // 3) Federhall
        float sl, sr2;
        spring.process (el, er, sl, sr2, smSDwell.getNextValue(), smSMix.getNextValue(), smSOn.getNextValue());

        const float g = smOut.getNextValue();
        L[i] = sl * g;
        R[i] = sr2 * g;
        vuStateL += (std::abs (L[i]) - vuStateL) * vuCoef;
        vuStateR += (std::abs (R[i]) - vuStateR) * vuCoef;
        peakL = juce::jmax (peakL, std::abs (L[i]));
        peakR = juce::jmax (peakR, std::abs (R[i]));
    }

    vuL.store (vuStateL);
    vuR.store (vuStateR);
    meterL.store (juce::jmax (peakL, meterL.load() * 0.9f));
    meterR.store (juce::jmax (peakR, meterR.load() * 0.9f));
}

//==============================================================================
void RaumAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RaumAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
            apvts.replaceState (tree);
            // Werte einrasten lassen (z. B. Schalter exakt auf 0/1)
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
        }
}

juce::AudioProcessorEditor* RaumAudioProcessor::createEditor() { return new RaumAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RaumAudioProcessor(); }
#endif

