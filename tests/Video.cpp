// InfinitumTool video <outdir> [bi]
// Scripted ~80 s product demo: renders the real editor offscreen at 30 fps on a virtual clock, drives it like a user
// (animated cursor, clicks, knob turns, typing), plays the real engine and writes frames/f%05d.jpg + audio.wav.
// Mux:  ffmpeg -framerate 30 -i frames/f%05d.jpg -i audio.wav -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 256k -shortest demo.mp4
#include "PluginEditor.h"
#include "ui/GeneratorPage.h"
#include "gen/Generator.h"
#include <BinaryData.h>
#include <map>
#include <algorithm>

namespace
{
constexpr int kFps = 30, kW = 1920, kH = 1080, kSub = 4;
constexpr double kSr = 48000.0;
constexpr int kSamplesPerFrame = (int) (kSr / kFps), kBlock = kSamplesPerFrame / kSub;
bool gBilingual = false;
constexpr float kUiScale = 1.35f;                       // 1280x800 design -> 1728x1080
const int kUiX = (kW - (int) (1280 * kUiScale)) / 2;

using namespace gx::ui;

class Director
{
public:
    explicit Director (const juce::File& dir) : out (dir)
    {
        out.getChildFile ("frames").createDirectory();
        for (auto& f : out.getChildFile ("frames").findChildFiles (juce::File::findFiles, false, "*.jpg")) f.deleteFile();
        virtualClock() = [this] { return vt; };
        forcedSeed() = 7001;
        gx::forcedLanguage() = "en";
        proc.prepareToPlay (kSr, kBlock);
        ed.reset (dynamic_cast<InfinitumEditor*> (proc.createEditor()));
        ed->setSize (InfinitumEditor::kDesignW, InfinitumEditor::kDesignH);
        ed->addToDesktop (0);
        ed->setVisible (true);
        cursor = { 900.0f, 520.0f };
        int size = 0;
        if (auto* data = BinaryData::getNamedResource ("factory_infinitumbank", size))
            bank = juce::JSON::parse (juce::String::fromUTF8 (data, size));
    }
    ~Director()
    {
        ed->removeFromDesktop();
        ed.reset();
        virtualClock() = nullptr;
    }

    // ------------------------------------------------------------ primitives
    void frame()
    {
        vt += 1.0 / kFps;
        juce::AudioBuffer<float> buf (2, kBlock);
        for (int s = 0; s < kSub; ++s)
        {
            juce::MidiBuffer m;
            buf.clear();
            proc.processBlock (buf, m);
            for (int i = 0; i < kBlock; ++i) { L.push_back (buf.getSample (0, i)); R.push_back (buf.getSample (1, i)); }
        }
        ed->videoFrame();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        writeFrame();
    }
    void wait (double sec) { for (int i = 0, n = (int) std::lround (sec * kFps); i < n; ++i) frame(); }

    void caption (const char* title, const char* sub = "", const char* esTitle = "", const char* esSub = "")
    {
        capTitle = juce::String::fromUTF8 (title); capSub = juce::String::fromUTF8 (sub);
        const auto et = juce::String::fromUTF8 (esTitle), es = juce::String::fromUTF8 (esSub);
        capEs = gBilingual ? (es.isEmpty() ? et : et + juce::String::fromUTF8 (" — ") + es) : juce::String();
        capStart = vt; capEnd = 0;
    }
    void clearCaption() { capEnd = vt; }

