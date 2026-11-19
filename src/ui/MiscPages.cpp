#include "Pages.h"

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

// ---------------------------------------------------------------- Logo
void Logo::drawMark (juce::Graphics& g, juce::Rectangle<float> r, float glow)
{
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.48f;
    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const float a = juce::MathConstants<float>::pi / 3.0f * (float) i - juce::MathConstants<float>::pi / 2.0f;
        const juce::Point<float> p (c.x + rad * std::cos (a), c.y + rad * std::sin (a));
        if (i == 0) hex.startNewSubPath (p); else hex.lineTo (p);
    }
    hex.closeSubPath();
    if (glow > 0.01f)
    {
        g.setColour (col::accent.withAlpha (0.25f * glow));
        g.strokePath (hex, juce::PathStrokeType (7.0f));
    }
    juce::ColourGradient grad (col::accent.brighter (0.3f), r.getX(), r.getY(), col::accent.darker (0.4f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.strokePath (hex, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered));
    // inner helix: two interleaved sine strands = "genesis"
    for (int strand = 0; strand < 2; ++strand)
    {
        juce::Path s;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = (float) i / 40.0f;
            const float x = c.x - rad * 0.6f + rad * 1.2f * t;
            const float y = c.y + rad * 0.42f * std::sin (t * kTwoPi + (float) strand * kPi);
            if (i == 0) s.startNewSubPath (x, y); else s.lineTo (x, y);
        }
        g.setColour (strand == 0 ? col::accent : col::accent2);
        g.strokePath (s, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    for (int i = 1; i < 5; ++i)
    {
        const float t = (float) i / 5.0f, x = c.x - rad * 0.6f + rad * 1.2f * t;
        const float y0 = c.y + rad * 0.42f * std::sin (t * kTwoPi), y1 = c.y - rad * 0.42f * std::sin (t * kTwoPi);
        g.setColour (col::text.withAlpha (0.35f));
        g.drawLine (x, y0, x, y1, 1.0f);
    }
}

void Logo::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawMark (g, r.removeFromLeft (r.getHeight()).reduced (4), glow);
    r.removeFromLeft (8);
    // "Infinitum": big, heavy, horizontal gradient + subtle glow
    const auto big = font (34.0f, true).withExtraKerningFactor (0.04f);
    juce::GlyphArrangement ga;
    ga.addLineOfText (big, "Infinitum", r.getX(), r.getCentreY() + 11.0f);
    juce::Path text;
    ga.createPath (text);
    const auto tb = text.getBounds();
    g.setColour (col::accent.withAlpha (0.15f + 0.25f * glow));
    g.strokePath (text, juce::PathStrokeType (3.0f));
    juce::ColourGradient grad (col::accent.brighter (0.25f), tb.getX(), tb.getY(), juce::Colour (0xffffb347), tb.getRight(), tb.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (text);
    g.setColour (col::textDim);
    g.setFont (font (13.0f, true));
    g.drawText ("by DSK", juce::Rectangle<float> (tb.getRight() + 8, tb.getBottom() - 16, 60, 16), juce::Justification::bottomLeft);
}

// ---------------------------------------------------------------- Header
Header::Header (Context& c, std::function<void (int)> onTab) : ctx (c), tabCb (std::move (onTab))
{
    addAndMakeVisible (logo);
    const juce::String names[] { L ("GENERADOR", "GENERATOR"), "PRESETS", "EDITOR", L ("EXPORTAR", "EXPORT"), L ("AJUSTES", "SETTINGS") };
    for (int i = 0; i < 5; ++i)
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
    presetName.setInterceptsMouseClicks (true, false);
    presetName.addMouseListener (this, false);
    addAndMakeVisible (presetName);
    setTab (0);
}

void Header::setTab (int t)
{
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == t, juce::dontSendNotification);
}

void Header::tick (const Frame& f)
{
    const float e = f.energy;
    if (std::abs (e - energy) > 0.01f) { energy = e; logo.glow = e; logo.repaint(); }
}

