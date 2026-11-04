#include "PluginEditor.h"

using namespace vintage;

namespace
{
const juce::Colour pedalTop    { 0xff3b6b45 };   // tiefes Grün, Hammerschlag-Lack
const juce::Colour pedalBottom { 0xff22422b };
const juce::Colour pedalInk    { 0xfff1e8cf };
}

//==============================================================================
void StompSwitch::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 4.0f;

    // Sockel (Mutter)
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c.translated (2.0f, 4.0f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8e8e8), c.x - r, c.y - r, juce::Colour (0xff4a4a4a), c.x + r, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    // Kappe – gedrückt etwas kleiner
    const float capR = r * (down ? 0.62f : 0.68f);
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xffffffff : 0xfff4f4f4), cap.getX(), cap.getY(),
                                             juce::Colour (0xff6e6e6e), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (cap, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawEllipse (cap.reduced (capR * 0.25f), 1.0f);
}

//==============================================================================
DriveAudioProcessorEditor::DriveAudioProcessorEditor (DriveAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addKnob ("drive", "DRIVE");
    addKnob ("tone",  "TON");
    addKnob ("level", "PEGEL");

    modeSwitch.setButtonText ("x");
    modeSwitch.getProperties().set ("onText", "KLAR");
    modeSwitch.getProperties().set ("offText", juce::String (juce::CharPointer_UTF8 ("GR\xc3\x9cN")));
    modeSwitch.getProperties().set ("lamp", (int) lampAmber.getARGB());
    modeSwitch.setColour (juce::ToggleButton::textColourId, pedalInk);
    addAndMakeVisible (modeSwitch);
    modeAtt = std::make_unique<ButtonAttachment> (proc.apvts, "mode", modeSwitch);

    addAndMakeVisible (stomp);
    stompAtt = std::make_unique<ButtonAttachment> (proc.apvts, "on", stomp);

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);

    setSize (760, 640);
    startTimerHz (60);
}

DriveAudioProcessorEditor::~DriveAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

DriveAudioProcessorEditor::Knob& DriveAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.setMouseDragSensitivity (240);
    k->slider.getProperties().set ("noScale", true);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramID, k->slider);
    if (auto* rp = proc.apvts.getParameter (paramID))
        k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void DriveAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);
    const bool on = stomp.getToggleState();
    if (on != lastOn)
    {
        lastOn = on;
        repaint (ledArea.expanded (20));
    }
}

void DriveAudioProcessorEditor::renderTextures()
{
    tolexImage = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    {
        juce::Image::BitmapData data (tolexImage, juce::Image::BitmapData::writeOnly);
        juce::Random rnd (7);
        for (int y = 0; y < tolexImage.getHeight(); ++y)
            for (int x = 0; x < tolexImage.getWidth(); ++x)
            {
                const int weave = ((x + y) % 4 == 0 || (x - y + 4096) % 4 == 0) ? -5 : 0;
                const int n = rnd.nextInt (13) - 6 + weave;
                data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, 34 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 28 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 24 + n)));
            }
    }
    if (pedal.isEmpty())
        return;

    // Pedalgehäuse mit Hammerschlag-Struktur
    pedalImage = juce::Image (juce::Image::ARGB, pedal.getWidth(), pedal.getHeight(), true);
    juce::Graphics g (pedalImage);
    const auto r = pedalImage.getBounds().toFloat();
    juce::Path body;
    body.addRoundedRectangle (r, 22.0f);
    g.reduceClipRegion (body);
    g.setGradientFill (juce::ColourGradient (pedalTop, 0, 0, pedalBottom, r.getWidth() * 0.3f, r.getHeight(), false));
    g.fillRoundedRectangle (r, 22.0f);
    juce::Random rnd (11);
    for (int i = 0; i < 2600; ++i)
    {
        const float x = rnd.nextFloat() * r.getWidth(), y = rnd.nextFloat() * r.getHeight();
        const float s = 2.0f + rnd.nextFloat() * 5.0f;
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.025f + rnd.nextFloat() * 0.03f));
        g.fillEllipse (x, y, s, s);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), 0, 0, juce::Colours::transparentWhite, 0, r.getHeight() * 0.4f, false));
    g.fillRect (r);
}

void DriveAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Kopfzeile
    const auto scriptF = lnf.scriptFont (52.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", juce::Rectangle<float> (36.0f, 14.0f, 300.0f, 70.0f).translated (2, 3), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", juce::Rectangle<float> (36.0f, 14.0f, 300.0f, 70.0f), juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (22.0f, 0.35f));
    g.drawText ("DRIVE", juce::Rectangle<float> (36.0f + sw + 12.0f, 38.0f, 160.0f, 34.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.5f));
    g.setFont (lnf.labelFont (11.0f, 0.3f));
    g.drawText ("OVERDRIVE VOR DEM AMP", juce::Rectangle<float> (40.0f, 84.0f, 340.0f, 14.0f), juce::Justification::centredLeft);

    // Pedal
    const auto pr = pedal.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (pr.translated (4.0f, 8.0f).expanded (2.0f), 24.0f);
    g.drawImageAt (pedalImage, pedal.getX(), pedal.getY());
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (pr.reduced (1.0f), 22.0f, 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (pr, 22.0f, 1.0f);
    for (auto [x, y] : { std::pair { pr.getX() + 16.0f, pr.getY() + 16.0f }, std::pair { pr.getRight() - 16.0f, pr.getY() + 16.0f },
                         std::pair { pr.getX() + 16.0f, pr.getBottom() - 16.0f }, std::pair { pr.getRight() - 16.0f, pr.getBottom() - 16.0f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 5.0f, 0.8f);

    // Knopfbeschriftung auf dem Pedal
    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        g.setColour (pedalInk);
        g.setFont (lnf.labelFont (14.0f, 0.2f));
        g.drawText (k->name, juce::Rectangle<float> (sb.getX() - 10.0f, sb.getY() - 20.0f, sb.getWidth() + 20.0f, 18.0f), juce::Justification::centred);
    }

    // LED
    VintageLookAndFeel::drawJewel (g, ledArea.toFloat(), lampRed, stomp.getToggleState());

    // Schriftzug
    const auto nameArea = juce::Rectangle<float> (pr.getX(), (float) ledArea.getBottom() + 6.0f, pr.getWidth(), 44.0f);
    g.setFont (lnf.scriptFont (38.0f));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawText ("Drive", nameArea.translated (1.5f, 2.0f), juce::Justification::centred);
    g.setColour (pedalInk);
    g.drawText ("Drive", nameArea, juce::Justification::centred);
    g.setFont (lnf.labelFont (11.0f, 0.4f));
    g.setColour (pedalInk.withAlpha (0.7f));
    g.drawText ("KLANGBILD", nameArea.withY (nameArea.getBottom() - 2.0f).withHeight (14.0f), juce::Justification::centred);
}

void DriveAudioProcessorEditor::resized()
{
    vuRight.setBounds (getWidth() - 36 - 170, 14, 170, 110);
    vuLeft.setBounds (getWidth() - 36 - 170 - 12 - 170, 14, 170, 110);

    pedal = juce::Rectangle<int> (340, 480).withCentre ({ getWidth() / 2, 140 + 240 });
    const int top = pedal.getY() + 46;
    const int kw = 104;
    knobs[0]->slider.setBounds (pedal.getX() + 14, top, kw, kw);
    knobs[2]->slider.setBounds (pedal.getRight() - 14 - kw, top, kw, kw);
    knobs[1]->slider.setBounds (pedal.getCentreX() - 46, top + 74, 92, 92);
    modeSwitch.setBounds (pedal.getCentreX() - 60, top + 176, 120, 34);
    ledArea = juce::Rectangle<int> (24, 24).withCentre ({ pedal.getCentreX(), top + 232 });
    stomp.setBounds (juce::Rectangle<int> (104, 104).withCentre ({ pedal.getCentreX(), pedal.getBottom() - 64 }));

    renderTextures();
}
