#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

class BoxAudioProcessorEditor : public juce::AudioProcessorEditor,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer
{
public:
    explicit BoxAudioProcessorEditor (BoxAudioProcessor&);
    ~BoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::String name; std::unique_ptr<SliderAttachment> attachment; };
    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    Knob& addKnob (const juce::String& paramID, const juce::String& name);
    void setupSwitch (juce::ToggleButton& b, const juce::String& paramID, const juce::String& onText, const juce::String& offText);
    void chooseModel (int slot);
    void timerCallback() override;
    void refreshInfo();
    void renderTextures();
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Justification);
    void drawPlate (juce::Graphics&, juce::Rectangle<float>, const BoxAudioProcessor::SlotInfo&, bool highlight);
    int slotForPosition (int x) const;

    BoxAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *lowKnob = nullptr, *highKnob = nullptr, *levelA = nullptr, *levelB = nullptr, *blendKnob = nullptr, *outKnob = nullptr;
    juce::ToggleButton modeSwitch, phaseSwitch, alignSwitch;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::array<juce::TextButton, 2> loadButtons, clearButtons;
    std::unique_ptr<juce::FileChooser> chooser;

    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    std::array<Section, 5> sections;
    std::array<juce::Rectangle<int>, 2> plates, infoAreas;
    std::array<juce::Rectangle<int>, 3> switchLabels;
    juce::Rectangle<int> faceplate, pilotArea, alignInfoArea;
    juce::Image tolexImage, faceplateImage;

    std::array<BoxAudioProcessor::SlotInfo, 2> info;
    int lastVersion = -1, dragSlot = -1;
    juce::String alignText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoxAudioProcessorEditor)
};