void Header::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient grad (col::panelHi, 0, 0, col::bg, 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (r);
    g.setColour (col::accent.withAlpha (0.7f));
    g.fillRect (r.removeFromBottom (2));
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromBottom (2);
    logo.setBounds (r.removeFromLeft (250));
    r.removeFromLeft (10);
    auto tabArea = r.removeFromLeft (520);
    const int tw = tabArea.getWidth() / tabs.size();
    for (auto* t : tabs) t->setBounds (tabArea.removeFromLeft (tw).reduced (2, 6));
    r.removeFromLeft (12);
    about.setBounds (r.removeFromRight (30).reduced (2, 8));
    redoButton.setBounds (r.removeFromRight (70).reduced (2, 8));
    undoButton.setBounds (r.removeFromRight (74).reduced (2, 8));
    abButton.setBounds (r.removeFromRight (34).reduced (2, 8));
    r.removeFromRight (6);
    prev.setBounds (r.removeFromLeft (26).reduced (1, 8));
    next.setBounds (r.removeFromRight (26).reduced (1, 8));
    presetName.setBounds (r.reduced (2, 8));
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
    auto box = getLocalBounds().withSizeKeepingCentre (520, 360).toFloat();
    drawPanel (g, box, 10);
    auto in = box.reduced (28);
    Logo::drawMark (g, in.removeFromTop (90).withSizeKeepingCentre (90, 90), 0.6f);
    g.setColour (col::accent);
    g.setFont (font (30.0f, true));
    g.drawText ("Infinitum", in.removeFromTop (38), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (font (14.0f, true));
    g.drawText ("by DSK  -  version " INFINITUM_VERSION, in.removeFromTop (22), juce::Justification::centred);
    g.setColour (col::text);
    g.setFont (font (13.0f));
    g.drawFittedText (L ("Sintetizador freeware de DSK Music.\nMotor de síntesis, generador inteligente de presets y diseño: DSK Music.\n"
                         "Construido con JUCE (GPLv3) y el VST3 SDK (MIT).\nLicencia: GNU GPL v3.",
                         "Freeware synthesizer by DSK Music.\nSynthesis engine, smart preset generator and design: DSK Music.\n"
                         "Built with JUCE (GPLv3) and the VST3 SDK (MIT).\nLicense: GNU GPL v3."),
                      in.removeFromTop (90).toNearestInt(), juce::Justification::centred, 5);
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText (L ("Clic para cerrar", "Click to close"), box.removeFromBottom (24), juce::Justification::centred);
}

void AboutOverlay::resized()
{
    auto box = getLocalBounds().withSizeKeepingCentre (520, 360);
    link.setBounds (box.withTrimmedTop (290).withHeight (28).withSizeKeepingCentre (200, 28));
}

// ---------------------------------------------------------------- Export page
ExportPage::ExportPage (Context& c) : Page (c)
{
    addAndMakeVisible (exportPreset);
    addAndMakeVisible (importPreset);
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
}

void ExportPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (12);
    drawPanel (g, r.removeFromTop (110).toFloat());
    g.setColour (col::text);
    g.setFont (font (15.0f, true));
    g.drawText (L ("FORMATO PROPIO .INFINITUM", "NATIVE .INFINITUM FORMAT"), getLocalBounds().reduced (26, 20).removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawText (L ("JSON versionado con wavetables y samples incrustados, patrones y metadatos.", "Versioned JSON with embedded wavetables and samples, patterns and metadata."), getLocalBounds().reduced (26, 44).removeFromTop (18), juce::Justification::centredLeft);
}

void ExportPage::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (110).reduced (14).withTrimmedTop (52);
    exportPreset.setBounds (top.removeFromLeft (230).reduced (2));
    importPreset.setBounds (top.removeFromLeft (200).reduced (2));
    r.removeFromTop (10);
    if (sfzPanel) sfzPanel->setBounds (r);
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
    langBox.addItemList ({ juce::String::fromUTF8 ("Español"), "English" }, 1);
    langBox.setSelectedItemIndex (ctx.settings.messageLanguage() == "en" ? 1 : 0, juce::dontSendNotification);
    langBox.onChange = [this]
    {
        const bool en = langBox.getSelectedItemIndex() == 1;
        ctx.settings.setMessageLanguage (en ? "en" : "es");
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
    auto r = getLocalBounds().reduced (12).removeFromLeft (640).toFloat();
    drawPanel (g, r);
    g.setColour (col::text);
    g.setFont (font (13.0f, true));
    const juce::String labels[] { L ("Tamaño de la interfaz", "Interface size"), L ("Animaciones", "Animations"), "Idioma / Language", "", "", L ("Límite del historial del generador", "Generator history limit") };
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (600);
    for (auto& l : labels) { g.drawText (l, rows.removeFromTop (40), juce::Justification::centredLeft); }
}

void SettingsPage::resized()
{
    auto rows = getLocalBounds().reduced (28, 28).removeFromLeft (600);
    auto row = [&] { auto x = rows.removeFromTop (40); return x.withTrimmedLeft (260).reduced (0, 7); };
    scaleBox.setBounds (row().withWidth (160));
    animBox.setBounds (row().withWidth (220));
    langBox.setBounds (row().withWidth (160));
    aiMsgs.setBounds (rows.removeFromTop (40).reduced (0, 8));
    preview.setBounds (rows.removeFromTop (40).reduced (0, 8));
    historyLimit.setBounds (row());
    rows.removeFromTop (16);
    auto b = rows.removeFromTop (34);
    openFolder.setBounds (b.removeFromLeft (200).reduced (2));
    clearLearn.setBounds (b.removeFromLeft (240).reduced (2));
    aboutBtn.setBounds (b.removeFromLeft (150).reduced (2));
}
}
