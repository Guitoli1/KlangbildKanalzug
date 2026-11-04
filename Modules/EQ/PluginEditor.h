#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "VintageUI.h"

// Farben des Röhrenbildschirms
namespace crt
{
const juce::Colour screenCentre { 0xff0f2116 };
const juce::Colour screenEdge   { 0xff040805 };
const juce::Colour phosphor     { 0xff6cf59a };
const juce::Colour grid         = phosphor.withAlpha (0.10f);
const juce::Colour gridStrong   = phosphor.withAlpha (0.22f);
const juce::Colour label        = phosphor.withAlpha (0.55f);
const juce::Colour curve        { 0xffffd08a };   // Bernstein
const juce::Colour preSpectrum  = phosphor.withAlpha (0.16f);
const juce::Colour postSpectrum = phosphor;

inline juce::Colour band (int i)
{
    static const juce::Colour c[] = { juce::Colour (0xffe0664f), juce::Colour (0xffe8954f), juce::Colour (0xffe3c25a),
                                      juce::Colour (0xff9cc46a), juce::Colour (0xff5fbfa8), juce::Colour (0xff6fa0d8),
                                      juce::Colour (0xffa88ad8), juce::Colour (0xffd784b8) };
    return c[i % 8];
}
} // namespace crt

//==============================================================================
// Röhrenbildschirm: Raster, Analyzer, EQ-Kurve und ziehbare Band-Punkte
class ResponseDisplay : public juce::Component, private juce::Timer
{
public:
    ResponseDisplay (KlangbildAudioProcessor&, VintageLookAndFeel&);
    ~ResponseDisplay() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setSelectedBand (int b) { selected = b; repaint(); }
    std::function<void (int)> onBandSelected;

private:
    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    float analyzerDbToY (float db) const;
    juce::Point<float> nodePosition (int band) const;
    int hitTestNode (juce::Point<float>) const;

    void setParam (const juce::String& id, float value);
    void updateCurves();
    void processSpectrum (std::vector<float>& smoothed, bool gotNewData);
    void renderOverlay();

    KlangbildAudioProcessor& proc;
    VintageLookAndFeel& lnf;

    static constexpr int fftSize = SpectrumFifo::fftSize;
    juce::dsp::FFT fft { SpectrumFifo::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> preDb, postDb, totalCurveDb, selectedCurveDb;
    juce::Image overlay;   // Scanlines, Vignette, Glas

    int selected = 2, hovered = -1, dragging = -1;
};

//==============================================================================
class KlangbildAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KlangbildAudioProcessorEditor (KlangbildAudioProcessor&);
    ~KlangbildAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void selectBand (int band);
    void timerCallback() override;
    void renderTextures();
    void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Justification);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    KlangbildAudioProcessor& proc;
    VintageLookAndFeel lnf;

    ResponseDisplay display;
    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };
    std::array<juce::TextButton, eq::numBands> bandButtons;

    juce::Slider freqSlider, gainSlider, qSlider, outSlider;
    juce::ToggleButton onButton;
    juce::ComboBox slopeBox, analyzerBox;

    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt, outAtt;
    std::unique_ptr<ButtonAttachment> onAtt;
    std::unique_ptr<ComboAttachment>  slopeAtt, analyzerAtt;

    juce::Rectangle<int> faceplate, bezel, analyzerArea, slopeLabelArea, onLabelArea, bandLabelArea, hintArea;
    juce::Image tolexImage, faceplateImage;
    int selectedBand = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KlangbildAudioProcessorEditor)
};
