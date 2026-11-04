#include "PluginEditor.h"

using namespace vintage;

namespace
{
constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
constexpr float eqRangeDb = 24.0f;
constexpr float analyzerTopDb = 0.0f, analyzerBottomDb = -90.0f;
constexpr float tiltDbPerOct = 3.0f;   // Neigung, damit Musik/Rosa Rauschen eher flach erscheint
}

// Farbzuordnung für den Röhrenbildschirm
namespace theme
{
const juce::Colour grid         = crt::grid;
const juce::Colour gridStrong   = crt::gridStrong;
const juce::Colour textDim      = crt::label;
const juce::Colour text         = crt::phosphor;
const juce::Colour curve        = crt::curve;
const juce::Colour preSpectrum  = crt::phosphor.darker (1.4f);
const juce::Colour postSpectrum = crt::postSpectrum;
const juce::Colour panel        { 0xff07110a };
const juce::Colour display      = crt::screenCentre;
inline juce::Colour band (int i) { return crt::band (i); }
}

//==============================================================================
ResponseDisplay::ResponseDisplay (KlangbildAudioProcessor& p, VintageLookAndFeel& l) : proc (p), lnf (l)
{
    setOpaque (false);
    startTimerHz (30);
}

ResponseDisplay::~ResponseDisplay() { stopTimer(); }

juce::Rectangle<float> ResponseDisplay::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (8).withTrimmedRight (36).withTrimmedTop (10).withTrimmedBottom (22);
}

float ResponseDisplay::freqToX (float f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / minFreq) / std::log (maxFreq / minFreq);
}

float ResponseDisplay::xToFreq (float x) const
{
    const auto a = plotArea();
    return minFreq * std::pow (maxFreq / minFreq, (x - a.getX()) / a.getWidth());
}

float ResponseDisplay::gainToY (float g) const
{
    const auto a = plotArea();
    return a.getCentreY() - (g / eqRangeDb) * a.getHeight() * 0.5f;
}

float ResponseDisplay::yToGain (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * eqRangeDb;
}

float ResponseDisplay::analyzerDbToY (float db) const
{
    const auto a = plotArea();
    return juce::jmap (db, analyzerBottomDb, analyzerTopDb, a.getBottom(), a.getY());
}

juce::Point<float> ResponseDisplay::nodePosition (int band) const
{
    const auto s = proc.getBandSettings (band);
    const bool cut = eq::isCut (eq::typeForBand (band));
    return { freqToX (s.freq), gainToY (cut ? 0.0f : juce::jlimit (-eqRangeDb, eqRangeDb, s.gain)) };
}

int ResponseDisplay::hitTestNode (juce::Point<float> p) const
{
    // das ausgewählte Band hat Vorrang, falls Punkte übereinander liegen
    if (selected >= 0 && nodePosition (selected).getDistanceFrom (p) < 12.0f)
        return selected;
    int best = -1;
    float bestDist = 12.0f;
    for (int b = 0; b < eq::numBands; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

void ResponseDisplay::setParam (const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

//==============================================================================
void ResponseDisplay::resized()
{
    const auto n = (size_t) juce::jmax (2, (int) plotArea().getWidth() + 1);
    preDb.assign (n, -120.0f);
    postDb.assign (n, -120.0f);
    totalCurveDb.assign (n, 0.0f);
    selectedCurveDb.assign (n, 0.0f);
    updateCurves();
    renderOverlay();
}

void ResponseDisplay::renderOverlay()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    overlay = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (overlay);
    const auto b = getLocalBounds().toFloat();

    // Zeilenraster der Bildröhre
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (int y = 0; y < getHeight(); y += 3)
        g.drawHorizontalLine (y, 0.0f, b.getWidth());

    // Vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                             juce::Colours::black.withAlpha (0.6f), b.getX(), b.getY(), true));
    g.fillRect (b);

    // Glasreflex
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), b.getX(), b.getY(),
                                             juce::Colours::white.withAlpha (0.0f), b.getX() + b.getWidth() * 0.35f, b.getY() + b.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (b.reduced (6.0f), 16.0f);

    // abgerundete Ecken der Röhre
    juce::Path corners;
    corners.addRectangle (b);
    corners.addRoundedRectangle (b, 18.0f);
    corners.setUsingNonZeroWinding (false);
    g.setColour (juce::Colour (0xff0b0a09));
    g.fillPath (corners);
}

void ResponseDisplay::paintOverChildren (juce::Graphics& g)
{
    g.drawImageAt (overlay, 0, 0);
}

void ResponseDisplay::updateCurves()
{
    const double sr = proc.currentSampleRate.load();
    const auto a = plotArea();
    const size_t n = totalCurveDb.size();

    std::array<eq::BandCoeffs, eq::numBands> bc;
    std::array<bool, eq::numBands> on {};
    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto s = proc.getBandSettings (b);
        on[(size_t) b] = s.on;
        bc[(size_t) b] = eq::makeBand (eq::typeForBand (b), sr, s.freq, s.gain, s.q, s.slope);
    }

    for (size_t i = 0; i < n; ++i)
    {
        const double f = xToFreq (a.getX() + (float) i);
        double total = 0.0;
        for (int b = 0; b < eq::numBands; ++b)
        {
            if (! on[(size_t) b] && b != selected)
                continue;
            const double db = f < sr * 0.5 ? eq::magnitudeDb (bc[(size_t) b], f, sr) : -120.0;
            if (on[(size_t) b]) total += db;
            if (b == selected)  selectedCurveDb[i] = (float) db;
        }
        totalCurveDb[i] = (float) total;
    }
}

