#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kb
{
// Gemeinsame Preset-Ablage aller Klangbild-Plugins.
// Ein Preset ist der komplette Zustand eines Plugins (bzw. eines Kanalzug-/Drums-Moduls) als Datei in
//   ~/Library/Application Support/Klangbild/Presets/<Name>/<Preset>.kbpreset
// Einzel-Plugin und gleichnamiges Modul im Kanalzug/Drums nutzen denselben Ordner – so tauchen eigene
// Opto-, EQ-, Band- … Presets überall auf, wo dieses Gerät steckt.
class PresetBar : public juce::Component
{
public:
    static constexpr int height = 34;

    PresetBar()
    {
        setLookAndFeel (&lnf);
        title.setText (juce::String (juce::CharPointer_UTF8 ("MEINE PRESETS")), juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centredLeft);
        title.setFont (juce::FontOptions (12.0f).withStyle ("Bold"));
        title.setColour (juce::Label::textColourId, juce::Colour (0xffd9c48f));
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);

        box.setTextWhenNothingSelected (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93 Preset w\xc3\xa4hlen \xe2\x80\x93")));
        box.setTextWhenNoChoicesAvailable ("Noch keine eigenen Presets");
        box.beforePopup = [this] { refresh(); };
        box.onChange = [this] { loadSelected(); };
        addAndMakeVisible (box);

        for (auto* b : { &saveBtn, &delBtn, &folderBtn })
            addAndMakeVisible (*b);
        saveBtn.setButtonText ("Sichern");
        delBtn.setButtonText (juce::String (juce::CharPointer_UTF8 ("L\xc3\xb6schen")));
        folderBtn.setButtonText ("Ordner");
        saveBtn.setTooltip ("Aktuelle Einstellung als eigenes Preset sichern");
        folderBtn.setTooltip ("Preset-Ordner im Finder zeigen");
        saveBtn.onClick = [this] { askSave(); };
        delBtn.onClick = [this] { askDelete(); };
        folderBtn.onClick = [this] { folder().createDirectory(); folder().revealToUser(); };
    }

    ~PresetBar() override { setLookAndFeel (nullptr); }

    // name = Gerät ("Opto", "EQ", "Kanalzug" …); bestimmt den Ordner
    void setTarget (juce::AudioProcessor* p, const juce::String& deviceName)
    {
        processor = p;
        device = deviceName.trim().isNotEmpty() ? deviceName.trim() : juce::String ("Allgemein");
        current.clear();
        refresh();
    }

    void setTitleText (const juce::String& t) { title.setText (t, juce::dontSendNotification); }

    // Ordnername aus Modultitel "KALIBRATOR" → "Kalibrator", "EQ" bleibt "EQ"
    static juce::String deviceFromTitle (const juce::String& t)
    {
        if (t.length() <= 2) return t.toUpperCase();
        return t.substring (0, 1).toUpperCase() + t.substring (1).toLowerCase();
    }

    // Gerätename aus "Klangbild Opto" → "Opto"
    static juce::String deviceFromPluginName (const juce::String& n)
    {
        auto s = n.trim();
        return s.startsWithIgnoreCase ("Klangbild") ? s.substring (9).trim() : s;
    }

    static juce::File rootFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                   .getChildFile ("Application Support")
#endif
                   .getChildFile ("Klangbild").getChildFile ("Presets");
    }

    juce::File folder() const { return rootFolder().getChildFile (juce::File::createLegalFileName (device)); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23201c), 0, 0, juce::Colour (0xff15130f), 0, b.getBottom(), false));
        g.fillRect (b);
        g.setColour (juce::Colour (0xffd9c48f).withAlpha (0.25f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, b.getRight());
        if (status.isNotEmpty())
        {
            g.setColour (juce::Colour (0xffd9c48f).withAlpha (0.8f));
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (status, statusArea, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 5);
        title.setBounds (r.removeFromLeft (118));
        box.setBounds (r.removeFromLeft (300));
        r.removeFromLeft (8);
        saveBtn.setBounds (r.removeFromLeft (86));
        r.removeFromLeft (6);
        delBtn.setBounds (r.removeFromLeft (86));
        r.removeFromLeft (6);
        folderBtn.setBounds (r.removeFromLeft (80));
        statusArea = r.reduced (8, 0);
    }

    void refresh()
    {
        files = folder().findChildFiles (juce::File::findFiles, false, "*.kbpreset");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
        box.clear (juce::dontSendNotification);
        for (int i = 0; i < files.size(); ++i)
        {
            box.addItem (files[i].getFileNameWithoutExtension(), i + 1);
            if (files[i].getFileNameWithoutExtension() == current)
                box.setSelectedId (i + 1, juce::dontSendNotification);
        }
        delBtn.setEnabled (current.isNotEmpty());
    }

    // Für Tests und für die Plugins selbst
    bool savePreset (const juce::String& name)
    {
        if (processor == nullptr || name.trim().isEmpty())
            return false;
        juce::MemoryBlock mb;
        processor->getStateInformation (mb);
        const auto f = folder().getChildFile (juce::File::createLegalFileName (name.trim()) + ".kbpreset");
        if (! folder().createDirectory() || ! f.replaceWithData (mb.getData(), mb.getSize()))
            return false;
        current = f.getFileNameWithoutExtension();
        refresh();
        setStatus ("Gesichert: " + current);
        return true;
    }

    bool loadPreset (const juce::File& f)
    {
        juce::MemoryBlock mb;
        if (processor == nullptr || ! f.loadFileAsData (mb) || mb.getSize() == 0)
            return false;
        processor->setStateInformation (mb.getData(), (int) mb.getSize());
        current = f.getFileNameWithoutExtension();
        for (int i = 0; i < files.size(); ++i)
            if (files[i] == f)
                box.setSelectedId (i + 1, juce::dontSendNotification);
        delBtn.setEnabled (true);
        setStatus ("Geladen: " + current);
        if (onLoaded) onLoaded();
        return true;
    }

    std::function<void()> onLoaded;   // z. B. Kanalzug baut danach die Moduloberfläche neu

private:
    struct Combo : juce::ComboBox
    {
        std::function<void()> beforePopup;
        void showPopup() override { if (beforePopup) beforePopup(); juce::ComboBox::showPopup(); }
    };

    struct Look : juce::LookAndFeel_V4
    {
        Look()
        {
            const juce::Colour cream (0xffefe3c6), dark (0xff2c2824), edge (0xff6b5f4c);
            setColour (juce::ComboBox::backgroundColourId, dark);
            setColour (juce::ComboBox::textColourId, cream);
            setColour (juce::ComboBox::outlineColourId, edge);
            setColour (juce::ComboBox::arrowColourId, juce::Colour (0xffd9c48f));
            setColour (juce::TextButton::buttonColourId, dark);
            setColour (juce::TextButton::textColourOffId, cream);
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff221f1b));
            setColour (juce::PopupMenu::textColourId, cream);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff6b5534));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff221f1b));
            setColour (juce::AlertWindow::textColourId, cream);
            setColour (juce::TextEditor::backgroundColourId, dark);
            setColour (juce::TextEditor::textColourId, cream);
            setColour (juce::TextEditor::outlineColourId, edge);
        }
    };

    void loadSelected()
    {
        const int i = box.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (i, files.size()))
            loadPreset (files[i]);
    }

    void askSave()
    {
        auto* w = new juce::AlertWindow ("Preset sichern",
                                         juce::String (juce::CharPointer_UTF8 ("Name f\xc3\xbcr die aktuelle Einstellung (")) + device + "):",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&lnf);
        w->addTextEditor ("name", current.isNotEmpty() ? current : juce::String ("Mein Preset"));
        w->addButton ("Sichern", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBar> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->savePreset (w->getTextEditorContents ("name"));
        }), true);
    }

    void askDelete()
    {
        if (current.isEmpty())
            return;
        auto* w = new juce::AlertWindow ("Preset l" + juce::String (juce::CharPointer_UTF8 ("\xc3\xb6")) + "schen",
                                         "\"" + current + "\" wirklich " + juce::String (juce::CharPointer_UTF8 ("l\xc3\xb6schen")) + "?",
                                         juce::MessageBoxIconType::NoIcon, this);
        w->setLookAndFeel (&lnf);
        w->addButton (juce::String (juce::CharPointer_UTF8 ("L\xc3\xb6schen")), 1);
        w->addButton ("Abbrechen", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<PresetBar> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe == nullptr || result != 1) return;
            safe->folder().getChildFile (juce::File::createLegalFileName (safe->current) + ".kbpreset").moveToTrash();
            safe->setStatus (juce::String (juce::CharPointer_UTF8 ("Gel\xc3\xb6scht: ")) + safe->current);
            safe->current.clear();
            safe->refresh();
        }), true);
    }

    void setStatus (const juce::String& s) { status = s; repaint(); }

    Look lnf;
    juce::Label title;
    Combo box;
    juce::TextButton saveBtn, delBtn, folderBtn;
    juce::Array<juce::File> files;
    juce::AudioProcessor* processor = nullptr;
    juce::String device { "Allgemein" }, current, status;
    juce::Rectangle<int> statusArea;
};
} // namespace kb
