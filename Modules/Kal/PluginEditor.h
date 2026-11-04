#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

class KalibratorAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       public juce::FileDragAndDropTarget,
                                       private juce::Timer
{
public:
    explicit KalibratorAudioProcessorEditor (KalibratorAudioProcessor&);
    ~KalibratorAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::String name; std::unique_ptr<SliderAttachment> attachment; };
    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    Knob& addKnob (const juce::String& paramID, const juce::String& name);
    void timerCallback() override;
    void chooseProfile();
    void renderTextures();
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Justification);

    KalibratorAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *ifaceKnob = nullptr, *refKnob = nullptr, *targetKnob = nullptr, *trimKnob = nullptr;
    juce::ComboBox interfaceBox;
    juce::TextButton readButton, measureButton;
    juce::ToggleButton modeSwitch;
    std::unique_ptr<ButtonAttachment> modeAttachment;
    std::unique_ptr<juce::FileChooser> chooser;

    VuMeter vuIn { lnf, "EIN" }, vuOut { lnf, "AUS" };

    std::array<Section, 4> sections;
    juce::Rectangle<int> faceplate, plateArea, profileInfoArea, ifaceHintArea, measureTextArea, progressArea,
                         modeLabelArea, gainPlate;
    juce::Image tolexImage, faceplateImage;

    KalibratorAudioProcessor::ProfileInfo profile;
    int lastVersion = -1;
    float lastGain = 1000.0f;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KalibratorAudioProcessorEditor)
};
