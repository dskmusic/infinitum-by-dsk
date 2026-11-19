#include "Visuals.h"

namespace gx::ui
{
// ---------------------------------------------------------------- FrameSource
void FrameSource::update (Frame& f, double dt)
{
    float tmp[4096];
    int n;
    while ((n = proc.readScope (tmp, 4096)) > 0)
        for (int i = 0; i < n; ++i) { ring[(size_t) ringPos] = tmp[i]; ringPos = (ringPos + 1) & 8191; }
    for (int i = 0; i < Frame::kScope; ++i) f.scope[(size_t) i] = ring[(size_t) ((ringPos - Frame::kScope + i) & 8191)];

    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    std::copy (f.scope.begin(), f.scope.end(), fftBuf.begin());
    window.multiplyWithWindowingTable (fftBuf.data(), 2048);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data());
    const double sr = 48000.0;
    for (int b = 0; b < Frame::kBands; ++b)
    {
        const double f0 = 30.0 * std::pow (600.0, (double) b / Frame::kBands), f1 = 30.0 * std::pow (600.0, (double) (b + 1) / Frame::kBands);
        const int k0 = juce::jlimit (1, 1023, (int) (f0 / sr * 2048)), k1 = juce::jlimit (k0 + 1, 1024, (int) (f1 / sr * 2048) + 1);
        float m = 0;
        for (int k = k0; k < k1; ++k) m = juce::jmax (m, fftBuf[(size_t) k]);
        const float db = juce::Decibels::gainToDecibels (m / 512.0f, -90.0f);
        f.spectrum[(size_t) b] = juce::jlimit (0.0f, 1.0f, (db + 78.0f) / 72.0f);
    }
    f.peakL = proc.peakL.load(); f.peakR = proc.peakR.load(); f.rms = proc.rms.load();
    f.energy += (juce::jmin (1.0f, f.rms * 6.0f) - f.energy) * (float) juce::jmin (1.0, dt * 4.0);
    f.time += dt;
    auto& t = proc.telemetry();
    for (int l = 0; l < kNumLayers; ++l)
    {
        f.presence[(size_t) l] = t.presence[(size_t) l].load();
        const float e = juce::jmin (1.0f, t.energy[(size_t) l].load() * 8.0f);
        f.layerEnergy[(size_t) l] += (e - f.layerEnergy[(size_t) l]) * (float) juce::jmin (1.0, dt * 3.0);
    }
    for (int i = 0; i < kChordVoices; ++i) f.chord[(size_t) i] = t.chordNotes[(size_t) i].load();
    f.rootPc = t.rootPc.load(); f.degree = t.chordDegree.load(); f.bellCount = t.bellCount.load();
    f.lastBellNote = t.lastBellNote.load(); f.lastBellPan = t.lastBellPan.load();
    f.runGain = t.runGain.load(); f.evoTime = t.evoTime.load(); f.musTime = t.musTime.load(); f.elapsed = t.sessionElapsed.load();
    f.running = t.running.load();
    f.playButton = proc.isPlayingInternal();
    f.hostPlaying = proc.isHostPlaying();
    f.light = proc.getParam (P::light);
    f.space = proc.getParam (P::space);
    f.harmony = proc.getParam (P::harmony);
    f.scale = (int) proc.getParam (P::scale);
    f.timerMin = (int) proc.getParam (P::timer);
    f.seed = (int) proc.getParam (P::seed);
    f.frozen = proc.getParam (P::freeze) > 0.5f;
}

