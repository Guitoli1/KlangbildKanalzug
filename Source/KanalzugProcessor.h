#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Der Kanalzug verkettet alle Klangbild-Plugins als Module:
// Kalibrator → Drive → Duo → Box → Opto → EQ → Raum
// wahlweise mit dem Opto vor dem Drive (Kompressor vor dem Amp):
// Kalibrator → Opto → Drive → Duo → Box → EQ → Raum
class KanalzugAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    KanalzugAudioProcessor();
    ~KanalzugAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    struct ModuleInfo { const char* id; const char* title; bool defaultOn; };
    static const std::array<ModuleInfo, 7>& moduleInfos();
    static constexpr int numModules = 7;

    juce::AudioProcessor& getModule (int index) { return *modules[(size_t) index].processor; }
    bool isModuleOn (int index) const { return modules[(size_t) index].on->load() > 0.5f; }
    static constexpr int optoIndex = 4;
    bool isOptoBeforeDrive() const { return optoPre->load() > 0.5f; }
    // Reihenfolge der Module im Signalweg (Indizes in moduleInfos)
    static std::array<int, 7> orderFor (bool optoBeforeDrive)
    {
        return optoBeforeDrive ? std::array<int, 7> { 0, 4, 1, 2, 3, 5, 6 } : std::array<int, 7> { 0, 1, 2, 3, 4, 5, 6 };
    }
    static juce::String onParamID (int index) { return juce::String ("on_") + moduleInfos()[(size_t) index].id; }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // Latenz der Module an den Host melden

    struct Module
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        std::atomic<float>* on = nullptr;
        juce::SmoothedValue<float> mix;
    };
    std::array<Module, numModules> modules;

    juce::AudioBuffer<float> dry;
    juce::MidiBuffer midi;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* optoPre = nullptr;
    bool activeOptoPre = false;              // im Audio-Thread gültige Reihenfolge
    juce::SmoothedValue<float> smSwap;       // kurzes Ab-/Aufblenden beim Umstecken
    juce::SmoothedValue<float> smOut;
    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    double sr = 48000.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KanalzugAudioProcessor)
};
