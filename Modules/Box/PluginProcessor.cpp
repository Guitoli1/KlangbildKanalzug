#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "IrTools.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr dbAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
}

Attr hzAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
                 {
                     return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
                 })
                 .withValueFromStringFunction ([] (const juce::String& s)
                 {
                     auto v = s.getFloatValue();
                     return s.containsIgnoreCase ("k") ? v * 1000.0f : v;
                 });
}

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
}

constexpr int maxAlignMs = 3;   // so weit wird beim Ausrichten gesucht
}

juce::AudioProcessorValueTreeState::ParameterLayout BoxAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "level_a", 1 }, "Pegel A", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "level_b", 1 }, "Pegel B", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "blend", 1 }, "Mischung A/B", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return "A " + juce::String (juce::roundToInt (100.0f - v)) + " / B " + juce::String (juce::roundToInt (v)); })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.fromLastOccurrenceOf ("B", false, true).getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Modus",
        juce::StringArray { "Mischen", "Stereo (links A, rechts B)" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "phase_b", 1 }, "Phase B umkehren", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "align", 1 }, "Automatisch ausrichten", true));
    l.add (std::make_unique<APF> (juce::ParameterID { "lowcut", 1 }, "Low Cut", logRange (20.0f, 400.0f), 70.0f, hzAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "highcut", 1 }, "High Cut", logRange (2000.0f, 20000.0f), 9000.0f, hzAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "out", 1 }, "Ausgang", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, dbAttr()));
    return l;
}

BoxAudioProcessor::BoxAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildBox", createLayout())
{
    pLevelA = apvts.getRawParameterValue ("level_a"); pLevelB = apvts.getRawParameterValue ("level_b");
    pBlend = apvts.getRawParameterValue ("blend");    pMode = apvts.getRawParameterValue ("mode");
    pPhaseB = apvts.getRawParameterValue ("phase_b"); pAlign = apvts.getRawParameterValue ("align");
    pLow = apvts.getRawParameterValue ("lowcut");     pHigh = apvts.getRawParameterValue ("highcut");
    pOut = apvts.getRawParameterValue ("out");
}

bool BoxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk  = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && in.size() <= out.size();
}

bool BoxAudioProcessor::isIrFile (const juce::String& path)
{
    for (auto ext : { ".wav", ".aif", ".aiff", ".flac" })
        if (path.endsWithIgnoreCase (ext))
            return true;
    return false;
}

//==============================================================================
bool BoxAudioProcessor::loadModel (int s, const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));

    {
        const juce::ScopedLock sl (slotLock);
        auto& slot = slots[(size_t) s];
        if (reader == nullptr || reader->lengthInSamples <= 0)
        {
            error = "Datei konnte nicht gelesen werden (WAV, AIFF oder FLAC)";
            slot.error = error;
        }
        else
        {
            const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 2.0));
            slot.raw.setSize ((int) juce::jmin (2u, reader->numChannels), len);
            reader->read (&slot.raw, 0, len, 0, true, slot.raw.getNumChannels() > 1);
            slot.rawRate = reader->sampleRate;
            slot.file = file;
            slot.name = file.getFileNameWithoutExtension();
            slot.loaded = true;
            slot.error = {};
            apvts.state.setProperty (s == 0 ? "pathA" : "pathB", file.getFullPathName(), nullptr);
        }
    }
    rebuild();
    modelVersion++;
    return error.isEmpty();
}

void BoxAudioProcessor::clearModel (int s)
{
    {
        const juce::ScopedLock sl (slotLock);
        slots[(size_t) s] = Slot();
        apvts.state.setProperty (s == 0 ? "pathA" : "pathB", juce::String(), nullptr);
    }
    rebuild();
    modelVersion++;
}

