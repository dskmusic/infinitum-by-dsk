#include "Pages.h"
#include "../dsp/Wavetable.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace gx::ui
{
using namespace P;

// ================================================================ wavetable editor dialog
class WavetableEditor : public juce::Component
{
public:
    WavetableEditor (Context& c, int o) : ctx (c), osc (o)
    {
        for (int i = 0; i < 2; ++i) for (int k = 0; k < 32; ++k) spec[i][(size_t) k] = (k == 0) ? 1.0f : (i == 0 ? 0.0f : 1.0f / (float) (k + 1));
        for (auto* b : { &make, &import, &close }) addAndMakeVisible (b);
        make.onClick = [this] { generate(); };
        close.onClick = [this] { setVisible (false); };
        import.onClick = [this] { importWav(); };
        frames.setRange (2, 64, 1); frames.setValue (32);
        frames.setSliderStyle (juce::Slider::LinearHorizontal);
        frames.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 18);
        addAndMakeVisible (frames);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        drawPanel (g, box().toFloat(), 8);
        auto in = box().reduced (16);
        g.setColour (col::text); g.setFont (font (14.0f, true));
        g.drawText (L ("EDITOR DE WAVETABLE - OSC ", "WAVETABLE EDITOR - OSC ") + juce::String (osc + 1), in.removeFromTop (22), juce::Justification::centredLeft);
        g.setColour (col::textDim); g.setFont (font (11.5f));
        g.drawText (L ("Dibuja el espectro inicial (A) y final (B): se generan los frames intermedios por morphing aditivo.", "Draw the start (A) and end (B) spectra: the frames in between are built by additive morphing."), in.removeFromTop (18), juce::Justification::centredLeft);
        for (int i = 0; i < 2; ++i)
        {
            auto r = bars (i).toFloat();
            drawScreen (g, r);
            const float w = r.getWidth() / 32;
            for (int k = 0; k < 32; ++k)
            {
                const float h = spec[i][(size_t) k] * (r.getHeight() - 8);
                g.setColour ((i == 0 ? col::accent : col::accent2).withAlpha (0.85f));
                g.fillRect (r.getX() + k * w + 1, r.getBottom() - 4 - h, w - 2, h);
            }
            g.setColour (col::phosphor.withAlpha (0.6f)); g.setFont (monoFont (10));
            g.drawText (i == 0 ? "A (frame 1)" : L ("B (último frame)", "B (last frame)"), r.reduced (6, 3), juce::Justification::topLeft);
        }
    }
    void resized() override
    {
        auto in = box().reduced (16);
        auto bottom = in.removeFromBottom (30);
        close.setBounds (bottom.removeFromRight (90).reduced (2));
        make.setBounds (bottom.removeFromRight (190).reduced (2));
        import.setBounds (bottom.removeFromLeft (220).reduced (2));
        frames.setBounds (bottom.removeFromLeft (220).reduced (4, 4));
    }
    void mouseDown (const juce::MouseEvent& e) override { drawAt (e); }
    void mouseDrag (const juce::MouseEvent& e) override { drawAt (e); }

private:
    juce::Rectangle<int> box() const { return getLocalBounds().withSizeKeepingCentre (760, 440); }
    juce::Rectangle<int> bars (int i) const
    {
        auto in = box().reduced (16).withTrimmedTop (46).withTrimmedBottom (40);
        return i == 0 ? in.removeFromTop (in.getHeight() / 2).reduced (0, 4) : in.withTrimmedTop (in.getHeight() / 2).reduced (0, 4);
    }
    void drawAt (const juce::MouseEvent& e)
    {
        for (int i = 0; i < 2; ++i)
        {
            auto r = bars (i).toFloat();
            if (! r.contains (e.position)) continue;
            const int k = juce::jlimit (0, 31, (int) ((e.position.x - r.getX()) / r.getWidth() * 32));
            spec[i][(size_t) k] = juce::jlimit (0.0f, 1.0f, (r.getBottom() - 4 - e.position.y) / (r.getHeight() - 8));
            repaint();
        }
    }
    void generate()
    {
        const int n = (int) frames.getValue();
        std::vector<std::vector<float>> fr;
        for (int f = 0; f < n; ++f)
        {
            const float t = (float) f / (float) juce::jmax (1, n - 1);
            std::vector<float> s ((size_t) Wavetable::kBaseLen, 0.0f);
            for (int k = 0; k < 32; ++k)
            {
                const float a = spec[0][(size_t) k] * (1 - t) + spec[1][(size_t) k] * t;
                if (a < 1e-4f) continue;
                for (int i = 0; i < Wavetable::kBaseLen; ++i) s[(size_t) i] += a * std::sin (kTwoPi * (float) ((k + 1) * i) / Wavetable::kBaseLen);
            }
            fr.push_back (std::move (s));
        }
        apply (Wavetable::fromFrames (fr, "Drawn"));
    }
    void importWav()
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Importar wavetable (WAV, ciclos de 2048 muestras)", "Import wavetable (WAV, 2048-sample cycles)"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (! rd) { if (ctx.toast) ctx.toast (L ("Formato no soportado", "Unsupported format")); return; }
            const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) 2048 * 256);
            juce::AudioBuffer<float> b (1, len);
            rd->read (&b, 0, len, 0, true, false);
            const int cycle = len >= 2048 ? 2048 : len;
            apply (Wavetable::fromAudio (b.getReadPointer (0), len, cycle, f.getFileNameWithoutExtension()));
        });
    }
    void apply (std::unique_ptr<Wavetable> t)
    {
        ctx.proc.setUserWavetable (osc, std::move (t));
        ctx.proc.setParamPlain (P::osc (osc, oType), (float) otWavetable);
        ctx.proc.setParamPlain (P::osc (osc, oTable), (float) numBuiltinTables()); // "User"
        if (ctx.toast) ctx.toast (L ("Wavetable de usuario cargada en OSC ", "User wavetable loaded into OSC ") + juce::String (osc + 1));
    }
    Context& ctx;
    int osc;
    std::array<std::array<float, 32>, 2> spec {};
    juce::TextButton make { L ("Generar wavetable", "Build wavetable") }, import { L ("Importar WAV...", "Import WAV...") }, close { L ("Cerrar", "Close") };
    juce::Slider frames;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ================================================================ OSC tab
