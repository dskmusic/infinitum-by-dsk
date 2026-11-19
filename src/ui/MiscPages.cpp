#include "Pages.h"
#include "../preset/Render.h"

namespace gx::ui
{
void Page::forward (juce::Component* c, const Frame& f)
{
    for (auto* ch : c->getChildren())
    {
        if (! ch->isShowing()) continue;
        if (auto* a = dynamic_cast<Animated*> (ch)) a->tick (f);
        else forward (ch, f);
    }
}

// ---------------------------------------------------------------- Logo: an infinity loop over a horizon
void Logo::drawMark (juce::Graphics& g, juce::Rectangle<float> r, float glow, float phase)
{
    const auto c = r.getCentre();
    const float a = juce::jmin (r.getWidth() * 0.5f, r.getHeight() * 0.95f) * 0.92f;
    juce::Path inf;
    for (int i = 0; i <= 96; ++i) // lemniscate of Bernoulli
    {
        const float t = kTwoPi * (float) i / 96.0f, s = std::sin (t), d = 1.0f + s * s;
        const juce::Point<float> p (c.x + a * std::cos (t) / d, c.y + a * std::sin (t) * std::cos (t) / d);
        if (i == 0) inf.startNewSubPath (p); else inf.lineTo (p);
    }
    inf.closeSubPath();
    if (glow > 0.01f)
    {
        g.setColour (col::accent.withAlpha (0.22f * glow));
        g.strokePath (inf, juce::PathStrokeType (a * 0.30f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::ColourGradient grad (col::accent, c.x - a, c.y, col::accent2, c.x + a, c.y, false);
    grad.addColour (0.5, col::accent3);
    g.setGradientFill (grad);
    g.strokePath (inf, juce::PathStrokeType (juce::jmax (1.6f, a * 0.13f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // a spark travelling along the loop
    const float t = kTwoPi * wrap01 (phase), s = std::sin (t), d = 1.0f + s * s;
    const juce::Point<float> sp (c.x + a * std::cos (t) / d, c.y + a * std::sin (t) * std::cos (t) / d);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillEllipse (juce::Rectangle<float> (a * 0.16f, a * 0.16f).withCentre (sp));
}

void Logo::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawMark (g, r.removeFromLeft (r.getHeight() * 1.6f).reduced (4, 10), glow, phase);
    r.removeFromLeft (8);
    juce::GlyphArrangement ga;
    ga.addLineOfText (techFont (25.0f), "INFINITUM", r.getX(), r.getCentreY() + 9.0f);
    juce::Path text;
    ga.createPath (text);
    const auto tb = text.getBounds();
    g.setColour (col::accent.withAlpha (0.12f + 0.25f * glow));
    g.strokePath (text, juce::PathStrokeType (3.0f));
    juce::ColourGradient grad (juce::Colours::white, tb.getX(), tb.getY(), col::accent, tb.getRight(), tb.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (text);
    g.setColour (col::textDim);
    g.setFont (font (12.0f, true));
    g.drawText ("by DSK", juce::Rectangle<float> (tb.getRight() + 7, tb.getBottom() - 14, 60, 16), juce::Justification::bottomLeft);
}

// ---------------------------------------------------------------- PlayButton
PlayButton::PlayButton (InfinitumProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (L ("Play / Stop (barra espaciadora). También arranca con el Play de tu DAW.", "Play / Stop (space bar). Your DAW's Play starts it too."));
}

void PlayButton::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    proc.setPlaying (! proc.isPlayingInternal());
    on = proc.isPlayingInternal();
    repaint();
}

void PlayButton::tick (const Frame& f)
{
    on = f.playButton;
    host = f.hostPlaying;
    level += (f.energy - level) * 0.15f;
    pulse += 0.05f;
    repaint();
}

void PlayButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f / 1.4f; // leaves room for the glow
    const auto c = b.getCentre();
    const bool active = on || host;
    const auto accent = active ? col::accent : col::accent2;
    const float pul = 0.5f + 0.5f * std::sin (pulse);
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.22f + 0.3f * (active ? level : pul * 0.4f)), c.x, c.y, accent.withAlpha (0.0f), c.x + r * 1.375f, c.y, true));
    g.fillEllipse (juce::Rectangle<float> (r * 2.75f, r * 2.75f).withCentre (c));
    g.setGradientFill (juce::ColourGradient (col::panelHi, c.x, c.y - r, col::bgDeep, c.x, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
    g.setColour (accent.withAlpha (0.9f));
    g.drawEllipse (juce::Rectangle<float> (r * 2 - 2, r * 2 - 2).withCentre (c), juce::jmax (1.5f, r * 0.07f));
    g.setColour (accent);
    const float s = r * 0.42f;
    if (on)
        g.fillRoundedRectangle (juce::Rectangle<float> (s * 1.5f, s * 1.5f).withCentre (c), s * 0.15f);
    else
    {
        juce::Path tri;
        tri.addTriangle (c.x - s * 0.55f, c.y - s * 0.85f, c.x - s * 0.55f, c.y + s * 0.85f, c.x + s * 0.95f, c.y);
        g.fillPath (tri);
    }
    if (host && ! on && r > 24)
    {
        g.setFont (font (9.5f, true));
        g.setColour (col::textDim);
        g.drawText ("DAW", juce::Rectangle<float> (c.x - r, c.y + r * 0.45f, r * 2, 12), juce::Justification::centred);
    }
}

// ---------------------------------------------------------------- Header
Header::Header (Context& c, std::function<void (int)> onTab) : ctx (c), play (c.proc), tabCb (std::move (onTab))
{
    addAndMakeVisible (logo);
    addAndMakeVisible (play);
    const juce::String names[] { L ("PAISAJE", "LANDSCAPE"), L ("SONIDO", "SOUND"), L ("GENERADOR", "GENERATOR"), "PRESETS", L ("EXPORTAR", "EXPORT"), L ("AJUSTES", "SETTINGS") };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = tabs.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { setTab (i); if (tabCb) tabCb (i); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &prev, &next, &about, &abButton, &undoButton, &redoButton }) addAndMakeVisible (b);
    prev.setTooltip (L ("Preset anterior", "Previous preset"));
    next.setTooltip (L ("Preset siguiente", "Next preset"));
    abButton.setTooltip (L ("Comparar A/B", "Compare A/B"));
    undoButton.setTooltip (L ("Deshacer (Ctrl+Z)", "Undo (Ctrl+Z)"));
    redoButton.setTooltip (L ("Rehacer (Ctrl+Y)", "Redo (Ctrl+Y)"));
    prev.onClick = [this] { if (onPresetStep) onPresetStep (-1); };
    next.onClick = [this] { if (onPresetStep) onPresetStep (1); };
    about.onClick = [this] { if (onAbout) onAbout(); };
    abButton.onClick = [this] { if (onAB) onAB(); };
    undoButton.onClick = [this] { if (onUndo) onUndo(); };
    redoButton.onClick = [this] { if (onRedo) onRedo(); };
    presetName.setJustificationType (juce::Justification::centred);
    presetName.setFont (font (14.0f, true));
    presetName.setColour (juce::Label::backgroundColourId, col::bgDeep);
    presetName.setColour (juce::Label::textColourId, col::accent);
    addAndMakeVisible (presetName);
    setTab (0);
}

void Header::setTab (int t)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == t, juce::dontSendNotification);
}

void Header::tick (const Frame& f)
{
    play.tick (f);
    logo.glow = f.energy;
    logo.phase = (float) std::fmod (f.time * (0.05 + 0.15 * f.runGain), 1.0);
    logo.repaint();
}

void Header::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (col::panelHi, 0, 0, col::bg, 0, r.getBottom(), false));
    g.fillRect (r);
    juce::ColourGradient line (col::accent.withAlpha (0.0f), 0, 0, col::accent.withAlpha (0.0f), r.getWidth(), 0, false);
    line.addColour (0.3, col::accent.withAlpha (0.8f));
    line.addColour (0.7, col::accent2.withAlpha (0.8f));
    g.setGradientFill (line);
    g.fillRect (r.removeFromBottom (2));
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromBottom (2);
    logo.setBounds (r.removeFromLeft (245));
    play.setBounds (r.removeFromLeft (56).withSizeKeepingCentre (56, 56));
    r.removeFromLeft (8);
    auto tabArea = r.removeFromLeft (540);
    const int tw = tabArea.getWidth() / tabs.size();
    for (auto* t : tabs) t->setBounds (tabArea.removeFromLeft (tw).reduced (2, 8));
    r.removeFromLeft (8);
    about.setBounds (r.removeFromRight (28).reduced (1, 10));
    redoButton.setBounds (r.removeFromRight (62).reduced (2, 10));
    undoButton.setBounds (r.removeFromRight (66).reduced (2, 10));
    abButton.setBounds (r.removeFromRight (30).reduced (2, 10));
    prev.setBounds (r.removeFromLeft (22).reduced (1, 10));
    next.setBounds (r.removeFromRight (22).reduced (1, 10));
    presetName.setBounds (r.reduced (2, 10));
}

