#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "KanalzugProcessor.h"
#include "VintageUI.h"

class KanalzugAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KanalzugAudioProcessorEditor (KanalzugAudioProcessor&);
    ~KanalzugAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Tile
    {
        juce::TextButton key;
        juce::ToggleButton power;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    void selectModule (int index);
    void layoutModuleEditor();
    void timerCallback() override;
    void renderTolex();

    KanalzugAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::array<Tile, KanalzugAudioProcessor::numModules> tiles;
    juce::Slider outSlider;
    std::unique_ptr<SliderAttachment> outAttachment;
    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    juce::Component moduleHolder;
    std::unique_ptr<juce::AudioProcessorEditor> moduleEditor;
    int selected = -1;

    juce::Rectangle<int> header, moduleArea;
    juce::Image tolexImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KanalzugAudioProcessorEditor)
};
