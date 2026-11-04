#include "PluginEditor.h"

using namespace vintage;

namespace
{
juce::String utf8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

juce::String signedDb (float v, const char* unit)
{
    return (v > 0.05f ? "+" : (v < -0.05f ? utf8 ("\xe2\x88\x92") : "")) + juce::String (std::abs (v), 1) + " " + unit;
}

juce::File& lastDirectory()
{
    static juce::File dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    return dir;
}
}

//==============================================================================
KalibratorAudioProcessorEditor::KalibratorAudioProcessorEditor (KalibratorAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    ifaceKnob  = &addKnob ("iface",  "MAX. EINGANG");
    refKnob    = &addKnob ("ref",    "REFERENZ");
    targetKnob = &addKnob ("target", "ZIEL-SPITZE");
    trimKnob   = &addKnob ("trim",   "FEINABGLEICH");

    for (int i = 0; i < kalibrator::numInterfacePresets; ++i)
    {
        const auto& ip = kalibrator::interfacePresets[i];
        interfaceBox.addItem (i == 0 ? juce::String (ip.name)
                                     : juce::String (ip.name) + "  (" + signedDb (ip.dbu, "dBu") + ")", i + 1);
    }
    interfaceBox.setSelectedId (proc.getInterfacePreset() + 1, juce::dontSendNotification);
    interfaceBox.onChange = [this] { proc.setInterfacePreset (interfaceBox.getSelectedId() - 1); };
    addAndMakeVisible (interfaceBox);

    readButton.setButtonText ("Profil lesen");
    readButton.getProperties().set ("noLamp", true);
    readButton.setColour (juce::TextButton::buttonOnColourId, gold);
    readButton.onClick = [this] { chooseProfile(); };
    addAndMakeVisible (readButton);

    measureButton.setButtonText ("Messen");
    measureButton.setColour (juce::TextButton::buttonOnColourId, lampRed);
    measureButton.onClick = [this]
    {
        if (proc.isMeasuring()) proc.cancelMeasurement();
        else                    proc.startMeasurement();
    };
    addAndMakeVisible (measureButton);

    modeSwitch.setButtonText ("x");
    modeSwitch.getProperties().set ("onText", "MESSUNG");
    modeSwitch.getProperties().set ("offText", "DBU-WERTE");
    modeSwitch.getProperties().set ("lamp", (int) lampAmber.getARGB());
    addAndMakeVisible (modeSwitch);
    modeAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "mode", modeSwitch);

    addAndMakeVisible (vuIn);
    addAndMakeVisible (vuOut);

    sections[0] = { "INTERFACE", {} };
    sections[1] = { "PROFIL",    {} };
    sections[2] = { "MESSUNG",   {} };
    sections[3] = { "ERGEBNIS",  {} };

    profile = proc.getProfileInfo();
    setSize (1040, 560);
    startTimerHz (60);
}

KalibratorAudioProcessorEditor::~KalibratorAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

KalibratorAudioProcessorEditor::Knob& KalibratorAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.setMouseDragSensitivity (260);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramID, k->slider);
    if (auto* rp = proc.apvts.getParameter (paramID))
        k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void KalibratorAudioProcessorEditor::chooseProfile()
{
    chooser = std::make_unique<juce::FileChooser> ("NAM-Profil auslesen", lastDirectory(), "*.nam");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              lastDirectory() = f.getParentDirectory();
                              proc.readProfile (f);
                          });
}

bool KalibratorAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".nam"))
            return true;
    return false;
}

void KalibratorAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".nam"))
        {
            lastDirectory() = juce::File (f).getParentDirectory();
            proc.readProfile (juce::File (f));
            break;
        }
}

void KalibratorAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuIn.update (proc.vuIn.load(), proc.peakIn.exchange (0.0f), dt);
    vuOut.update (proc.vuOut.load(), proc.peakOut.exchange (0.0f), dt);

    proc.pollMeasurement();

    // Wird der Regler von Hand verstellt, passt das gewählte Interface nicht mehr
    const int preset = proc.getInterfacePreset();
    if (preset > 0 && std::abs ((float) ifaceKnob->slider.getValue() - kalibrator::interfacePresets[preset].dbu) > 0.05f)
        proc.setInterfacePreset (0);

    const bool byDbu = ! modeSwitch.getToggleState();
    refKnob->slider.setAlpha (byDbu ? 1.0f : 0.5f);
    ifaceKnob->slider.setAlpha (byDbu ? 1.0f : 0.5f);
    targetKnob->slider.setAlpha (byDbu ? 0.5f : 1.0f);

    const bool measuring = proc.isMeasuring();
    measureButton.setButtonText (measuring ? "Abbrechen" : "Messen");
    blink = (blink + 1) % 40;
    const bool lampOn = measuring && blink < 24;
    if ((bool) measureButton.getProperties()["bandOn"] != lampOn)
    {
        measureButton.getProperties().set ("bandOn", lampOn);
        measureButton.repaint();
    }

    bool needsRepaint = measuring;
    if (proc.uiVersion.load() != lastVersion)
    {
        lastVersion = proc.uiVersion.load();
        profile = proc.getProfileInfo();
        interfaceBox.setSelectedId (proc.getInterfacePreset() + 1, juce::dontSendNotification);
        needsRepaint = true;
    }
    const float g = proc.computeGainDb();
    if (std::abs (g - lastGain) > 0.01f)
    {
        lastGain = g;
        needsRepaint = true;
    }
    if (needsRepaint)
        repaint (faceplate);
}

