#include "Widgets.h"

namespace gx::ui
{
// ---------------------------------------------------------------- Settings
Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Infinitum";
    o.filenameSuffix = "settings";
    o.folderName = "DSK Music/Infinitum";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.processLock = nullptr;
    props = std::make_unique<juce::PropertiesFile> (Library::defaultRoot().getChildFile ("settings.xml"), o);
}

// ---------------------------------------------------------------- UndoStack
UndoStack::UndoStack (InfinitumProcessor& p) : proc (p) { last = Preset::capture (proc); }

bool UndoStack::differs (const Preset& a) const
{
    for (int i = 0; i < P::numParams; ++i)
        if (a.params[(size_t) i] != proc.getParam (i)) return true;
    return std::memcmp (&a.patterns, &proc.getPatterns(), sizeof (Patterns)) != 0;
}

void UndoStack::poll (bool dragging)
{
    if (applying || dragging || ! differs (last)) return;
    undo.push_back (last);
    if (undo.size() > 100) undo.erase (undo.begin());
    redo.clear();
    last = Preset::capture (proc);
}

void UndoStack::performUndo()
{
    if (undo.empty()) return;
    redo.push_back (Preset::capture (proc));
    applying = true;
    auto meta = proc.currentMeta;
    undo.back().applyTo (proc);
    proc.currentMeta = meta;
    applying = false;
    last = undo.back();
    undo.pop_back();
}

void UndoStack::performRedo()
{
    if (redo.empty()) return;
    undo.push_back (Preset::capture (proc));
    applying = true;
    auto meta = proc.currentMeta;
    redo.back().applyTo (proc);
    proc.currentMeta = meta;
    applying = false;
    last = redo.back();
    redo.pop_back();
}

void UndoStack::reset() { undo.clear(); redo.clear(); last = Preset::capture (proc); }

// ---------------------------------------------------------------- clipboard
juce::String fieldOf (const juce::String& id) { return id.fromFirstOccurrenceOf ("_", false, false); }

static std::map<juce::String, float>& clip() { static std::map<juce::String, float> c; return c; }
static juce::String& clipKind() { static juce::String k; return k; }
static juce::String kindOf (const juce::String& group) { return group.retainCharacters ("abcdefghijklmnopqrstuvwxyz."); }

void copySection (InfinitumProcessor& p, const juce::String& group)
{
    clip().clear();
    clipKind() = kindOf (group);
    for (auto& d : paramDefs())
        if (d.group == group) clip()[fieldOf (d.id)] = p.getParam (d.index);
}

bool pasteSection (InfinitumProcessor& p, const juce::String& group)
{
    if (clip().empty() || clipKind() != kindOf (group)) return false;
    for (auto& d : paramDefs())
        if (d.group == group)
            if (auto it = clip().find (fieldOf (d.id)); it != clip().end()) p.setParamPlain (d.index, it->second);
    return true;
}

// ---------------------------------------------------------------- ParamKnob
ParamKnob::ParamKnob (Context& c, int i, const juce::String& label) : ctx (c), index (i)
{
    const auto& d = paramDefs()[(size_t) i];
    static const std::map<juce::String, juce::String> shortNames {
        { "uni_voices", "VOICES" }, { "uni_detune", "DETUNE" }, { "uni_spread", "SPREAD" }, { "uni_blend", "BLEND" },
        { "bend_range", "BEND" }, { "vel_sens", "VEL SENS" }, { "eq_lo_f", "LOW F" }, { "eq_lo_g", "LOW G" },
        { "eq_m1_f", "MID1 F" }, { "eq_m1_g", "MID1 G" }, { "eq_m2_f", "MID2 F" }, { "eq_m2_g", "MID2 G" },
        { "eq_hi_f", "HIGH F" }, { "eq_hi_g", "HIGH G" }, { "eq_q", "MID Q" }, { "tg_steps", "STEPS" }, { "tg_depth", "DEPTH" },
        { "tg_attack", "ATTACK" }, { "tg_release", "RELEASE" }, { "arp_oct", "OCTAVES" }, { "arp_gate", "GATE" },
        { "arp_swing", "SWING" }, { "arp_steps", "STEPS" }, { "seq_steps", "STEPS" }, { "seq_glide", "GLIDE" },
        { "comp_thresh", "THRESH" }, { "comp_attack", "ATTACK" }, { "comp_release", "RELEASE" }, { "comp_makeup", "MAKEUP" },
        { "granular_pitch", "JITTER" }, { "wide_bass", "MONO <" }, { "crush_rate", "RATE" } };
    name = label;
    if (name.isEmpty())
        if (auto it = shortNames.find (d.id); it != shortNames.end()) name = it->second;
    for (auto suf : { "_a_curve", "_d_curve", "_r_curve" })
        if (label.isEmpty() && d.id.endsWith (suf)) name = juce::String (suf).substring (1, 2).toUpperCase() + " CURVE";
    if (name.isEmpty())
    {
        name = d.name;
        for (auto pre : { "osc", "filter", "env", "lfo" })
            if (d.group.startsWith (pre)) name = d.name.fromFirstOccurrenceOf (" ", false, false).fromFirstOccurrenceOf (" ", false, false);
        if (d.group.startsWith ("fx.") && d.name.containsChar (' ')) name = d.name.fromFirstOccurrenceOf (" ", false, false);
        if (name.isEmpty()) name = d.name;
    }
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr, 2000);
    slider.setDoubleClickReturnValue (true, (double) d.def);
    slider.setTooltip (d.name);
    slider.getProperties().set ("bipolar", d.range.start < 0 && d.range.end > 0);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, d.id, slider);
    slider.textFromValueFunction = [d] (double v)
    {
        if (d.unit == "Hz") return v >= 1000 ? juce::String (v / 1000.0, 2) + " kHz" : (v < 100 ? juce::String (v, 1) : juce::String ((int) std::lround (v))) + " Hz";
        if (d.unit == "s") return v < 1 ? juce::String ((int) std::lround (v * 1000.0)) + " ms" : juce::String (v, 2) + " s";
        if (d.unit == "dB") return juce::String (v, 1) + " dB";
        if (d.kind == pkInt) return juce::String ((int) std::lround (v)) + (d.unit.isNotEmpty() ? " " + d.unit : juce::String());
        if (d.range.start == 0 && d.range.end == 1 && d.unit.isEmpty()) return juce::String ((int) std::lround (v * 100.0)) + " %";
        return juce::String (v, 2) + (d.unit.isNotEmpty() ? " " + d.unit : juce::String());
    };
    slider.updateText();
    if (d.destIndex > 0) startTimerHz (10);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    const int s = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (s, s));
}

