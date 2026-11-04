#include "PluginEditor.h"

using namespace vintage;

namespace
{
juce::File& lastDirectory()
{
    static juce::File dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return dir;
}
}

//==============================================================================
DuoAudioProcessorEditor::DuoAudioProcessorEditor (DuoAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    inKnob    = &addKnob ("in",      "EINGANG");
    gateKnob  = &addKnob ("gate",    "NOISE GATE");
    levelA    = &addKnob ("level_a", "PEGEL A");
    levelB    = &addKnob ("level_b", "PEGEL B");
    blendKnob = &addKnob ("blend",   "A / B");
    outKnob   = &addKnob ("out",     "AUSGANG");
    blendKnob->slider.getProperties().set ("abScale", true);

    setupSwitch (modeSwitch,  "mode",      "STEREO", "MIX");
    setupSwitch (phaseSwitch, "phase_b",   "GEDREHT", "NORMAL");
    setupSwitch (normSwitch,  "normalize", "AN", "AUS");

    for (int s = 0; s < 2; ++s)
    {
        auto& lb = loadButtons[(size_t) s];
        lb.setButtonText ("Laden");
        lb.getProperties().set ("noLamp", true);
        lb.setColour (juce::TextButton::buttonOnColourId, gold);
        lb.onClick = [this, s] { chooseModel (s); };
        addAndMakeVisible (lb);

        auto& cb = clearButtons[(size_t) s];
        cb.setButtonText ("Leeren");
        cb.getProperties().set ("noLamp", true);
        cb.setColour (juce::TextButton::buttonOnColourId, inkLight);
        cb.onClick = [this, s] { proc.clearModel (s); };
        addAndMakeVisible (cb);
    }

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);

    sections[0] = { "EINGANG",  {} };
    sections[1] = { "KANAL A",  {} };
    sections[2] = { "MISCHUNG", {} };
    sections[3] = { "KANAL B",  {} };
    sections[4] = { "AUSGANG",  {} };

    refreshInfo();
    setSize (1100, 560);
    startTimerHz (60);
}

DuoAudioProcessorEditor::~DuoAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

DuoAudioProcessorEditor::Knob& DuoAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
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

void DuoAudioProcessorEditor::setupSwitch (juce::ToggleButton& b, const juce::String& paramID,
                                           const juce::String& onText, const juce::String& offText)
{
    b.setButtonText ("x");
    b.getProperties().set ("onText", onText);
    b.getProperties().set ("offText", offText);
    b.getProperties().set ("lamp", (int) lampAmber.getARGB());
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, paramID, b));
}

void DuoAudioProcessorEditor::chooseModel (int slot)
{
    auto start = info[(size_t) slot].loaded ? info[(size_t) slot].file.getParentDirectory() : lastDirectory();
    chooser = std::make_unique<juce::FileChooser> (juce::String ("NAM-Profil f") + juce::String (juce::CharPointer_UTF8 ("\xc3\xbc")) + "r Kanal " + (slot == 0 ? "A" : "B"),
                                                   start, "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              lastDirectory() = f.getParentDirectory();
                              juce::String err;
                              proc.loadModel (slot, f, err);
                          });
}

void DuoAudioProcessorEditor::refreshInfo()
{
    for (int s = 0; s < 2; ++s)
        info[(size_t) s] = proc.getSlotInfo (s);
    lastVersion = proc.modelVersion.load();
    repaint();
}

void DuoAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);

    if (proc.modelVersion.load() != lastVersion)
        refreshInfo();

    const bool gate = proc.gateOpen.load();
    if (gate != lastGate)
    {
        lastGate = gate;
        repaint (gateLampArea.expanded (12));
    }
}

//==============================================================================
int DuoAudioProcessorEditor::slotForPosition (int x) const
{
    return x < getWidth() / 2 ? 0 : 1;
}

bool DuoAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".nam"))
            return true;
    return false;
}

void DuoAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int x, int) { dragSlot = slotForPosition (x); repaint(); }
void DuoAudioProcessorEditor::fileDragMove (const juce::StringArray&, int x, int)
{
    const int s = slotForPosition (x);
    if (s != dragSlot) { dragSlot = s; repaint(); }
}
void DuoAudioProcessorEditor::fileDragExit (const juce::StringArray&) { dragSlot = -1; repaint(); }

void DuoAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int)
{
    dragSlot = -1;
    int slot = slotForPosition (x);
    for (auto& path : files)
    {
        if (! path.endsWithIgnoreCase (".nam"))
            continue;
        juce::String err;
        const juce::File f (path);
        lastDirectory() = f.getParentDirectory();
        proc.loadModel (slot, f, err);
        slot = 1 - slot;   // zwei Dateien auf einmal: A und B
    }
    repaint();
}

//==============================================================================
void DuoAudioProcessorEditor::renderTextures()
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

void DuoAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                            juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText (text, r.translated (0.0f, 1.0f), j);
    g.setColour (ink);
    g.drawText (text, r, j);
}

void DuoAudioProcessorEditor::drawPlate (juce::Graphics& g, juce::Rectangle<float> r, const DuoAudioProcessor::SlotInfo& si, bool highlight)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (1.0f, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, r.getY(), juce::Colour (0xff120f0c), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlight ? gold : juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, highlight ? 2.0f : 1.0f);
    VintageLookAndFeel::drawScrew (g, { r.getX() + 10.0f, r.getCentreY() }, 3.5f, 0.7f);
    VintageLookAndFeel::drawScrew (g, { r.getRight() - 10.0f, r.getCentreY() }, 3.5f, 2.0f);

    const auto textArea = r.reduced (20.0f, 2.0f).toNearestInt();
    if (highlight)
    {
        g.setColour (gold);
        g.setFont (lnf.labelFont (14.0f, 0.1f));
        g.drawFittedText ("HIER ABLEGEN", textArea, juce::Justification::centred, 1);
    }
    else if (si.loaded)
    {
        g.setColour (logo);
        g.setFont (lnf.labelFont (15.0f, 0.03f));
        g.drawFittedText (si.name, textArea, juce::Justification::centred, 1, 0.7f);
    }
    else
    {
        g.setColour (logo.withAlpha (0.4f));
        g.setFont (lnf.labelFontRegular (14.0f));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94 kein Profil \xe2\x80\x94")), textArea, juce::Justification::centred, 1);
    }
}