class OscPanel : public Box, private juce::Timer
{
public:
    OscPanel (Context& c, int o, std::function<void (int)> openWt) : Box (c, L ("Oscilador ", "Oscillator ") + juce::String (o + 1), "osc" + juce::String (o + 1)), osc (o)
    {
        on = &add<ParamToggle> (c, P::osc (o, oOn), "ON");
        type = &add<ParamCombo> (c, P::osc (o, oType));
        table = &add<ParamCombo> (c, P::osc (o, oTable));
        noise = &add<ParamCombo> (c, P::osc (o, oNoise));
        algo = &add<ParamCombo> (c, P::osc (o, oFmAlgo));
        view = &add<OscView> (c.proc, o);
        edit = &add<juce::TextButton> (L ("Editar WT", "Edit WT"));
        edit->onClick = [openWt, o] { openWt (o); };
        loadSmp = &add<juce::TextButton> (L ("Cargar sample...", "Load sample..."));
        loadSmp->setTooltip (L ("Carga un WAV/AIFF/FLAC (mono, máx. 10 s, nota raíz C4). Tipo de ruido 'Sample'. Shape > 50 % = loop.", "Loads a WAV/AIFF/FLAC (mono, max 10 s, root note C4). Noise type 'Sample'. Shape > 50 % = loop."));
        loadSmp->onClick = [this] { loadSample(); };
        for (int f : { oLevel, oPan, oOct, oSemi, oFine, oMorph, oShape, oSync, oFold })
            common.push_back (&add<ParamKnob> (c, P::osc (o, f)));
        for (int f : { oFmR2, oFmR3, oFmR4, oFmI2, oFmI3, oFmI4 }) fm.push_back (&add<ParamKnob> (c, P::osc (o, f)));
        mut = &add<ParamKnob> (c, P::osc (o, oMutRate));
        randPh = &add<ParamToggle> (c, P::osc (o, oRandPhase), "RND PHASE");
        layout = [this] (juce::Rectangle<int> r) { doLayout (r); };
        startTimerHz (5);
    }
private:
    void timerCallback() override
    {
        if ((int) ctx.proc.getParam (P::osc (osc, oType)) != lastType) doLayout (content());
    }
    void doLayout (juce::Rectangle<int> r)
    {
        const int t = (int) ctx.proc.getParam (P::osc (osc, oType));
        lastType = t;
        auto top = r.removeFromTop (24);
        on->setBounds (top.removeFromLeft (52));
        type->setBounds (top.removeFromLeft (150).reduced (2, 1));
        table->setVisible (t == otWavetable); noise->setVisible (t == otNoise); algo->setVisible (t == otFM); edit->setVisible (t == otWavetable);
        if (t == otWavetable) { table->setBounds (top.removeFromLeft (140).reduced (2, 1)); edit->setBounds (top.removeFromLeft (84).reduced (2, 1)); }
        loadSmp->setVisible (t == otNoise);
        if (t == otNoise) { noise->setBounds (top.removeFromLeft (120).reduced (2, 1)); loadSmp->setBounds (top.removeFromLeft (110).reduced (2, 1)); }
        if (t == otFM) algo->setBounds (top.removeFromLeft (130).reduced (2, 1));
        randPh->setBounds (top.removeFromRight (100));
        r.removeFromTop (4);
        view->setBounds (r.removeFromLeft (170).reduced (0, 2));
        r.removeFromLeft (6);
        std::vector<juce::Component*> ks (common.begin(), common.end());
        for (auto* k : fm) k->setVisible (t == otFM);
        mut->setVisible (t == otMutation);
        if (t == otFM) ks.insert (ks.end(), fm.begin(), fm.end());
        if (t == otMutation) ks.push_back (mut);
        gridKnobs (ks, r, t == otFM ? 8 : 5);
    }
    void loadSample()
    {
        chooser = std::make_unique<juce::FileChooser> (L ("Cargar sample", "Load sample"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            if (! rd) { if (ctx.toast) ctx.toast (L ("Formato no soportado", "Unsupported format")); return; }
            const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 10.0));
            juce::AudioBuffer<float> b ((int) rd->numChannels, len);
            rd->read (&b, 0, len, 0, true, true);
            auto s = std::make_unique<SampleData>();
            s->data.assign ((size_t) len, 0.0f);
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = 0; i < len; ++i) s->data[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
            s->sampleRate = rd->sampleRate;
            s->rootNote = 60;
            s->name = f.getFileNameWithoutExtension();
            ctx.proc.setSample (std::move (s));
            ctx.proc.setParamPlain (P::osc (osc, oType), (float) otNoise);
            ctx.proc.setParamPlain (P::osc (osc, oNoise), (float) ntSample);
            if (ctx.toast) ctx.toast (L ("Sample cargado: ", "Sample loaded: ") + f.getFileName());
        });
    }
    int osc, lastType = -1;
    ParamToggle* on; ParamCombo *type, *table, *noise, *algo;
    OscView* view; juce::TextButton *edit, *loadSmp; ParamKnob* mut; ParamToggle* randPh;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::Component*> common, fm;
};

