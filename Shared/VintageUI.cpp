#include "VintageUI.h"
#include "BinaryData.h"

using namespace vintage;

//==============================================================================
VintageLookAndFeel::VintageLookAndFeel()
{
    script        = juce::Typeface::createSystemTypefaceFor (BinaryData::YellowtailRegular_ttf, BinaryData::YellowtailRegular_ttfSize);
    oswaldMedium  = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldMedium_ttf,     BinaryData::OswaldMedium_ttfSize);
    oswaldRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::OswaldRegular_ttf,    BinaryData::OswaldRegular_ttfSize);
    setDefaultSansSerifTypeface (oswaldRegular);

    setColour (juce::ComboBox::textColourId, cream);
    setColour (juce::PopupMenu::backgroundColourId, cream);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ink);
    setColour (juce::PopupMenu::highlightedTextColourId, cream);
}

juce::Font VintageLookAndFeel::labelFont (float h, float kerning) const
{
    return juce::Font (juce::FontOptions (oswaldMedium).withHeight (h)).withExtraKerningFactor (kerning);
}
juce::Font VintageLookAndFeel::labelFontRegular (float h) const { return juce::Font (juce::FontOptions (oswaldRegular).withHeight (h)); }
juce::Font VintageLookAndFeel::scriptFont (float h) const       { return juce::Font (juce::FontOptions (script).withHeight (h)); }

juce::Label* VintageLookAndFeel::createSliderTextBox (juce::Slider& s) { return LookAndFeel_V4::createSliderTextBox (s); }

void VintageLookAndFeel::drawJewel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool lit)
{
    if (lit)
    {
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.45f), r.getCentreX(), r.getCentreY(),
                                                 c.withAlpha (0.0f), r.getCentreX() + r.getWidth(), r.getCentreY(), true));
        g.fillEllipse (r.expanded (r.getWidth() * 0.55f));
    }
    // Chromring
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeeeeee), r.getX(), r.getY(),
                                             juce::Colour (0xff4d4d4d), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 0.8f);

    const auto inner = r.reduced (r.getWidth() * 0.17f);
    const auto top = inner.getCentre().translated (-inner.getWidth() * 0.15f, -inner.getHeight() * 0.18f);
    if (lit)
        g.setGradientFill (juce::ColourGradient (c.brighter (0.9f), top.x, top.y, c.darker (0.5f), inner.getRight(), inner.getBottom(), true));
    else
        g.setGradientFill (juce::ColourGradient (c.darker (1.6f), top.x, top.y, c.darker (3.5f), inner.getRight(), inner.getBottom(), true));
    g.fillEllipse (inner);

    // Facetten
    g.setColour (juce::Colours::black.withAlpha (lit ? 0.18f : 0.3f));
    const auto cc = inner.getCentre();
    for (int k = 0; k < 6; ++k)
    {
        const float a = (float) k * juce::MathConstants<float>::pi / 3.0f;
        g.drawLine ({ cc.getPointOnCircumference (inner.getWidth() * 0.18f, a), cc.getPointOnCircumference (inner.getWidth() * 0.48f, a) }, 0.6f);
    }
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.75f : 0.3f));
    g.fillEllipse (juce::Rectangle<float> (inner.getWidth() * 0.28f, inner.getHeight() * 0.2f).withCentre (top));
}

