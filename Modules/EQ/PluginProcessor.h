#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "EQDsp.h"

//==============================================================================
// Sammelt Samples im Audio-Thread und gibt alle 1024 Samples einen Block von
// 4096 Samples (75 % Überlappung) an die Oberfläche weiter – ohne Locks.
class SpectrumFifo
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = 1024;

    void reset() { writePos = 0; hopCounter = 0; ring.fill (0.0f); ready.store (false); }

    void push (float sample) noexcept
    {
        ring[(size_t) writePos] = sample;
        writePos = (writePos + 1) & (fftSize - 1);

        if (++hopCounter >= hopSize)
        {
            hopCounter = 0;
            if (! ready.load (std::memory_order_acquire))
            {
                for (int i = 0; i < fftSize; ++i)
                    block[(size_t) i] = ring[(size_t) ((writePos + i) & (fftSize - 1))];
                ready.store (true, std::memory_order_release);
            }
        }
    }

    // dest muss mindestens fftSize * 2 groß sein (für die FFT)
    bool pull (float* dest) noexcept
    {
        if (! ready.load (std::memory_order_acquire))
            return false;
        std::copy (block.begin(), block.end(), dest);
        std::fill (dest + fftSize, dest + fftSize * 2, 0.0f);
        ready.store (false, std::memory_order_release);
        return true;
    }

private:
    std::array<float, fftSize> ring {}, block {};
    int writePos = 0, hopCounter = 0;
    std::atomic<bool> ready { false };
};

//==============================================================================
class KlangbildAudioProcessor : public juce::AudioProcessor
{
public:
    KlangbildAudioProcessor();
    ~KlangbildAudioProcessor() override = default;

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
    static juce::String paramID (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }
    static juce::StringArray bandNames();

    struct BandSettings { float freq, gain, q; bool on; int slope; };
    BandSettings getBandSettings (int band) const;

    juce::AudioProcessorValueTreeState apvts;
    SpectrumFifo preFifo, postFifo;
    std::atomic<double> currentSampleRate { 48000.0 };
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };   // für die VU-Meter

    enum AnalyzerMode { analyzerOff = 0, analyzerPre, analyzerPost, analyzerBoth };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct BandPtrs { std::atomic<float>* freq = nullptr; std::atomic<float>* gain = nullptr;
                      std::atomic<float>* q = nullptr;    std::atomic<float>* on = nullptr;
                      std::atomic<float>* slope = nullptr; };
    std::array<BandPtrs, eq::numBands> ptrs;
    std::atomic<float>* outGainPtr = nullptr;
    std::atomic<float>* analyzerPtr = nullptr;

    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, eq::numBands> smFreq, smQ;
    std::array<juce::SmoothedValue<float>, eq::numBands> smGain;
    juce::SmoothedValue<float> smOut;

    std::array<eq::BandCoeffs, eq::numBands> coeffs;
    std::array<int,  eq::numBands> lastSlope {};
    std::array<bool, eq::numBands> lastOn {};
    std::array<bool, eq::numBands> dirty {};
    eq::BiquadState state[2][eq::numBands][eq::maxStages];

    double sr = 48000.0;
    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlangbildAudioProcessor)
};