class OscTab : public juce::Component
{
public:
    explicit OscTab (Context& c) : voice (c, L ("Voz", "Voice"), "global")
    {
        for (int o = 0; o < kNumOsc; ++o)
        {
            panels.add (new OscPanel (c, o, [this, &c] (int osc) { wtEditor = std::make_unique<WavetableEditor> (c, osc); addAndMakeVisible (*wtEditor); wtEditor->setBounds (getLocalBounds()); }));
            addAndMakeVisible (panels.getLast());
        }
        voice.accent = col::accent2;
        mode = &voice.add<ParamCombo> (c, P::playMode);
        legato = &voice.add<ParamToggle> (c, P::glideLegato, "LEGATO GLIDE");
        mpe = &voice.add<ParamToggle> (c, P::mpeOn, "MPE");
        for (int p : { P::master, P::polyphony, P::glide, P::uniVoices, P::uniDetune, P::uniSpread, P::uniBlend, P::bendRange, P::velSens })
            knobs.push_back (&voice.add<ParamKnob> (c, p));
        voice.layout = [this] (juce::Rectangle<int> r)
        {
            mode->setBounds (r.removeFromTop (24).reduced (2, 1));
            auto t = r.removeFromTop (24);
            legato->setBounds (t.removeFromLeft (t.getWidth() / 2 + 20)); mpe->setBounds (t);
            r.removeFromTop (4);
            gridKnobs (knobs, r, 3);
        };
        addAndMakeVisible (voice);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        voice.setBounds (r.removeFromRight (250).reduced (4));
        const int w = r.getWidth() / 2, h = r.getHeight() / 2;
        for (int i = 0; i < 4; ++i) panels[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % 2) * w, r.getY() + (i / 2) * h, w, h).reduced (4));
        if (wtEditor) wtEditor->setBounds (getLocalBounds());
    }