juce::String chordName (int rootPc, int scale, int degree, float harmony)
{
    const auto& s = scaleSteps (scale);
    const int m = (int) s.size();
    auto semis = [&] (int d) { return s[(size_t) (d % m)] + 12 * (d / m); };
    const int r = semis (degree);
    const juce::String root = keyNames()[(rootPc + r) % 12];
    if (harmony < 0.12f) return keyNames()[rootPc] + "5";
    if (harmony < 0.25f) return root + "5";
    const int third = semis (degree + 2) - r, fifth = semis (degree + 4) - r, seventh = semis (degree + 6) - r;
    juce::String q;
    if (third == 3 && fifth == 6) q = "dim";
    else if (third == 4 && fifth == 8) q = "aug";
    else if (third == 3) q = "m";
    else if (third != 4) q = "sus";
    if (harmony > 0.45f && m >= 7)
    {
        const bool nine = harmony > 0.8f;
        if (q.isEmpty()) q = seventh == 11 ? (nine ? "maj9" : "maj7") : (nine ? "9" : "7");
        else if (q == "m") q = seventh == 11 ? "m(maj7)" : (nine ? "m9" : "m7");
        else if (q == "dim") q = seventh == 10 ? "m7b5" : "dim7";
        else q += nine ? "9" : "7";
    }
    return root + q;
}

// ---------------------------------------------------------------- Landscape
Landscape::Landscape()
{
    setInterceptsMouseClicks (true, false);
    for (size_t i = 0; i < stars.size(); ++i)
    {
        stars[i] = { rng.nextFloat(), std::pow (rng.nextFloat(), 1.4f) * 0.62f };
        twinkle[i] = 0.4f + rng.nextFloat() * 2.5f;
    }
}

void Landscape::tick (const Frame& fr)
{
    const double dt = juce::jlimit (0.0, 0.2, fr.time - lastT);
    lastT = fr.time;
    f = fr;
    for (int l = 0; l < kNumLayers; ++l)
    {
        pres[(size_t) l] += (f.presence[(size_t) l] - pres[(size_t) l]) * (float) juce::jmin (1.0, dt * 1.5);
        glow[(size_t) l] += (f.layerEnergy[(size_t) l] - glow[(size_t) l]) * (float) juce::jmin (1.0, dt * 4.0);
    }
    const auto b = getLocalBounds().toFloat();
    if (lastBells >= 0 && f.bellCount != lastBells && getWidth() > 0)
    {
        const int count = juce::jmin (4, f.bellCount - lastBells);
        for (int k = 0; k < count; ++k)
        {
            const float x = b.getWidth() * (0.5f + f.lastBellPan * 0.45f) + rng.nextFloat() * 24.0f - 12.0f;
            const float y = b.getHeight() * (0.55f - juce::jlimit (0.0f, 1.0f, (float) (f.lastBellNote - 60) / 40.0f) * 0.4f);
            sparks.push_back ({ x, y, -8.0f - rng.nextFloat() * 10.0f, 1.0f, 2.0f + rng.nextFloat() * 2.5f, rng.nextBool() ? col::accent2 : juce::Colours::white });
        }
    }
    lastBells = f.bellCount;
    for (auto& s : sparks) { s.y += s.vy * (float) dt; s.life -= (float) dt / 4.0f; }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0; }), sparks.end());
    if (sparks.size() > 80) sparks.erase (sparks.begin(), sparks.begin() + (long) (sparks.size() - 80));
    repaint();
}