void VintageLookAndFeel::drawScrew (juce::Graphics& g, juce::Point<float> c, float rad, float angle)
{
    const auto r = juce::Rectangle<float> (rad * 2, rad * 2).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (r.translated (0.6f, 1.0f).expanded (0.6f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f2), r.getX(), r.getY(), juce::Colour (0xff6a6a6a), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawLine ({ c.getPointOnCircumference (rad * 0.75f, angle), c.getPointOnCircumference (rad * 0.75f, angle + juce::MathConstants<float>::pi) }, 1.3f);
}

void VintageLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto tagArea = b.removeFromBottom (16.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - (slider.getProperties()["noScale"] ? 6.0f : (slider.getProperties()["abScale"] ? 26.0f : 20.0f));
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    const bool abScale = slider.getProperties()["abScale"];
    if (abScale)
    {
        for (int i = 0; i <= 20; ++i)
        {
            const float a = startAngle + (float) i / 20.0f * (endAngle - startAngle);
            g.setColour (ink.withAlpha (0.85f));
            g.drawLine ({ c.getPointOnCircumference (r + 3.0f, a), c.getPointOnCircumference (r + (i % 10 == 0 ? 10.0f : (i % 5 == 0 ? 7.5f : 5.0f)), a) },
                        i % 10 == 0 ? 1.6f : 1.0f);
        }
        g.setFont (labelFont (15.0f, 0.0f));
        for (auto [t, f] : { std::pair { "A", 0.0f }, std::pair { "A+B", 0.5f }, std::pair { "B", 1.0f } })
        {
            const float a = startAngle + f * (endAngle - startAngle);
            g.drawText (t, juce::Rectangle<float> (40.0f, 16.0f).withCentre (c.getPointOnCircumference (r + 20.0f, a)), juce::Justification::centred);
        }
    }
    // Skala 0–10, auf die Frontplatte gedruckt
    const bool noScale = slider.getProperties()["noScale"];
    for (int i = 0; ! abScale && ! noScale && i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        g.setColour (ink.withAlpha (0.85f));
        g.drawLine ({ c.getPointOnCircumference (r + 3.0f, a), c.getPointOnCircumference (r + (i % 5 == 0 ? 9.0f : 6.5f), a) }, i % 5 == 0 ? 1.4f : 1.0f);
        g.setFont (labelFontRegular (10.0f));
        const auto np = c.getPointOnCircumference (r + 15.5f, a);
        g.drawText (juce::String (i), juce::Rectangle<float> (16.0f, 12.0f).withCentre (np), juce::Justification::centred);
    }

    const auto knob = juce::Rectangle<float> (r * 2, r * 2).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knob.translated (2.0f, 3.5f).expanded (1.0f));

    // Rock mit Riffelung
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b423b), c.x - r * 0.4f, c.y - r * 0.6f,
                                             juce::Colour (0xff110e0c), c.x + r * 0.7f, c.y + r * 0.9f, true));
    g.fillEllipse (knob);
    for (int k = 0; k < 48; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::twoPi / 48.0f;
        g.setColour ((k & 1) ? juce::Colours::white.withAlpha (0.07f) : juce::Colours::black.withAlpha (0.35f));
        g.drawLine ({ c.getPointOnCircumference (r * 0.84f, a), c.getPointOnCircumference (r * 0.99f, a) }, 1.0f);
    }

    // Kappe (Bakelit)
    const float capR = r * 0.68f;
    const auto cap = juce::Rectangle<float> (capR * 2, capR * 2).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d534b), c.x - capR * 0.5f, c.y - capR * 0.6f,
                                             juce::Colour (0xff17130f), c.x + capR, c.y + capR, true));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (cap, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillEllipse (juce::Rectangle<float> (capR * 0.9f, capR * 0.55f).withCentre (c.translated (-capR * 0.25f, -capR * 0.45f)));

    // Zeiger
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawLine ({ c.getPointOnCircumference (r * 0.22f, angle).translated (0.8f, 1.0f), c.getPointOnCircumference (r * 0.96f, angle).translated (0.8f, 1.0f) }, 2.6f);
    g.setColour (cream);
    g.drawLine ({ c.getPointOnCircumference (r * 0.22f, angle), c.getPointOnCircumference (r * 0.96f, angle) }, 2.4f);

    // Wert anzeigen, solange die Maus darüber ist
    if (slider.isMouseOverOrDragging() && slider.isEnabled())
    {
        const auto text = slider.getTextFromValue (slider.getValue());
        const auto f = labelFontRegular (12.0f);
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (f, text) + 12.0f;
        const auto tag = juce::Rectangle<float> (tw, 15.0f).withCentre (tagArea.getCentre());
        g.setColour (ink.withAlpha (0.9f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (cream);
        g.setFont (f);
        g.drawText (text, tag, juce::Justification::centred);
    }
}

void VintageLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = r.getHeight();
    const bool on = b.getToggleState();
    const auto lamp = juce::Colour ((juce::uint32) (int) b.getProperties().getWithDefault ("lamp", (int) lampRed.getARGB()));

    drawJewel (g, r.removeFromLeft (h).reduced (h * 0.2f), lamp, on);
    r.removeFromLeft (4.0f);

    // Kippschalter
    const auto sw = r.removeFromLeft (h * 0.8f);
    const auto c = sw.getCentre();
    const float nutR = h * 0.2f;
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (juce::Rectangle<float> (nutR * 2.6f, nutR * 2.6f).withCentre (c.translated (0.8f, 1.2f)));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff0f0f0), c.x - nutR, c.y - nutR, juce::Colour (0xff595959), c.x + nutR, c.y + nutR, false));
    juce::Path hex;
    for (int k = 0; k < 6; ++k)
    {
        const auto p = c.getPointOnCircumference (nutR * 1.3f, (float) k * juce::MathConstants<float>::pi / 3.0f + 0.52f);
        if (k == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    g.fillPath (hex);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (hex, juce::PathStrokeType (0.7f));

    const float len = h * 0.44f * (on ? -1.0f : 1.0f);
    const juce::Point<float> tip { c.x, c.y + len };
    juce::Path lever;
    lever.startNewSubPath (c.x - 3.0f, c.y);
    lever.lineTo (tip.x - 2.0f, tip.y);
    lever.lineTo (tip.x + 2.0f, tip.y);
    lever.lineTo (c.x + 3.0f, c.y);
    lever.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (lever, juce::AffineTransform::translation (1.5f, 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), c.x - 3.0f, c.y, juce::Colour (0xff7a7a7a), c.x + 3.0f, c.y, false));
    g.fillPath (lever);
    const auto ball = juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), ball.getX(), ball.getY(), juce::Colour (0xff666666), ball.getRight(), ball.getBottom(), false));
    g.fillEllipse (ball);
    g.setColour (juce::Colours::black.withAlpha (over ? 0.6f : 0.4f));
    g.drawEllipse (ball, 0.7f);

    // AN / AUS
    if (! b.getButtonText().isEmpty())
    {
        g.setColour (b.isColourSpecified (juce::ToggleButton::textColourId) ? b.findColour (juce::ToggleButton::textColourId) : ink);
        g.setFont (labelFont (10.0f, 0.1f));
        const auto onText  = b.getProperties().getWithDefault ("onText",  "AN").toString();
        const auto offText = b.getProperties().getWithDefault ("offText", "AUS").toString();
        g.drawText (onText,  r.withTrimmedLeft (4.0f).removeFromTop (h * 0.5f), juce::Justification::centredLeft);
        g.drawText (offText, r.withTrimmedLeft (4.0f).removeFromBottom (h * 0.5f), juce::Justification::centredLeft);
    }
}

void VintageLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    const bool plate = box.getProperties()["plate"];

    if (plate)
    {
        g.setGradientFill (juce::ColourGradient (cream.brighter (0.1f), 0, 0, creamDark, 0, (float) h, false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        drawScrew (g, { 9.0f, r.getCentreY() }, 4.0f, 0.6f);
        drawScrew (g, { r.getRight() - 9.0f, r.getCentreY() }, 4.0f, 2.1f);
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a322c), 0, 0, juce::Colour (0xff15110e), 0, (float) h, false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (1, 3.0f, (float) w - 3.0f);
    }

    juce::Path arrow;
    const float ax = plate ? r.getRight() - 26.0f : r.getRight() - 14.0f, ay = r.getCentreY();
    arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (plate ? ink : cream.withAlpha (0.8f));
    g.fillPath (arrow);
}

juce::Font VintageLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return box.getProperties()["plate"] ? labelFont (16.0f, 0.06f) : labelFontRegular (14.0f);
}

void VintageLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const bool plate = box.getProperties()["plate"];
    label.setBounds (plate ? 18 : 6, 1, box.getWidth() - (plate ? 50 : 24), box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, plate ? ink : cream);
    label.setJustificationType (plate ? juce::Justification::centred : juce::Justification::centredLeft);
}

juce::Font VintageLookAndFeel::getPopupMenuFont() { return labelFontRegular (15.0f); }