private:
    juce::OwnedArray<OscPanel> panels;
    Box voice;
    ParamCombo* mode; ParamToggle *legato, *mpe;
    std::vector<juce::Component*> knobs;
    std::unique_ptr<WavetableEditor> wtEditor;
};

// ================================================================ FILTER tab
class FilterTab : public juce::Component
{
public:
    explicit FilterTab (Context& c) : view (c.proc), routing (c, L ("Enrutado", "Routing"), "global"), env2 (c, L ("Envolvente de filtro (Env 2)", "Filter envelope (Env 2)"), "env2")
    {
        addAndMakeVisible (view);
        for (int f = 0; f < kNumFilters; ++f)
        {
            auto* b = boxes.add (new Box (c, L ("Filtro ", "Filter ") + juce::String (f + 1), "filter" + juce::String (f + 1)));
            b->accent = f == 0 ? col::accent : col::accent2;
            auto* on = &b->add<ParamToggle> (c, P::filt (f, fOn), "ON");
            auto* ty = &b->add<ParamCombo> (c, P::filt (f, fType));
            std::vector<juce::Component*> ks;
            for (int fld : { fCutoff, fReso, fDrive, fKey, fEnv }) ks.push_back (&b->add<ParamKnob> (c, P::filt (f, fld)));
            b->layout = [on, ty, ks] (juce::Rectangle<int> r)
            {
                auto t = r.removeFromTop (24);
                on->setBounds (t.removeFromLeft (52)); ty->setBounds (t.removeFromLeft (160).reduced (2, 1));
                r.removeFromTop (6);
                gridKnobs (ks, r.withHeight (juce::jmin (r.getHeight(), 90)), 5);
            };
            addAndMakeVisible (b);
        }
        auto* rt = &routing.add<ParamCombo> (c, P::filtRouting);
        auto* bal = &routing.add<ParamKnob> (c, P::filtBalance, "BALANCE F1/F2");
        routing.layout = [rt, bal] (juce::Rectangle<int> r) { rt->setBounds (r.removeFromTop (24).reduced (2, 1)); bal->setBounds (r.reduced (10, 4).withHeight (80)); };
        addAndMakeVisible (routing);
        auto* ev = &env2.add<EnvView> (c.proc, 1);
        std::vector<juce::Component*> ks;
        for (int fld : { eDelay, eAttack, eHold, eDecay, eSustain, eRelease, eACurve, eDCurve, eRCurve }) ks.push_back (&env2.add<ParamKnob> (c, P::env (1, fld)));
        env2.layout = [ev, ks] (juce::Rectangle<int> r) { ev->setBounds (r.removeFromLeft (260).reduced (2)); gridKnobs (ks, r, 9); };
        addAndMakeVisible (env2);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        view.setBounds (r.removeFromTop (200).reduced (0, 2));
        auto row = r.removeFromTop (180);
        routing.setBounds (row.removeFromRight (200).reduced (4));
        boxes[0]->setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (4));
        boxes[1]->setBounds (row.reduced (4));
        env2.setBounds (r.removeFromTop (180).reduced (4));
    }
private:
    FilterView view;
    juce::OwnedArray<Box> boxes;
    Box routing, env2;
};

// ================================================================ MOD tab
class MatrixRow : public juce::Component
{
public:
    MatrixRow (Context& c, int slot) : src (c, P::mod (slot, mSrc)), dst (c, P::mod (slot, mDst)), s (slot)
    {
        addAndMakeVisible (src); addAndMakeVisible (dst);
        amt.setSliderStyle (juce::Slider::LinearBar);
        amt.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        amt.getProperties().set ("bipolar", true);
        amt.getProperties().set ("arc", (juce::int64) col::accent3.getARGB());
        amt.setPopupDisplayEnabled (true, true, nullptr);
        amt.setDoubleClickReturnValue (true, 0.0);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (c.proc.apvts, paramDefs()[(size_t) P::mod (slot, mAmt)].id, amt);
        addAndMakeVisible (amt);
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::textDim); g.setFont (monoFont (10.0f));
        g.drawText (juce::String (s + 1).paddedLeft ('0', 2), getLocalBounds().removeFromLeft (20), juce::Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (22);
        src.setBounds (r.removeFromLeft (100).reduced (1));
        amt.setBounds (r.removeFromRight (110).reduced (2, 4));
        dst.setBounds (r.reduced (1));
    }
private:
    ParamCombo src, dst;
    juce::Slider amt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    int s;
};