void DuoAudioProcessorEditor::paint (juce::Graphics& g)
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
    g.drawText ("DUO", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 120.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("ZWEI NAM-PROFILE \xc2\xb7 EIN SIGNAL"), juce::Rectangle<float> (44.0f, 90.0f, 420.0f, 16.0f),
                juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.4f));
    g.setFont (lnf.labelFontRegular (12.0f));
    juce::String hint = juce::String (juce::CharPointer_UTF8 (".nam-Dateien einfach auf Kanal A oder B ziehen"));
    if (proc.getLatencySamples() > 0)
        hint << juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  Latenz ")) << proc.getLatencySamples() << " Samples";
    g.drawText (hint, juce::Rectangle<float> (44.0f, 118.0f, 520.0f, 16.0f), juce::Justification::centredLeft);

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

    // Sektionen
    for (size_t i = 0; i < sections.size(); ++i)
    {
        const auto sb = sections[i].bounds.toFloat();
        drawEngraved (g, sections[i].title, sb.withHeight (36.0f).withTrimmedTop (4.0f), lnf.labelFont (18.0f, 0.22f), juce::Justification::centredLeft);
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

    // Kanal-Lampen (leuchten, wenn ein Profil geladen ist)
    for (int s = 0; s < 2; ++s)
    {
        const auto sb = sections[(size_t) (s == 0 ? 1 : 3)].bounds.toFloat();
        VintageLookAndFeel::drawJewel (g, juce::Rectangle<float> (20.0f, 20.0f).withPosition (sb.getRight() - 24.0f, sb.getY() + 8.0f),
                                       s == 0 ? lampRed : lampGreen, info[(size_t) s].loaded);
        drawPlate (g, plates[(size_t) s].toFloat(), info[(size_t) s], dragSlot == s);

        const auto& si = info[(size_t) s];
        juce::String line = si.error.isNotEmpty() ? si.error : (si.loaded ? si.details : juce::String ("Ohne Profil wird das Signal durchgereicht"));
        g.setColour (si.error.isNotEmpty() ? juce::Colour (0xffa02a1a) : inkLight);
        g.setFont (lnf.labelFontRegular (12.0f));
        g.drawFittedText (line, infoAreas[(size_t) s], juce::Justification::centred, 2, 0.85f);
    }

    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        drawEngraved (g, k->name, juce::Rectangle<float> (sb.getX(), sb.getBottom() - 2.0f, sb.getWidth(), 18.0f),
                      lnf.labelFont (13.0f, 0.14f), juce::Justification::centred);
    }

    const char* switchNames[] = { "MODUS", "PHASE B", "LAUTHEIT\nANGLEICHEN" };
    for (size_t i = 0; i < 3; ++i)
    {
        const auto r = switchLabels[i].toFloat();
        g.setFont (lnf.labelFont (12.0f, 0.12f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawFittedText (switchNames[i], r.translated (0, 1).toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
        g.setColour (ink);
        g.drawFittedText (switchNames[i], r.toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
    }

    // Gate-Lampe
    const bool gateActive = proc.apvts.getRawParameterValue ("gate")->load() > -89.95f;
    VintageLookAndFeel::drawJewel (g, gateLampArea.toFloat(), lampGreen, gateActive && proc.gateOpen.load());

    // Betriebslampe und Typenschild
    const auto pa = pilotArea.toFloat();
    VintageLookAndFeel::drawJewel (g, juce::Rectangle<float> (34.0f, 34.0f).withCentre ({ pa.getCentreX(), pa.getY() + 30.0f }), lampRed, true);
    drawEngraved (g, "BETRIEB", juce::Rectangle<float> (pa.getX(), pa.getY() + 52.0f, pa.getWidth(), 16.0f), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);
    const auto tag = juce::Rectangle<float> (pa.getWidth() - 16.0f, 26.0f).withCentre ({ pa.getCentreX(), pa.getBottom() - 24.0f });
    g.setColour (ink.withAlpha (0.85f));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (gold);
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("MODELL KD-2", tag, juce::Justification::centred);
}

void DuoAudioProcessorEditor::resized()
{
    const int W = getWidth();
    vuRight.setBounds (W - 36 - 200, 20, 200, 130);
    vuLeft.setBounds (W - 36 - 200 - 16 - 200, 20, 200, 130);

    faceplate = { 24, 178, W - 48, 362 };
    auto inner = faceplate.reduced (24, 14);
    const int widths[] = { 140, 240, 220, 240, 120 };
    int sum = 0;
    for (int w : widths) sum += w;
    const int gap = (inner.getWidth() - sum) / 4;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        sections[i].bounds = inner.removeFromLeft (widths[i]).withHeight (inner.getHeight());
        inner.removeFromLeft (gap);
    }

    constexpr int sliderH = 116;
    auto sec = [this] (int i) { return sections[(size_t) i].bounds; };

    // Eingang
    inKnob->slider.setBounds (sec (0).getX(), sec (0).getY() + 48, sec (0).getWidth(), sliderH);
    gateKnob->slider.setBounds (sec (0).getX(), sec (0).getY() + 48 + 146, sec (0).getWidth(), sliderH);
    gateLampArea = juce::Rectangle<int> (14, 14).withPosition (sec (0).getRight() - 18, sec (0).getY() + 48 + 146 + 2);

    // Kanäle
    for (int s = 0; s < 2; ++s)
    {
        const auto b = sec (s == 0 ? 1 : 3);
        plates[(size_t) s] = juce::Rectangle<int> (b.getX() + 4, b.getY() + 54, b.getWidth() - 8, 36);
        infoAreas[(size_t) s] = juce::Rectangle<int> (b.getX(), b.getY() + 94, b.getWidth(), 30);
        loadButtons[(size_t) s].setBounds (b.getX() + 24, b.getY() + 130, 100, 32);
        clearButtons[(size_t) s].setBounds (b.getX() + 132, b.getY() + 130, 84, 32);
        auto* lk = s == 0 ? levelA : levelB;
        lk->slider.setBounds (b.getX(), b.getY() + 48 + 146, b.getWidth(), sliderH);
    }

    // Mischung
    {
        const auto b = sec (2);
        blendKnob->slider.setBounds (b.getX(), b.getY() + 44, b.getWidth(), 150);
        auto rows = juce::Rectangle<int> (b.getX() + 6, b.getY() + 216, b.getWidth() - 12, 114);
        juce::ToggleButton* sws[] = { &modeSwitch, &phaseSwitch, &normSwitch };
        for (int i = 0; i < 3; ++i)
        {
            auto row = rows.removeFromTop (38);
            switchLabels[(size_t) i] = row.removeFromLeft (96);
            sws[i]->setBounds (row.removeFromLeft (124).withSizeKeepingCentre (124, 34));
        }
    }

    // Ausgang
    outKnob->slider.setBounds (sec (4).getX(), sec (4).getY() + 48, sec (4).getWidth(), sliderH);
    pilotArea = juce::Rectangle<int> (sec (4).getX(), sec (4).getY() + 48 + 140, sec (4).getWidth(), 140);

    renderTextures();
}