//==============================================================================
void VuMeter::update (float avg, float peak, float dt)
{
    // Kommt kein neuer Wert mehr (Plugin in Logic ausgeschaltet oder angehalten),
    // fällt der Zeiger nach einer Viertelsekunde in seine Ruhelage zurück
    if (avg == lastAvg) staleTime += dt; else staleTime = 0.0f;
    lastAvg = avg;
    const bool stale = staleTime > 0.25f;
    if (stale) peak = 0.0f;

    const float vu = juce::Decibels::gainToDecibels (avg * 1.1107f, -80.0f) + 18.0f;   // 0 VU = -18 dBFS (Sinus)
    const float target = stale ? (restAtZeroVu ? vuToFraction (0.0f) : 0.0f)
                               : juce::jlimit (0.0f, 1.12f, avg > 1.0e-6f ? vuToFraction (vu) : 0.0f);

    // Zeiger mit Masse und Feder: ~300 ms Anstiegszeit, leichtes Überschwingen wie ein echtes VU-Meter
    constexpr float omega = 13.0f, zeta = 0.78f;
    const int steps = 4;
    const float hstep = dt / (float) steps;
    for (int i = 0; i < steps; ++i)
    {
        const float acc = omega * omega * (target - position) - 2.0f * zeta * omega * velocity;
        velocity += acc * hstep;
        position += velocity * hstep;
        if (position < 0.0f)  { position = 0.0f;  velocity = 0.0f; }
        if (position > 1.12f) { position = 1.12f; velocity = 0.0f; }
    }

    // PEAK-Lampe in zwei Stufen: bernstein ab -6 dBFS (12 dB über 0 VU), rot ab -1 dBFS; leuchtet 0,8 s nach
    const int level = peak >= 0.891f ? 2 : (peak >= 0.501f ? 1 : 0);
    if (level > 0 && level >= peakLevel)
    {
        peakLevel = level;
        peakHold = 0.8f;
    }
    peakHold -= dt;
    if (peakHold <= 0.0f)
    {
        peakHold = 0.0f;
        peakLevel = 0;
    }
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // Chromrahmen
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (b.translated (2.0f, 3.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeaeaea), 0, b.getY(), juce::Colour (0xff5f5f5f), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b, 9.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5f5f5f), 0, b.getY(), juce::Colour (0xffdddddd), 0, b.getBottom(), false));
    g.fillRoundedRectangle (b.reduced (3.0f), 7.0f);

    const auto face = b.reduced (8.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (face.expanded (1.5f), 5.0f);

    // hinterleuchtetes Ziffernblatt
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff9edc4), face.getCentreX(), face.getBottom() - face.getHeight() * 0.25f,
                                             juce::Colour (0xffcfa95e), face.getX(), face.getY(), true));
    g.fillRoundedRectangle (face, 4.0f);

    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (face.toNearestInt());

    const juce::Point<float> pivot { face.getCentreX(), face.getBottom() + face.getHeight() * 0.38f };
    const float radius = face.getHeight() * 0.92f;
    constexpr float maxAngle = 0.70f;
    auto angleFor = [] (float f) { return -maxAngle + 2.0f * maxAngle * f; };

    // Skala
    const float a0 = angleFor (vuToFraction (-20.0f)), aZero = angleFor (vuToFraction (0.0f)), aMax = angleFor (1.0f);
    juce::Path blackArc, redArc;
    blackArc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, a0, aZero, true);
    redArc.addCentredArc (pivot.x, pivot.y, radius + 2.5f, radius + 2.5f, 0.0f, aZero, aMax, true);
    g.setColour (ink);
    g.strokePath (blackArc, juce::PathStrokeType (1.4f));
    g.setColour (juce::Colour (0xffc0281b));
    g.strokePath (redArc, juce::PathStrokeType (5.0f));

    const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    for (float v : marks)
    {
        const float a = angleFor (vuToFraction (v));
        const bool red = v > 0.0f;
        g.setColour (red ? juce::Colour (0xffc0281b) : ink);
        g.drawLine ({ pivot.getPointOnCircumference (radius, a), pivot.getPointOnCircumference (radius + 9.0f, a) }, 1.3f);
        g.setFont (lnf.labelFontRegular (11.0f));
        const auto lp = pivot.getPointOnCircumference (radius + 18.0f, a);
        g.drawText (juce::String ((int) std::abs (v)), juce::Rectangle<float> (20.0f, 12.0f).withCentre (lp), juce::Justification::centred);
    }
    // feine Zwischenstriche
    g.setColour (ink.withAlpha (0.6f));
    for (int step = -40; step < 6; ++step)
    {
        if (step < -20 && step % 4 != 0) continue;
        const float a = angleFor (vuToFraction ((float) step * 0.5f));
        g.drawLine ({ pivot.getPointOnCircumference (radius, a), pivot.getPointOnCircumference (radius + 4.0f, a) }, 0.7f);
    }

    g.setColour (ink);
    g.setFont (lnf.labelFont (20.0f, 0.1f));
    g.drawText ("VU", juce::Rectangle<float> (60.0f, 24.0f).withCentre ({ face.getCentreX(), face.getY() + face.getHeight() * 0.68f }), juce::Justification::centred);
    g.setFont (lnf.labelFont (11.0f, 0.1f));
    g.setColour (ink.withAlpha (0.7f));
    g.drawText (name, juce::Rectangle<float> (face.getX() + 8.0f, face.getBottom() - 20.0f, 20.0f, 14.0f), juce::Justification::centredLeft);

    // Peak-Lampe
    const auto led = juce::Rectangle<float> (14.0f, 14.0f).withPosition (face.getRight() - 21.0f, face.getBottom() - 21.0f);
    const auto peakColour = peakLevel == 2 ? juce::Colour (0xffff2a10) : juce::Colour (0xffff8c00);
    if (peakLevel > 0)
    {
        // kräftiger Schein, damit die Lampe auf dem hellen Ziffernblatt gut sichtbar ist
        g.setGradientFill (juce::ColourGradient (peakColour.withAlpha (0.75f), led.getCentreX(), led.getCentreY(),
                                                 peakColour.withAlpha (0.0f), led.getCentreX() + led.getWidth() * 1.4f, led.getCentreY(), true));
        g.fillEllipse (led.expanded (led.getWidth() * 0.9f));
    }
    VintageLookAndFeel::drawJewel (g, led, peakColour, peakLevel > 0);
    g.setFont (lnf.labelFont (9.0f, 0.1f));
    g.setColour (ink.withAlpha (0.7f));
    g.drawText ("PEAK", led.withX (led.getX() - 34.0f).withWidth (30.0f), juce::Justification::centredRight);

    // Zeiger
    const float na = angleFor (position);
    const auto tip = pivot.getPointOnCircumference (radius + 14.0f, na);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine ({ pivot.translated (3.0f, 2.0f), tip.translated (3.0f, 2.0f) }, 1.6f);
    g.setColour (juce::Colour (0xff1a1410));
    g.drawLine ({ pivot, tip }, 1.5f);

    // Abdeckung über dem Drehpunkt
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3029), face.getCentreX(), face.getBottom() - 14.0f,
                                             juce::Colour (0xff120e0b), face.getCentreX(), face.getBottom(), false));
    g.fillEllipse (juce::Rectangle<float> (face.getWidth() * 0.42f, face.getHeight() * 0.34f).withCentre ({ face.getCentreX(), face.getBottom() + 2.0f }));

    // Glas
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), face.getX(), face.getY(),
                                             juce::Colours::white.withAlpha (0.0f), face.getX() + face.getWidth() * 0.4f, face.getY() + face.getHeight() * 0.6f, false));
    g.fillRoundedRectangle (face, 4.0f);
}