void BoxAudioProcessor::rebuild()
{
    const juce::ScopedLock sl (slotLock);
    const bool align = pAlign->load() > 0.5f;
    lastAlign.store (align);

    std::array<juce::AudioBuffer<float>, 2> irs;
    for (int s = 0; s < 2; ++s)
        if (slots[(size_t) s].loaded)
        {
            irs[(size_t) s] = irtools::prepare (slots[(size_t) s].raw, slots[(size_t) s].rawRate, sr);
            irtools::normalise (irs[(size_t) s]);
        }

    alignmentText = {};
    if (align && slots[0].loaded && slots[1].loaded)
    {
        const auto al = irtools::findAlignment (irs[0], irs[1], (int) (sr * maxAlignMs / 1000.0));
        if (al.lag > 0)      irtools::advance (irs[1], al.lag);   // B kommt später -> B vorziehen
        else if (al.lag < 0) irtools::advance (irs[0], -al.lag);  // A kommt später -> A vorziehen
        if (al.inverted)
            irs[1].applyGain (-1.0f);

        const double ms = std::abs (al.lag) * 1000.0 / sr;
        juce::String t;
        if (al.lag == 0) t = "Bereits zeitgleich";
        else             t = juce::String (al.lag > 0 ? "B" : "A") + " um " + juce::String (ms, 2).replaceCharacter ('.', ',') + " ms vorgezogen";
        if (al.inverted) t << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 Phase B gedreht"));
        alignmentText = t;
    }

    for (int s = 0; s < 2; ++s)
    {
        if (slots[(size_t) s].loaded)
        {
            conv[(size_t) s].loadImpulseResponse (std::move (irs[(size_t) s]), sr,
                                                  juce::dsp::Convolution::Stereo::no,
                                                  juce::dsp::Convolution::Trim::no,
                                                  juce::dsp::Convolution::Normalise::no);
            active[(size_t) s].store (true);
        }
        else
        {
            active[(size_t) s].store (false);
        }
    }
}

BoxAudioProcessor::SlotInfo BoxAudioProcessor::getSlotInfo (int s) const
{
    const juce::ScopedLock sl (slotLock);
    const auto& slot = slots[(size_t) s];
    SlotInfo info;
    info.loaded = slot.loaded;
    info.name = slot.name;
    info.file = slot.file;
    info.error = slot.error;
    if (slot.loaded)
    {
        const double ms = slot.raw.getNumSamples() * 1000.0 / slot.rawRate;
        info.details = juce::String (juce::roundToInt (ms)) + " ms " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 "))
                     + juce::String (slot.rawRate / 1000.0, 1).replaceCharacter ('.', ',') + " kHz";
        if (ms > irtools::maxSeconds * 1000.0)
            info.details << " (auf " << juce::roundToInt (irtools::maxSeconds * 1000.0) << " ms gekürzt)";
    }
    return info;
}

juce::String BoxAudioProcessor::getAlignmentText() const
{
    const juce::ScopedLock sl (slotLock);
    return alignmentText;
}