// ---------------------------------------------------------------- About
AboutOverlay::AboutOverlay()
{
    link.setFont (font (16.0f, true), false);
    link.setColour (juce::HyperlinkButton::textColourId, col::accent);
    addAndMakeVisible (link);
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    auto box = getLocalBounds().withSizeKeepingCentre (540, 380).toFloat();
    drawPanel (g, box, 10);
    auto in = box.reduced (28);
    Logo::drawMark (g, in.removeFromTop (80).withSizeKeepingCentre (150, 70), 0.6f, 0.2f);
    g.setColour (col::accent);
    g.setFont (techFont (28.0f));
    g.drawText ("INFINITUM", in.removeFromTop (40), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (font (14.0f, true));
    g.drawText ("by DSK  -  version " INFINITUM_VERSION, in.removeFromTop (22), juce::Justification::centred);
    g.setColour (col::text);
    g.setFont (font (13.0f));
    g.drawFittedText (L ("Generador de atmósferas infinitas: un paisaje sonoro que nunca se repite.\nFreeware de DSK Music. Motor, generador de presets y diseño: DSK Music.\n"
                         "Construido con JUCE (GPLv3) y el VST3 SDK (MIT). Licencia: GNU GPL v3.",
                         "Endless soundscape generator: a landscape of sound that never repeats.\nFreeware by DSK Music. Engine, preset generator and design: DSK Music.\n"
                         "Built with JUCE (GPLv3) and the VST3 SDK (MIT). License: GNU GPL v3."),
                      in.removeFromTop (96).toNearestInt(), juce::Justification::centred, 6);
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText (L ("Clic para cerrar", "Click to close"), box.removeFromBottom (24), juce::Justification::centred);
}

void AboutOverlay::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (540, 380);
    link.setBounds (box.withTrimmedTop (300).withHeight (28).withSizeKeepingCentre (200, 28));
}