class ChipBox : public Section, public Animated
{
public:
    ChipBox (Context& c) : Section (c, L ("Fuentes (arrastra a un knob)", "Sources (drag onto a knob)"))
    {
        for (int sIdx = 1; sIdx < msNumSources; ++sIdx) { auto* ch = chips.add (new SourceChip (sIdx)); addAndMakeVisible (ch); }
        accent = col::accent3;
    }
    void resized() override
    {
        auto r = content();
        const int cols = 4, w = r.getWidth() / cols, h = juce::jmin (24, r.getHeight() / ((chips.size() + cols - 1) / cols));
        for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % cols) * w, r.getY() + (i / cols) * h, w, h).reduced (2));
    }
    void tick (const Frame& f) override
    {
        for (auto* ch : chips)
        {
            float v = 0;
            if (ch->src >= msEnv1 && ch->src <= msEnv4) v = f.env[(size_t) (ch->src - msEnv1)];
            else if (ch->src >= msLfo1 && ch->src <= msLfo4) v = f.lfo[(size_t) (ch->src - msLfo1)];
            else if (ch->src >= msMacro1 && ch->src <= msMacro8) v = f.macro[(size_t) (ch->src - msMacro1)];
            else if (ch->src == msVelocity) v = f.lastVelocity;
            else if (ch->src == msStepSeq) v = f.seqValue;
            ch->setLevel (v);
        }
    }
private:
    juce::OwnedArray<SourceChip> chips;
};

class ModTab : public juce::Component
{
public:
    explicit ModTab (Context& c) : macros (c, "Macros", "macros"), chips (c), matrix (c, L ("Matriz de modulación", "Modulation matrix"), "matrix")
    {
        for (int e = 0; e < kNumEnv; ++e)
        {
            auto* b = envs.add (new Box (c, "Env " + juce::String (e + 1) + (e == 0 ? " (amp)" : e == 1 ? L (" (filtro)", " (filter)") : ""), "env" + juce::String (e + 1)));
            auto* ev = &b->add<EnvView> (c.proc, e);
            std::vector<juce::Component*> ks;
            for (int fld : { eDelay, eAttack, eHold, eDecay, eSustain, eRelease, eACurve, eDCurve, eRCurve }) ks.push_back (&b->add<ParamKnob> (c, P::env (e, fld)));
            b->layout = [ev, ks] (juce::Rectangle<int> r) { ev->setBounds (r.removeFromTop (58).reduced (0, 1)); gridKnobs (ks, r, 5); };
            addAndMakeVisible (b);
        }
        for (int l = 0; l < kNumLfo; ++l)
        {
            auto* b = lfos.add (new Box (c, "LFO " + juce::String (l + 1), "lfo" + juce::String (l + 1)));
            b->accent = col::accent3;
            auto* lv = &b->add<LfoView> (c.proc, l);
            auto* shape = &b->add<ParamCombo> (c, P::lfo (l, lShape));
            auto* div = &b->add<ParamCombo> (c, P::lfo (l, lDiv));
            auto* sync = &b->add<ParamToggle> (c, P::lfo (l, lSync), "SYNC");
            auto* rt = &b->add<ParamToggle> (c, P::lfo (l, lRetrig), "RETRIG");
            std::vector<juce::Component*> ks;
            for (int fld : { lRate, lFade, lPhase }) ks.push_back (&b->add<ParamKnob> (c, P::lfo (l, fld)));
            b->layout = [=] (juce::Rectangle<int> r)
            {
                lv->setBounds (r.removeFromLeft (r.getWidth() * 4 / 10).reduced (0, 1));
                r.removeFromLeft (4);
                auto t = r.removeFromTop (22); shape->setBounds (t.removeFromLeft (t.getWidth() / 2).reduced (1)); div->setBounds (t.reduced (1));
                auto t2 = r.removeFromTop (22); sync->setBounds (t2.removeFromLeft (t2.getWidth() / 2)); rt->setBounds (t2);
                gridKnobs (ks, r, 3);
            };
            addAndMakeVisible (b);
        }
        macros.accent = col::accent3;
        std::vector<juce::Component*> mk;
        for (int m = 0; m < kNumMacros; ++m)
        {
            auto* k = &macros.add<ParamKnob> (c, P::macro1 + m, "MACRO " + juce::String (m + 1));
            k->slider.getProperties().set ("arc", (juce::int64) col::accent3.getARGB());
            mk.push_back (k);
        }
        macros.layout = [mk] (juce::Rectangle<int> r) { gridKnobs (mk, r, 4); };
        addAndMakeVisible (macros);
        addAndMakeVisible (chips);
        for (int sl = 0; sl < kNumModSlots; ++sl) rows.add (&matrix.add<MatrixRow> (c, sl));
        matrix.accent = col::accent3;
        matrix.layout = [this] (juce::Rectangle<int> r)
        {
            const int h = r.getHeight() / 8, w = r.getWidth() / 2;
            for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (juce::Rectangle<int> (r.getX() + (i / 8) * w, r.getY() + (i % 8) * h, w, h).reduced (2, 1));
        };
        addAndMakeVisible (matrix);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (2);
        auto er = r.removeFromTop (220);
        const int ew = er.getWidth() / 4;
        for (auto* e : envs) e->setBounds (er.removeFromLeft (ew).reduced (3));
        auto lr = r.removeFromTop (150);
        for (auto* l : lfos) l->setBounds (lr.removeFromLeft (ew).reduced (3));
        macros.setBounds (r.removeFromLeft (250).reduced (3));
        chips.setBounds (r.removeFromLeft (300).reduced (3));
        matrix.setBounds (r.reduced (3));
    }
private:
    juce::OwnedArray<Box> envs, lfos;
    Box macros;
    ChipBox chips;
    Box matrix;
    juce::Array<MatrixRow*> rows;
};

