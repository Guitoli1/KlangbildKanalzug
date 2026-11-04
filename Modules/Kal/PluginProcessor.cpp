#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr unitAttr (const char* unit, int decimals = 1, bool sign = false)
{
    return Attr().withStringFromValueFunction ([=] (float v, int)
                 {
                     return (sign && v > 0.0f ? "+" : "") + juce::String (v, decimals) + " " + unit;
                 })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout KalibratorAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Modus",
                                                         juce::StringArray { "Nach dBu-Werten", "Nach Messung" }, 0));
    l.add (std::make_unique<APF> (juce::ParameterID { "iface", 1 }, "Interface max. Eingang",
                                  juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f, unitAttr ("dBu", 1, true)));
    l.add (std::make_unique<APF> (juce::ParameterID { "ref", 1 }, "Profil-Referenz",
                                  juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f, unitAttr ("dBu", 1, true)));
    l.add (std::make_unique<APF> (juce::ParameterID { "target", 1 }, "Ziel-Spitzenpegel",
                                  juce::NormalisableRange<float> (-30.0f, 0.0f, 0.1f), -10.0f, unitAttr ("dBFS")));
    l.add (std::make_unique<APF> (juce::ParameterID { "trim", 1 }, "Feinabgleich",
                                  juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, unitAttr ("dB", 1, true)));
    return l;
}

KalibratorAudioProcessor::KalibratorAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildKalibrator", createLayout())
{
    pMode = apvts.getRawParameterValue ("mode");
    pIface = apvts.getRawParameterValue ("iface");
    pRef = apvts.getRawParameterValue ("ref");
    pTarget = apvts.getRawParameterValue ("target");
    pTrim = apvts.getRawParameterValue ("trim");
}

bool KalibratorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && in == out;
}

//==============================================================================
float KalibratorAudioProcessor::computeGainDb() const
{
    float g = pTrim->load();
    if (pMode->load() < 0.5f)
        g += pIface->load() - pRef->load();          // digital am Profil = digital am Interface + (Interface-dBu - Profil-dBu)
    else if (measuredDb.load() > noMeasurement + 1.0f)
        g += pTarget->load() - measuredDb.load();    // gemessene Spitzen auf den Zielwert bringen
    return juce::jlimit (-36.0f, 36.0f, g);
}

void KalibratorAudioProcessor::startMeasurement()
{
    measurePeak.store (0.0f);
    measuredSamples.store (0);
    measurementDone.store (false);
    measuring.store (true);
    uiVersion++;
}

void KalibratorAudioProcessor::cancelMeasurement()
{
    measuring.store (false);
    uiVersion++;
}

float KalibratorAudioProcessor::measurementProgress() const
{
    return juce::jlimit (0.0f, 1.0f, (float) measuredSamples.load() / (float) juce::jmax ((int64_t) 1, measureLength));
}

void KalibratorAudioProcessor::pollMeasurement()
{
    if (! measurementDone.exchange (false))
        return;
    const float peak = measurePeak.load();
    const float db = peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : noMeasurement;
    measuredDb.store (db);
    apvts.state.setProperty ("measured", db, nullptr);
    uiVersion++;
}

//==============================================================================
KalibratorAudioProcessor::ProfileInfo KalibratorAudioProcessor::readProfile (const juce::File& file)
{
    ProfileInfo info;
    info.name = file.getFileNameWithoutExtension();

    const auto json = juce::JSON::parse (file);
    if (! json.isObject())
    {
        info.error = "Datei ist kein lesbares NAM-Profil";
    }
    else
    {
        const auto meta = json["metadata"];
        const auto level = meta["input_level_dbu"];
        if (level.isDouble() || level.isInt() || level.isInt64())
        {
            info.hasLevel = true;
            info.levelDbu = (float) (double) level;
            if (auto* p = apvts.getParameter ("ref"))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 24.0f, info.levelDbu)));
                p->endChangeGesture();
            }
        }
        juce::StringArray parts;
        for (auto* key : { "gear_make", "gear_model", "modeled_by" })
            if (meta[key].isString() && meta[key].toString().isNotEmpty())
                parts.add (meta[key].toString());
        info.details = parts.joinIntoString (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    }

    auto& st = apvts.state;
    st.setProperty ("profileName", info.name, nullptr);
    st.setProperty ("profileDetails", info.details, nullptr);
    st.setProperty ("profileHasLevel", info.hasLevel, nullptr);
    st.setProperty ("profileLevel", info.levelDbu, nullptr);
    st.setProperty ("profileError", info.error, nullptr);
    uiVersion++;
    return info;
}

KalibratorAudioProcessor::ProfileInfo KalibratorAudioProcessor::getProfileInfo() const
{
    ProfileInfo info;
    const auto& st = apvts.state;
    info.name = st.getProperty ("profileName").toString();
    info.details = st.getProperty ("profileDetails").toString();
    info.hasLevel = st.getProperty ("profileHasLevel", false);
    info.levelDbu = (float) st.getProperty ("profileLevel", 0.0f);
    info.error = st.getProperty ("profileError").toString();
    return info;
}

void KalibratorAudioProcessor::setInterfacePreset (int index)
{
    index = juce::jlimit (0, kalibrator::numInterfacePresets - 1, index);
    apvts.state.setProperty ("interface", index, nullptr);
    if (index > 0)
        if (auto* p = apvts.getParameter ("iface"))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (kalibrator::interfacePresets[index].dbu));
            p->endChangeGesture();
        }
    uiVersion++;
}

int KalibratorAudioProcessor::getInterfacePreset() const
{
    return juce::jlimit (0, kalibrator::numInterfacePresets - 1, (int) apvts.state.getProperty ("interface", kalibrator::defaultInterfacePreset));
}

//==============================================================================
void KalibratorAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    measureLength = (int64_t) (measureSeconds * sr);
    smGain.reset (sr, 0.05);
    smGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (computeGainDb()));
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateIn = vuStateOut = 0;
}

void KalibratorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    if (numCh == 0)
        return;

    smGain.setTargetValue (juce::Decibels::decibelsToGain (computeGainDb()));

    float pkIn = 0, pkOut = 0;
    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float inL = L[i], inR = R ? R[i] : inL;
        const float a = juce::jmax (std::abs (inL), std::abs (inR));
        const float g = smGain.getNextValue();
        L[i] = inL * g;
        if (R) R[i] = inR * g;
        const float b = a * g;

        vuStateIn  += (a - vuStateIn) * vuCoef;
        vuStateOut += (b - vuStateOut) * vuCoef;
        pkIn = juce::jmax (pkIn, a);
        pkOut = juce::jmax (pkOut, b);
    }

    if (measuring.load())
    {
        measurePeak.store (juce::jmax (measurePeak.load(), pkIn));
        const auto done = measuredSamples.fetch_add (n) + n;
        if (done >= measureLength)
        {
            measuring.store (false);
            measurementDone.store (true);
        }
    }

    vuIn.store (vuStateIn);
    vuOut.store (vuStateOut);
    peakIn.store (juce::jmax (pkIn, peakIn.load()));
    peakOut.store (juce::jmax (pkOut, peakOut.load()));
}

//==============================================================================
void KalibratorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("measured", measuredDb.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KalibratorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
    measuredDb.store ((float) tree.getProperty ("measured", noMeasurement));
    uiVersion++;
}

juce::AudioProcessorEditor* KalibratorAudioProcessor::createEditor() { return new KalibratorAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new KalibratorAudioProcessor(); }
#endif