void ResponseDisplay::processSpectrum (std::vector<float>& smoothed, bool gotNewData)
{
    if (! gotNewData)
    {
        for (auto& v : smoothed)
            v = juce::jmax (-120.0f, v - 1.5f);   // langsam ausblenden, wenn kein Audio kommt
        return;
    }

    const double sr = proc.currentSampleRate.load();
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const float norm = 4.0f / (float) fftSize;   // Hann-Fenster (0.5) + einseitiges Spektrum
    const int maxBin = fftSize / 2;
    const auto a = plotArea();

    for (size_t i = 0; i < smoothed.size(); ++i)
    {
        const double f0 = xToFreq (a.getX() + (float) i - 0.5f);
        const double f1 = xToFreq (a.getX() + (float) i + 0.5f);
        const double b0 = f0 * fftSize / sr, b1 = f1 * fftSize / sr;

        float mag = 0.0f;
        if (b1 - b0 < 1.0)
        {
            const double b = juce::jlimit (0.0, (double) maxBin - 1.0, 0.5 * (b0 + b1));
            const int idx = (int) b;
            const float frac = (float) (b - idx);
            mag = fftData[(size_t) idx] * (1.0f - frac) + fftData[(size_t) idx + 1] * frac;
        }
        else
        {
            for (int k = juce::jmax (0, (int) b0); k <= juce::jmin ((int) std::ceil (b1), maxBin); ++k)
                mag = juce::jmax (mag, fftData[(size_t) k]);
        }

        const double fc = std::sqrt (f0 * f1);
        float db = juce::Decibels::gainToDecibels (mag * norm, -120.0f)
                 + tiltDbPerOct * (float) std::log2 (fc / 1000.0);
        if (fc >= sr * 0.5)
            db = -120.0f;

        auto& v = smoothed[i];
        v += (db - v) * (db > v ? 0.6f : 0.12f);   // schneller Anstieg, langsamer Abfall
    }
}

void ResponseDisplay::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
    const bool wantPre  = mode == KlangbildAudioProcessor::analyzerPre  || mode == KlangbildAudioProcessor::analyzerBoth;
    const bool wantPost = mode == KlangbildAudioProcessor::analyzerPost || mode == KlangbildAudioProcessor::analyzerBoth;

    if (wantPre)  processSpectrum (preDb,  proc.preFifo.pull (fftData.data()));
    else          std::fill (preDb.begin(), preDb.end(), -120.0f);

    if (wantPost) processSpectrum (postDb, proc.postFifo.pull (fftData.data()));
    else          std::fill (postDb.begin(), postDb.end(), -120.0f);

    updateCurves();
    repaint();
}