void ParamKnob::paint (juce::Graphics& g)
{
    g.setColour (col::textDim);
    g.setFont (font (10.5f, true));
    g.drawFittedText (name.toUpperCase(), getLocalBounds().removeFromBottom (14), juce::Justification::centred, 1, 0.7f);
    if (dropHover)
    {
        g.setColour (col::accent3.withAlpha (0.35f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }
}

void ParamKnob::timerCallback()
{
    const int dest = paramDefs()[(size_t) index].destIndex;
    float sum = 0;
    for (int s = 0; s < kNumModSlots; ++s)
        if ((int) ctx.proc.getParam (P::mod (s, P::mDst)) == dest && (int) ctx.proc.getParam (P::mod (s, P::mSrc)) > 0)
            sum += ctx.proc.getParam (P::mod (s, P::mAmt));
    const float prev = (float) (double) slider.getProperties().getWithDefault ("mod", 0.0);
    if (std::abs (prev - sum) > 1.0e-4f) { slider.getProperties().set ("mod", sum); slider.repaint(); }
}

bool ParamKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return paramDefs()[(size_t) index].destIndex > 0 && d.description.toString().startsWith ("modsrc:");
}

void ParamKnob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    repaint();
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    const int dest = paramDefs()[(size_t) index].destIndex;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int cs = (int) ctx.proc.getParam (P::mod (s, P::mSrc)), cd = (int) ctx.proc.getParam (P::mod (s, P::mDst));
        if ((cs == src && cd == dest) || cs == 0 || cd == 0)
        {
            ctx.proc.setParamPlain (P::mod (s, P::mSrc), (float) src);
            ctx.proc.setParamPlain (P::mod (s, P::mDst), (float) dest);
            if (cs != src || cd != dest) ctx.proc.setParamPlain (P::mod (s, P::mAmt), 0.35f);
            if (ctx.toast) ctx.toast (sourceNames()[src] + " -> " + paramDefs()[(size_t) index].name + " (slot " + juce::String (s + 1) + ")");
            return;
        }
    }
    if (ctx.toast) ctx.toast (L ("Matriz llena: libera un slot", "Matrix full: free a slot"));
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) showMenu();
}

void ParamKnob::showMenu()
{
    juce::PopupMenu m;
    const auto& d = paramDefs()[(size_t) index];
    m.addSectionHeader (d.name);
    m.addItem (1, "MIDI Learn");
    m.addItem (2, L ("Valor por defecto", "Default value"));
    if (d.destIndex > 0) m.addItem (3, L ("Quitar modulaciones", "Remove modulations"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, dest = d.destIndex, def = d.def] (int r)
    {
        if (r == 1 && ctx.startMidiLearn) ctx.startMidiLearn (index);
        if (r == 2) ctx.proc.setParamPlain (index, def);
        if (r == 3)
            for (int s = 0; s < kNumModSlots; ++s)
                if ((int) ctx.proc.getParam (P::mod (s, P::mDst)) == dest)
                {
                    ctx.proc.setParamPlain (P::mod (s, P::mSrc), 0);
                    ctx.proc.setParamPlain (P::mod (s, P::mDst), 0);
                    ctx.proc.setParamPlain (P::mod (s, P::mAmt), 0);
                }
    });
}