//==============================================================================
void BoxAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = ! prepared || std::abs (sampleRate - sr) > 0.5;
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    bufA.setSize (1, maxBlock);
    bufB.setSize (1, maxBlock);

    const juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (auto& c : conv)
    {
        c.prepare (spec);
        c.reset();
    }
    prepared = true;
    if (rateChanged)
        rebuild();
    else
        for (int s = 0; s < 2; ++s)
            active[(size_t) s].store (slots[(size_t) s].loaded);

    auto init = [this] (juce::SmoothedValue<float>& v, float x) { v.reset (sr, 0.03); v.setCurrentAndTargetValue (x); };
    init (smA, juce::Decibels::decibelsToGain (pLevelA->load()));
    init (smB, juce::Decibels::decibelsToGain (pLevelB->load()));
    init (smBlend, pBlend->load() * 0.01f);
    init (smOut, juce::Decibels::decibelsToGain (pOut->load()));
    smLow.reset (sr, 0.05);  smLow.setCurrentAndTargetValue (pLow->load());
    smHigh.reset (sr, 0.05); smHigh.setCurrentAndTargetValue (pHigh->load());
    hpC = eq::makeHighPass (sr, smLow.getTargetValue(), 0.7071);
    lpC = eq::makeLowPass (sr, std::min ((double) smHigh.getTargetValue(), sr * 0.45), 0.7071);
    for (int c = 0; c < 2; ++c) { hpS[c].reset(); lpS[c].reset(); }

    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void BoxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    if (inCh == 0 || outCh == 0)
        return;

    // "Automatisch ausrichten" umgeschaltet: neu aufbereiten (außerhalb des Audio-Threads)
    if ((pAlign->load() > 0.5f) != lastAlign.load())
    {
        lastAlign.store (pAlign->load() > 0.5f);
        juce::MessageManager::callAsync ([safe = juce::WeakReference<BoxAudioProcessor> (this)]
        {
            if (safe != nullptr) { safe->rebuild(); safe->modelVersion++; }
        });
    }

    const bool stereoMode = pMode->load() > 0.5f && outCh > 1;
    const float phaseB = pPhaseB->load() > 0.5f ? -1.0f : 1.0f;
    smA.setTargetValue (juce::Decibels::decibelsToGain (pLevelA->load()));
    smB.setTargetValue (juce::Decibels::decibelsToGain (pLevelB->load()) * phaseB);
    smBlend.setTargetValue (pBlend->load() * 0.01f);
    smOut.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    smLow.setTargetValue (pLow->load());
    smHigh.setTargetValue (pHigh->load());

    float* L = buffer.getWritePointer (0);
    float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    const float* inR = inCh > 1 ? buffer.getReadPointer (1) : nullptr;
    const bool actA = active[0].load(), actB = active[1].load();
    const int nCh = stereoMode ? 2 : 1;
    float peakL = 0, peakR = 0;

    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);
        float* a = bufA.getWritePointer (0);
        float* b = bufB.getWritePointer (0);

        for (int i = 0; i < n; ++i)
        {
            const float l = L[pos + i], r = inR ? inR[pos + i] : l;
            if (stereoMode) { a[i] = l; b[i] = r; }        // passt zum Stereo-Modus von Klangbild Duo
            else            { a[i] = b[i] = 0.5f * (l + r); }
        }

        auto convolve = [n] (juce::dsp::Convolution& c, juce::AudioBuffer<float>& buf)
        {
            juce::dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), 1, (size_t) n);
            c.process (juce::dsp::ProcessContextReplacing<float> (block));
        };
        if (actA) convolve (conv[0], bufA);
        if (actB) convolve (conv[1], bufB);

        // Filter (Koeffizienten folgen den geglätteten Reglern)
        const float lowHz = smLow.skip (n), highHz = smHigh.skip (n);
        hpC = eq::makeHighPass (sr, lowHz, 0.7071);
        lpC = eq::makeLowPass (sr, std::min ((double) highHz, sr * 0.45), 0.7071);

        for (int i = 0; i < n; ++i)
        {
            const float ga = smA.getNextValue(), gb = smB.getNextValue(), bl = smBlend.getNextValue(), og = smOut.getNextValue();
            float l, r;
            if (stereoMode)
            {
                l = a[i] * ga * juce::jmin (1.0f, 2.0f * (1.0f - bl));
                r = b[i] * gb * juce::jmin (1.0f, 2.0f * bl);
            }
            else
            {
                const float wA = std::cos (bl * juce::MathConstants<float>::halfPi);
                const float wB = std::sin (bl * juce::MathConstants<float>::halfPi);
                l = r = a[i] * ga * wA + b[i] * gb * wB;
            }

            l = lpS[0].process (lpC, hpS[0].process (hpC, l)) * og;
            if (nCh > 1) r = lpS[1].process (lpC, hpS[1].process (hpC, r)) * og;
            else         r = l;

            L[pos + i] = l;
            if (R) R[pos + i] = r;
            vuStateL += (std::abs (l) - vuStateL) * vuCoef;
            vuStateR += (std::abs (r) - vuStateR) * vuCoef;
            peakL = juce::jmax (peakL, std::abs (l));
            peakR = juce::jmax (peakR, std::abs (r));
        }
    }

    vuL.store (vuStateL);
    vuR.store (R ? vuStateR : vuStateL);
    meterL.store (juce::jmax (peakL, meterL.load()));
    meterR.store (juce::jmax (R ? peakR : peakL, meterR.load()));
}

//==============================================================================
void BoxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BoxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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
        if (path.isEmpty()) { clearModel (s); continue; }
        const juce::File f (path);
        if (! f.existsAsFile())
        {
            clearModel (s);
            const juce::ScopedLock sl (slotLock);
            slots[(size_t) s].error = "Nicht gefunden: " + f.getFileName();
            apvts.state.setProperty (s == 0 ? "pathA" : "pathB", path, nullptr);
            modelVersion++;
            continue;
        }
        juce::String err;
        loadModel (s, f, err);
    }
}

juce::AudioProcessorEditor* BoxAudioProcessor::createEditor() { return new BoxAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BoxAudioProcessor(); }
#endif