// ---------------------------------------------------------------- Export page
ExportPage::ExportPage (Context& c) : Page (c)
{
    const int mins[] { 1, 5, 10, 15, 30, 45, 60, 90, 120, 180 };
    for (int m : mins) duration.addItem (juce::String (m) + " min", m);
    duration.setSelectedId (10, juce::dontSendNotification);
    format.addItemList ({ "WAV 24-bit", "FLAC 24-bit", "OGG Vorbis" }, 1);
    format.setSelectedItemIndex (0, juce::dontSendNotification);
    rate.addItem ("44.1 kHz", 44100);
    rate.addItem ("48 kHz", 48000);
    rate.setSelectedId (48000, juce::dontSendNotification);
    for (auto* comp : std::initializer_list<juce::Component*> { &duration, &format, &rate, &exportAudio, &cancel, &exportPreset, &importPreset })
        addAndMakeVisible (comp);
    cancel.setEnabled (false);
    exportAudio.onClick = [this] { start(); };
    cancel.onClick = [this] { cancelFlag = true; };
    exportPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Exportar preset", "Export preset"), Library::defaultRoot().getChildFile (ctx.proc.currentMeta.name + ".infinitum"), "*.infinitum");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto p = Preset::capture (ctx.proc);
            if (p.saveTo (f.withFileExtension ("infinitum")) && ctx.toast) ctx.toast (L ("Exportado: ", "Exported: ") + f.getFileName());
        });
    };
    importPreset.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Importar preset o banco", "Import preset or bank"), Library::defaultRoot(), "*.infinitum;*.infinitumbank");
        chooser->launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            const int n = ctx.library.importFile (f);
            if (ctx.toast) ctx.toast (juce::String (n) + L (" preset(s) importados a la biblioteca", " preset(s) imported into the library"));
        });
    };
    status = L ("Renderiza el sonido actual (con su semilla) a un archivo largo: ideal para vídeos, meditación o estudio.",
                "Renders the current sound (with its seed) to a long file: ideal for videos, meditation or study.");
}