// ================================================================ ARP / GATE / SEQ tab
class RhythmTab : public juce::Component
{
public:
    explicit RhythmTab (Context& c) : arp (c, L ("Arpegiador", "Arpeggiator"), "arp"), gate (c, "Trance Gate", "gate"), seq (c, L ("Step Sequencer (fuente de modulación)", "Step Sequencer (modulation source)"), "seq"), chord (c, L ("Acordes / Strum / Humanize", "Chords / Strum / Humanize"), "global")
    {
        auto lane = [&c] (Box& b, const juce::String& name, StepGrid::Getter g, float lo, float hi, bool q, int stepsParam, int kind, juce::Colour colr, int maxS)
        {
            auto& sg = b.add<StepGrid> (c.proc, std::move (g), maxS, lo, hi, stepsParam, kind);
            sg.label = name; sg.quantize = q; sg.colour = colr;
            return (juce::Component*) &sg;
        };
        std::vector<juce::Component*> top;
        top.push_back (&arp.add<ParamToggle> (c, P::arpOn, "ARP ON"));
        top.push_back (&arp.add<ParamCombo> (c, P::arpMode));
        top.push_back (&arp.add<ParamCombo> (c, P::arpRate));
        top.push_back (&arp.add<ParamToggle> (c, P::arpLatch, "LATCH"));
        std::vector<juce::Component*> ak;
        for (int p : { P::arpOct, P::arpGate, P::arpSwing, P::arpSteps }) ak.push_back (&arp.add<ParamKnob> (c, p));
        std::vector<juce::Component*> lanes {
            lane (arp, L ("PASO ON/OFF", "STEP ON/OFF"), [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].on; }, 0, 1, true, P::arpSteps, 0, col::accent, kMaxArpSteps),
            lane (arp, L ("VELOCIDAD", "VELOCITY"), [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].vel; }, 0, 1, false, P::arpSteps, 0, col::accent2, kMaxArpSteps),
            lane (arp, "GATE", [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].gate; }, 0.05f, 2, false, P::arpSteps, 0, col::accent3, kMaxArpSteps),
            lane (arp, "RATCHET", [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].ratchet; }, 1, 4, true, P::arpSteps, 0, col::accent, kMaxArpSteps),
            lane (arp, L ("PROBABILIDAD", "PROBABILITY"), [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].prob; }, 0, 1, false, P::arpSteps, 0, col::accent2, kMaxArpSteps),
            lane (arp, "TRANSPOSE", [] (Patterns& pt, int i) -> float& { return pt.arp[(size_t) i].transpose; }, -12, 12, true, P::arpSteps, 0, col::accent3, kMaxArpSteps) };
        arp.layout = [top, ak, lanes] (juce::Rectangle<int> r)
        {
            auto t = r.removeFromTop (24);
            for (auto* x : top) x->setBounds (t.removeFromLeft (130).reduced (2, 1));
            gridKnobs (ak, r.removeFromTop (72), 4);
            const int lh = r.getHeight() / (int) lanes.size();
            for (auto* l : lanes) l->setBounds (r.removeFromTop (lh).reduced (0, 2));
        };
        std::vector<juce::Component*> gt { &gate.add<ParamToggle> (c, P::tgOn, "GATE ON"), &gate.add<ParamCombo> (c, P::tgRate) };
        std::vector<juce::Component*> gk;
        for (int p : { P::tgSteps, P::tgDepth, P::tgAttack, P::tgRelease }) gk.push_back (&gate.add<ParamKnob> (c, p));
        auto* gl = lane (gate, L ("NIVEL POR PASO", "LEVEL PER STEP"), [] (Patterns& pt, int i) -> float& { return pt.gate[(size_t) i]; }, 0, 1, false, P::tgSteps, 1, col::accent3, kMaxGateSteps);
        gate.layout = [gt, gk, gl] (juce::Rectangle<int> r)
        {
            auto t = r.removeFromTop (24);
            for (auto* x : gt) x->setBounds (t.removeFromLeft (120).reduced (2, 1));
            gridKnobs (gk, r.removeFromTop (70), 4);
            gl->setBounds (r.reduced (0, 2));
        };
        auto* sr = &seq.add<ParamCombo> (c, P::seqRate);
        std::vector<juce::Component*> sk;
        for (int p : { P::seqSteps, P::seqGlide }) sk.push_back (&seq.add<ParamKnob> (c, p));
        auto* sl = lane (seq, L ("VALOR (-1..+1)", "VALUE (-1..+1)"), [] (Patterns& pt, int i) -> float& { return pt.seq[(size_t) i]; }, -1, 1, false, P::seqSteps, 2, col::accent, kMaxSeqSteps);
        seq.layout = [sr, sk, sl] (juce::Rectangle<int> r)
        {
            auto t = r.removeFromTop (70);
            sr->setBounds (t.removeFromLeft (110).withHeight (24).reduced (2, 1));
            gridKnobs (sk, t, 2);
            sl->setBounds (r.reduced (0, 2));
        };
        auto* cm = &chord.add<ParamCombo> (c, P::chordMode);
        std::vector<juce::Component*> ck2 { &chord.add<ParamKnob> (c, P::strum), &chord.add<ParamKnob> (c, P::humanize) };
        chord.layout = [cm, ck2] (juce::Rectangle<int> r) { cm->setBounds (r.removeFromTop (24).withWidth (140).reduced (2, 1)); gridKnobs (ck2, r, 2); };
        for (auto* b : { &arp, &gate, &seq, &chord }) addAndMakeVisible (b);
        gate.accent = seq.accent = col::accent3;
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        arp.setBounds (r.removeFromLeft (r.getWidth() * 55 / 100).reduced (3));
        gate.setBounds (r.removeFromTop (r.getHeight() * 38 / 100).reduced (3));
        seq.setBounds (r.removeFromTop (r.getHeight() * 60 / 100).reduced (3));
        chord.setBounds (r.reduced (3));
    }
