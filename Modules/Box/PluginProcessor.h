#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "../EQ/EQDsp.h"

class BoxAudioProcessor : public juce::AudioProcessor
{
public:
    BoxAudioProcessor();
    ~BoxAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Impulsantworten (Aufruf vom Message-Thread)
    bool loadModel (int slot, const juce::File& file, juce::String& error);
    void clearModel (int slot);

    struct SlotInfo { bool loaded = false; juce::String name, details, error; juce::File file; };
    SlotInfo getSlotInfo (int slot) const;
    juce::String getAlignmentText() const;
    std::atomic<int> modelVersion { 0 };

    static juce::String fileFilter() { return "*.wav;*.aif;*.aiff;*.flac"; }
    static bool isIrFile (const juce::String& path);

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuild();   // IRs aufbereiten, ausrichten und an die Faltung übergeben

    struct Slot
    {
        juce::AudioBuffer<float> raw;
        double rawRate = 48000.0;
        juce::File file;
        juce::String name, error;
        bool loaded = false;
    };
    std::array<Slot, 2> slots;
    mutable juce::CriticalSection slotLock;   // schützt slots[] (wird nie im Audio-Thread benutzt)

    std::array<juce::dsp::Convolution, 2> conv;
    std::array<std::atomic<bool>, 2> active { { false, false } };
    juce::String alignmentText;
    std::atomic<bool> lastAlign { true };

    std::atomic<float>* pLevelA = nullptr; std::atomic<float>* pLevelB = nullptr;
    std::atomic<float>* pBlend = nullptr;  std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pPhaseB = nullptr; std::atomic<float>* pAlign = nullptr;
    std::atomic<float>* pLow = nullptr;    std::atomic<float>* pHigh = nullptr;
    std::atomic<float>* pOut = nullptr;

    juce::SmoothedValue<float> smA, smB, smBlend, smOut;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smLow, smHigh;
    eq::Coeffs hpC, lpC;
    eq::BiquadState hpS[2], lpS[2];

    juce::AudioBuffer<float> bufA, bufB;
    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    double sr = 48000.0;
    int maxBlock = 512;
    bool prepared = false;

    JUCE_DECLARE_WEAK_REFERENCEABLE (BoxAudioProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoxAudioProcessor)
};