ExportPage::~ExportPage()
{
    cancelFlag = true;
    if (worker.joinable()) worker.join();
}

void ExportPage::start()
{
    if (busy.load()) return;
    const auto ext = exportFormatExtension (format.getSelectedItemIndex());
    const auto name = ctx.proc.currentMeta.name.isNotEmpty() ? ctx.proc.currentMeta.name : juce::String ("Infinitum");
    const auto def = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                         .getChildFile (juce::File::createLegalFileName (name + " - " + juce::String (duration.getSelectedId()) + " min" + ext));
    chooser = std::make_unique<juce::FileChooser> (L ("Guardar audio", "Save audio"), def, "*" + ext);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this, ext] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        target = f.withFileExtension (ext);
        if (worker.joinable()) worker.join();
        busy = true; done = false; cancelFlag = false; progress = 0;
        exportAudio.setEnabled (false);
        cancel.setEnabled (true);
        startedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto preset = Preset::capture (ctx.proc);
        const int fmt = format.getSelectedItemIndex();
        const double secs = duration.getSelectedId() * 60.0, sr = rate.getSelectedId();
        worker = std::thread ([this, preset, fmt, secs, sr]
        {
            result = gx::exportAudio (preset, target, fmt, secs, sr, &progress, &cancelFlag);
            done = true;
        });
        startTimerHz (10);
    });
}

void ExportPage::timerCallback()
{
    if (done.load())
    {
        stopTimer();
        if (worker.joinable()) worker.join();
        busy = false;
        exportAudio.setEnabled (true);
        cancel.setEnabled (false);
        if (result.isEmpty())
        {
            status = L ("Listo: ", "Done: ") + target.getFullPathName();
            if (ctx.toast) ctx.toast (L ("Audio exportado: ", "Audio exported: ") + target.getFileName());
            target.revealToUser();
        }
        else status = result == "cancelled" ? L ("Exportación cancelada.", "Export cancelled.") : L ("Error: ", "Error: ") + result;
    }
    else
    {
        const double el = juce::Time::getMillisecondCounterHiRes() * 0.001 - startedAt;
        const float p = progress.load();
        juce::String eta;
        if (p > 0.02f) { const int left = (int) (el / p * (1.0 - p)); eta = "  ·  " + L ("quedan ", "") + juce::String (left / 60) + ":" + juce::String (left % 60).paddedLeft ('0', 2) + L ("", " left"); }
        status = L ("Renderizando… ", "Rendering… ") + juce::String ((int) (p * 100)) + " %" + eta;
    }
    repaint();
}

void ExportPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (300).toFloat();
    drawPanel (g, top);
    auto in = top.reduced (24, 18);
    g.setColour (col::text);
    g.setFont (techFont (17.0f));
    g.drawText (L ("EXPORTAR AUDIO LARGO", "LONG AUDIO EXPORT"), in.removeFromTop (26), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (12.5f));
    g.drawText (L ("YouTube, meditación, estudio, cine: hasta 3 horas sin repetirse, con fundido final.", "YouTube, meditation, study, film: up to 3 hours that never repeat, with a final fade-out."),
                in.removeFromTop (20), juce::Justification::centredLeft);
    in.removeFromTop (14);
    g.setFont (font (12.0f, true));
    g.setColour (col::text);
    auto row = in.removeFromTop (20);
    for (auto* label : { "DURACIÓN|DURATION", "FORMATO|FORMAT", "FRECUENCIA|SAMPLE RATE" })
    {
        const juce::String s = juce::String::fromUTF8 (label);
        g.drawText (L (s.upToFirstOccurrenceOf ("|", false, false).toRawUTF8(), s.fromFirstOccurrenceOf ("|", false, false).toRawUTF8()), row.removeFromLeft (200), juce::Justification::centredLeft);
    }
    // progress bar
    auto bar = getLocalBounds().reduced (12).removeFromTop (300).reduced (24, 18).withTrimmedTop (170).removeFromTop (14).toFloat();
    g.setColour (col::bgDeep);
    g.fillRoundedRectangle (bar, 4);
    if (busy.load() || progress.load() > 0)
    {
        g.setGradientFill (juce::ColourGradient (col::accent, bar.getX(), 0, col::accent2, bar.getRight(), 0, false));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress.load()), 4);
    }
    g.setColour (col::text);
    g.setFont (font (12.5f));
    g.drawFittedText (status, bar.translated (0, 22).withHeight (40).toNearestInt(), juce::Justification::topLeft, 2);

    auto low = r.withTrimmedTop (12).removeFromTop (120).toFloat();
    drawPanel (g, low);
    g.setColour (col::text);
    g.setFont (techFont (15.0f));
    g.drawText (L ("PRESETS (.INFINITUM)", "PRESETS (.INFINITUM)"), low.reduced (24, 14).removeFromTop (22), juce::Justification::centredLeft);
}

void ExportPage::resized()
{
    auto in = getLocalBounds().reduced (12).removeFromTop (300).reduced (24, 18);
    in.removeFromTop (26 + 20 + 14 + 20);
    auto row = in.removeFromTop (30);
    duration.setBounds (row.removeFromLeft (200).withTrimmedRight (20));
    format.setBounds (row.removeFromLeft (200).withTrimmedRight (20));
    rate.setBounds (row.removeFromLeft (200).withTrimmedRight (20));
    in.removeFromTop (12);
    auto btns = in.removeFromTop (34);
    exportAudio.setBounds (btns.removeFromLeft (220).reduced (0, 0));
    btns.removeFromLeft (10);
    cancel.setBounds (btns.removeFromLeft (120));
    auto low = getLocalBounds().reduced (12).withTrimmedTop (312).removeFromTop (120).reduced (24, 14).withTrimmedTop (34);
    exportPreset.setBounds (low.removeFromLeft (240).removeFromTop (32));
    low.removeFromLeft (10);
    importPreset.setBounds (low.removeFromLeft (220).removeFromTop (32));
}

