#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RaumDsp.h"

class RaumAudioProcessor : public juce::AudioProcessor
{
public:
    RaumAudioProcessor();
    ~RaumAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 8.0; }

    // Werks-Presets (erscheinen in Logic im Preset-Menü)
    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Preset>& presets();
    int getNumPrograms() override { return (int) presets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::StringArray syncNames();

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> meterL { 0 }, meterR { 0 };   // Spitzenwerte
    std::atomic<float> vuL { 0 }, vuR { 0 };         // gleichgerichteter Mittelwert für die VU-Meter

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float echoTimeMs() const;

    raum::Doubler doubler;
    raum::TapeEcho echo;
    raum::SpringReverb spring;

    struct P { std::atomic<float>* v = nullptr; float get() const { return v->load(); } };
    P dOn, dMix, dWidth, dDepth;
    P eOn, eTime, eSync, eFb, eTone, eWow, eMix, ePP;
    P sOn, sDwell, sDecay, sTone, sMix;
    P outGain;

    juce::SmoothedValue<float> smDOn, smDMix, smDWidth, smDDepth;
    juce::SmoothedValue<float> smEOn, smEFb, smEWow, smEMix;
    juce::SmoothedValue<float> smSOn, smSDwell, smSMix, smOut;
    float lastETone = -1, lastSDecay = -1, lastSTone = -1;

    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    int currentProgram = 0;
    double sr = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RaumAudioProcessor)
};