void Landscape::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth(), h = b.getHeight();
    const float lt = f.light, t = (float) f.time;
    const float horizon = h * 0.70f;
    juce::Path clip;
    clip.addRoundedRectangle (b, 10.0f);
    g.saveState();
    g.reduceClipRegion (clip);

    // ---- sky: night -> dusk -> day
    auto mix3 = [lt] (juce::Colour night, juce::Colour dusk, juce::Colour day)
    { return lt < 0.5f ? night.interpolatedWith (dusk, lt * 2.0f) : dusk.interpolatedWith (day, (lt - 0.5f) * 2.0f); };
    const auto skyTop = mix3 (juce::Colour (0xff03040c), juce::Colour (0xff1b1544), juce::Colour (0xff2c64ad));
    const auto skyLow = mix3 (juce::Colour (0xff151b42), juce::Colour (0xffd9765f), juce::Colour (0xfff0d7a8));
    g.setGradientFill (juce::ColourGradient (skyTop, 0, 0, skyLow, 0, horizon, false));
    g.fillRect (b);

    // ---- stars
    const float starA = juce::jlimit (0.0f, 1.0f, (0.62f - lt) * 2.2f);
    if (starA > 0.01f)
        for (size_t i = 0; i < stars.size(); ++i)
        {
            const float tw = 0.55f + 0.45f * std::sin (t * twinkle[i] + (float) i);
            g.setColour (juce::Colours::white.withAlpha (starA * tw * (i % 7 == 0 ? 0.95f : 0.55f)));
            const float s = i % 11 == 0 ? 2.2f : 1.3f;
            g.fillEllipse (stars[i].x * w, stars[i].y * horizon, s, s);
        }

    // ---- sun / moon on the evolution clock
    {
        const float ph = wrap01 (f.evoTime / 420.0f + (float) (f.seed % 97) * 0.0137f);
        const float x = w * (0.08f + 0.84f * ph), y = horizon + 10.0f - std::sin (ph * kPi) * h * 0.5f;
        const bool sun = lt > 0.5f;
        const auto c = sun ? juce::Colour (0xffffd27a) : juce::Colour (0xffdfe6ff);
        const float rad = 15.0f + 8.0f * lt;
        const float gl = 90.0f + 110.0f * f.energy;
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), x, y, c.withAlpha (0.0f), x + gl, y, true));
        g.fillEllipse (x - gl, y - gl, gl * 2, gl * 2);
        g.setColour (c);
        g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
        if (! sun) { g.setColour (skyTop.withAlpha (0.55f)); g.fillEllipse (x - rad * 0.45f, y - rad * 1.05f, rad * 1.7f, rad * 1.7f); }
    }

    // ---- aurora ribbons (Shimmer)
    {
        const float a = juce::jlimit (0.0f, 1.0f, pres[lyShimmer] * (0.35f + glow[lyShimmer] * 1.5f));
        if (a > 0.01f)
            for (int k = 0; k < 3; ++k)
            {
                juce::Path top, rib;
                const float base = h * (0.14f + 0.09f * (float) k), thick = h * (0.10f + 0.04f * (float) k);
                for (int i = 0; i <= 64; ++i)
                {
                    const float x = w * (float) i / 64.0f;
                    const float y = base + std::sin (x * 0.006f + t * 0.13f + (float) k * 1.7f) * h * 0.05f + std::sin (x * 0.017f - t * 0.09f + (float) k) * h * 0.025f;
                    if (i == 0) rib.startNewSubPath (x, y); else rib.lineTo (x, y);
                }
                for (int i = 64; i >= 0; --i)
                {
                    const float x = w * (float) i / 64.0f;
                    const float y = base + thick + std::sin (x * 0.006f + t * 0.13f + (float) k * 1.7f + 0.6f) * h * 0.05f;
                    rib.lineTo (x, y);
                }
                rib.closeSubPath();
                const auto c = (k == 1 ? col::accent3 : col::accent);
                juce::ColourGradient gr (c.withAlpha (0.0f), 0, base - h * 0.04f, c.withAlpha (0.0f), 0, base + thick + h * 0.05f, false);
                gr.addColour (0.45, c.withAlpha (a * (0.30f - 0.06f * (float) k)));
                g.setGradientFill (gr);
                g.fillPath (rib);
            }
    }

    // ---- mountain ranges: far = Drone, mid = Pad, near = Texture, then the ground
    auto ridge = [&] (int k, float baseY, float amp, float scroll, float f1, float f2, float f3)
    {
        juce::Path p;
        p.startNewSubPath (0, h);
        const float s0 = (float) (f.seed % 1000) * 0.37f + (float) k * 2.1f;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = w * (float) i / 160.0f, xs = x + scroll;
            const float n = 0.55f * std::sin (xs * f1 + s0) + 0.3f * std::sin (xs * f2 + s0 * 1.7f) + 0.15f * std::sin (xs * f3 + s0 * 2.3f);
            p.lineTo (x, baseY - amp * (0.5f + 0.5f * n));
        }
        p.lineTo (w, h);
        p.closeSubPath();
        return p;
    };
    const float mt = f.musTime;
    const auto haze = skyLow.withAlpha (1.0f);
    struct R { int layer; float base, amp, speed, f1, f2, f3, shade; };
    const R ranges[3] { { lyDrone, horizon - h * 0.02f, h * 0.30f, 0.6f, 0.0042f, 0.011f, 0.027f, 0.35f },
                        { lyPad, horizon + h * 0.07f, h * 0.22f, 1.5f, 0.0061f, 0.016f, 0.041f, 0.55f },
                        { lyTexture, horizon + h * 0.16f, h * 0.14f, 3.2f, 0.0089f, 0.023f, 0.058f, 0.75f } };
    for (auto& r : ranges)
    {
        const float pr = pres[(size_t) r.layer];
        const float amp = r.amp * (0.35f + 0.65f * pr) * (1.0f + 0.12f * glow[(size_t) r.layer]);
        auto p = ridge (r.layer, r.base, amp, mt * r.speed, r.f1, r.f2, r.f3);
        const auto lc = col::layer[r.layer];
        const auto topC = lc.interpolatedWith (haze, 0.55f - 0.35f * r.shade).darker (0.6f + r.shade * 1.4f).withMultipliedSaturation (0.6f + 0.4f * pr);
        const auto botC = juce::Colour (0xff05060c).interpolatedWith (topC, 0.25f);
        g.setGradientFill (juce::ColourGradient (topC.brighter (0.25f * glow[(size_t) r.layer]), 0, r.base - r.amp, botC, 0, h, false));
        g.fillPath (p);
        g.setColour (lc.withAlpha (0.18f + 0.5f * pr * glow[(size_t) r.layer]));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        // mist (Wind) between the ranges
        const float mist = pres[lyWind] * (0.25f + 0.6f * glow[lyWind]);
        if (mist > 0.01f)
            for (int i = 0; i < 5; ++i)
            {
                const float mx = std::fmod ((float) i * w * 0.31f + mt * (12.0f + 6.0f * (float) i) * (0.6f + r.speed * 0.3f), w * 1.6f) - w * 0.3f;
                const float my = r.base - r.amp * 0.15f + (float) i * 3.0f;
                const float mw = w * (0.35f + 0.1f * (float) (i % 3)), mh = h * 0.06f;
                g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f * mist), mx, my, juce::Colours::white.withAlpha (0.0f), mx + mw * 0.5f, my, true));
                g.fillEllipse (mx - mw * 0.5f, my - mh * 0.5f, mw, mh);
            }
    }
    // grain dust (Texture)
    {
        const float a = pres[lyTexture] * (0.3f + glow[lyTexture] * 1.6f);
        if (a > 0.01f)
            for (int i = 0; i < 70; ++i)
            {
                const float x = std::fmod ((float) (i * 7919 % 1000) / 1000.0f * w + mt * (4.0f + (float) (i % 5)), w);
                const float y = horizon + h * 0.05f - (float) (i * 104729 % 1000) / 1000.0f * h * 0.32f + std::sin (t * 0.7f + (float) i) * 4.0f;
                const float tw = 0.5f + 0.5f * std::sin (t * (1.5f + (float) (i % 4)) + (float) i * 1.3f);
                g.setColour (col::layer[lyTexture].withAlpha (juce::jlimit (0.0f, 0.9f, a * tw * 0.7f)));
                g.fillEllipse (x, y, 1.8f, 1.8f);
            }
    }
    // ground
    {
        auto p = ridge (9, h * 0.97f, h * 0.10f, mt * 6.0f, 0.012f, 0.031f, 0.07f);
        g.setColour (juce::Colour (0xff04050a));
        g.fillPath (p);
    }
    // bell sparks
    for (auto& s : sparks)
    {
        const float a = juce::jlimit (0.0f, 1.0f, s.life * 1.4f) * pres[lyBells] + 0.2f * s.life;
        g.setGradientFill (juce::ColourGradient (s.c.withAlpha (0.45f * a), s.x, s.y, s.c.withAlpha (0.0f), s.x + s.size * 7, s.y, true));
        g.fillEllipse (s.x - s.size * 7, s.y - s.size * 7, s.size * 14, s.size * 14);
        g.setColour (s.c.withAlpha (a));
        g.fillEllipse (s.x - s.size * 0.5f, s.y - s.size * 0.5f, s.size, s.size);
        g.drawLine (s.x - s.size * 2.5f, s.y, s.x + s.size * 2.5f, s.y, 0.8f);
        g.drawLine (s.x, s.y - s.size * 2.5f, s.x, s.y + s.size * 2.5f, 0.8f);
    }
    // frozen tint
    if (f.frozen)
    {
        g.setColour (juce::Colour (0xff9fd8ff).withAlpha (0.10f));
        g.fillRect (b);
    }
    // stopped: dim
    const float dim = 0.45f * (1.0f - f.runGain);
    if (dim > 0.01f) { g.setColour (juce::Colours::black.withAlpha (dim)); g.fillRect (b); }
    g.restoreState();

    // ---- overlay texts
    if (showOverlay)
    {
        auto r = b.reduced (16, 12);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (techFont (24.0f));
        g.drawText (chordName (f.rootPc, f.scale, f.degree, f.harmony), r.removeFromTop (30), juce::Justification::topLeft);
        g.setFont (font (12.5f, true));
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawText (keyNames()[(int) f.rootPc % 12] + " " + scaleNames()[f.scale] + juce::String::fromUTF8 ("   ·   ") + L ("semilla ", "seed ") + juce::String (f.seed),
                    r.removeFromTop (18), juce::Justification::topLeft);
        auto mmss = [] (float s) { const int v = (int) s; return juce::String (v / 60).paddedLeft ('0', 2) + ":" + juce::String (v % 60).paddedLeft ('0', 2); };
        auto top = b.reduced (16, 12);
        g.setFont (techFont (18.0f));
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        juce::String clock = mmss (f.hostPlaying ? f.musTime : f.elapsed);
        if (f.timerMin > 0 && ! f.hostPlaying) clock = L ("quedan ", "") + mmss (juce::jmax (0.0f, (float) f.timerMin * 60.0f - f.elapsed)) + L ("", " left");
        g.drawText (clock, top.removeFromTop (26), juce::Justification::topRight);
        g.setFont (font (11.5f, true));
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawText (f.hostPlaying ? L ("SIGUE AL DAW", "FOLLOWING THE DAW") : (f.running ? L ("SONANDO", "PLAYING") : L ("PARADO", "STOPPED")), top.removeFromTop (16), juce::Justification::topRight);
        if (f.frozen)
        {
            g.setFont (techFont (16.0f));
            g.setColour (juce::Colour (0xffbfe6ff));
            g.drawText (L ("CONGELADO", "FROZEN"), b.reduced (12).removeFromTop (26), juce::Justification::centredTop);
        }
        if (! f.running && f.runGain <= 0.01f)
        {
            g.setFont (techFont (22.0f));
            g.setColour (juce::Colours::white.withAlpha (0.8f + 0.2f * std::sin (t * 2.0f)));
            g.drawText (L ("▶  PULSA PLAY", "▶  PRESS PLAY"), b, juce::Justification::centred);
        }
        // layer presence legend
        auto leg = b.reduced (16, 12).removeFromBottom (16);
        const float lw = juce::jmin (120.0f, leg.getWidth() / kNumLayers);
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto cell = leg.removeFromLeft (lw).reduced (3, 0);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (col::layer[l].withAlpha (0.85f));
            g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * pres[(size_t) l]), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (font (10.0f, true));
            g.drawText (layerName (l), cell, juce::Justification::centred);
        }
    }
}
}