// ---------------------------------------------------------------- Settings page
SettingsPage::SettingsPage (Context& c, std::function<void (float)> setScale, std::function<void()> showAbout) : Page (c)
{
    const int scales[] { 50, 75, 90, 100, 110, 125, 150, 175, 200 };
    for (int i = 0; i < 9; ++i) scaleBox.addItem (juce::String (scales[i]) + " %", scales[i]);
    scaleBox.setSelectedId ((int) std::lround (ctx.settings.scale() * 100), juce::dontSendNotification);
    if (scaleBox.getSelectedId() == 0) scaleBox.setSelectedId (100, juce::dontSendNotification);
    scaleBox.onChange = [this, setScale] { setScale ((float) scaleBox.getSelectedId() / 100.0f); };
    animBox.addItemList ({ L ("Completas (60 fps)", "Full (60 fps)"), L ("Reducidas (15 fps)", "Reduced (15 fps)"), L ("Desactivadas", "Off") }, 1);
    animBox.setSelectedItemIndex (ctx.settings.animation(), juce::dontSendNotification);
    animBox.onChange = [this] { ctx.settings.setAnimation (animBox.getSelectedItemIndex()); };
    langBox.addItemList ({ L ("Automático (idioma del sistema)", "Automatic (system language)"), "English", juce::String::fromUTF8 ("Español") }, 1);
    const auto ls = ctx.settings.languageSetting();
    langBox.setSelectedItemIndex (ls == "en" ? 1 : ls == "es" ? 2 : 0, juce::dontSendNotification);
    langBox.onChange = [this]
    {
        const int i = langBox.getSelectedItemIndex();
        ctx.settings.setLanguageSetting (i == 1 ? "en" : i == 2 ? "es" : "auto");
        if (ctx.languageChanged) ctx.languageChanged();
    };
    aiMsgs.setToggleState (ctx.settings.aiMessages(), juce::dontSendNotification);
    aiMsgs.onClick = [this] { ctx.settings.setAiMessages (aiMsgs.getToggleState()); };
    preview.setToggleState (ctx.settings.previewOnSelect(), juce::dontSendNotification);
    preview.onClick = [this] { ctx.settings.setPreviewOnSelect (preview.getToggleState()); };
    historyLimit.setRange (10, 2000, 10);
    historyLimit.setValue (ctx.settings.historyLimit(), juce::dontSendNotification);
    historyLimit.setSliderStyle (juce::Slider::LinearHorizontal);
    historyLimit.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    historyLimit.onValueChange = [this] { ctx.settings.setHistoryLimit ((int) historyLimit.getValue()); };
    openFolder.onClick = [] { Library::defaultRoot().startAsProcess(); };
    clearLearn.onClick = [this] { ctx.proc.clearMidiLearn(); if (ctx.toast) ctx.toast (L ("Asignaciones MIDI Learn borradas", "MIDI Learn assignments cleared")); };
    aboutBtn.onClick = std::move (showAbout);
    for (auto* comp : std::initializer_list<juce::Component*> { &scaleBox, &animBox, &langBox, &aiMsgs, &preview, &historyLimit, &openFolder, &clearLearn, &aboutBtn })
        addAndMakeVisible (comp);
}

void SettingsPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12).removeFromLeft (680).toFloat();
    drawPanel (g, r);
    g.setColour (col::text);
    g.setFont (font (13.0f, true));
    const juce::String labels[] { L ("Tamaño de la interfaz", "Interface size"), L ("Animaciones", "Animations"), "Idioma / Language", "", "", L ("Límite del historial del generador", "Generator history limit") };
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (640);
    for (auto& l : labels) { g.drawText (l, rows.removeFromTop (40), juce::Justification::centredLeft); }
    rows.removeFromTop (70);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawFittedText (L ("Consejo: clic derecho en cualquier mando para asignarlo a tu controlador (MIDI Learn). Barra espaciadora: Play / Stop.",
                         "Tip: right-click any knob to map it to your controller (MIDI Learn). Space bar: Play / Stop."),
                      rows.removeFromTop (40), juce::Justification::topLeft, 2);
}

void SettingsPage::resized()
{
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (640);
    auto row = [&] { auto x = rows.removeFromTop (40); return x.withTrimmedLeft (260).reduced (0, 7); };
    scaleBox.setBounds (row().withWidth (160));
    animBox.setBounds (row().withWidth (220));
    langBox.setBounds (row().withWidth (260));
    aiMsgs.setBounds (rows.removeFromTop (40).reduced (0, 8));
    preview.setBounds (rows.removeFromTop (40).reduced (0, 8));
    historyLimit.setBounds (row());
    rows.removeFromTop (16);
    auto b = rows.removeFromTop (34);
    openFolder.setBounds (b.removeFromLeft (200).reduced (2));
    clearLearn.setBounds (b.removeFromLeft (250).reduced (2));
    aboutBtn.setBounds (b.removeFromLeft (170).reduced (2));
}
}
