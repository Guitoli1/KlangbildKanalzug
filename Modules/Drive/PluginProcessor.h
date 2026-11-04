#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DriveDsp.h"

class DriveAudioProcessor : public juce::AudioProcessor
{
public:
    DriveAudioProcessor();
    ~DriveAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Klangbild Drive"; }
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

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    std::array<drive::Channel, 2> channels;
    juce::AudioBuffer<float> dryBuffer;
    std::array<std::vector<float>, 2> dryDelay;   // gleicht die Latenz des Oversamplings aus
    int dryPos = 0;
    juce::SmoothedValue<float> smLevel, smOn;

    std::atomic<float>* pDrive = nullptr; std::atomic<float>* pTone = nullptr; std::atomic<float>* pLevel = nullptr;
    std::atomic<float>* pMode = nullptr;  std::atomic<float>* pOn = nullptr;

    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    int maxBlock = 512, latency = 0;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveAudioProcessor)
};