//==============================================================================
void ResponseDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto a = plotArea();

    // Röhrenbildschirm
    g.setGradientFill (juce::ColourGradient (crt::screenCentre, bounds.getCentreX(), bounds.getCentreY(),
                                             crt::screenEdge, bounds.getX(), bounds.getY(), true));
    g.fillRect (bounds);

    // --- Raster ---------------------------------------------------------
    g.setFont (lnf.labelFontRegular (12.0f));
    const int freqLines[] = { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (int f : freqLines)
    {
        const bool strong = (f == 100 || f == 1000 || f == 10000);
        g.setColour (strong ? theme::gridStrong : theme::grid);
        g.drawVerticalLine (juce::roundToInt (freqToX ((float) f)), a.getY(), a.getBottom());
    }

    const std::pair<float, const char*> freqLabels[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                         { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
    g.setColour (theme::textDim);
    for (auto& [f, txt] : freqLabels)
        g.drawText (txt, juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ freqToX (f), a.getBottom() + 11.0f }),
                    juce::Justification::centred);

    for (int db = -18; db <= 18; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour (db == 0 ? theme::gridStrong.brighter (0.3f) : theme::grid);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (theme::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getRight() + 2.0f, y - 8.0f, 32.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (a.toNearestInt());

    // --- Analyzer -------------------------------------------------------
    auto spectrumPath = [&] (const std::vector<float>& data, bool closed)
    {
        juce::Path p;
        for (size_t i = 0; i < data.size(); ++i)
        {
            const float x = a.getX() + (float) i;
            const float y = juce::jlimit (a.getY(), a.getBottom() + 2.0f, analyzerDbToY (data[i]));
            if (i == 0) p.startNewSubPath (x, closed ? a.getBottom() + 2.0f : y);
            if (i == 0 && closed) p.lineTo (x, y);
            else if (i > 0) p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (a.getRight(), a.getBottom() + 2.0f);
            p.closeSubPath();
        }
        return p;
    };

    if (! preDb.empty())
    {
        g.setColour (theme::preSpectrum.withAlpha (0.45f));
        g.fillPath (spectrumPath (preDb, true));
    }
    if (! postDb.empty())
    {
        g.setGradientFill (juce::ColourGradient (theme::postSpectrum.withAlpha (0.13f), 0, a.getY(),
                                                 theme::postSpectrum.withAlpha (0.02f), 0, a.getBottom(), false));
        g.fillPath (spectrumPath (postDb, true));
        g.setColour (theme::postSpectrum.withAlpha (0.18f));
        g.strokePath (spectrumPath (postDb, false), juce::PathStrokeType (4.0f));
        g.setColour (theme::postSpectrum.withAlpha (0.85f));
        g.strokePath (spectrumPath (postDb, false), juce::PathStrokeType (1.3f));
    }

    // --- Kurve des gewählten Bands ---------------------------------------
    const float zeroY = gainToY (0.0f);
    if (selected >= 0 && ! selectedCurveDb.empty())
    {
        const bool on = proc.getBandSettings (selected).on;
        juce::Path p;
        p.startNewSubPath (a.getX(), zeroY);
        for (size_t i = 0; i < selectedCurveDb.size(); ++i)
            p.lineTo (a.getX() + (float) i, juce::jlimit (a.getY() - 2.0f, a.getBottom() + 2.0f, gainToY (selectedCurveDb[i])));
        p.lineTo (a.getRight(), zeroY);
        p.closeSubPath();
        const auto col = theme::band (selected);
        g.setColour (col.withAlpha (on ? 0.18f : 0.07f));
        g.fillPath (p);
        g.setColour (col.withAlpha (on ? 0.7f : 0.3f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // --- Summenkurve ------------------------------------------------------
    {
        juce::Path p;
        for (size_t i = 0; i < totalCurveDb.size(); ++i)
        {
            const float y = juce::jlimit (a.getY() - 2.0f, a.getBottom() + 2.0f, gainToY (totalCurveDb[i]));
            if (i == 0) p.startNewSubPath (a.getX(), y);
            else        p.lineTo (a.getX() + (float) i, y);
        }
        g.setColour (theme::curve.withAlpha (0.10f));
        g.strokePath (p, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::curve.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::curve);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- Punkte -----------------------------------------------------------
    for (int pass = 0; pass < 2; ++pass)   // gewähltes Band zuletzt zeichnen
    {
        for (int b = 0; b < eq::numBands; ++b)
        {
            if ((pass == 0) == (b == selected))
                continue;
            const auto pos = nodePosition (b);
            const bool on = proc.getBandSettings (b).on;
            const bool hi = (b == selected || b == hovered || b == dragging);
            const float r = hi ? 9.5f : 8.0f;
            const auto col = theme::band (b);
            const auto circle = juce::Rectangle<float> (r * 2, r * 2).withCentre (pos);

            if (on)
            {
                g.setColour (col.withAlpha (0.9f));
                g.fillEllipse (circle);
            }
            else
            {
                g.setColour (theme::display.withAlpha (0.85f));
                g.fillEllipse (circle);
                g.setColour (col.withAlpha (0.55f));
                g.drawEllipse (circle, 1.5f);
            }
            if (b == selected)
            {
                g.setColour (theme::curve);
                g.drawEllipse (circle.expanded (2.5f), 1.5f);
            }
            g.setColour (on ? juce::Colours::black.withAlpha (0.8f) : col.withAlpha (0.8f));
            g.setFont (lnf.labelFont (12.0f, 0.04f));
            g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
        }
    }

    // --- Info-Box beim Ziehen / Überfahren ---------------------------------
    const int infoBand = dragging >= 0 ? dragging : hovered;
    if (infoBand >= 0)
    {
        auto txt = [&] (const char* n) { return proc.apvts.getParameter (KlangbildAudioProcessor::paramID (infoBand, n))->getCurrentValueAsText(); };
        const bool cut = eq::isCut (eq::typeForBand (infoBand));
        juce::String line = txt ("freq");
        if (! cut) line << "   " << txt ("gain");
        line << "   Q " << txt ("q");
        if (cut)   line << "   " << txt ("slope");

        const auto title = KlangbildAudioProcessor::bandNames()[infoBand]
                         + (proc.getBandSettings (infoBand).on ? juce::String() : juce::String (" (aus)"));
        const float w = juce::jmax (150.0f, 7.0f * (float) line.length() + 16.0f);
        auto pos = nodePosition (infoBand);
        auto box = juce::Rectangle<float> (w, 40.0f).withCentre ({ pos.x, pos.y - 38.0f });
        if (box.getY() < a.getY() + 2.0f) box.setY (pos.y + 18.0f);
        box = box.constrainedWithin (a.reduced (2.0f));

        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (theme::band (infoBand));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setFont (lnf.labelFont (12.0f, 0.04f));
        g.drawText (title, box.withHeight (20.0f).withTrimmedTop (3.0f), juce::Justification::centred);
        g.setColour (theme::text);
        g.setFont (lnf.labelFontRegular (13.0f));
        g.drawText (line, box.withTrimmedTop (18.0f).withTrimmedBottom (3.0f), juce::Justification::centred);
    }
}

//==============================================================================
void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTestNode (e.position);
    if (dragging < 0)
        return;

    selected = dragging;
    if (onBandSelected)
        onBandSelected (dragging);

    for (auto* n : { "freq", "gain" })
        if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (dragging, n)))
            p->beginChangeGesture();
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || e.getDistanceFromDragStart() < 2)
        return;

    // Ein ausgeschaltetes Band wird beim Ziehen automatisch eingeschaltet
    if (! proc.getBandSettings (dragging).on)
        setParam (KlangbildAudioProcessor::paramID (dragging, "on"), 1.0f);

    const auto a = plotArea();
    const float x = juce::jlimit (a.getX(), a.getRight(), e.position.x);
    setParam (KlangbildAudioProcessor::paramID (dragging, "freq"), juce::jlimit (minFreq, maxFreq, xToFreq (x)));

    if (! eq::isCut (eq::typeForBand (dragging)))
        setParam (KlangbildAudioProcessor::paramID (dragging, "gain"),
                  juce::jlimit (-eqRangeDb, eqRangeDb, yToGain (e.position.y)));
    repaint();
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* n : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (dragging, n)))
                p->endChangeGesture();
    dragging = -1;
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h < 0)
        return;
    if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (h, "on")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int h = hitTestNode (e.position);
    const int band = h >= 0 ? h : selected;
    if (band < 0)
        return;

    if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (band, "q")))
    {
        const float q = proc.getBandSettings (band).q;
        const float delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        const float newQ = juce::jlimit (0.1f, 18.0f, q * std::pow (2.0f, delta * 2.0f));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (newQ));
        p->endChangeGesture();
    }
}