private:
    Box arp, gate, seq, chord;
};

// ================================================================ FX tab
static const int kFxOnParam[kNumFx] { P::distOn, P::choOn, P::flaOn, P::phaOn, P::dlyOn, P::revOn, P::cmpOn, P::eqOn, P::bitOn, P::widOn, P::shmOn, P::grnOn };

class FxOrderList : public Section, private juce::Timer
{
public:
    FxOrderList (Context& c) : Section (c, L ("Orden de efectos", "Effect order")) { startTimerHz (8); }
    void paint (juce::Graphics& g) override
    {
        Section::paint (g);
        const auto& ord = ctx.proc.getPatterns().fxOrder;
        for (int i = 0; i < kNumFx; ++i)
        {
            auto r = row (i).toFloat();
            const int fx = ord[(size_t) i];
            g.setColour (i == dragFrom ? col::accent.withAlpha (0.3f) : col::bgDeep);
            g.fillRoundedRectangle (r.reduced (1), 3);
            const bool on = ctx.proc.getParam (kFxOnParam[fx]) > 0.5f;
            drawLed (g, { r.getX() + 12, r.getCentreY() }, 4.5f, col::accent2, on ? 1.0f : 0.0f);
            g.setColour (on ? col::text : col::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (juce::String (i + 1) + ".  " + fxNames()[fx], r.withTrimmedLeft (24), juce::Justification::centredLeft);
            g.setColour (col::textDim);
            g.drawText ("=", r.withTrimmedRight (8), juce::Justification::centredRight);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        Section::mouseDown (e);
        dragFrom = rowAt (e.y);
        if (dragFrom >= 0 && e.x < 24)
        {
            const int fx = ctx.proc.getPatterns().fxOrder[(size_t) dragFrom];
            ctx.proc.setParamPlain (kFxOnParam[fx], ctx.proc.getParam (kFxOnParam[fx]) > 0.5f ? 0.0f : 1.0f);
            dragFrom = -1;
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const int to = rowAt (e.y);
        if (dragFrom < 0 || to < 0 || to == dragFrom) return;
        auto pats = ctx.proc.getPatterns();
        std::swap (pats.fxOrder[(size_t) dragFrom], pats.fxOrder[(size_t) to]);
        ctx.proc.setPatterns (pats);
        dragFrom = to;
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { dragFrom = -1; repaint(); }
private:
    void timerCallback() override { repaint(); }
    juce::Rectangle<int> row (int i) const { auto r = content(); const int h = juce::jmin (30, r.getHeight() / kNumFx); return { r.getX(), r.getY() + i * h, r.getWidth(), h }; }
    int rowAt (int y) const { for (int i = 0; i < kNumFx; ++i) if (y >= row (i).getY() && y < row (i).getBottom()) return i; return -1; }
    int dragFrom = -1;
};

class FxTab : public juce::Component
{
public:
    explicit FxTab (Context& c) : order (c)
    {
        addAndMakeVisible (order);
        const char* groups[] { "fx.dist", "fx.chorus", "fx.flanger", "fx.phaser", "fx.delay", "fx.reverb", "fx.comp", "fx.eq", "fx.crush", "fx.widener", "fx.shimmer", "fx.granular" };
        for (int i = 0; i < kNumFx; ++i)
        {
            auto* p = panels.add (new AutoPanel (c, fxNames()[i], groups[i], i == fxEq ? 5 : 3));
            if (i >= fxShimmer) p->accent = col::accent2;
            addAndMakeVisible (p);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        order.setBounds (r.removeFromLeft (210).reduced (3));
        const int w = r.getWidth() / 4, h = r.getHeight() / 3;
        for (int i = 0; i < kNumFx; ++i) panels[i]->setBounds (juce::Rectangle<int> (r.getX() + (i % 4) * w, r.getY() + (i / 4) * h, w, h).reduced (3));
    }
private:
    FxOrderList order;
    juce::OwnedArray<AutoPanel> panels;
};

// ================================================================ EditorPage
EditorPage::EditorPage (Context& c) : Page (c)
{
    const juce::String names[] { "OSC", L ("FILTROS", "FILTERS"), "MOD", "ARP / GATE / SEQ", "FX" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = subTabs.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { showSub (i); };
        addAndMakeVisible (b);
    }
    subs.add (new OscTab (c));
    subs.add (new FilterTab (c));
    subs.add (new ModTab (c));
    subs.add (new RhythmTab (c));
    subs.add (new FxTab (c));
    for (auto* s : subs) addChildComponent (s);
    showSub (0);
}

void EditorPage::showSub (int i)
{
    current = i;
    for (int k = 0; k < subs.size(); ++k) { subs[k]->setVisible (k == i); subTabs[k]->setToggleState (k == i, juce::dontSendNotification); }
}

void EditorPage::refresh() { for (auto* s : subs) s->repaint(); }

void EditorPage::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void EditorPage::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (30).reduced (8, 3);
    for (auto* b : subTabs) b->setBounds (bar.removeFromLeft (b->getButtonText().length() > 6 ? 150 : 96).reduced (2, 0));
    for (auto* s : subs) s->setBounds (r);
}
}
