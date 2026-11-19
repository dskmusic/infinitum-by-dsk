#include "GeneratorPage.h"
#include "../sfz/SfzExporter.h"
#include <thread>

namespace gx::ui
{
// SFZ export panel: options, single/batch export on a worker thread, cancellable progress.
class SfzPanel : public Box, private juce::Timer
{
public:
    explicit SfzPanel (Context& c) : Box (c, L ("Exportar a SFZ (multisample)", "Export to SFZ (multisample)"))
    {
        auto addCombo = [this] (juce::ComboBox& cb, juce::StringArray items, int sel)
        { cb.addItemList (items, 1); cb.setSelectedItemIndex (sel, juce::dontSendNotification); addAndMakeVisible (cb); };
        addCombo (step, { L ("1 semitono", "1 semitone"), L ("2 semitonos", "2 semitones"), L ("3 semitonos", "3 semitones"), L ("4 semitonos", "4 semitones"), L ("6 semitonos", "6 semitones"), L ("12 semitonos", "12 semitones") }, 2);
        addCombo (layers, { L ("1 capa", "1 layer"), L ("2 capas", "2 layers"), L ("3 capas", "3 layers"), L ("4 capas", "4 layers"), L ("5 capas", "5 layers"), L ("6 capas", "6 layers"), L ("7 capas", "7 layers"), L ("8 capas", "8 layers") }, 1);
        addCombo (rate, { "44.1 kHz", "48 kHz" }, 0);
        addCombo (bits, { "24 bit", "16 bit" }, 0);
        addCombo (source, { L ("Preset actual", "Current preset"), L ("Lote: presets favoritos", "Batch: favourite presets"), L ("Lote: categoría del preset actual", "Batch: current preset's category"), L ("Lote: todos los presets de usuario", "Batch: all user presets") }, 0);
        auto addSlider = [this] (juce::Slider& s, double lo, double hi, double st, double v, const juce::String& suffix)
        {
            s.setRange (lo, hi, st); s.setValue (v, juce::dontSendNotification);
            s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
            s.setTextValueSuffix (suffix); addAndMakeVisible (s);
        };
        addSlider (lowNote, 0, 127, 1, 24, "");
        addSlider (highNote, 0, 127, 1, 96, "");
        addSlider (noteLen, 0.3, 8.0, 0.1, 2.0, " s");
        addSlider (tail, 0.0, 6.0, 0.1, 1.0, " s");
        addSlider (xfade, 5, 300, 1, 50, " ms");
        lowNote.textFromValueFunction = highNote.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 4); };
        lowNote.updateText(); highNote.updateText();
        loop.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (loop);
        addAndMakeVisible (exportBtn);
        addAndMakeVisible (cancelBtn);
        addAndMakeVisible (bar);
        addAndMakeVisible (status);
        status.setJustificationType (juce::Justification::topLeft);
        status.setFont (font (12.0f));
        status.setColour (juce::Label::textColourId, col::textDim);
        cancelBtn.setEnabled (false);
        exportBtn.onClick = [this] { chooseAndRun(); };
        cancelBtn.onClick = [this] { cancel = true; };
        layout = [this] (juce::Rectangle<int> r) { doLayout (r); };
        startTimerHz (10);
    }
    ~SfzPanel() override { cancel = true; if (worker.joinable()) worker.join(); }

    void paint (juce::Graphics& g) override
    {
        Section::paint (g);
        g.setColour (col::textDim);
        g.setFont (font (12.0f, true));
        for (auto& [label, comp] : labels()) g.drawText (label, comp->getBounds().translated (-150, 0).withWidth (146), juce::Justification::centredRight);
    }