    juce::Point<float> centreOf (juce::Component* c) const { return ed->getLocalArea (c, c->getLocalBounds()).toFloat().getCentre(); }
    void moveTo (juce::Point<float> p, double sec = 0.55)
    {
        const auto from = cursor;
        const int n = juce::jmax (1, (int) std::lround (sec * kFps));
        for (int i = 1; i <= n; ++i)
        {
            const float t = (float) i / (float) n, e = t * t * (3.0f - 2.0f * t);
            cursor = from + (p - from) * e;
            frame();
        }
    }
    void clickFx() { clickAt = vt; clickPos = cursor; }
    void click (juce::Component* c, double settle = 0.35)
    {
        if (c == nullptr) { std::printf ("video: target not found\n"); return; }
        moveTo (centreOf (c));
        clickFx();
        if (auto* b = dynamic_cast<juce::Button*> (c)) b->triggerClick();
        else if (auto* g = dynamic_cast<GenerateButton*> (c)) { if (g->onClick) g->onClick(); }
        else if (auto* pb = dynamic_cast<PlayButton*> (c)) { juce::ignoreUnused (pb); proc.setPlaying (! proc.isPlayingInternal()); }
        wait (settle);
    }
    // turn a knob (found by parameter) with the cursor on it
    void turn (int param, float to, double sec = 1.4)
    {
        auto* k = find<ParamKnob> (ed.get(), [param] (ParamKnob* x) { return x->param() == param; });
        if (k == nullptr) { std::printf ("video: knob %d not found\n", param); return; }
        moveTo (centreOf (&k->slider), 0.45);
        clickFx();
        const float from = proc.getParam (param);
        const int n = (int) std::lround (sec * kFps);
        for (int i = 1; i <= n; ++i)
        {
            const float t = (float) i / (float) n, e = t * t * (3 - 2 * t);
            proc.setParamPlain (param, from + (to - from) * e);
            cursor = centreOf (&k->slider).translated (0, -18.0f * e);
            frame();
        }
    }
    void type (juce::TextEditor* te, const juce::String& text)
    {
        if (te == nullptr) return;
        click (te, 0.1);
        for (int i = 1; i <= text.length(); ++i) { te->setText (text.substring (0, i), true); wait (0.12); }
    }
    void selectRow (juce::ListBox* lb, int row)
    {
        if (lb == nullptr) return;
        const auto r = lb->getRowPosition (row, true).toFloat();
        moveTo (ed->getLocalArea (lb, r.toNearestInt()).toFloat().getCentre().translated (-150.0f, 0.0f));
        clickFx();
        lb->selectRow (row);
        wait (0.3);
    }
    void generate()
    {
        click (find<GenerateButton> (&ed->generatorPage()), 0.1);
        for (int i = 0; i < 15 * kFps && ed->generatorPage().isBusy(); ++i) frame();
        wait (0.4);
    }
    // load the first factory preset of a sub-mood (keeps playing: live change)
    void loadName (const juce::String& name)
    {
        if (auto* arr = bank.getProperty ("presets", {}).getArray())
            for (auto& v : *arr)
            {
                auto p = gx::Preset::fromVar (v);
                if (p.meta.name != name) continue;
                p.applyTo (proc);
                proc.presetGeneration.fetch_add (1);
                return;
            }
        std::printf ("video: preset %s not found\n", name.toRawUTF8());
    }
    // load the best-scoring factory preset of a sub-mood (nth = the next best ones); keeps playing: live change
    void load (const juce::String& sub, int nth = 0)
    {
        std::vector<std::pair<float, gx::Preset>> ranked;
        if (auto* arr = bank.getProperty ("presets", {}).getArray())
            for (auto& v : *arr)
            {
                auto p = gx::Preset::fromVar (v);
                if (p.meta.subcategory != sub) continue;
                auto val = gx::Generator::validate (p, false);
                if (! val.ok) continue;
                // the demo must sound clean: no noise beds, little wind
                float sc = gx::Generator::interestScore (p, val);
                if ((int) p.get (gx::P::noiseType) != 0) sc -= 10.0f * p.get (gx::P::noiseLevel) + 2.0f;
                sc -= 4.0f * p.get (gx::P::layer (gx::lyWind, gx::P::lLevel)) * p.get (gx::P::layer (gx::lyWind, gx::P::lPresence));
                ranked.push_back ({ sc, p });
            }
        if (ranked.empty()) return;
        std::sort (ranked.begin(), ranked.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        ranked[(size_t) juce::jmin (nth, (int) ranked.size() - 1)].second.applyTo (proc);
        proc.presetGeneration.fetch_add (1);
    }

    template <typename T>
    T* find (juce::Component* root, std::function<bool (T*)> pred = {}) const
    {
        if (root == nullptr || ! root->isShowing()) return nullptr;
        if (auto* t = dynamic_cast<T*> (root)) if (! pred || pred (t)) return t;
        for (auto* ch : root->getChildren()) if (auto* r = find<T> (ch, pred)) return r;
        return nullptr;
    }
    juce::Button* button (const juce::String& text, juce::Component* root = nullptr) const
    {
        return find<juce::Button> (root ? root : ed.get(), [&] (juce::Button* b) { return b->getButtonText() == text; });
    }

    InfinitumProcessor proc;
    std::unique_ptr<InfinitumEditor> ed;

    // ------------------------------------------------------------ title cards
    double cardFrom = -1, cardTo = -1; bool endCard = false;
    void card (bool isEnd, double fadeIn, double hold, double fadeOut)
    {
        endCard = isEnd; cardFrom = vt; cardTo = vt + fadeIn + hold;
        cardFadeIn = fadeIn; cardFadeOut = fadeOut;
        wait (fadeIn + hold);
        cardOutStart = vt;
        wait (fadeOut);
        cardFrom = cardTo = -1;
    }

    void finishAudio()
    {
        // gentle level matching (slow AGC towards -18 dBFS RMS), soft limiter, fade-out at the end
        const size_t hop = (size_t) (kSr * 0.01), win = (size_t) (kSr * 1.0);
        std::vector<float> gainDb (L.size() / hop + 2, 0.0f);
        float gDb = 0.0f;
        for (size_t h = 0; h < gainDb.size(); ++h)
        {
            const size_t c = h * hop, a0 = c > win / 2 ? c - win / 2 : 0, a1 = juce::jmin (L.size(), c + win / 2);
            double e = 0;
            for (size_t i = a0; i < a1; ++i) e += 0.5 * (L[i] * L[i] + R[i] * R[i]);
            const float rmsDb = a1 > a0 ? (float) juce::Decibels::gainToDecibels (std::sqrt (e / (double) (a1 - a0)), -120.0) : -120.0f;
            if (rmsDb > -55.0f) gDb += (juce::jlimit (-12.0f, 12.0f, -18.0f - rmsDb) - gDb) * 0.01f;
            gainDb[h] = gDb;
        }
        auto limit = [] (float x) { const float a = std::abs (x); return a < 0.7f ? x : std::copysign (0.7f + 0.24f * std::tanh ((a - 0.7f) / 0.24f), x); };
        const size_t fadeEnd = (size_t) (kSr * 2.5);
        juce::AudioBuffer<float> b (2, (int) L.size());
        for (size_t i = 0; i < L.size(); ++i)
        {
            const size_t h = i / hop;
            const float t = (float) (i % hop) / (float) hop;
            const float a = juce::Decibels::decibelsToGain (gainDb[h] + (gainDb[h + 1] - gainDb[h]) * t);
            float fd = 1.0f;
            if (i + fadeEnd > L.size()) fd *= (float) (L.size() - i) / (float) fadeEnd;
            b.setSample (0, (int) i, limit (L[i] * a * fd));
            b.setSample (1, (int) i, limit (R[i] * a * fd));
        }
        auto f = out.getChildFile ("audio.wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kSr).withNumChannels (2).withBitsPerSample (24));
        if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        std::printf ("audio: %.1f s\n", (double) L.size() / kSr);
    }

private:
    void writeFrame()
    {
        juce::Image img (juce::Image::RGB, kW, kH, true);
        {   // scope: native (Direct2D) contexts only flush when the Graphics is destroyed
            juce::Graphics g (img);
            g.fillAll (col::bgDeep);
            auto ui = ed->createComponentSnapshot (ed->getLocalBounds(), true, kUiScale);
            g.drawImageAt (ui, kUiX, 0);
            drawCaption (g);
            drawCursor (g);
            drawCard (g);
        }
        auto f = out.getChildFile ("frames").getChildFile ("f" + juce::String (frameNo++).paddedLeft ('0', 5) + ".jpg");
        juce::FileOutputStream os (f);
        juce::JPEGImageFormat jpg;
        jpg.setQuality (0.93f);
        jpg.writeImageToStream (img, os);
    }

    juce::Point<float> toScreen (juce::Point<float> p) const { return { kUiX + p.x * kUiScale, p.y * kUiScale }; }

    void drawCursor (juce::Graphics& g)
    {
        if (cardFrom >= 0) return;
        const auto p = toScreen (cursor);
        const double ca = vt - clickAt;
        if (ca >= 0 && ca < 0.45)
        {
            const float t = (float) (ca / 0.45);
            g.setColour (col::accent.withAlpha (0.9f * (1.0f - t)));
            g.drawEllipse (juce::Rectangle<float> (10 + 46 * t, 10 + 46 * t).withCentre (toScreen (clickPos)), 3.0f);
        }
        juce::Path a;
        a.startNewSubPath (0, 0); a.lineTo (0, 26); a.lineTo (7, 20); a.lineTo (12, 31); a.lineTo (17, 29);
        a.lineTo (12, 18); a.lineTo (21, 18); a.closeSubPath();
        a.applyTransform (juce::AffineTransform::scale (1.15f).translated (p));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (a, juce::AffineTransform::translation (2, 3));
        g.setColour (juce::Colours::white);
        g.fillPath (a);
        g.setColour (juce::Colours::black);
        g.strokePath (a, juce::PathStrokeType (1.6f));
    }

    static float textWidth (const juce::Font& f, const juce::String& t)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, t, 0, 0);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    void drawCaption (juce::Graphics& g)
    {
        if (capTitle.isEmpty()) return;
        float a = (float) juce::jlimit (0.0, 1.0, (vt - capStart) / 0.3);
        if (capEnd > capStart) a *= (float) juce::jlimit (0.0, 1.0, 1.0 - (vt - capEnd) / 0.3);
        if (a <= 0.0f) return;
        auto big = font (34.0f, true), small = font (22.0f), esFont = font (22.0f, true);
        const float w = juce::jmax (textWidth (big, capTitle), textWidth (small, capSub), textWidth (esFont, capEs)) + 70.0f;
        const float esH = capEs.isEmpty() ? 0.0f : 40.0f;
        const float h = (capSub.isEmpty() ? 64.0f : 100.0f) + esH;
        const float bottom = kH - 92.0f;  // above the on-screen keyboard
        juce::Rectangle<float> r ((kW - w) * 0.5f, bottom - h, w, h);
        g.setColour (juce::Colour (0xf00b0d1a).withMultipliedAlpha (a));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (col::accent.withAlpha (a));
        g.fillRoundedRectangle (r.withWidth (6.0f), 3.0f);
        g.setColour (col::text.withAlpha (a));
        g.setFont (big);
        g.drawText (capTitle, r.withTrimmedLeft (10).withHeight (64.0f).translated (0, capSub.isEmpty() ? 0.0f : 6.0f), juce::Justification::centred);
        if (capSub.isNotEmpty())
        {
            g.setColour (col::textDim.withAlpha (a));
            g.setFont (small);
            g.drawText (capSub, r.withTrimmedLeft (10).withTrimmedTop (58).withHeight (30.0f), juce::Justification::centred);
        }
        if (capEs.isNotEmpty())
        {
            const auto line = r.withTrimmedLeft (10).withTrimmedTop (h - esH - 4.0f).withHeight (esH);
            g.setColour (col::accent.withAlpha (0.35f * a));
            g.fillRect (line.getX() + 40.0f, line.getY() + 2.0f, line.getWidth() - 80.0f, 1.0f);
            g.setColour (col::accent2.withAlpha (a));
            g.setFont (esFont);
            g.drawText (capEs, line, juce::Justification::centred);
        }
    }

    void drawCard (juce::Graphics& g)
    {
        if (cardFrom < 0) return;
        float a = cardFadeIn <= 0 ? 1.0f : (float) juce::jlimit (0.0, 1.0, (vt - cardFrom) / cardFadeIn);
        if (vt > cardTo) a = (float) juce::jlimit (0.0, 1.0, 1.0 - (vt - cardOutStart) / juce::jmax (0.01, cardFadeOut));
        if (a <= 0.0f) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.setOpacity (a);
        juce::Image layer (juce::Image::ARGB, kW, kH, true);
        {
            juce::Graphics lg (layer);
            lg.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1544), kW * 0.5f, kH * 0.75f, juce::Colour (0xff03040c), kW * 0.5f, 0, true));
            lg.fillAll();
            const float t = (float) (vt - cardFrom);
            Logo::drawMark (lg, juce::Rectangle<float> (kW * 0.5f - 210.0f, (endCard ? 170.0f : 220.0f), 420.0f, 220.0f), 0.7f + 0.3f * std::sin (t * 2.0f), t * 0.25f);
            lg.setColour (juce::Colours::white);
            lg.setFont (techFont (110.0f));
            lg.drawText ("INFINITUM", 0, endCard ? 420 : 470, kW, 120, juce::Justification::centred);
            lg.setColour (col::textDim);
            lg.setFont (font (32.0f, true));
            lg.drawText ("by DSK Music", 0, endCard ? 540 : 590, kW, 44, juce::Justification::centred);
            lg.setFont (font (30.0f));
            if (! endCard)
            {
                lg.setColour (col::text);
                lg.drawText ("Endless soundscape generator  -  a landscape of sound that never repeats", 0, 670, kW, 40, juce::Justification::centred);
                if (gBilingual)
                {
                    lg.setColour (col::accent2.withAlpha (0.9f));
                    lg.drawText (juce::String::fromUTF8 ("Generador de atmósferas infinitas: un paisaje sonoro que nunca se repite"), 0, 716, kW, 40, juce::Justification::centred);
                }
            }
            else
            {
                lg.setColour (col::text);
                lg.drawText (gBilingual ? juce::String::fromUTF8 ("Free & open source  ·  Gratuito y de código abierto  (GPLv3)")
                                        : juce::String ("Free & open source (GPLv3)"), 0, 620, kW, 40, juce::Justification::centred);
                lg.drawText (juce::String::fromUTF8 ("VST3 · AU · Standalone   —   Windows · macOS · Linux"), 0, 666, kW, 40, juce::Justification::centred);
                lg.setColour (col::accent2);
                lg.setFont (font (42.0f, true));
                lg.drawText ("dskmusic.com", 0, 750, kW, 50, juce::Justification::centred);
            }
        }
        g.drawImageAt (layer, 0, 0);
    }

    juce::File out;
    juce::var bank;
    double vt = 1000.0;
    int frameNo = 0;
    std::vector<float> L, R;
    juce::Point<float> cursor, clickPos;
    double clickAt = -10;
    juce::String capTitle, capSub, capEs;
    double capStart = 0, capEnd = 0;
    double cardFadeIn = 0, cardFadeOut = 0, cardOutStart = 0;
};
} // namespace

