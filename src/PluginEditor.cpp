#include "PluginEditor.h"
#include "ui/GeneratorPage.h"
#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define GX_HAS_BINARYDATA 1
#endif

LibraryHolder::LibraryHolder()
{
#if GX_HAS_BINARYDATA
    int size = 0;
    if (auto* data = BinaryData::getNamedResource ("factory_infinitumbank", size))
        library.setFactoryBank (juce::String::fromUTF8 (data, size));
#endif
}

InfinitumEditor::InfinitumEditor (InfinitumProcessor& p)
    : AudioProcessorEditor (p), proc (p), undo (p),
      ctx { p, settings, libHolder->library, undo, {}, {}, {} },
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard),
      frames (p)
{
    gx::uiEnglish() = settings.messageLanguage() == "en"; // before any page builds its labels
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    ctx.toast = [this] (const juce::String& m) { toast (m); };
    ctx.refreshAll = [this] { refreshAll(); };
    ctx.startMidiLearn = [this] (int param)
    {
        proc.learnParam.store (param);
        toast (gx::L ("MIDI Learn: mueve un control de tu controlador para ", "MIDI Learn: move a control on your controller for ") + gx::paramDefs()[(size_t) param].name);
    };

    ctx.languageChanged = [this] { pendingRebuild = true; }; // rebuilt from the timer, never inside the combo callback

    addAndMakeVisible (root);
    root.addAndMakeVisible (keyboard);
    root.addChildComponent (toastLabel);
    buildUi();
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, gx::ui::col::accent.withAlpha (0.8f));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, gx::ui::col::accent.withAlpha (0.3f));
    keyboard.setLowestVisibleKey (24);
    keyboard.setKeyWidth (18.0f);
    toastLabel.setColour (juce::Label::backgroundColourId, gx::ui::col::bgDeep.withAlpha (0.92f));
    toastLabel.setColour (juce::Label::outlineColourId, gx::ui::col::accent);
    toastLabel.setColour (juce::Label::textColourId, gx::ui::col::text);
    toastLabel.setJustificationType (juce::Justification::centred);
    toastLabel.setFont (gx::ui::font (13.0f, true));

    tooltips = std::make_unique<juce::TooltipWindow> (&root, 600);
    constrainer.setFixedAspectRatio ((double) kDesignW / kDesignH);
    constrainer.setSizeLimits (kDesignW / 2, kDesignH / 2, kDesignW * 2, kDesignH * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, settings.scale());
    setSize ((int) (kDesignW * s), (int) (kDesignH * s));
    header->setPresetName (proc.currentMeta.name);
    showPage (0);
    lastTime = gx::ui::nowSeconds();
    lastPresetGen = proc.presetGeneration.load();
    startTimerHz (60);
}

// Builds header + pages with the current language (gx::uiEnglish()).
void InfinitumEditor::buildUi()
{
    header = std::make_unique<gx::ui::Header> (ctx, [this] (int t) { showPage (t); });
    landscapePage = std::make_unique<gx::ui::LandscapePage> (ctx);
    soundPage = std::make_unique<gx::ui::SoundPage> (ctx);
    generator = std::make_unique<gx::ui::GeneratorPage> (ctx);
    presets = std::make_unique<gx::ui::PresetsPage> (ctx);
    exportPage = std::make_unique<gx::ui::ExportPage> (ctx);
    settingsPage = std::make_unique<gx::ui::SettingsPage> (ctx, [this] (float s) { setUiScale (s); }, [this] { about->setVisible (true); about->toFront (false); });
    about = std::make_unique<gx::ui::AboutOverlay>();
    pages = { landscapePage.get(), soundPage.get(), generator.get(), presets.get(), exportPage.get(), settingsPage.get() };
    root.addAndMakeVisible (*header);
    for (auto* pg : pages) root.addChildComponent (pg);
    root.addChildComponent (*about);
    toastLabel.toFront (false);

    header->onPresetStep = [this] (int d) { presets->step (d); };
    header->onAbout = [this] { about->setVisible (true); about->toFront (false); };
    header->onAB = [this] { toggleAB(); };
    header->onUndo = [this] { undo.performUndo(); refreshAll(); };
    header->onRedo = [this] { undo.performRedo(); refreshAll(); };
    header->abButton.setButtonText (abActive == 0 ? "A" : "B");
    presets->onLoaded = [this] (const gx::Preset& pr) { header->setPresetName (pr.meta.name); generator->presetLoaded(); };
    generator->onPresetChanged = [this] { header->setPresetName (proc.currentMeta.name); undo.reset(); };
    generator->openEditor = [this] { showPage (1); };
    generator->openExport = [this] { showPage (4); };
    generator->openPresets = [this] { showPage (3); };
}