//==============================================================================
// Tasten (z. B. Bandwahl): dunkles Bakelit, gewählt = hell hinterleuchtet
void VintageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool sel = b.getToggleState();
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (1.0f, down ? 1.0f : 2.5f), 4.0f);
    if (down) r = r.translated (0.0f, 1.0f);

    if (sel)
        g.setGradientFill (juce::ColourGradient (cream.brighter (0.15f), 0, r.getY(), creamDark.darker (0.05f), 0, r.getBottom(), false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colour (over ? 0xff4a4038 : 0xff3c332c), 0, r.getY(), juce::Colour (0xff15110e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (sel ? 0.5f : 0.08f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 4.0f, r.getRight() - 4.0f);

    // Farbstreifen + Lämpchen (an, wenn das Band aktiv ist)
    g.setColour (accent.withAlpha (sel ? 1.0f : 0.7f));
    g.fillRect (r.withHeight (3.0f).withY (r.getBottom() - 4.0f).reduced (6.0f, 0.0f));
    if (b.getProperties()["noLamp"])
        return;
    const bool bandOn = b.getProperties()["bandOn"];
    drawJewel (g, juce::Rectangle<float> (9.0f, 9.0f).withPosition (r.getX() + 5.0f, r.getCentreY() - 6.0f), accent, bandOn);
}

void VintageLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const bool sel = b.getToggleState();
    auto r = b.getLocalBounds().toFloat().withTrimmedLeft (b.getProperties()["noLamp"] ? 3.0f : 16.0f).withTrimmedRight (3.0f).withTrimmedBottom (4.0f);
    if (down) r = r.translated (0.0f, 1.0f);
    g.setColour (sel ? ink : cream.withAlpha (0.85f));
    g.setFont (labelFont (11.5f, 0.06f));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.toNearestInt(), juce::Justification::centred, 1, 0.85f);
}