//==============================================================================
void KalibratorAudioProcessorEditor::renderTextures()
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

void KalibratorAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                                   juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawFittedText (text, r.translated (0.0f, 1.0f).toNearestInt(), j, 3, 0.9f);
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt(), j, 3, 0.9f);
}

void KalibratorAudioProcessorEditor::paint (juce::Graphics& g)
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
    g.setFont (lnf.labelFont (26.0f, 0.3f));
    g.drawText ("KALIBRATOR", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 260.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (utf8 ("EINGANGSPEGEL F\xc3\x9cR NAM-PROFILE"), juce::Rectangle<float> (44.0f, 90.0f, 420.0f, 16.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.4f));
    g.setFont (lnf.labelFontRegular (12.0f));
    g.drawText (utf8 ("Direkt hinter den Gitarreneingang setzen \xe2\x80\x93 vor Klangbild Duo oder dein NAM-Plugin"),
                juce::Rectangle<float> (44.0f, 118.0f, 560.0f, 16.0f), juce::Justification::centredLeft);

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

    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        drawEngraved (g, k->name, { sb.getX(), sb.getBottom() - 2.0f, sb.getWidth(), 18.0f }, lnf.labelFont (13.0f, 0.14f), juce::Justification::centred);
    }

    const auto small = lnf.labelFontRegular (12.5f);

    // Interface
    drawEngraved (g, utf8 ("Gain-Regler am Interface auf Minimum stellen \xe2\x80\x93 nur dann stimmt der Wert."),
                  ifaceHintArea.toFloat(), small, juce::Justification::centredLeft);

    // Profil-Schild
    {
        const auto r = plateArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (1.0f, 2.0f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, r.getY(), juce::Colour (0xff120f0c), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        VintageLookAndFeel::drawScrew (g, { r.getX() + 10.0f, r.getCentreY() }, 3.5f, 0.7f);
        VintageLookAndFeel::drawScrew (g, { r.getRight() - 10.0f, r.getCentreY() }, 3.5f, 2.0f);
        g.setColour (profile.name.isNotEmpty() ? logo : logo.withAlpha (0.4f));
        g.setFont (profile.name.isNotEmpty() ? lnf.labelFont (15.0f, 0.03f) : lnf.labelFontRegular (14.0f));
        g.drawFittedText (profile.name.isNotEmpty() ? profile.name : utf8 ("\xe2\x80\x94 kein Profil gelesen \xe2\x80\x94"),
                          r.reduced (20.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

        juce::String line;
        if (profile.error.isNotEmpty())                 line = profile.error;
        else if (profile.name.isEmpty())                line = utf8 (".nam-Datei lesen oder hierher ziehen \xe2\x80\x93 oder Referenz von Hand einstellen");
        else if (profile.hasLevel)                      line = "Kalibriert auf " + signedDb (profile.levelDbu, "dBu") + (profile.details.isNotEmpty() ? utf8 (" \xc2\xb7 ") + profile.details : juce::String());
        else                                            line = utf8 ("Keine Pegelangabe im Profil \xe2\x80\x93 Referenz von Hand setzen (\xc3\xbc" "blich: +12 dBu) oder messen");
        g.setColour (profile.error.isNotEmpty() ? juce::Colour (0xffa02a1a) : inkLight);
        g.setFont (small);
        g.drawFittedText (line, profileInfoArea, juce::Justification::centred, 2, 0.85f);
    }

    // Messung
    {
        const float measured = proc.getMeasuredPeakDb();
        const bool has = measured > KalibratorAudioProcessor::noMeasurement + 1.0f;
        juce::String text;
        if (proc.isMeasuring())
        {
            const int left = (int) std::ceil ((1.0f - proc.measurementProgress()) * (float) KalibratorAudioProcessor::measureSeconds);
            text = utf8 ("Jetzt so hart wie m\xc3\xb6glich spielen \xe2\x80\xa6 noch ") + juce::String (left) + " s";
        }
        else if (has)
        {
            text = "Lauteste Spitze: " + signedDb (measured, "dBFS");
            text << "\n" << utf8 ("\xe2\x89\x88 ") << signedDb (measured + (float) ifaceKnob->slider.getValue(), "dBu") << " an der Gitarre";
        }
        else
            text = utf8 ("\xe2\x80\x9eMessen\xe2\x80\x9c dr\xc3\xbc" "cken und 10 s lang so hart spielen, wie du es live tust.");
        drawEngraved (g, text, measureTextArea.toFloat(), small, juce::Justification::centred);

        const auto bar = progressArea.toFloat();
        g.setColour (ink.withAlpha (0.25f));
        g.fillRoundedRectangle (bar, 3.0f);
        const float prog = proc.isMeasuring() ? proc.measurementProgress() : (has ? 1.0f : 0.0f);
        if (prog > 0.0f)
        {
            g.setColour (proc.isMeasuring() ? lampRed.darker (0.2f) : gold.darker (0.2f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 3.0f);
        }
    }

    // Ergebnis
    drawEngraved (g, "MODUS", modeLabelArea.toFloat(), lnf.labelFont (12.0f, 0.12f), juce::Justification::centredLeft);
    {
        const auto r = gainPlate.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (1.0f, 2.0f), 5.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, r.getY(), juce::Colour (0xff120f0c), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (gold.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (2.0f), 4.0f, 1.0f);

        const float gdb = proc.computeGainDb();
        const bool waiting = modeSwitch.getToggleState() && proc.getMeasuredPeakDb() <= KalibratorAudioProcessor::noMeasurement + 1.0f;
        g.setColour (logo);
        g.setFont (lnf.labelFont (30.0f, 0.04f));
        g.drawText (signedDb (gdb, "dB"), r.withTrimmedBottom (18.0f), juce::Justification::centred);
        g.setColour (gold);
        g.setFont (lnf.labelFont (10.5f, 0.2f));
        g.drawText (waiting ? "ERST MESSEN" : (modeSwitch.getToggleState() ? "AUS DER MESSUNG" : "AUS DEN DBU-WERTEN"),
                    r.withTrimmedTop (r.getHeight() - 22.0f), juce::Justification::centred);
    }
}

void KalibratorAudioProcessorEditor::resized()
{
    const int W = getWidth();
    vuOut.setBounds (W - 36 - 200, 20, 200, 130);
    vuIn.setBounds (W - 36 - 200 - 16 - 200, 20, 200, 130);

    faceplate = { 24, 178, W - 48, 362 };
    auto inner = faceplate.reduced (24, 14);
    const int widths[] = { 230, 250, 230, 200 };
    int sum = 0;
    for (int w : widths) sum += w;
    const int gap = (inner.getWidth() - sum) / 3;
    for (size_t i = 0; i < sections.size(); ++i)
    {
        sections[i].bounds = inner.removeFromLeft (widths[i]).withHeight (inner.getHeight());
        inner.removeFromLeft (gap);
    }
    auto sec = [this] (int i) { return sections[(size_t) i].bounds; };
    constexpr int sliderH = 116, knobY = 180;

    // Interface
    interfaceBox.setBounds (sec (0).getX(), sec (0).getY() + 54, sec (0).getWidth(), 28);
    ifaceHintArea = { sec (0).getX(), sec (0).getY() + 90, sec (0).getWidth(), 40 };
    ifaceKnob->slider.setBounds (sec (0).getX(), sec (0).getY() + knobY, sec (0).getWidth(), sliderH);

    // Profil
    plateArea = { sec (1).getX() + 2, sec (1).getY() + 52, sec (1).getWidth() - 4, 36 };
    profileInfoArea = { sec (1).getX(), sec (1).getY() + 92, sec (1).getWidth(), 34 };
    readButton.setBounds (sec (1).getCentreX() - 65, sec (1).getY() + 132, 130, 32);
    refKnob->slider.setBounds (sec (1).getX(), sec (1).getY() + knobY, sec (1).getWidth(), sliderH);

    // Messung
    measureButton.setBounds (sec (2).getCentreX() - 70, sec (2).getY() + 52, 140, 36);
    measureTextArea = { sec (2).getX(), sec (2).getY() + 94, sec (2).getWidth(), 50 };
    progressArea = { sec (2).getX() + 20, sec (2).getY() + 150, sec (2).getWidth() - 40, 7 };
    targetKnob->slider.setBounds (sec (2).getX(), sec (2).getY() + knobY, sec (2).getWidth(), sliderH);

    // Ergebnis
    modeLabelArea = { sec (3).getX(), sec (3).getY() + 52, 56, 34 };
    modeSwitch.setBounds (sec (3).getX() + 58, sec (3).getY() + 52, 140, 34);
    gainPlate = { sec (3).getX() + 4, sec (3).getY() + 96, sec (3).getWidth() - 8, 72 };
    trimKnob->slider.setBounds (sec (3).getX(), sec (3).getY() + knobY, sec (3).getWidth(), sliderH);

    renderTextures();
}
