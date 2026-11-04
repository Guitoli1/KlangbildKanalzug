#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace vintage
{
const juce::Colour tolex      { 0xff221c18 };
const juce::Colour cream      { 0xffe8dcc0 };
const juce::Colour creamDark  { 0xffd3c4a2 };
const juce::Colour ink        { 0xff3b2a1e };   // gravierte Schrift
const juce::Colour inkLight   { 0xff7a6450 };
const juce::Colour gold       { 0xffc9a25a };
const juce::Colour logo       { 0xfff1e4c3 };
const juce::Colour lampRed    { 0xffff4a2a };
const juce::Colour lampAmber  { 0xffffa726 };
const juce::Colour lampGreen  { 0xff8bd450 };
}

//==============================================================================
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VintageLookAndFeel();

    juce::Font labelFont (float height, float kerning = 0.08f) const;
    juce::Font labelFontRegular (float height) const;
    juce::Font scriptFont (float height) const;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    static void drawJewel (juce::Graphics&, juce::Rectangle<float>, juce::Colour colour, bool lit);
    static void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);

private:
    juce::Typeface::Ptr script, oswaldMedium, oswaldRegular;
};

//==============================================================================
class VuMeter : public juce::Component
{
public:
    VuMeter (VintageLookAndFeel& l, juce::String channelName) : lnf (l), name (std::move (channelName)) {}

    // rectifiedAverage: gleichgerichteter Mittelwert (linear), peak: Spitzenwert (linear)
    void update (float rectifiedAverage, float peak, float dt);
    void paint (juce::Graphics&) override;
    void setLabel (const juce::String& s) { if (s != name) { name = s; repaint(); } }

private:
    static float vuToFraction (float vu) { return std::pow (10.0f, (vu - 3.0f) / 20.0f); }

    VintageLookAndFeel& lnf;
    juce::String name;
    float position = 0.0f, velocity = 0.0f;   // Zeigerstellung 0..1 (inkl. Masse/Feder)
    float peakHold = 0.0f;   // Haltezeit der Peak-Lampe in Sekunden
    int peakLevel = 0;       // 0 = aus, 1 = bernstein (ab -6 dBFS), 2 = rot (ab -1 dBFS)
};

