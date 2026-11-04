#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Interfaces.h"

class KalibratorAudioProcessor : public juce::AudioProcessor
{
public:
    KalibratorAudioProcessor();
    ~KalibratorAudioProcessor() override = default;

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
    static constexpr float noMeasurement = -1000.0f;
    static constexpr double measureSeconds = 10.0;

    // Verstärkung, die sich aus den aktuellen Einstellungen ergibt (dB)
    float computeGainDb() const;

    // Messung der Spitzenpegel (Message-Thread)
    void startMeasurement();
    void cancelMeasurement();
    bool isMeasuring() const { return measuring.load(); }
    float measurementProgress() const;   // 0..1
    float getMeasuredPeakDb() const { return measuredDb.load(); }
    void pollMeasurement();              // vom Editor-Timer: übernimmt fertige Messung in den Zustand

    // NAM-Profil nur auslesen (nicht abspielen): input_level_dbu aus den Metadaten
    struct ProfileInfo { juce::String name, details; bool hasLevel = false; float levelDbu = 0; juce::String error; };
    ProfileInfo readProfile (const juce::File& file);
    ProfileInfo getProfileInfo() const;
    void setInterfacePreset (int index);
    int getInterfacePreset() const;

    std::atomic<int> uiVersion { 0 };
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuIn { 0 }, vuOut { 0 }, peakIn { 0 }, peakOut { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* pMode = nullptr; std::atomic<float>* pIface = nullptr; std::atomic<float>* pRef = nullptr;
    std::atomic<float>* pTarget = nullptr; std::atomic<float>* pTrim = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smGain;

    std::atomic<bool> measuring { false }, measurementDone { false };
    std::atomic<int64_t> measuredSamples { 0 };
    std::atomic<float> measurePeak { 0 }, measuredDb { noMeasurement };
    int64_t measureLength = 480000;

    float vuStateIn = 0, vuStateOut = 0, vuCoef = 0.001f;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KalibratorAudioProcessor)
};
