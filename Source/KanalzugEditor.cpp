#include "KanalzugEditor.h"

using namespace vintage;

KanalzugAudioProcessorEditor::KanalzugAudioProcessorEditor (KanalzugAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    for (int i = 0; i < KanalzugAudioProcessor::numModules; ++i)
    {
        auto& t = tiles[(size_t) i];
        t.key.setButtonText (KanalzugAudioProcessor::moduleInfos()[(size_t) i].title);
        t.key.setColour (juce::TextButton::buttonOnColourId, gold);
        t.key.getProperties().set ("noLamp", true);
        t.key.onClick = [this, i] { selectModule (i); };
        addAndMakeVisible (t.key);

        t.power.setButtonText ("x");
        t.power.getProperties().set ("lamp", (int) lampRed.getARGB());
        t.power.setColour (juce::ToggleButton::textColourId, logo);
        addAndMakeVisible (t.power);
        t.attachment = std::make_unique<ButtonAttachment> (proc.apvts, KanalzugAudioProcessor::onParamID (i), t.power);
    }

    // Position des Opto-Kompressors: nach der Box (Standard) oder vor dem Drive
    optoPosSwitch.setButtonText ("x");
    optoPosSwitch.getProperties().set ("lamp", (int) lampAmber.getARGB());
    optoPosSwitch.getProperties().set ("onText", "VOR DRIVE");
    optoPosSwitch.getProperties().set ("offText", "NACH BOX");
    optoPosSwitch.setColour (juce::ToggleButton::textColourId, logo);
    addAndMakeVisible (optoPosSwitch);
    optoPosAttachment = std::make_unique<ButtonAttachment> (proc.apvts, "optopre", optoPosSwitch);
    shownOptoPre = proc.isOptoBeforeDrive();

    outSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    outSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    outSlider.getProperties().set ("noScale", true);
    outSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (outSlider);
    outAttachment = std::make_unique<SliderAttachment> (proc.apvts, "out", outSlider);

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);
    addAndMakeVisible (moduleHolder);
    addAndMakeVisible (modulePresets);

    setSize (1120, 840);
    selectModule (juce::jlimit (0, KanalzugAudioProcessor::numModules - 1, (int) proc.apvts.state.getProperty ("selected", 2)));
    startTimerHz (60);
}

KanalzugAudioProcessorEditor::~KanalzugAudioProcessorEditor()
{
    stopTimer();
    moduleEditor.reset();
    setLookAndFeel (nullptr);
}

void KanalzugAudioProcessorEditor::selectModule (int index)
{
    if (index == selected && moduleEditor != nullptr)
        return;
    selected = index;
    proc.apvts.state.setProperty ("selected", index, nullptr);
    for (int i = 0; i < KanalzugAudioProcessor::numModules; ++i)
        tiles[(size_t) i].key.setToggleState (i == index, juce::dontSendNotification);

    moduleEditor.reset();
    moduleEditor.reset (proc.getModule (index).createEditorIfNeeded());
    {
        const juce::String t (KanalzugAudioProcessor::moduleInfos()[(size_t) index].title);
        modulePresets.setTarget (&proc.getModule (index), kb::PresetBar::deviceFromTitle (t));
        modulePresets.setTitleText ("MODUL " + t);
    }
    if (moduleEditor != nullptr)
        moduleHolder.addAndMakeVisible (*moduleEditor);
    layoutModuleEditor();
    repaint();
}

void KanalzugAudioProcessorEditor::layoutModuleEditor()
{
    moduleHolder.setBounds (moduleArea);
    if (moduleEditor == nullptr || moduleArea.isEmpty())
        return;
    const int w = moduleEditor->getWidth(), h = moduleEditor->getHeight();
    // Moduloberfläche in ihrer Originalgröße zeigen, bei Bedarf passend verkleinert
    const float s = juce::jmin (1.0f, (float) moduleArea.getWidth() / (float) w, (float) moduleArea.getHeight() / (float) h);
    const float x = ((float) moduleArea.getWidth() - (float) w * s) * 0.5f;
    const float y = ((float) moduleArea.getHeight() - (float) h * s) * 0.5f;
    moduleEditor->setTopLeftPosition (0, 0);
    moduleEditor->setTransform (juce::AffineTransform::scale (s).translated (x, y));
}

void KanalzugAudioProcessorEditor::timerCallback()
{
    if (proc.isOptoBeforeDrive() != shownOptoPre)
    {
        shownOptoPre = proc.isOptoBeforeDrive();
        layoutTiles();
        repaint();
    }

    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);

    for (int i = 0; i < KanalzugAudioProcessor::numModules; ++i)
    {
        auto& key = tiles[(size_t) i].key;
        const float alpha = proc.isModuleOn (i) ? 1.0f : 0.55f;   // ausgeschaltete Module etwas blasser
        if (std::abs (key.getAlpha() - alpha) > 0.01f)
            key.setAlpha (alpha);
    }
}

