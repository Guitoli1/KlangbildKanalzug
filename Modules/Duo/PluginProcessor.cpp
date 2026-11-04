#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr dbAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
}

constexpr float gateOffDb = -90.0f;
constexpr double normTargetDb = -18.0;   // Ziel-Lautheit beim Angleichen
}

juce::AudioProcessorValueTreeState::ParameterLayout DuoAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "in", 1 }, "Eingang", juce::NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "gate", 1 }, "Noise Gate", juce::NormalisableRange<float> (gateOffDb, -30.0f, 0.1f), gateOffDb,
        Attr().withStringFromValueFunction ([] (float v, int) { return v <= gateOffDb + 0.05f ? juce::String ("Aus") : juce::String (v, 0) + " dB"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.trim().startsWithIgnoreCase ("aus") ? gateOffDb : s.getFloatValue(); })));
    l.add (std::make_unique<APF> (juce::ParameterID { "level_a", 1 }, "Pegel A", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "level_b", 1 }, "Pegel B", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "blend", 1 }, "Mischung A/B", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return "A " + juce::String (juce::roundToInt (100.0f - v)) + " / B " + juce::String (juce::roundToInt (v)); })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.fromLastOccurrenceOf ("B", false, true).getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Modus", juce::StringArray { "Mischen", "Stereo (A links, B rechts)" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "phase_b", 1 }, "Phase B umkehren", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "normalize", 1 }, "Lautheit angleichen", true));
    l.add (std::make_unique<APF> (juce::ParameterID { "out", 1 }, "Ausgang", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    return l;
}

DuoAudioProcessor::DuoAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildDuo", createLayout())
{
    pIn = apvts.getRawParameterValue ("in");           pGate = apvts.getRawParameterValue ("gate");
    pLevelA = apvts.getRawParameterValue ("level_a");  pLevelB = apvts.getRawParameterValue ("level_b");
    pBlend = apvts.getRawParameterValue ("blend");     pMode = apvts.getRawParameterValue ("mode");
    pPhaseB = apvts.getRawParameterValue ("phase_b");  pNorm = apvts.getRawParameterValue ("normalize");
    pOut = apvts.getRawParameterValue ("out");
}

DuoAudioProcessor::~DuoAudioProcessor()
{
    const juce::SpinLock::ScopedLockType lock (slotLock);
    slots[0].reset();
    slots[1].reset();
}

bool DuoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk  = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && in.size() <= out.size();
}

//==============================================================================
bool DuoAudioProcessor::loadModel (int s, const juce::File& file, juce::String& error)
{
    auto fresh = NamSlot::load (file, sr, maxBlock, error);
    {
        const juce::ScopedLock il (infoLock);
        slotErrors[(size_t) s] = fresh ? juce::String() : error;
    }
    if (fresh == nullptr)
    {
        modelVersion++;
        return false;
    }

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        std::swap (slots[(size_t) s], fresh);
    }
    fresh.reset();   // altes Modell außerhalb der Sperre freigeben

    apvts.state.setProperty (s == 0 ? "pathA" : "pathB", file.getFullPathName(), nullptr);
    updateLatency();
    modelVersion++;
    return true;
}

void DuoAudioProcessor::clearModel (int s)
{
    std::unique_ptr<NamSlot> old;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        std::swap (slots[(size_t) s], old);
    }
    old.reset();
    {
        const juce::ScopedLock il (infoLock);
        slotErrors[(size_t) s] = {};
    }
    apvts.state.setProperty (s == 0 ? "pathA" : "pathB", juce::String(), nullptr);
    updateLatency();
    modelVersion++;
}

DuoAudioProcessor::SlotInfo DuoAudioProcessor::getSlotInfo (int s) const
{
    SlotInfo info;
    const juce::SpinLock::ScopedLockType lock (slotLock);
    if (auto* slot = slots[(size_t) s].get())
    {
        info.loaded = true;
        info.name = slot->name;
        info.file = slot->file;
        info.details = juce::String (slot->modelRate / 1000.0, 1) + " kHz";
        if (slot->isResampling())
            info.details << juce::String (juce::CharPointer_UTF8 (" (umgerechnet)"));
        if (slot->hasLoudness)
            info.details << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 Lautheit ")) << juce::String (slot->loudnessDb, 1) << " dB";
    }
    const juce::ScopedLock il (infoLock);
    info.error = slotErrors[(size_t) s];
    return info;
}

void DuoAudioProcessor::updateLatency()
{
    int lat = 0;
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        for (auto& s : slots)
            if (s) lat = juce::jmax (lat, s->getLatency());
    }
    if (lat != getLatencySamples())
        setLatencySamples (lat);
}

