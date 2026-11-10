#include "KanalzugProcessor.h"
#include "KanalzugEditor.h"
#include "ScaledEditor.h"

#include "../Modules/Kal/PluginProcessor.h"
#include "../Modules/Drive/PluginProcessor.h"
#include "../Modules/Duo/PluginProcessor.h"
#include "../Modules/Box/PluginProcessor.h"
#include "../Modules/Opto/PluginProcessor.h"
#include "../Modules/EQ/PluginProcessor.h"
#include "../Modules/Raum/PluginProcessor.h"

const std::array<KanalzugAudioProcessor::ModuleInfo, 7>& KanalzugAudioProcessor::moduleInfos()
{
    static const std::array<ModuleInfo, 7> m = { {
        { "kal",   "KALIBRATOR", true  },
        { "drive", "DRIVE",      false },
        { "duo",   "DUO",        true  },
        { "box",   "BOX",        true  },
        { "opto",  "OPTO",       false },
        { "eq",    "EQ",         false },
        { "raum",  "RAUM",       true  },
    } };
    return m;
}

juce::AudioProcessorValueTreeState::ParameterLayout KanalzugAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < numModules; ++i)
    {
        const auto& mi = moduleInfos()[(size_t) i];
        const juce::String title = juce::String (mi.title).substring (0, 1) + juce::String (mi.title).substring (1).toLowerCase();
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { onParamID (i), 1 }, title + " an", mi.defaultOn));
    }
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "optopre", 1 }, "Opto vor Drive", false));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Ausgang",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

KanalzugAudioProcessor::KanalzugAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildKanalzug", createLayout())
{
    modules[0].processor = std::make_unique<KalibratorAudioProcessor>();
    modules[1].processor = std::make_unique<DriveAudioProcessor>();
    modules[2].processor = std::make_unique<DuoAudioProcessor>();
    modules[3].processor = std::make_unique<BoxAudioProcessor>();
    modules[4].processor = std::make_unique<OptoAudioProcessor>();
    modules[5].processor = std::make_unique<KlangbildAudioProcessor>();
    modules[6].processor = std::make_unique<RaumAudioProcessor>();

    for (int i = 0; i < numModules; ++i)
    {
        modules[(size_t) i].on = apvts.getRawParameterValue (onParamID (i));
        modules[(size_t) i].processor->setPlayConfigDetails (2, 2, sr, maxBlock);
    }
    pOut = apvts.getRawParameterValue ("out");
    optoPre = apvts.getRawParameterValue ("optopre");
    activeOptoPre = optoPre->load() > 0.5f;
    startTimerHz (4);
}

KanalzugAudioProcessor::~KanalzugAudioProcessor()
{
    stopTimer();
}

bool KanalzugAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void KanalzugAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    dry.setSize (2, maxBlock);
    for (auto& m : modules)
    {
        m.processor->setPlayConfigDetails (2, 2, sr, maxBlock);
        m.processor->prepareToPlay (sr, maxBlock);
        m.mix.reset (sr, 0.02);
        m.mix.setCurrentAndTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);
    }
    activeOptoPre = optoPre->load() > 0.5f;
    smSwap.reset (sr, 0.008);
    smSwap.setCurrentAndTargetValue (1.0f);
    smOut.reset (sr, 0.03);
    smOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
    timerCallback();
}

void KanalzugAudioProcessor::releaseResources()
{
    for (auto& m : modules)
        m.processor->releaseResources();
}

void KanalzugAudioProcessor::timerCallback()
{
    int latency = 0;
    for (auto& m : modules)
        if (m.on->load() > 0.5f)
            latency += m.processor->getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void KanalzugAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || getTotalNumInputChannels() == 0)
        return;

    // Mono-Eingang auf beide Kanäle verteilen; die Module arbeiten immer in Stereo
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    juce::AudioBuffer<float> stereo (buffer.getArrayOfWritePointers(), 2, n);
    if (dry.getNumSamples() < n)
        dry.setSize (2, n, false, false, true);

    // Opto-Position umstecken: erst kurz ausblenden, dann im stillen Moment umschalten, wieder einblenden
    const bool wantPre = optoPre->load() > 0.5f;
    if (wantPre != activeOptoPre)
    {
        if (! smSwap.isSmoothing() && smSwap.getCurrentValue() <= 0.0f)
        {
            activeOptoPre = wantPre;
            smSwap.setTargetValue (1.0f);
        }
        else
            smSwap.setTargetValue (0.0f);
    }
    else
        smSwap.setTargetValue (1.0f);

    for (int idx : orderFor (activeOptoPre))
    {
        auto& m = modules[(size_t) idx];
        m.processor->setPlayHead (getPlayHead());
        m.mix.setTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);

        if (! m.mix.isSmoothing())
        {
            if (m.mix.getCurrentValue() < 0.5f)
                continue;                       // Modul aus: wird gar nicht gerechnet
            midi.clear();
            m.processor->processBlock (stereo, midi);
            continue;
        }

        // Beim Ein-/Ausschalten kurz überblenden, damit nichts knackt
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom (ch, 0, stereo, ch, 0, n);
        midi.clear();
        m.processor->processBlock (stereo, midi);
        for (int i = 0; i < n; ++i)
        {
            const float g = m.mix.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = stereo.getWritePointer (ch);
                const float x = dry.getSample (ch, i);
                d[i] = x + g * (d[i] - x);
            }
        }
    }

    smOut.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    float pkL = 0, pkR = 0;
    float* L = stereo.getWritePointer (0);
    float* R = stereo.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = smOut.getNextValue() * smSwap.getNextValue();
        L[i] *= g;
        R[i] *= g;
        vuStateL += (std::abs (L[i]) - vuStateL) * vuCoef;
        vuStateR += (std::abs (R[i]) - vuStateR) * vuCoef;
        pkL = juce::jmax (pkL, std::abs (L[i]));
        pkR = juce::jmax (pkR, std::abs (R[i]));
    }
    vuL.store (vuStateL); vuR.store (vuStateR);
    meterL.store (juce::jmax (pkL, meterL.load())); meterR.store (juce::jmax (pkR, meterR.load()));
}

//==============================================================================
void KanalzugAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("KlangbildKanalzugState");
    root.setAttribute ("version", 1);
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    for (int i = 0; i < numModules; ++i)
    {
        juce::MemoryBlock mb;
        modules[(size_t) i].processor->getStateInformation (mb);
        auto* e = root.createNewChildElement ("Module");
        e->setAttribute ("id", moduleInfos()[(size_t) i].id);
        e->addTextElement (mb.toBase64Encoding());
    }
    copyXmlToBinary (root, destData);
}

void KanalzugAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("KlangbildKanalzugState"))
        return;

    if (auto* params = root->getChildByName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*params));
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
    }

    for (auto* e : root->getChildWithTagNameIterator ("Module"))
    {
        const auto id = e->getStringAttribute ("id");
        for (int i = 0; i < numModules; ++i)
            if (id == moduleInfos()[(size_t) i].id)
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (e->getAllSubText()) && mb.getSize() > 0)
                    modules[(size_t) i].processor->setStateInformation (mb.getData(), (int) mb.getSize());
            }
    }
    for (auto& m : modules)
        m.mix.setCurrentAndTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);
    activeOptoPre = optoPre->load() > 0.5f;
}

juce::AudioProcessorEditor* KanalzugAudioProcessor::createEditor() { return new kb::ScaledEditor (*this, new KanalzugAudioProcessorEditor (*this)); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KanalzugAudioProcessor(); }
