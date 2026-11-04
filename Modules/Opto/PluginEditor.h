#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

class OptoAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OptoAudioProcessorEditor (OptoAudioProcessor&);
    ~OptoAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::String name; std::unique_ptr<SliderAttachment> attachment; bool big = false; };

    Knob& addKnob (const juce::String& paramID, const juce::String& name, bool big);
    void setupSwitch (juce::ToggleButton& b, const juce::String& paramID, const juce::String& onText, const juce::String& offText, juce::Colour lamp);
    void timerCallback() override;
    void renderTextures();
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Justification);

    OptoAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *prKnob = nullptr, *gainKnob = nullptr, *emphKnob = nullptr, *mixKnob = nullptr;
    juce::ToggleButton limitSwitch, tubeSwitch, meterSwitch;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    juce::ComboBox presetBox;

    VuMeter vu { lnf, "GR" };

    juce::Rectangle<int> faceplate, presetArea, vuBezel, pilotArea, dividerArea;
    std::array<juce::Rectangle<int>, 3> switchLabels;
    juce::Image tolexImage, faceplateImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptoAudioProcessorEditor)
};