//==============================================================================
void DuoAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    inD.assign ((size_t) maxBlock, 0.0);
    outA.assign ((size_t) maxBlock, 0.0);
    outB.assign ((size_t) maxBlock, 0.0);

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        for (auto& s : slots)
            if (s) s->prepare (sr, maxBlock);
    }
    for (auto& d : align)
        d.prepare ((int) (sr * 0.02));
    updateLatency();

    auto init = [this] (juce::SmoothedValue<float>& s, float v) { s.reset (sr, 0.03); s.setCurrentAndTargetValue (v); };
    init (smIn, juce::Decibels::decibelsToGain (pIn->load()));
    init (smA, juce::Decibels::decibelsToGain (pLevelA->load()));
    init (smB, juce::Decibels::decibelsToGain (pLevelB->load()));
    init (smBlend, pBlend->load() * 0.01f);
    init (smOut, juce::Decibels::decibelsToGain (pOut->load()));

    gateEnv = 0; gateGain = 1; gateHold = 0; gateIsOpen = true;
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void DuoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    if (inCh == 0 || outCh == 0)
        return;

    smIn.setTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    smBlend.setTargetValue (pBlend->load() * 0.01f);
    smOut.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    const bool stereoMode = pMode->load() > 0.5f && outCh > 1;
    const bool normalize = pNorm->load() > 0.5f;
    const float phaseB = pPhaseB->load() > 0.5f ? -1.0f : 1.0f;
    const float gateDb = pGate->load();
    const bool gateOn = gateDb > gateOffDb + 0.05f;
    const float gateThr = juce::Decibels::decibelsToGain (gateDb);
    const float gateRel = std::exp (-1.0f / (0.05f * (float) sr));
    const float gainAtt = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
    const float gainRel = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
    const int holdSamples = (int) (0.05 * sr);

    float* L = buffer.getWritePointer (0);
    float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inR = inCh > 1 ? buffer.getReadPointer (1) : nullptr;

    const juce::SpinLock::ScopedTryLockType lock (slotLock);
    NamSlot* sa = lock.isLocked() ? slots[0].get() : nullptr;
    NamSlot* sb = lock.isLocked() ? slots[1].get() : nullptr;
    const int latA = sa ? sa->getLatency() : 0, latB = sb ? sb->getLatency() : 0;
    const int target = juce::jmax (latA, latB);

    auto normGain = [&] (NamSlot* s) { return (normalize && s && s->hasLoudness) ? (float) juce::Decibels::decibelsToGain (normTargetDb - s->loudnessDb) : 1.0f; };
    smA.setTargetValue (juce::Decibels::decibelsToGain (pLevelA->load()) * normGain (sa));
    smB.setTargetValue (juce::Decibels::decibelsToGain (pLevelB->load()) * normGain (sb) * phaseB);

    float peakL = 0, peakR = 0;

    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);

        for (int i = 0; i < n; ++i)
        {
            const float x = inR ? 0.5f * (L[pos + i] + inR[pos + i]) : L[pos + i];
            inD[(size_t) i] = (double) (x * smIn.getNextValue());
        }

        if (sa) sa->process (inD.data(), outA.data(), n); else std::copy (inD.begin(), inD.begin() + n, outA.begin());
        if (sb) sb->process (inD.data(), outB.data(), n); else std::copy (inD.begin(), inD.begin() + n, outB.begin());

        for (int i = 0; i < n; ++i)
        {
            // Gate-Erkennung am (unbearbeiteten) Eingang
            float g = 1.0f;
            if (gateOn)
            {
                const float level = (float) std::abs (inD[(size_t) i]);
                gateEnv = juce::jmax (level, gateEnv * gateRel);
                if (gateEnv > gateThr) { gateIsOpen = true; gateHold = holdSamples; }
                else if (gateEnv < gateThr * 0.5f && --gateHold <= 0) gateIsOpen = false;
                const float tgt = gateIsOpen ? 1.0f : 0.0f;
                gateGain += (tgt - gateGain) * (tgt > gateGain ? gainAtt : gainRel);
                g = gateGain;
            }

            const float a = (float) align[0].process (outA[(size_t) i], target - latA) * smA.getNextValue();
            const float b = (float) align[1].process (outB[(size_t) i], target - latB) * smB.getNextValue();
            const float bl = smBlend.getNextValue();
            const float og = smOut.getNextValue() * g;

            float l, r;
            if (stereoMode)
            {
                // Stereo: A links, B rechts – der Mischregler wirkt als Balance
                l = a * juce::jmin (1.0f, 2.0f * (1.0f - bl)) * og;
                r = b * juce::jmin (1.0f, 2.0f * bl) * og;
            }
            else
            {
                const float wA = std::cos (bl * juce::MathConstants<float>::halfPi);
                const float wB = std::sin (bl * juce::MathConstants<float>::halfPi);
                l = r = (a * wA + b * wB) * og;
            }

            L[pos + i] = l;
            if (R) R[pos + i] = r;
            vuStateL += (std::abs (l) - vuStateL) * vuCoef;
            vuStateR += (std::abs (r) - vuStateR) * vuCoef;
            peakL = juce::jmax (peakL, std::abs (l));
            peakR = juce::jmax (peakR, std::abs (r));
        }
    }

    if (R == nullptr)
        vuStateR = vuStateL;
    vuL.store (vuStateL);
    vuR.store (vuStateR);
    meterL.store (juce::jmax (peakL, meterL.load()));
    meterR.store (juce::jmax (R ? peakR : peakL, meterR.load()));
    gateOpen.store (! gateOn || gateIsOpen);
}

//==============================================================================
void DuoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DuoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));

    for (int s = 0; s < 2; ++s)
    {
        const juce::String path = tree.getProperty (s == 0 ? "pathA" : "pathB").toString();
        if (path.isEmpty())
        {
            clearModel (s);
            continue;
        }
        juce::String err;
        const juce::File f (path);
        if (! f.existsAsFile())
        {
            clearModel (s);
            const juce::ScopedLock il (infoLock);
            slotErrors[(size_t) s] = "Nicht gefunden: " + f.getFileName();
            apvts.state.setProperty (s == 0 ? "pathA" : "pathB", path, nullptr);   // Pfad merken
            modelVersion++;
            continue;
        }
        loadModel (s, f, err);
    }
}

juce::AudioProcessorEditor* DuoAudioProcessor::createEditor() { return new DuoAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DuoAudioProcessor(); }
#endif

