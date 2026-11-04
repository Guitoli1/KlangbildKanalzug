#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "NamSlot.h"

class DuoAudioProcessor : public juce::AudioProcessor
{
public:
    DuoAudioProcessor();
    ~DuoAudioProcessor() override;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Modelle (Aufruf vom Message-Thread)
    bool loadModel (int slot, const juce::File& file, juce::String& error);
    void clearModel (int slot);

    struct SlotInfo { bool loaded = false; juce::String name, details, error; juce::File file; };
    SlotInfo getSlotInfo (int slot) const;
    std::atomic<int> modelVersion { 0 };   // ändert sich, wenn ein Modell geladen/entfernt wurde

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };
    std::atomic<bool> gateOpen { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateLatency();

    juce::SpinLock slotLock;                       // schützt slots[] gegenüber dem Audio-Thread
    std::array<std::unique_ptr<NamSlot>, 2> slots;
    mutable juce::CriticalSection infoLock;
    std::array<juce::String, 2> slotErrors;

    std::atomic<float>* pIn = nullptr;   std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pLevelA = nullptr; std::atomic<float>* pLevelB = nullptr;
    std::atomic<float>* pBlend = nullptr; std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pPhaseB = nullptr; std::atomic<float>* pNorm = nullptr;
    std::atomic<float>* pOut = nullptr;

    juce::SmoothedValue<float> smIn, smA, smB, smBlend, smOut;

    // Arbeitspuffer (double, weil NAM mit double rechnet)
    std::vector<double> inD, outA, outB;
    // Laufzeitausgleich, falls nur ein Modell umgerechnet werden muss
    struct Delay
    {
        std::vector<double> buf; int pos = 0;
        void prepare (int maxDelay) { buf.assign ((size_t) juce::jmax (1, maxDelay + 1), 0.0); pos = 0; }
        double process (double x, int d)
        {
            if (d <= 0) return x;
            const int size = (int) buf.size();
            buf[(size_t) pos] = x;
            const int rp = (pos - d + size) % size;
            pos = (pos + 1) % size;
            return buf[(size_t) rp];
        }
    };
    std::array<Delay, 2> align;

    // Noise-Gate (erkennt am Eingang, wirkt am Ausgang)
    float gateEnv = 0, gateGain = 0;
    int gateHold = 0;
    bool gateIsOpen = false;

    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    double sr = 48000.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuoAudioProcessor)
};