//==============================================================================
KlangbildAudioProcessorEditor::KlangbildAudioProcessorEditor (KlangbildAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p, lnf)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (display);
    display.onBandSelected = [this] (int b) { selectBand (b); };
    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);

    const auto names = KlangbildAudioProcessor::bandNames();
    for (int b = 0; b < eq::numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        btn.setButtonText (names[b]);
        btn.setColour (juce::TextButton::buttonOnColourId, crt::band (b));
        btn.onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (btn);
    }

    for (auto* s : { &freqSlider, &gainSlider, &qSlider, &outSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (220);
        addAndMakeVisible (*s);
    }

    onButton.setButtonText ("on");   // zeigt AN/AUS neben dem Schalter
    addAndMakeVisible (onButton);

    slopeBox.addItemList ({ "12 dB/Okt", "24 dB/Okt", "36 dB/Okt", "48 dB/Okt" }, 1);
    addAndMakeVisible (slopeBox);

    analyzerBox.getProperties().set ("plate", true);
    analyzerBox.addItemList ({ "Aus", "Pre", "Post", "Pre + Post" }, 1);
    addAndMakeVisible (analyzerBox);

    outAtt      = std::make_unique<SliderAttachment> (proc.apvts, "output", outSlider);
    analyzerAtt = std::make_unique<ComboAttachment>  (proc.apvts, "analyzer", analyzerBox);
    outSlider.setDoubleClickReturnValue (true, 0.0);

    selectBand (2);
    setSize (1000, 700);
    startTimerHz (60);
}