private:
    std::vector<std::pair<juce::String, juce::Component*>> labels()
    {
        return { { L ("Muestrear cada", "Sample every"), &step }, { L ("Capas de velocidad", "Velocity layers"), &layers }, { L ("Frecuencia", "Sample rate"), &rate }, { L ("Profundidad", "Bit depth"), &bits },
                 { L ("Nota inferior", "Lowest note"), &lowNote }, { L ("Nota superior", "Highest note"), &highNote }, { L ("Duración nota", "Note length"), &noteLen },
                 { L ("Cola de release", "Release tail"), &tail }, { L ("Crossfade del loop", "Loop crossfade"), &xfade }, { L ("Origen", "Source"), &source } };
    }
    void doLayout (juce::Rectangle<int> r)
    {
        auto left = r.removeFromLeft (520).withTrimmedLeft (150);
        for (auto* c : std::initializer_list<juce::Component*> { &step, &layers, &rate, &bits, &lowNote, &highNote, &noteLen, &tail, &xfade })
            c->setBounds (left.removeFromTop (34).reduced (0, 5).withWidth (juce::jmin (left.getWidth(), 340)));
        loop.setBounds (left.removeFromTop (30).withWidth (360));
        r.removeFromLeft (30);
        auto rr = r.withTrimmedLeft (150);
        source.setBounds (rr.removeFromTop (34).reduced (0, 5).withWidth (320));
        rr.removeFromTop (10);
        auto btns = rr.removeFromTop (36);
        exportBtn.setBounds (btns.removeFromLeft (220).reduced (2));
        cancelBtn.setBounds (btns.removeFromLeft (110).reduced (2));
        rr.removeFromTop (10);
        bar.setBounds (rr.removeFromTop (22).withX (r.getX()).withWidth (r.getWidth()));
        rr.removeFromTop (8);
        status.setBounds (rr.withX (r.getX()).withWidth (r.getWidth()));
    }

    SfzOptions options() const
    {
        static const int steps[] { 1, 2, 3, 4, 6, 12 };
        SfzOptions o;
        o.stepSemis = steps[juce::jmax (0, step.getSelectedItemIndex())];
        o.velLayers = layers.getSelectedItemIndex() + 1;
        o.sampleRate = rate.getSelectedItemIndex() == 1 ? 48000.0 : 44100.0;
        o.bitDepth = bits.getSelectedItemIndex() == 1 ? 16 : 24;
        o.lowNote = (int) lowNote.getValue(); o.highNote = (int) highNote.getValue();
        o.noteSeconds = noteLen.getValue(); o.releaseTail = tail.getValue();
        o.loop = loop.getToggleState(); o.crossfadeSeconds = xfade.getValue() / 1000.0;
        return o;
    }

    void chooseAndRun()
    {
        if (running) return;
        // collect presets (message thread)
        std::vector<Preset> list;
        const int src = source.getSelectedItemIndex();
        if (src == 0) list.push_back (Preset::capture (ctx.proc));
        else
        {
            PresetFilter f;
            if (src == 1) f.favoritesOnly = true;
            if (src == 2) f.category = ctx.proc.currentMeta.category;
            if (src == 3) f.type = PresetFilter::User;
            for (int i : ctx.library.query (f)) { Preset p; if (ctx.library.load (i, p)) list.push_back (p); }
        }
        if (list.empty()) { status.setText (L ("No hay presets que exportar con ese origen.", "No presets to export from that source."), juce::dontSendNotification); return; }
        chooser = std::make_unique<juce::FileChooser> (L ("Carpeta de destino del SFZ", "SFZ destination folder"), Library::defaultRoot().getChildFile ("SFZ"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, list = std::move (list)] (const juce::FileChooser& fc) mutable
            {
                auto dir = fc.getResult();
                if (dir == juce::File()) return;
                run (std::move (list), dir);
            });
    }

    void run (std::vector<Preset> list, juce::File dir)
    {
        if (worker.joinable()) worker.join();
        running = true; cancel = false; progress = 0;
        exportBtn.setEnabled (false); cancelBtn.setEnabled (true);
        const auto opts = options();
        const auto warns = SfzExporter::unrepresentable (list.front());
        status.setText (L ("Renderizando… ", "Rendering… ") + (warns.isEmpty() ? juce::String() : L ("\nAvisos:\n- ", "\nWarnings:\n- ") + warns.joinIntoString ("\n- ")), juce::dontSendNotification);
        worker = std::thread ([this, list = std::move (list), dir, opts]
        {
            juce::String msg;
            int ok = 0;
            for (size_t i = 0; i < list.size() && ! cancel.load(); ++i)
            {
                auto r = SfzExporter::exportPreset (list[i], opts, dir, &cancel, [this, i, n = list.size()] (float pr) { progress = ((double) i + pr) / (double) n; });
                if (r.ok) { ++ok; lastFile = r.sfzFile.getFullPathName(); warnings = r.warnings.joinIntoString ("\n- "); }
                else if (! r.cancelled) msg << r.error << "\n";
            }
            resultText = cancel.load() ? L ("Exportación cancelada.", "Export cancelled.") : juce::String (ok) + L (" instrumento(s) SFZ exportados.\n", " SFZ instrument(s) exported.\n") + lastFile + "\n" + msg
                                                                                    + (warnings.isNotEmpty() ? L ("\nNo representable en SFZ:\n- ", "\nNot representable in SFZ:\n- ") + warnings : juce::String());
            running = false;
        });
    }

    void timerCallback() override
    {
        bar.setValue (progress);
        if (! running && worker.joinable())
        {
            worker.join();
            exportBtn.setEnabled (true); cancelBtn.setEnabled (false);
            status.setText (resultText, juce::dontSendNotification);
            if (ctx.toast) ctx.toast (resultText.upToFirstOccurrenceOf ("\n", false, false));
        }
    }

    struct Bar : juce::Component
    {
        double value = 0;
        void setValue (double v) { if (std::abs (v - value) > 0.001) { value = v; repaint(); } }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (col::bgDeep); g.fillRoundedRectangle (r, 4);
            g.setColour (col::accent); g.fillRoundedRectangle (r.withWidth (r.getWidth() * (float) juce::jlimit (0.0, 1.0, value)), 4);
            g.setColour (col::text); g.setFont (font (11.0f, true));
            g.drawText (juce::String ((int) std::lround (value * 100)) + " %", r, juce::Justification::centred);
        }
    } bar;

    juce::ComboBox step, layers, rate, bits, source;
    juce::Slider lowNote, highNote, noteLen, tail, xfade;
    juce::ToggleButton loop { L ("Loop sin clicks (crossfade) en sonidos sostenidos", "Click-free loop (crossfade) on sustained sounds") };
    juce::TextButton exportBtn { L ("Exportar SFZ...", "Export SFZ...") }, cancelBtn { L ("Cancelar", "Cancel") };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    std::thread worker;
    std::atomic<bool> cancel { false }, running { false };
    std::atomic<double> progress { 0 };
    juce::String resultText, lastFile, warnings;
};

std::unique_ptr<juce::Component> makeSfzPanel (Context& c) { return std::make_unique<SfzPanel> (c); }
}