int runVideo (int argc, char** argv)
{
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "video");
    dir.createDirectory();
    for (int a = 3; a < argc; ++a) gBilingual |= juce::String (argv[a]) == "bi";
    Director d (dir);
    using namespace gx;

    // ---- intro: a Cinematic landscape fades in behind the title card
    d.load ("Epic");
    d.proc.setParamPlain (P::fadeIn, 2.0f);
    d.proc.setPlaying (true);
    d.ed->showPage (0);
    d.card (false, 0.0, 3.4, 0.9);

    // ---- landscape + macros
    d.caption ("A landscape of sound that never repeats", "Seven layers drift in and out on their own",
               "Un paisaje sonoro que nunca se repite", "siete capas que entran y salen solas");
    d.wait (3.0);
    d.caption ("Light: from deep night to midday", "", "Luz: de noche cerrada a mediodía");
    d.turn (P::light, 0.85f, 2.2);
    d.turn (P::light, 0.3f, 2.0);
    d.caption ("Evolution, Density, Space", "Four big macros shape the whole journey", "Evolución, Densidad, Espacio", "cuatro macros grandes dan forma al viaje");
    d.turn (P::evolution, 0.75f, 1.4);
    d.turn (P::space, 0.95f, 1.4);
    d.wait (0.8);

    // ---- landscapes (every preset change dips smoothly: no clicks)
    d.caption ("Six living landscapes", "Valley · Starfield · Underwater · Neon · Ocean · Aurora", "Seis paisajes vivos", "Valle · Campo estelar · Bajo el agua · Neón · Océano · Aurora");
    d.load ("Deep Blue");
    d.click (d.button ("Underwater"), 4.0);
    d.load ("Celestial");
    d.click (d.button ("Aurora"), 3.4);
    d.load ("Deep Space");
    d.click (d.button ("Starfield"), 3.0);

    // ---- neo-noir
    d.caption ("Neon nights", "Analog 80s pads, ensemble chorus, glassy bell phrases", "Noches de neón", "pads analógicos de los 80, chorus de ensemble y frases de campanas de cristal");
    d.load ("Neo-Noir");
    d.click (d.button ("Neon Rain"), 6.0);

    // ---- derelict
    d.caption ("Or a derelict ship in deep space...", "", "O una nave abandonada en el espacio...");
    d.load ("Derelict");
    d.click (d.button ("Starfield"), 4.0);
    d.caption ("Freeze the moment", "", "Congela el momento");
    d.click (d.button ("FREEZE"), 2.4);
    d.click (d.button ("FREEZE"), 0.6);

    // ---- sound page
    d.caption ("Drone · Pad · Texture · Shimmer · Wind · Bells · Bass", "Noise generator: pink, brown, white, ocean waves, vinyl",
               "Drone · Pad · Textura · Shimmer · Viento · Campanas · Bajo", "generador de ruido: rosa, marrón, blanco, olas, vinilo");
    d.load ("Glow");
    d.click (d.button ("SOUND"), 4.0);

    // ---- generator
    d.caption ("12 moods, 256 presets", "Epic · Forest · Desert · Sleep · Ethereal · Oceanic · Nostalgia...", "12 estados de ánimo, 256 presets", "Épico · Bosque · Desierto · Dormir · Etéreo · Océano · Nostalgia...");
    d.click (d.button ("GENERATOR"), 0.4);
    d.click (d.button ("Epic", &d.ed->generatorPage()), 0.3);
    d.generate();
    d.wait (3.0);
    d.click (d.button ("Forest", &d.ed->generatorPage()), 0.3);
    d.generate();
    d.wait (3.0);

    // ---- export
    d.caption ("Export audio up to 3 hours, or an MP4 of the landscape", "Sleep mode · session timer · 432 Hz · tempo sync",
               "Exporta audio de hasta 3 horas o un MP4 del paisaje", "modo Dormir · temporizador · 432 Hz · sincronía con el tempo");
    d.click (d.button ("EXPORT"), 3.4);

    // ---- the finale: Rising Citadel over the neon city
    d.caption ("Follows your DAW. Bounces match playback.", "VST3 · AU · Standalone", "Sigue a tu DAW: lo que exportas suena igual que lo que oíste", "");
    d.loadName ("Rising Citadel");
    d.click (d.button ("LANDSCAPE"), 0.3);
    d.click (d.button ("Neon Rain"), 6.0);
    d.clearCaption();
    d.wait (1.5);
    d.proc.setParamPlain (P::fadeOut, 3.0f);
    d.proc.setPlaying (false);
    d.card (true, 1.0, 3.6, 0.0);

    d.finishAudio();
    std::printf ("video frames written to %s\n", dir.getChildFile ("frames").getFullPathName().toRawUTF8());
    return 0;
}
