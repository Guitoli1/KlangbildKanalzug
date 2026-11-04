#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

//==============================================================================
class RaumAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RaumAudioProcessorEditor (RaumAudioProcessor&);
    ~RaumAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::String name;
        std::unique_ptr<SliderAttachment> attachment;
        int section = 0;
    };

    struct Section { juce::String title; juce::Rectangle<int> bounds; juce::ToggleButton* power; };

    Knob& addKnob (const juce::String& paramID, const juce::String& name, int section);
    void setupSwitch (juce::ToggleButton& b, const juce::String& paramID, juce::Colour lamp);
    void timerCallback() override;
    void renderTextures();
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Justification);

    RaumAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *dMix = nullptr, *dWidth = nullptr, *dDepth = nullptr;
    Knob *eTime = nullptr, *eFb = nullptr, *eTone = nullptr, *eWow = nullptr, *eMix = nullptr;
    Knob *sMix = nullptr, *sDecay = nullptr, *sTone = nullptr, *sDwell = nullptr, *outKnob = nullptr;

    juce::ToggleButton dOn, eOn, sOn, pingPong;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    juce::ComboBox syncBox, presetBox;
    std::unique_ptr<ComboAttachment> syncAttachment;

    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    std::array<Section, 4> sections;
    juce::Rectangle<int> faceplate, presetArea, syncLabelArea, pingPongLabelArea, pilotArea;
    juce::Image tolexImage, faceplateImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RaumAudioProcessorEditor)
};