KlangbildAudioProcessorEditor::~KlangbildAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KlangbildAudioProcessorEditor::selectBand (int band)
{
    selectedBand = band;
    display.setSelectedBand (band);
    for (int b = 0; b < eq::numBands; ++b)
        bandButtons[(size_t) b].setToggleState (b == band, juce::dontSendNotification);

    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); onAtt.reset(); slopeAtt.reset();
    auto id = [band] (const char* n) { return KlangbildAudioProcessor::paramID (band, n); };
    freqAtt = std::make_unique<SliderAttachment> (proc.apvts, id ("freq"), freqSlider);
    gainAtt = std::make_unique<SliderAttachment> (proc.apvts, id ("gain"), gainSlider);
    qAtt    = std::make_unique<SliderAttachment> (proc.apvts, id ("q"),    qSlider);
    onAtt   = std::make_unique<ButtonAttachment> (proc.apvts, id ("on"),   onButton);

    for (auto [s, n] : { std::pair { &freqSlider, "freq" }, std::pair { &gainSlider, "gain" }, std::pair { &qSlider, "q" } })
        if (auto* rp = proc.apvts.getParameter (id (n)))
            s->setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));

    onButton.getProperties().set ("lamp", (int) crt::band (band).getARGB());

    const bool cut = eq::isCut (eq::typeForBand (band));
    gainSlider.setEnabled (! cut);
    gainSlider.setAlpha (cut ? 0.45f : 1.0f);
    slopeBox.setVisible (cut);
    if (cut)
        slopeAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("slope"), slopeBox);
    repaint();
}

void KlangbildAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);

    for (int b = 0; b < eq::numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        const bool on = proc.getBandSettings (b).on;
        if ((bool) btn.getProperties()["bandOn"] != on)
        {
            btn.getProperties().set ("bandOn", on);
            btn.repaint();
        }
    }
}

//==============================================================================
void KlangbildAudioProcessorEditor::renderTextures()
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
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colour (0xff6b5534)).withAlpha (0.015f + rnd.nextFloat() * 0.035f));
        g.drawHorizontalLine (y, 0.0f, r.getWidth());
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colour (0x30694a1c), 0, 0, true));
    g.fillRoundedRectangle (r, 6.0f);
}

void KlangbildAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                                  juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText (text, r.translated (0.0f, 1.0f), j);
    g.setColour (ink);
    g.drawText (text, r, j);
}