void KanalzugAudioProcessorEditor::layoutTiles()
{
    // Kacheln in der aktuellen Signal-Reihenfolge anordnen
    const int W = getWidth();
    const int tilesX = 24, tilesW = W - 48;
    const int gap = 18;
    const int tw = (tilesW - gap * (KanalzugAudioProcessor::numModules - 1)) / KanalzugAudioProcessor::numModules;
    const auto order = KanalzugAudioProcessor::orderFor (shownOptoPre);
    for (int pos = 0; pos < KanalzugAudioProcessor::numModules; ++pos)
    {
        auto& t = tiles[(size_t) order[(size_t) pos]];
        const int x = tilesX + pos * (tw + gap);
        t.key.setBounds (x, 108, tw, 38);
        t.power.setBounds (x + (tw - 96) / 2, 148, 96, 32);
    }
}

void KanalzugAudioProcessorEditor::renderTolex()
{
    tolexImage = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    juce::Image::BitmapData data (tolexImage, juce::Image::BitmapData::writeOnly);
    juce::Random rnd (7);
    for (int y = 0; y < tolexImage.getHeight(); ++y)
        for (int x = 0; x < tolexImage.getWidth(); ++x)
        {
            const int weave = ((x + y) % 4 == 0 || (x - y + 4096) % 4 == 0) ? -5 : 0;
            const int n = rnd.nextInt (13) - 6 + weave;
            data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, 30 + n),
                                                     (juce::uint8) juce::jlimit (0, 255, 25 + n),
                                                     (juce::uint8) juce::jlimit (0, 255, 21 + n)));
        }
}

void KanalzugAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Logo
    const auto scriptF = lnf.scriptFont (48.0f);
    const auto logoArea = juce::Rectangle<float> (30.0f, 4.0f, 320.0f, 64.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", logoArea.translated (2.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", logoArea, juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (22.0f, 0.35f));
    g.drawText ("KANALZUG", juce::Rectangle<float> (logoArea.getX() + sw + 12.0f, 24.0f, 220.0f, 32.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.45f));
    g.setFont (lnf.labelFontRegular (12.0f));
    g.drawText (juce::CharPointer_UTF8 ("Modul anklicken zum Bearbeiten \xc2\xb7 Schalter darunter: Modul an/aus"),
                juce::Rectangle<float> (34.0f, 62.0f, 520.0f, 16.0f), juce::Justification::centredLeft);

    // Pfeile im Signalweg
    g.setColour (gold.withAlpha (0.7f));
    const auto order = KanalzugAudioProcessor::orderFor (shownOptoPre);
    for (int i = 0; i + 1 < KanalzugAudioProcessor::numModules; ++i)
    {
        const auto a = tiles[(size_t) order[(size_t) i]].key.getBounds().toFloat(), b = tiles[(size_t) order[(size_t) i + 1]].key.getBounds().toFloat();
        const float y = a.getCentreY(), x0 = a.getRight() + 3.0f, x1 = b.getX() - 3.0f;
        g.drawLine (x0, y, x1 - 4.0f, y, 1.6f);
        juce::Path head;
        head.addTriangle (x1, y, x1 - 6.0f, y - 4.0f, x1 - 6.0f, y + 4.0f);
        g.fillPath (head);
    }

    // Beschriftung Opto-Schalter
    g.setColour (gold.withAlpha (0.85f));
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("OPTO-POSITION", optoPosSwitch.getBounds().toFloat().withY ((float) optoPosSwitch.getY() - 18.0f).withHeight (14.0f).expanded (20.0f, 0.0f),
                juce::Justification::centred);

    // Ausgang
    const auto ob = outSlider.getBounds().toFloat();
    g.setColour (logo.withAlpha (0.7f));
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("AUSGANG", ob.withY (ob.getBottom() - 4.0f).withHeight (14.0f).expanded (10.0f, 0.0f), juce::Justification::centred);

    // Rahmen um den Modulbereich
    const auto ma = moduleArea.toFloat().expanded (6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (ma.translated (0, 2.0f), 10.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.2f), 0, ma.getY(), gold.darker (0.7f), 0, ma.getBottom(), false));
    g.drawRoundedRectangle (ma, 10.0f, 2.0f);
    g.setColour (juce::Colour (0xff0f0c0a));
    g.fillRoundedRectangle (moduleArea.toFloat(), 6.0f);
}

void KanalzugAudioProcessorEditor::resized()
{
    const int W = getWidth();
    vuRight.setBounds (W - 24 - 150, 8, 150, 96);
    vuLeft.setBounds (W - 24 - 150 - 10 - 150, 8, 150, 96);
    outSlider.setBounds (W - 24 - 320 - 86, 14, 78, 74);

    header = { 0, 0, W, 186 };
    optoPosSwitch.setBounds (W - 24 - 320 - 86 - 150, 36, 130, 40);
    layoutTiles();

    modulePresets.setBounds (16, 190, W - 32, kb::PresetBar::height);
    moduleArea = juce::Rectangle<int> (16, 190 + kb::PresetBar::height + 10, W - 32, getHeight() - 190 - kb::PresetBar::height - 10 - 14);
    renderTolex();
    layoutModuleEditor();
}
