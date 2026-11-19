#pragma once
#include "Visuals.h"

namespace gx::ui
{
// Container that forwards frames to every Animated descendant that is showing.
class Page : public juce::Component, public Animated
{
public:
    explicit Page (Context& c) : ctx (c) {}
    void tick (const Frame& f) override { forward (this, f); }
    virtual void refresh() {}          // after preset load / undo
    virtual void pageShown() {}
    static void forward (juce::Component* c, const Frame& f);
protected:
    Context& ctx;
};

class Logo : public juce::Component, public juce::SettableTooltipClient
{
public:
    Logo() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("dskmusic.com"); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { juce::URL ("https://dskmusic.com").launchInDefaultBrowser(); }
    static void drawMark (juce::Graphics&, juce::Rectangle<float>, float glow);
    float glow = 0;
};

class Header : public juce::Component, public Animated
{
public:
    Header (Context& c, std::function<void (int)> onTab);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (const Frame&) override;
    void setTab (int t);
    void setPresetName (const juce::String& n) { presetName.setText (n, juce::dontSendNotification); }
    std::function<void (int delta)> onPresetStep;
    std::function<void()> onPresetNameClick, onAbout, onAB, onUndo, onRedo;
    juce::TextButton abButton { "A" };
    juce::TextButton undoButton { L ("Deshacer", "Undo") }, redoButton { L ("Rehacer", "Redo") };
private:
    Context& ctx;
    Logo logo;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::TextButton prev { "<" }, next { ">" }, about { "i" };
    juce::Label presetName;
    std::function<void (int)> tabCb;
    float energy = 0;
};

class AboutOverlay : public juce::Component
{
public:
    AboutOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
private:
    juce::HyperlinkButton link { "dskmusic.com", juce::URL ("https://dskmusic.com") };
};

class WavetableEditor; // OSC page dialog

class EditorPage : public Page
{
public:
    explicit EditorPage (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    void showSub (int i);
    int currentSub() const { return current; }
private:
    juce::OwnedArray<juce::TextButton> subTabs;
    juce::OwnedArray<juce::Component> subs;
    int current = 0;
};

class PresetsPage : public Page, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetsPage (Context& c);
    ~PresetsPage() override { ctx.library.onChange = nullptr; }
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    void pageShown() override { refresh(); }
    bool keyPressed (const juce::KeyPress&) override;
    void step (int delta);                         // header < > buttons
    int rowOfPreset (const juce::String& name) const;  // row in the current filtered list, -1 if hidden
    std::function<void (const Preset&)> onLoaded;
private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void textEditorTextChanged (juce::TextEditor&) override { requery(); }
    void requery();
    void loadRow (int row);
    void showRowMenu (int row);
    void editMeta (int libIndex);
    void rebuildCategoryList();

    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::ListBox catList, subList;
    struct StringModel : juce::ListBoxModel
    {
        juce::StringArray items; int selected = 0; std::function<void (int)> onSelect;
        int getNumRows() override { return items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool sel) override;
        void selectedRowsChanged (int row) override { selected = row; if (onSelect) onSelect (row); }
    } catModel, subModel;
    juce::OwnedArray<juce::TextButton> tagButtons;
    juce::ComboBox typeBox, sortBox, authorBox, folderBox;
    juce::ToggleButton favOnly { L ("Favoritos", "Favourites") };
    juce::TextButton saveBtn { L ("Guardar", "Save") }, saveAsBtn { L ("Guardar como", "Save as") }, importBtn { L ("Importar", "Import") }, exportBtn { L ("Exportar banco", "Export bank") },
                     prevBtn { L ("Anterior", "Previous") }, nextBtn { L ("Siguiente", "Next") }, folderBtn { L ("Nueva carpeta", "New folder") }, rescanBtn { L ("Reescanear", "Rescan") };
    juce::Label info;
    std::vector<int> shown;
    bool syncingSelection = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class ExportPage : public Page
{
public:
    explicit ExportPage (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    std::unique_ptr<juce::Component> sfzPanel; // created by the SFZ module
private:
    juce::TextButton exportPreset { L ("Exportar preset .infinitum", "Export .infinitum preset") }, importPreset { L ("Importar preset", "Import preset") };
    std::unique_ptr<juce::FileChooser> chooser;
};

class GeneratorPage; // Generator.h

class SettingsPage : public Page
{
public:
    SettingsPage (Context& c, std::function<void (float)> setScale, std::function<void()> showAbout);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::ComboBox scaleBox, animBox, langBox;
    juce::ToggleButton aiMsgs { L ("Mensajes de la IA al generar", "AI messages while generating") }, preview { L ("Previsualizar al seleccionar preset", "Preview on preset select") };
    juce::Slider historyLimit;
    juce::TextButton openFolder { L ("Abrir carpeta de presets", "Open presets folder") }, clearLearn { L ("Borrar asignaciones MIDI Learn", "Clear MIDI Learn assignments") }, aboutBtn { L ("Acerca de Infinitum", "About Infinitum") };
};
}