void KlangbildAudioProcessorEditor::paint (juce::Graphics& g)
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
    g.drawText ("EQ", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 120.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("8-BAND-EQUALIZER \xc2\xb7 ANALYZER"), juce::Rectangle<float> (44.0f, 90.0f, 400.0f, 16.0f),
                juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.6f));
    g.setFont (lnf.labelFont (10.0f, 0.3f));
    g.drawText ("ANALYZER", analyzerArea.toFloat().withY ((float) analyzerArea.getY() - 15.0f).withHeight (13.0f), juce::Justification::centredLeft);

    // Bildschirmrahmen
    const auto bz = bezel.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (bz.translated (2.0f, 4.0f), 14.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e2e2), 0, bz.getY(), juce::Colour (0xff5c5c5c), 0, bz.getBottom(), false));
    g.fillRoundedRectangle (bz, 14.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2622), 0, bz.getY(), juce::Colour (0xff100d0b), 0, bz.getBottom(), false));
    g.fillRoundedRectangle (bz.reduced (4.0f), 11.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (display.getBounds().toFloat().expanded (3.0f), 20.0f);
    for (auto [x, y, a] : { std::tuple { bz.getX() + 13.0f, bz.getY() + 13.0f, 0.4f }, std::tuple { bz.getRight() - 13.0f, bz.getY() + 13.0f, 1.3f },
                            std::tuple { bz.getX() + 13.0f, bz.getBottom() - 13.0f, 2.2f }, std::tuple { bz.getRight() - 13.0f, bz.getBottom() - 13.0f, 0.9f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 4.5f, a);

    // Frontplatte
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

    // Beschriftungen
    const auto lf = lnf.labelFont (13.0f, 0.14f);
    drawEngraved (g, "BANDWAHL", bandLabelArea.toFloat(), lf, juce::Justification::centredLeft);
    for (auto [s, n] : { std::pair { &freqSlider, "FREQUENZ" }, std::pair { &gainSlider, "GAIN" }, std::pair { &qSlider, "GÜTE (Q)" }, std::pair { &outSlider, "PEGEL" } })
    {
        const auto sb = s->getBounds().toFloat();
        drawEngraved (g, juce::String (juce::CharPointer_UTF8 (n)), { sb.getX(), sb.getBottom() - 2.0f, sb.getWidth(), 18.0f }, lf, juce::Justification::centred);
    }
    drawEngraved (g, KlangbildAudioProcessor::bandNames()[selectedBand].toUpperCase(), onLabelArea.toFloat(), lf, juce::Justification::centredLeft);
    if (slopeBox.isVisible())
        drawEngraved (g, "FLANKE", slopeLabelArea.toFloat(), lf, juce::Justification::centredLeft);
    drawEngraved (g, juce::CharPointer_UTF8 ("Punkt ziehen: Frequenz/Gain  \xc2\xb7  Mausrad: G\xc3\xbc" "te  \xc2\xb7  Doppelklick: an/aus"),
                  hintArea.toFloat(), lnf.labelFontRegular (11.5f), juce::Justification::centredLeft);

    // Trennlinien zwischen den Spalten
    for (int x : { freqSlider.getX() - 8, onButton.getX() - 11, outSlider.getX() - 6 })
        for (int dx : { -2, 2 })
        {
            g.setColour (ink.withAlpha (0.35f));
            g.drawVerticalLine (x + dx, fp.getY() + 14.0f, fp.getBottom() - 14.0f);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawVerticalLine (x + dx + 1, fp.getY() + 14.0f, fp.getBottom() - 14.0f);
        }
}

void KlangbildAudioProcessorEditor::resized()
{
    const int W = getWidth();
    vuRight.setBounds (W - 36 - 200, 20, 200, 130);
    vuLeft.setBounds (W - 36 - 200 - 16 - 200, 20, 200, 130);

    analyzerArea = { 44, 128, 230, 32 };
    analyzerBox.setBounds (analyzerArea);

    bezel = { 24, 178, W - 48, 318 };
    display.setBounds (bezel.reduced (20, 18));

    faceplate = { 24, 510, W - 48, 172 };
    auto inner = faceplate.reduced (30, 12);

    // Spalte 1: Bandwahl
    auto bands = inner.removeFromLeft (318);
    bandLabelArea = bands.removeFromTop (22).withTrimmedTop (4);
    bands.removeFromTop (4);
    const int bw = 76, bh = 34, gap = 4;
    for (int b = 0; b < eq::numBands; ++b)
    {
        const int row = b / 4, col = b % 4;
        bandButtons[(size_t) b].setBounds (bands.getX() + col * (bw + gap), bands.getY() + row * (bh + 6), bw, bh);
    }
    hintArea = { bands.getX(), bands.getY() + 2 * (bh + 6) + 6, 330, 18 };

    inner.removeFromLeft (16);
    // Spalte 2: Knöpfe
    constexpr int cellW = 96, sliderH = 118;
    freqSlider.setBounds (inner.removeFromLeft (cellW).withHeight (sliderH));
    gainSlider.setBounds (inner.removeFromLeft (cellW).withHeight (sliderH));
    qSlider.setBounds    (inner.removeFromLeft (cellW).withHeight (sliderH));

    inner.removeFromLeft (22);
    // Spalte 3: Band an/aus, Flanke
    auto ctl = inner.removeFromLeft (140);
    onLabelArea = ctl.removeFromTop (22).withTrimmedTop (4);
    onButton.setBounds (ctl.removeFromTop (38).withWidth (118));
    ctl.removeFromTop (8);
    slopeLabelArea = ctl.removeFromTop (18);
    slopeBox.setBounds (ctl.removeFromTop (28).withWidth (128));

    // Spalte 4: Pegel
    outSlider.setBounds (inner.withSizeKeepingCentre (juce::jmin (110, inner.getWidth()), inner.getHeight()).withHeight (sliderH));

    renderTextures();
}
