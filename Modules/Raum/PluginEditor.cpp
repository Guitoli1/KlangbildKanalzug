#include "PluginEditor.h"

using namespace vintage;

//==============================================================================
RaumAudioProcessorEditor::RaumAudioProcessorEditor (RaumAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    setupSwitch (dOn, "d_on", lampRed);
    setupSwitch (eOn, "e_on", lampRed);
    setupSwitch (sOn, "s_on", lampRed);
    setupSwitch (pingPong, "e_pp", lampAmber);
    for (auto* b : { &dOn, &eOn, &sOn })
        b->setButtonText ("on");   // zeigt "AN/AUS" neben dem Schalter

    dMix   = &addKnob ("d_mix",   "MIX",    0);
    dWidth = &addKnob ("d_width", "BREITE", 0);
    dDepth = &addKnob ("d_depth", "TIEFE",  0);
    eTime  = &addKnob ("e_time",  "ZEIT",   1);
    eFb    = &addKnob ("e_fb",    "FEEDBACK", 1);
    eMix   = &addKnob ("e_mix",   "MIX",    1);
    eTone  = &addKnob ("e_tone",  "TON",    1);
    eWow   = &addKnob ("e_wow",   "WOW",    1);
    sMix   = &addKnob ("s_mix",   "MIX",    2);
    sDecay = &addKnob ("s_decay", juce::String (juce::CharPointer_UTF8 ("L\xc3\x84NGE")), 2);
    sTone  = &addKnob ("s_tone",  "TON",    2);
    sDwell = &addKnob ("s_dwell", "DWELL",  2);
    outKnob = &addKnob ("out",    "PEGEL",  3);

    syncBox.addItemList (RaumAudioProcessor::syncNames(), 1);
    addAndMakeVisible (syncBox);
    syncAttachment = std::make_unique<ComboAttachment> (proc.apvts, "e_sync", syncBox);

    presetBox.getProperties().set ("plate", true);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);

    sections[0] = { "DOUBLER",   {}, &dOn };
    sections[1] = { "BANDECHO",  {}, &eOn };
    sections[2] = { "FEDERHALL", {}, &sOn };
    sections[3] = { "AUSGANG",   {}, nullptr };

    setSize (1000, 540);
    startTimerHz (60);
}

RaumAudioProcessorEditor::~RaumAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

RaumAudioProcessorEditor::Knob& RaumAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name, int section)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->section = section;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.setMouseDragSensitivity (220);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramID, k->slider);
    if (auto* rp = proc.apvts.getParameter (paramID))
        k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void RaumAudioProcessorEditor::setupSwitch (juce::ToggleButton& b, const juce::String& paramID, juce::Colour lamp)
{
    b.getProperties().set ("lamp", (int) lamp.getARGB());
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, paramID, b));
}

void RaumAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);

    const bool on[] = { dOn.getToggleState(), eOn.getToggleState(), sOn.getToggleState(), true };
    for (auto& k : knobs)
        k->slider.setAlpha (on[k->section] ? 1.0f : 0.55f);
    syncBox.setAlpha (on[1] ? 1.0f : 0.55f);
    pingPong.setAlpha (on[1] ? 1.0f : 0.55f);
    eTime->slider.setEnabled (syncBox.getSelectedId() <= 1);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

//==============================================================================
void RaumAudioProcessorEditor::renderTextures()
{
    // Tolex-Bezug
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

    // gebürstete Frontplatte
    if (faceplate.isEmpty())
        return;
    faceplateImage = juce::Image (juce::Image::ARGB, faceplate.getWidth(), faceplate.getHeight(), true);
    juce::Graphics g (faceplateImage);
    const auto r = faceplateImage.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (cream.brighter (0.06f), 0, 0, creamDark, 0, r.getHeight(), false));
    g.fillRoundedRectangle (r, 6.0f);
    juce::Random rnd (3);
    for (int y = 0; y < faceplate.getHeight(); ++y)
    {
        const bool light = rnd.nextBool();
        g.setColour ((light ? juce::Colours::white : juce::Colour (0xff6b5534)).withAlpha (0.015f + rnd.nextFloat() * 0.035f));
        g.drawHorizontalLine (y, 0.0f, r.getWidth());
    }
    // leichte Vergilbung an den Rändern
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colour (0x30694a1c), 0, 0, true));
    g.fillRoundedRectangle (r, 6.0f);
}

void RaumAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                             juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText (text, r.translated (0.0f, 1.0f), j);
    g.setColour (ink);
    g.drawText (text, r, j);
}

void RaumAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Logo
    const auto scriptF = lnf.scriptFont (64.0f);
    const auto logoArea = juce::Rectangle<float> (40.0f, 8.0f, 400.0f, 84.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", logoArea.translated (2.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", logoArea, juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (26.0f, 0.35f));
    g.drawText ("RAUM", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 120.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("DOUBLER \xc2\xb7 BANDECHO \xc2\xb7 FEDERHALL"), juce::Rectangle<float> (44.0f, 90.0f, 400.0f, 16.0f),
                juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.6f));
    g.setFont (lnf.labelFont (10.0f, 0.3f));
    g.drawText ("PROGRAMM", presetArea.toFloat().withY ((float) presetArea.getY() - 15.0f).withHeight (13.0f), juce::Justification::centredLeft);

    // Keder um die Frontplatte
    const auto fp = faceplate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (fp.expanded (4.0f).translated (0, 2.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.3f), 0, fp.getY(), gold.darker (0.6f), 0, fp.getBottom(), false));
    g.fillRoundedRectangle (fp.expanded (3.0f), 8.0f);
    g.drawImageAt (faceplateImage, faceplate.getX(), faceplate.getY());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (fp, 6.0f, 1.0f);

    for (auto [x, y, a] : { std::tuple { fp.getX() + 12.0f, fp.getY() + 12.0f, 0.4f }, std::tuple { fp.getRight() - 12.0f, fp.getY() + 12.0f, 1.3f },
                            std::tuple { fp.getX() + 12.0f, fp.getBottom() - 12.0f, 2.2f }, std::tuple { fp.getRight() - 12.0f, fp.getBottom() - 12.0f, 0.9f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 5.0f, a);

    // Sektionen
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto& s = sections[i];
        const auto sb = s.bounds.toFloat();
        drawEngraved (g, s.title, sb.withHeight (36.0f).withTrimmedTop (4.0f), lnf.labelFont (18.0f, 0.22f), juce::Justification::centredLeft);
        g.setColour (ink.withAlpha (0.5f));
        g.drawHorizontalLine ((int) sb.getY() + 40, sb.getX(), sb.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawHorizontalLine ((int) sb.getY() + 41, sb.getX(), sb.getRight());

        if (i + 1 < sections.size())
        {
            const float x = (sb.getRight() + sections[i + 1].bounds.toFloat().getX()) * 0.5f;
            for (float dx : { -2.0f, 2.0f })
            {
                g.setColour (ink.withAlpha (0.35f));
                g.drawVerticalLine ((int) (x + dx), sb.getY(), sb.getBottom());
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.drawVerticalLine ((int) (x + dx) + 1, sb.getY(), sb.getBottom());
            }
        }
    }

    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        drawEngraved (g, k->name, juce::Rectangle<float> (sb.getX(), sb.getBottom() - 2.0f, sb.getWidth(), 18.0f),
                      lnf.labelFont (13.0f, 0.14f), juce::Justification::centred);
    }

    drawEngraved (g, "SYNC", syncLabelArea.toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centredLeft);
    drawEngraved (g, "PING-PONG", pingPongLabelArea.toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centredLeft);

    // Betriebslampe und Typenschild
    const auto pa = pilotArea.toFloat();
    VintageLookAndFeel::drawJewel (g, juce::Rectangle<float> (34.0f, 34.0f).withCentre ({ pa.getCentreX(), pa.getY() + 30.0f }), lampRed, true);
    drawEngraved (g, "BETRIEB", juce::Rectangle<float> (pa.getX(), pa.getY() + 52.0f, pa.getWidth(), 16.0f), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);
    const auto tag = juce::Rectangle<float> (pa.getWidth() - 20.0f, 26.0f).withCentre ({ pa.getCentreX(), pa.getBottom() - 22.0f });
    g.setColour (ink.withAlpha (0.85f));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (gold);
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("MODELL KR-1", tag, juce::Justification::centred);
}

void RaumAudioProcessorEditor::resized()
{
    const int W = getWidth();

    vuRight.setBounds (W - 36 - 200, 20, 200, 130);
    vuLeft.setBounds (W - 36 - 200 - 16 - 200, 20, 200, 130);

    presetArea = { 44, 128, 270, 32 };
    presetBox.setBounds (presetArea);

    faceplate = { 24, 176, W - 48, 344 };
    auto inner = faceplate.reduced (24, 14);
    const int widths[] = { 205, 318, 212, 140 };
    const int gap = (inner.getWidth() - (widths[0] + widths[1] + widths[2] + widths[3])) / 3;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        sections[i].bounds = inner.removeFromLeft (widths[i]).withHeight (inner.getHeight());
        inner.removeFromLeft (gap);
        if (sections[i].power != nullptr)
            sections[i].power->setBounds (sections[i].bounds.getRight() - 84, sections[i].bounds.getY() + 2, 84, 34);
    }

    constexpr int rowH = 138, sliderH = 116;
    auto cell = [&] (int section, int row, int col, int cols)
    {
        const auto sb = sections[(size_t) section].bounds;
        const int cw = sb.getWidth() / cols;
        return juce::Rectangle<int> (sb.getX() + col * cw, sb.getY() + 46 + row * rowH, cw, sliderH);
    };

    dMix->slider.setBounds   (cell (0, 0, 0, 2));
    dWidth->slider.setBounds (cell (0, 0, 1, 2));
    dDepth->slider.setBounds (cell (0, 1, 0, 2));

    eTime->slider.setBounds (cell (1, 0, 0, 3));
    eFb->slider.setBounds   (cell (1, 0, 1, 3));
    eMix->slider.setBounds  (cell (1, 0, 2, 3));
    eTone->slider.setBounds (cell (1, 1, 0, 3));
    eWow->slider.setBounds  (cell (1, 1, 1, 3));
    {
        auto c = cell (1, 1, 2, 3).reduced (6, 4);
        syncLabelArea = c.removeFromTop (16);
        syncBox.setBounds (c.removeFromTop (26));
        c.removeFromTop (12);
        pingPongLabelArea = c.removeFromTop (16);
        pingPong.setBounds (c.removeFromTop (36).withWidth (70));
    }

    sMix->slider.setBounds   (cell (2, 0, 0, 2));
    sDecay->slider.setBounds (cell (2, 0, 1, 2));
    sTone->slider.setBounds  (cell (2, 1, 0, 2));
    sDwell->slider.setBounds (cell (2, 1, 1, 2));

    outKnob->slider.setBounds (cell (3, 0, 0, 1).withSizeKeepingCentre (110, sliderH));
    pilotArea = cell (3, 1, 0, 1).withHeight (rowH);

    renderTextures();
}