// Language switch: recreate every page so all labels, menus and tooltips change at once.
void InfinitumEditor::rebuildUi()
{
    pendingRebuild = false;
    gx::uiEnglish() = settings.messageLanguage() == "en";
    const auto genState = generator->uiState();
    const int page = currentPage;
    pages.clear();
    header.reset(); landscapePage.reset(); soundPage.reset(); generator.reset(); presets.reset(); exportPage.reset(); settingsPage.reset(); about.reset();
    buildUi();
    generator->setUiState (genState);
    resized();
    header->setPresetName (proc.currentMeta.name);
    showPage (page);
    toast (gx::L ("Idioma: Español", "Language: English"));
}

InfinitumEditor::~InfinitumEditor()
{
    stopTimer();
    proc.keyboardState.allNotesOff (1);
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lnf) juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void InfinitumEditor::paint (juce::Graphics& g) { g.fillAll (gx::ui::col::bg); }

void InfinitumEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    root.setBounds (0, 0, kDesignW, kDesignH);
    root.setTransform (juce::AffineTransform::scale (s));
    auto r = root.getLocalBounds();
    header->setBounds (r.removeFromTop (64));
    keyboard.setBounds (r.removeFromBottom (52).reduced (4, 2));
    for (auto* pg : pages) pg->setBounds (r);
    about->setBounds (root.getLocalBounds());
    toastLabel.setBounds (root.getLocalBounds().withSizeKeepingCentre (560, 34).withY (kDesignH - 110));
}

void InfinitumEditor::setUiScale (float s)
{
    settings.setScale (s);
    setSize ((int) (kDesignW * s), (int) (kDesignH * s));
}

void InfinitumEditor::showPage (int index)
{
    currentPage = juce::jlimit (0, (int) pages.size() - 1, index);
    for (int i = 0; i < (int) pages.size(); ++i) pages[(size_t) i]->setVisible (i == currentPage);
    header->setTab (currentPage);
    pages[(size_t) currentPage]->pageShown();
}


void InfinitumEditor::toast (const juce::String& msg)
{
    toastLabel.setText (msg, juce::dontSendNotification);
    toastLabel.setVisible (true);
    toastLabel.toFront (false);
    toastUntil = gx::ui::nowSeconds() + 2.6;
}

void InfinitumEditor::refreshAll()
{
    header->setPresetName (proc.currentMeta.name);
    for (auto* pg : pages) pg->refresh();
    root.repaint();
}

void InfinitumEditor::toggleAB()
{
    abSlots[(size_t) abActive] = gx::Preset::capture (proc);
    abValid[(size_t) abActive] = true;
    abActive ^= 1;
    if (! abValid[(size_t) abActive]) { abSlots[(size_t) abActive] = abSlots[(size_t) (abActive ^ 1)]; abValid[(size_t) abActive] = true; }
    const auto meta = proc.currentMeta;
    abSlots[(size_t) abActive].applyTo (proc);
    proc.currentMeta = meta;
    header->abButton.setButtonText (abActive == 0 ? "A" : "B");
    toast (abActive == 0 ? gx::L ("Comparando: A", "Comparing: A") : gx::L ("Comparando: B (edita libremente, A se conserva)", "Comparing: B (edit freely, A is kept)"));
    refreshAll();
}

void InfinitumEditor::advanceFrames (double dt)
{
    frames.update (frame, dt);
    frame.reduced = settings.animation() == 1;
    header->tick (frame);
    pages[(size_t) currentPage]->tick (frame);
}

void InfinitumEditor::timerCallback()
{
    const double now = gx::ui::nowSeconds();
    const double dt = juce::jlimit (0.0, 0.25, now - lastTime);
    const int anim = settings.animation();
    const double interval = anim == 0 ? 0.0 : anim == 1 ? 1.0 / 15.0 : 0.5;
    if (now - slowTickAt >= interval)
    {
        slowTickAt = now;
        lastTime = now;
        advanceFrames (dt);
    }
    if (now > undoPollAt)
    {
        undoPollAt = now + 0.3;
        undo.poll (juce::ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown());
        header->undoButton.setEnabled (undo.canUndo());
        header->redoButton.setEnabled (undo.canRedo());
        if (const int g = proc.presetGeneration.load(); g != lastPresetGen) { lastPresetGen = g; refreshAll(); }
    }
    if (toastLabel.isVisible() && now > toastUntil) toastLabel.setVisible (false);
    if (pendingRebuild && ! generator->isBusy() && ! exportPage->isBusy()) rebuildUi(); // waits for a running generation / export
}

bool InfinitumEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z' && ! mods.isShiftDown()) { undo.performUndo(); refreshAll(); return true; }
    if (mods.isCommandDown() && (k.getKeyCode() == 'Y' || (k.getKeyCode() == 'Z' && mods.isShiftDown()))) { undo.performRedo(); refreshAll(); return true; }
    if (k.getKeyCode() == juce::KeyPress::spaceKey && ! mods.isAnyModifierKeyDown()) { proc.setPlaying (! proc.isPlayingInternal()); return true; }
    if (currentPage == 2 && generator->handleKey (k)) return true;
    return false;
}
