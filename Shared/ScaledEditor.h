#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetBar.h"

namespace kb
{
// Rahmen um jede Klangbild-Oberfläche: macht sie stufenlos skalierbar (Ecke ziehen),
// hält das Seitenverhältnis fest und merkt sich die zuletzt gewählte Größe pro Plugin
// in ~/Library/Application Support/Klangbild/Klangbild.settings.
class ScaledEditor : public juce::AudioProcessorEditor,
                     private juce::ComponentListener
{
public:
    static constexpr double minScale = 0.75, maxScale = 1.5;

    ScaledEditor (juce::AudioProcessor& p, juce::AudioProcessorEditor* innerEditor)
        : AudioProcessorEditor (p), inner (innerEditor)
    {
        addAndMakeVisible (*inner);
        presets.setTarget (&p, PresetBar::deviceFromPluginName (p.getName()));
        presets.onLoaded = [this] { inner->repaint(); };
        addAndMakeVisible (presets);
        baseW = juce::jmax (1, inner->getWidth());
        baseH = juce::jmax (1, inner->getHeight()) + PresetBar::height;
        scale = juce::jlimit (minScale, maxScale, settings().getDoubleValue (key(), 1.0));
        inner->addComponentListener (this);

        applyScale();
    }

    ~ScaledEditor() override
    {
        inner->removeComponentListener (this);
        settings().setValue (key(), scale);
        settings().saveIfNeeded();
    }

    void resized() override
    {
        const double s = juce::jmin ((double) getWidth() / (double) baseW, (double) getHeight() / (double) baseH);
        if (! internalChange)
            scale = juce::jlimit (minScale, maxScale, s);
        presets.setBounds (0, 0, baseW, PresetBar::height);
        presets.setTransform (juce::AffineTransform::scale ((float) s));
        inner->setTransform (juce::AffineTransform::scale ((float) s).translated (0.0f, (float) (PresetBar::height * s)));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

    juce::AudioProcessorEditor& getInner() noexcept { return *inner; }
    PresetBar& getPresetBar() noexcept { return presets; }

private:
    // Die innere Oberfläche ändert ihre Grundgröße selbst (z. B. Kurve ein/aus)
    void componentMovedOrResized (juce::Component& c, bool, bool wasResized) override
    {
        if (! wasResized || &c != inner.get()) return;
        if (inner->getWidth() == baseW && inner->getHeight() + PresetBar::height == baseH) return;
        baseW = juce::jmax (1, inner->getWidth());
        baseH = juce::jmax (1, inner->getHeight()) + PresetBar::height;
        applyScale();
    }

    // Grenzen und Größe setzen, ohne dass die Zwischenschritte die gewählte Größe verändern
    void applyScale()
    {
        const juce::ScopedValueSetter<bool> guard (internalChange, true);
        setResizable (true, true);
        setResizeLimits (juce::roundToInt (baseW * minScale), juce::roundToInt (baseH * minScale),
                         juce::roundToInt (baseW * maxScale), juce::roundToInt (baseH * maxScale));
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio ((double) baseW / (double) baseH);
        setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
        resized();
    }

    juce::String key() const { return "uiScale_" + processor.getName().removeCharacters (" "); }

    static juce::PropertiesFile& settings()
    {
        static juce::PropertiesFile file ([]
        {
            juce::PropertiesFile::Options o;
            o.applicationName     = "Klangbild";
            o.filenameSuffix      = ".settings";
            o.folderName          = "Klangbild";
            o.osxLibrarySubFolder = "Application Support";
            o.millisecondsBeforeSaving = 500;
            return o;
        }());
        return file;
    }

    std::unique_ptr<juce::AudioProcessorEditor> inner;
    PresetBar presets;
    int baseW = 1, baseH = 1;
    double scale = 1.0;
    bool internalChange = false;
};
} // namespace kb