// ---------------------------------------------------------------- toggles/combos
ParamToggle::ParamToggle (Context& c, int i, const juce::String& label)
{
    const auto& d = paramDefs()[(size_t) i];
    setButtonText (label.isNotEmpty() ? label : d.name.fromLastOccurrenceOf (" ", false, false).toUpperCase());
    setTooltip (d.name);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (c.proc.apvts, d.id, *this);
}

ParamCombo::ParamCombo (Context& c, int i)
{
    const auto& d = paramDefs()[(size_t) i];
    addItemList (d.choices, 1);
    setTooltip (d.name);
    att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (c.proc.apvts, d.id, *this);
}

// ---------------------------------------------------------------- Section
Section::Section (Context& c, juce::String t, juce::String g) : title (std::move (t)), group (std::move (g)), ctx (c) {}

void Section::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    auto head = getLocalBounds().reduced (8, 4).removeFromTop (16);
    g.setColour (accent);
    g.fillRoundedRectangle (head.removeFromLeft (3).toFloat(), 1.5f);
    head.removeFromLeft (6);
    g.setColour (col::text);
    g.setFont (font (12.0f, true));
    g.drawText (title.toUpperCase(), head, juce::Justification::centredLeft);
}

void Section::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() || group.isEmpty() || e.y > 24) return;
    juce::PopupMenu m;
    m.addItem (1, L ("Copiar ", "Copy ") + title);
    m.addItem (2, L ("Pegar en ", "Paste into ") + title);
    m.showMenuAsync ({}, [this] (int r)
    {
        if (r == 1) { copySection (ctx.proc, group); if (ctx.toast) ctx.toast (title + L (" copiado", " copied")); }
        if (r == 2 && ! pasteSection (ctx.proc, group) && ctx.toast) ctx.toast (L ("El portapapeles no es compatible con ", "Clipboard is not compatible with ") + title);
    });
}

// ---------------------------------------------------------------- SourceChip
void SourceChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1);
    g.setColour (col::bgDeep);
    g.fillRoundedRectangle (r, 4);
    g.setColour (col::accent3.withAlpha (0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, std::abs (level))));
    g.drawRoundedRectangle (r, 4, 1.2f);
    g.fillRoundedRectangle (r.removeFromLeft (4).reduced (0, 3), 2);
    g.setColour (col::text);
    g.setFont (font (11.0f, true));
    g.drawFittedText (sourceNames()[src], getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
}

void SourceChip::mouseDrag (const juce::MouseEvent&)
{
    if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! c->isDragAndDropActive()) c->startDragging ("modsrc:" + juce::String (src), this);
}

// ---------------------------------------------------------------- AutoPanel
AutoPanel::AutoPanel (Context& c, const juce::String& t, const juce::String& grp, int cols, std::vector<int> skip)
    : Section (c, t, grp), columns (cols)
{
    for (auto& d : paramDefs())
    {
        if (d.group != grp || std::find (skip.begin(), skip.end(), d.index) != skip.end()) continue;
        std::unique_ptr<juce::Component> comp;
        if (d.kind == pkBool) comp = std::make_unique<ParamToggle> (c, d.index);
        else if (d.kind == pkChoice) comp = std::make_unique<ParamCombo> (c, d.index);
        else comp = std::make_unique<ParamKnob> (c, d.index);
        addAndMakeVisible (*comp);
        controls.push_back (std::move (comp));
    }
}

void AutoPanel::resized()
{
    auto r = content();
    // toggles + combos in a top strip, knobs in a grid
    std::vector<juce::Component*> top, knobs;
    for (auto& c : controls) (dynamic_cast<ParamKnob*> (c.get()) ? knobs : top).push_back (c.get());
    if (! top.empty())
    {
        auto strip = r.removeFromTop (24);
        const int w = juce::jmin (130, strip.getWidth() / (int) top.size());
        for (auto* c : top) c->setBounds (strip.removeFromLeft (w).reduced (2, 1));
        r.removeFromTop (4);
    }
    if (knobs.empty()) return;
    const int cols = columns > 0 ? columns : juce::jmax (1, r.getWidth() / 62);
    const int rows = ((int) knobs.size() + cols - 1) / cols;
    const int cw = r.getWidth() / cols, ch = juce::jmin (78, r.getHeight() / juce::jmax (1, rows));
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (r.getX() + (int) (i % (size_t) cols) * cw, r.getY() + (int) (i / (size_t) cols) * ch, cw, ch);
}
}
