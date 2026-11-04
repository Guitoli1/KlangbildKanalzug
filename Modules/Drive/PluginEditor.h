#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

// Fußschalter im Stil eines Pedals
class StompSwitch : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class DriveAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DriveAudioProcessorEditor (DriveAudioProcessor&);
    ~DriveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::String name; std::unique_ptr<SliderAttachment> attachment; };
    Knob& addKnob (const juce::String& paramID, const juce::String& name);
    void timerCallback() override;
    void renderTextures();

    DriveAudioProcessor& proc;
    VintageLookAndFeel lnf;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton modeSwitch;
    StompSwitch stomp;
    std::unique_ptr<ButtonAttachment> modeAtt, stompAtt;
    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    juce::Rectangle<int> pedal, ledArea;
    juce::Image tolexImage, pedalImage;
    bool lastOn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveAudioProcessorEditor)
};
