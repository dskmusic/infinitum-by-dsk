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
    f.scene = (int) proc.getParam (P::scene);
    f.rain = proc.getParam (P::rain);
    f.evolution = proc.getParam (P::evolution);
    f.density = proc.getParam (P::density);
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
namespace
{
inline float hashF (uint32_t a, uint32_t b)
{
    uint32_t x = a * 0x9E3779B1u ^ (b + 0x85ebca6bu);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (float) (x >> 8) * (1.0f / 16777216.0f);
}
juce::Colour mix3 (float lt, juce::Colour night, juce::Colour dusk, juce::Colour day)
{
    return lt < 0.5f ? night.interpolatedWith (dusk, lt * 2.0f) : dusk.interpolatedWith (day, (lt - 0.5f) * 2.0f);
}
void softBlob (juce::Graphics& g, float x, float y, float rx, float ry, juce::Colour c)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::scale (1.0f, ry / juce::jmax (1.0f, rx), x, y));
    g.setGradientFill (juce::ColourGradient (c, x, y, c.withAlpha (0.0f), x + rx, y, true));
    g.fillEllipse (x - rx, y - rx, rx * 2, rx * 2);
}
} // namespace

Landscape::Landscape()
{
    setInterceptsMouseClicks (true, false);
    for (size_t i = 0; i < stars.size(); ++i)
    {
        stars[i] = { rng.nextFloat(), rng.nextFloat() };
        twinkle[i] = 0.4f + rng.nextFloat() * 2.5f;
        starSize[i] = rng.nextFloat() < 0.08f ? 2.3f : (rng.nextFloat() < 0.3f ? 1.6f : 1.1f);
    }
}

void Landscape::rebuildCache()
{
    const int w = getWidth(), h = getHeight();
    cacheKey = juce::String (f.seed) + "/" + juce::String (w) + "x" + juce::String (h);
    juce::Random r ((juce::int64) f.seed * 7919 + 17);
    hasLake = r.nextFloat() < 0.5f;
    hasForest = r.nextFloat() < 0.55f;
    hasBirds = r.nextFloat() < 0.5f;
    hasRings = r.nextFloat() < 0.55f;
    numClouds = 2 + r.nextInt (5);
    planetR = (float) h * (0.35f + 0.35f * r.nextFloat());
    planetPos = { (float) w * (r.nextBool() ? 0.12f + 0.15f * r.nextFloat() : 0.72f + 0.18f * r.nextFloat()), (float) h * (0.75f + 0.35f * r.nextFloat()) };
    const juce::Colour pals[][2] { { juce::Colour (0xff3a6ea5), juce::Colour (0xff0b1a33) }, { juce::Colour (0xffc0704a), juce::Colour (0xff2a0f0a) },
                                   { juce::Colour (0xff7a5ac8), juce::Colour (0xff160b2e) }, { juce::Colour (0xff4fb3a0), juce::Colour (0xff06211d) } };
    const int pi = r.nextInt (4);
    planetCol = pals[pi][0]; planetCol2 = pals[pi][1];

    // ---- Neon Rain skyline: three layers of buildings with lit windows, cached as images
    signs.clear();
    arcology = { (float) w * (0.2f + 0.6f * r.nextFloat()), (float) h * 0.62f };
    if (w <= 0 || h <= 0) return;
    const float base = (float) h * 0.86f;
    for (int layer = 0; layer < 3; ++layer)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);
        const juce::Colour body[] { juce::Colour (0xff1a2240), juce::Colour (0xff0e1226), juce::Colour (0xff04050b) };
        const float maxH[] { 0.55f, 0.45f, 0.32f }, minW[] { 18, 26, 40 }, maxW[] { 46, 70, 110 };
        float x = -10.0f;
        while (x < (float) w)
        {
            const float bw = minW[layer] + r.nextFloat() * (maxW[layer] - minW[layer]);
            const float bh = (float) h * (0.08f + maxH[layer] * std::pow (r.nextFloat(), 0.8f));
            const juce::Rectangle<float> rb (x, base - bh + (float) layer * h * 0.04f, bw, bh + (float) h);
            g.setColour (body[layer]);
            g.fillRect (rb);
            if (r.nextFloat() < 0.3f) g.fillRect (rb.getCentreX() - 1.0f, rb.getY() - bh * 0.15f, 2.0f, bh * 0.15f); // antenna
            // windows
            const float wsz = layer == 0 ? 2.0f : layer == 1 ? 2.5f : 3.0f;
            for (float wy = rb.getY() + 6; wy < base + (float) layer * h * 0.04f - 4; wy += wsz * 3.0f)
                for (float wx = rb.getX() + 4; wx < rb.getRight() - 4; wx += wsz * 2.6f)
                    if (r.nextFloat() < (layer == 2 ? 0.18f : 0.3f))
                    {
                        const auto wc = r.nextFloat() < 0.7f ? juce::Colour (0xffffd59a) : (r.nextBool() ? juce::Colour (0xff7fe8ff) : juce::Colour (0xffff7ac8));
                        g.setColour (wc.withAlpha (0.35f + 0.55f * r.nextFloat()));
                        g.fillRect (wx, wy, wsz, wsz * 1.3f);
                    }
            // neon signs on the two nearer layers
            if (layer > 0 && r.nextFloat() < 0.45f)
            {
                const juce::Colour nc[] { juce::Colour (0xffff3fa4), juce::Colour (0xff3ff0ff), juce::Colour (0xffffb23f), juce::Colour (0xffa16bff), juce::Colour (0xffff4d4d) };
                const bool vertical = r.nextBool();
                const float sw = vertical ? 6.0f + 4.0f * r.nextFloat() : bw * (0.4f + 0.4f * r.nextFloat());
                const float sh = vertical ? bh * (0.2f + 0.25f * r.nextFloat()) : 7.0f + 5.0f * r.nextFloat();
                signs.push_back ({ juce::Rectangle<float> (rb.getX() + (bw - sw) * r.nextFloat(), rb.getY() + 10 + r.nextFloat() * bh * 0.4f, sw, sh),
                                   nc[r.nextInt (5)], r.nextFloat() * 6.28f, layer });
            }
            x += bw + (layer == 2 ? 2.0f : 0.0f) + r.nextFloat() * 6.0f;
        }
        skyline[(size_t) layer] = img;
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
            Spark s { x, y, 0.0f, -8.0f - rng.nextFloat() * 10.0f, 1.0f, 2.0f + rng.nextFloat() * 2.5f, rng.nextBool() ? col::accent2 : juce::Colours::white, 0 };
            if (f.scene == scStarfield) { s.kind = 1; s.vx = (rng.nextBool() ? 1.0f : -1.0f) * (60.0f + 60.0f * rng.nextFloat()); s.vy = 25.0f + 20.0f * rng.nextFloat(); s.y *= 0.6f; }
            if (f.scene == scNeon) { s.kind = 2; s.vx = (rng.nextBool() ? 1.0f : -1.0f) * (35.0f + 50.0f * rng.nextFloat()); s.vy = 0; s.x = s.vx > 0 ? -10.0f : b.getWidth() + 10.0f;
                                     s.y = b.getHeight() * (0.2f + 0.35f * rng.nextFloat()); s.c = rng.nextBool() ? juce::Colour (0xffff5a5a) : juce::Colour (0xffbfe9ff); }
            sparks.push_back (s);
        }
    }
    lastBells = f.bellCount;
    for (auto& s : sparks) { s.x += s.vx * (float) dt; s.y += s.vy * (float) dt; s.life -= (float) dt / (s.kind == 2 ? 14.0f : 4.0f); }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [&] (const Spark& s) { return s.life <= 0 || s.x < -40 || s.x > b.getWidth() + 40; }), sparks.end());
    if (sparks.size() > 80) sparks.erase (sparks.begin(), sparks.begin() + (long) (sparks.size() - 80));
    repaint();
}

void Landscape::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto key = juce::String (f.seed) + "/" + juce::String (getWidth()) + "x" + juce::String (getHeight());
    if (key != cacheKey) rebuildCache();
    juce::Path clip;
    clip.addRoundedRectangle (b, 10.0f);
    g.saveState();
    g.reduceClipRegion (clip);
    switch (f.scene)
    {
        case scStarfield: paintStarfield (g, b); break;
        case scNebula: paintNebula (g, b); break;
        case scNeon: paintNeon (g, b); break;
        default: paintValley (g, b); break;
    }
    paintSparks (g);
    if (f.frozen) { g.setColour (juce::Colour (0xff9fd8ff).withAlpha (0.10f)); g.fillRect (b); }
    const float dim = 0.45f * (1.0f - f.runGain);
    if (dim > 0.01f) { g.setColour (juce::Colours::black.withAlpha (dim)); g.fillRect (b); }
    g.restoreState();
    if (showOverlay) paintOverlay (g, b);
}

// ---- Valley: sky, sun/moon, clouds, shooting stars, aurora, mountain ranges, forest, lake, birds, mist, dust
void Landscape::paintValley (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float w = b.getWidth(), h = b.getHeight(), lt = f.light, t = (float) f.time, mt = f.musTime;
    const float horizon = h * (hasLake ? 0.62f : 0.70f);
    const auto skyTop = mix3 (lt, juce::Colour (0xff03040c), juce::Colour (0xff1b1544), juce::Colour (0xff2c64ad));
    const auto skyLow = mix3 (lt, juce::Colour (0xff151b42), juce::Colour (0xffd9765f), juce::Colour (0xfff0d7a8));
    g.setGradientFill (juce::ColourGradient (skyTop, 0, 0, skyLow, 0, horizon, false));
    g.fillRect (b);
    const float starA = juce::jlimit (0.0f, 1.0f, (0.62f - lt) * 2.2f);
    if (starA > 0.01f)
        for (size_t i = 0; i < stars.size(); ++i)
        {
            const float tw = 0.55f + 0.45f * std::sin (t * twinkle[i] + (float) i);
            g.setColour (juce::Colours::white.withAlpha (starA * tw * (i % 7 == 0 ? 0.95f : 0.55f)));
            g.fillEllipse (stars[i].x * w, stars[i].y * horizon * 0.95f, starSize[i], starSize[i]);
        }
    // shooting stars at night
    if (starA > 0.3f)
    {
        const int slot = (int) (t / 6.0f);
        const float ph = (t - (float) slot * 6.0f) / 0.9f;
        if (hashF ((uint32_t) f.seed, (uint32_t) slot) < 0.55f && ph < 1.0f)
        {
            const float sx = w * (0.1f + 0.8f * hashF ((uint32_t) slot, 3)), sy = horizon * (0.1f + 0.35f * hashF ((uint32_t) slot, 5));
            const float x = sx + ph * w * 0.18f, y = sy + ph * h * 0.08f;
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (starA * (1.0f - ph)), x, y, juce::Colours::white.withAlpha (0.0f), x - w * 0.08f, y - h * 0.035f, false));
            g.drawLine (x - w * 0.08f, y - h * 0.035f, x, y, 1.6f);
        }
    }
    // sun / moon on the evolution clock
    {
        const float ph = wrap01 (f.evoTime / 420.0f + (float) (f.seed % 97) * 0.0137f);
        const float x = w * (0.08f + 0.84f * ph), y = horizon + 10.0f - std::sin (ph * kPi) * h * 0.5f;
        const bool sun = lt > 0.5f;
        const auto c = sun ? juce::Colour (0xffffd27a) : juce::Colour (0xffdfe6ff);
        const float rad = 15.0f + 8.0f * lt, gl = 90.0f + 110.0f * f.energy;
        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), x, y, c.withAlpha (0.0f), x + gl, y, true));
        g.fillEllipse (x - gl, y - gl, gl * 2, gl * 2);
        g.setColour (c);
        g.fillEllipse (x - rad, y - rad, rad * 2, rad * 2);
        if (! sun) { g.setColour (skyTop.withAlpha (0.55f)); g.fillEllipse (x - rad * 0.45f, y - rad * 1.05f, rad * 1.7f, rad * 1.7f); }
    }
    // aurora (Shimmer)
    {
        const float a = juce::jlimit (0.0f, 1.0f, pres[lyShimmer] * (0.35f + glow[lyShimmer] * 1.5f));
        if (a > 0.01f)
            for (int k = 0; k < 3; ++k)
            {
                juce::Path rib;
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
                    rib.lineTo (x, base + thick + std::sin (x * 0.006f + t * 0.13f + (float) k * 1.7f + 0.6f) * h * 0.05f);
                }
                rib.closeSubPath();
                const auto c = (k == 1 ? col::accent3 : col::accent);
                juce::ColourGradient gr (c.withAlpha (0.0f), 0, base - h * 0.04f, c.withAlpha (0.0f), 0, base + thick + h * 0.05f, false);
                gr.addColour (0.45, c.withAlpha (a * (0.30f - 0.06f * (float) k)));
                g.setGradientFill (gr);
                g.fillPath (rib);
            }
    }
    // clouds drifting (more visible by day and dusk)
    {
        const auto cc = mix3 (lt, juce::Colour (0xff3a4268), juce::Colour (0xffe8a48c), juce::Colours::white);
        for (int i = 0; i < numClouds; ++i)
        {
            const float sp = 3.0f + 2.0f * hashF ((uint32_t) f.seed, (uint32_t) i + 40);
            const float cx = std::fmod (w * hashF ((uint32_t) f.seed, (uint32_t) i + 50) + mt * sp, w * 1.4f) - w * 0.2f;
            const float cy = horizon * (0.15f + 0.45f * hashF ((uint32_t) f.seed, (uint32_t) i + 60));
            const float cw = w * (0.08f + 0.08f * hashF ((uint32_t) f.seed, (uint32_t) i + 70));
            for (int k = 0; k < 4; ++k)
                softBlob (g, cx + (float) (k - 1.5f) * cw * 0.45f, cy + (k % 2) * cw * 0.08f, cw * (0.5f + 0.15f * (float) (k % 3)), cw * 0.22f, cc.withAlpha (0.16f + 0.18f * lt));
        }
    }
    // mountain ranges
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
    auto ridgeY = [&] (int k, float baseY, float amp, float scroll, float f1, float f2, float f3, float x)
    {
        const float s0 = (float) (f.seed % 1000) * 0.37f + (float) k * 2.1f, xs = x + scroll;
        const float n = 0.55f * std::sin (xs * f1 + s0) + 0.3f * std::sin (xs * f2 + s0 * 1.7f) + 0.15f * std::sin (xs * f3 + s0 * 2.3f);
        return baseY - amp * (0.5f + 0.5f * n);
    };
    struct R { int layer; float base, amp, speed, f1, f2, f3, shade; };
    const R ranges[3] { { lyDrone, horizon - h * 0.02f, h * 0.30f, 0.6f, 0.0042f, 0.011f, 0.027f, 0.35f },
                        { lyPad, horizon + h * 0.07f, h * 0.22f, 1.5f, 0.0061f, 0.016f, 0.041f, 0.55f },
                        { lyTexture, horizon + h * 0.16f, h * 0.14f, 3.2f, 0.0089f, 0.023f, 0.058f, 0.75f } };
    for (int ri = 0; ri < (hasLake ? 2 : 3); ++ri)
    {
        const auto& r = ranges[ri];
        const float pr = pres[(size_t) r.layer];
        const float amp = r.amp * (0.35f + 0.65f * pr) * (1.0f + 0.12f * glow[(size_t) r.layer]);
        auto p = ridge (r.layer, r.base, amp, mt * r.speed, r.f1, r.f2, r.f3);
        const auto lc = col::layer[r.layer];
        const auto topC = lc.interpolatedWith (skyLow, 0.55f - 0.35f * r.shade).darker (0.6f + r.shade * 1.4f).withMultipliedSaturation (0.6f + 0.4f * pr);
        g.setGradientFill (juce::ColourGradient (topC.brighter (0.25f * glow[(size_t) r.layer]), 0, r.base - r.amp, juce::Colour (0xff05060c).interpolatedWith (topC, 0.25f), 0, h, false));
        g.fillPath (p);
        g.setColour (lc.withAlpha (0.18f + 0.5f * pr * glow[(size_t) r.layer]));
        g.strokePath (p, juce::PathStrokeType (1.2f));
        // pine forest on the middle range
        if (ri == 1 && hasForest)
        {
            g.setColour (juce::Colour (0xff05070f).interpolatedWith (topC, 0.2f));
            for (int i = 0; i < 70; ++i)
            {
                const float x = w * hashF ((uint32_t) f.seed, (uint32_t) i + 100);
                const float y = ridgeY (r.layer, r.base, amp, mt * r.speed, r.f1, r.f2, r.f3, x);
                const float th = h * (0.025f + 0.03f * hashF ((uint32_t) f.seed, (uint32_t) i + 200));
                juce::Path tree;
                tree.addTriangle (x - th * 0.28f, y + 2, x + th * 0.28f, y + 2, x, y - th);
                g.fillPath (tree);
            }
        }
        // mist (Wind) between the ranges
        const float mist = pres[lyWind] * (0.25f + 0.6f * glow[lyWind]);
        if (mist > 0.01f)
            for (int i = 0; i < 5; ++i)
            {
                const float mx = std::fmod ((float) i * w * 0.31f + mt * (12.0f + 6.0f * (float) i) * (0.6f + r.speed * 0.3f), w * 1.6f) - w * 0.3f;
                softBlob (g, mx, r.base - r.amp * 0.15f + (float) i * 3.0f, w * (0.18f + 0.05f * (float) (i % 3)), h * 0.03f, juce::Colours::white.withAlpha (0.10f * mist));
            }
    }
    // lake with reflections
    if (hasLake)
    {
        const float wy = horizon + h * 0.10f;
        auto water = b.withTop (wy);
        g.setGradientFill (juce::ColourGradient (skyLow.darker (0.5f).withAlpha (0.95f), 0, wy, skyTop.darker (0.8f), 0, h, false));
        g.fillRect (water);
        for (int i = 0; i < 26; ++i)
        {
            const float y = wy + 3.0f + (float) i * (h - wy) / 26.0f;
            const float len = w * (0.05f + 0.12f * hashF ((uint32_t) i, 9)) * (1.0f + 0.3f * std::sin (t * 0.8f + (float) i));
            const float x = w * hashF ((uint32_t) i, (uint32_t) (t * 0.3f) + 11);
            g.setColour (skyLow.brighter (0.3f).withAlpha (0.10f + 0.12f * lt));
            g.fillRect (x, y, len, 1.0f);
        }
        // grain dust over the water (Texture) as glints
        const float a = pres[lyTexture] * (0.3f + glow[lyTexture] * 1.5f);
        for (int i = 0; i < 40 && a > 0.01f; ++i)
        {
            const float x = w * hashF ((uint32_t) i, 77), y = wy + (h - wy) * hashF ((uint32_t) i, 78);
            g.setColour (col::layer[lyTexture].withAlpha (juce::jlimit (0.0f, 0.8f, a * (0.5f + 0.5f * std::sin (t * 2.0f + (float) i)))));
            g.fillRect (x, y, 3.0f, 1.0f);
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
    // birds by day
    if (hasBirds && lt > 0.5f)
    {
        g.setColour (juce::Colour (0xff1a1a24).withAlpha (0.7f * juce::jmin (1.0f, (lt - 0.5f) * 4.0f)));
        const float bx = std::fmod (mt * 14.0f + w * 0.3f, w * 1.5f) - w * 0.25f, by = horizon * 0.42f;
        for (int i = 0; i < 7; ++i)
        {
            const float x = bx - (float) std::abs (i - 3) * 14.0f, y = by + (float) i * 3.0f + (float) std::abs (i - 3) * 6.0f;
            const float flap = 3.0f * std::sin (t * 6.0f + (float) i);
            juce::Path bird;
            bird.startNewSubPath (x - 6, y - flap);
            bird.quadraticTo (x - 3, y - 3, x, y);
            bird.quadraticTo (x + 3, y - 3, x + 6, y - flap);
            g.strokePath (bird, juce::PathStrokeType (1.3f));
        }
    }
    // ground
    {
        auto p = ridge (9, h * 0.97f, h * 0.10f, mt * 6.0f, 0.012f, 0.031f, 0.07f);
        g.setColour (juce::Colour (0xff04050a));
        g.fillPath (p);
    }
}

// ---- Starfield: parallax stars, galactic band (Shimmer), planet (Drone) + moon (Pad), asteroid dust, comets (Bells)
void Landscape::paintStarfield (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float w = b.getWidth(), h = b.getHeight(), t = (float) f.time, mt = f.musTime;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff02030a), 0, 0, juce::Colour (0xff0a0f24).interpolatedWith (planetCol2, 0.3f), 0, h, false));
    g.fillRect (b);
    // galactic band
    {
        const float a = 0.15f + 0.6f * pres[lyShimmer] * (0.4f + glow[lyShimmer]);
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-0.35f, w * 0.5f, h * 0.5f));
        juce::ColourGradient band (col::accent3.withAlpha (0.0f), 0, h * 0.3f, col::accent3.withAlpha (0.0f), 0, h * 0.7f, false);
        band.addColour (0.5, col::accent3.interpolatedWith (col::accent, 0.4f).withAlpha (0.22f * a));
        g.setGradientFill (band);
        g.fillRect (-w * 0.3f, h * 0.3f, w * 1.6f, h * 0.4f);
        for (int i = 0; i < 160; ++i)
        {
            const float x = -w * 0.3f + w * 1.6f * hashF ((uint32_t) i, 301), y = h * 0.5f + h * 0.13f * (hashF ((uint32_t) i, 302) + hashF ((uint32_t) i, 303) - 1.0f);
            g.setColour (juce::Colours::white.withAlpha (a * 0.6f * hashF ((uint32_t) i, 304)));
            g.fillRect (x, y, 1.0f, 1.0f);
        }
    }
    // parallax stars (three depths), speed follows Evolution
    const float speed = 2.0f + 30.0f * f.evolution;
    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float depth = 0.3f + 0.7f * (float) (i % 3) / 2.0f;
        const float x = std::fmod (stars[i].x * w - mt * speed * depth + w * 100.0f, w), y = stars[i].y * h;
        const float tw = 0.6f + 0.4f * std::sin (t * twinkle[i] + (float) i);
        g.setColour (juce::Colours::white.withAlpha (tw * (0.3f + 0.6f * depth)));
        const float s = starSize[i] * depth * 1.2f;
        g.fillEllipse (x, y, s, s);
        if (f.density > 0.75f && depth > 0.9f) // light speed streaks when dense
        {
            g.setColour (juce::Colours::white.withAlpha (0.15f * (f.density - 0.75f) * 4.0f));
            g.drawLine (x, y + s * 0.5f, x + s * 10.0f, y + s * 0.5f, 0.6f);
        }
    }
    // solar wind (Wind)
    if (pres[lyWind] > 0.02f)
        for (int i = 0; i < 30; ++i)
        {
            const float x = std::fmod (w * hashF ((uint32_t) i, 401) - mt * 40.0f * (1.0f + hashF ((uint32_t) i, 402)) + w * 100.0f, w);
            const float y = h * hashF ((uint32_t) i, 403);
            g.setColour (col::layer[lyWind].withAlpha (0.12f * pres[lyWind] * (0.5f + glow[lyWind])));
            g.drawLine (x, y, x + 40.0f, y, 0.8f);
        }
    // planet (Drone) with atmosphere and optional rings
    {
        const float r = planetR, gl = r * (1.15f + 0.1f * glow[lyDrone] + 0.05f * pres[lyDrone]);
        const auto p = planetPos;
        g.setGradientFill (juce::ColourGradient (planetCol.withAlpha (0.35f * (0.5f + glow[lyDrone])), p.x, p.y, planetCol.withAlpha (0.0f), p.x + gl * 1.2f, p.y, true));
        g.fillEllipse (p.x - gl * 1.2f, p.y - gl * 1.2f, gl * 2.4f, gl * 2.4f);
        auto ringPath = [&] (bool back)
        {
            juce::Path rp;
            rp.addCentredArc (p.x, p.y, r * 1.9f, r * 0.35f, -0.25f, back ? -kPi * 0.5f : kPi * 0.5f, back ? kPi * 0.5f : kPi * 1.5f, true);
            return rp;
        };
        if (hasRings) { g.setColour (planetCol.brighter (0.5f).withAlpha (0.35f)); g.strokePath (ringPath (true), juce::PathStrokeType (r * 0.06f)); }
        g.setGradientFill (juce::ColourGradient (planetCol.brighter (0.3f), p.x - r * 0.6f, p.y - r * 0.6f, planetCol2, p.x + r * 0.5f, p.y + r * 0.6f, true));
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path disc; disc.addEllipse (p.x - r, p.y - r, r * 2, r * 2);
        g.reduceClipRegion (disc);
        for (int k = 0; k < 7; ++k) // cloud bands drifting
        {
            const float by = p.y - r + (float) k * r * 0.3f + std::sin (t * 0.05f + (float) k) * 4.0f;
            g.setColour (planetCol.interpolatedWith (juce::Colours::white, 0.2f).withAlpha (0.10f));
            g.fillRect (p.x - r, by, r * 2, r * 0.08f);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, p.x - r * 0.3f, p.y - r * 0.3f, juce::Colours::black.withAlpha (0.75f), p.x + r, p.y + r, true));
        g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
    }
    if (hasRings)
    {
        juce::Path rp;
        rp.addCentredArc (planetPos.x, planetPos.y, planetR * 1.9f, planetR * 0.35f, -0.25f, kPi * 0.5f, kPi * 1.5f, true);
        g.setColour (planetCol.brighter (0.6f).withAlpha (0.5f));
        g.strokePath (rp, juce::PathStrokeType (planetR * 0.06f));
    }
    // moon (Pad) orbiting
    {
        const float a = f.evoTime * 0.02f + (float) (f.seed % 31);
        const float mx = planetPos.x + std::cos (a) * planetR * 2.4f;
        float my = planetPos.y - planetR * 0.9f + std::sin (a) * planetR * 0.5f;
        const float mr = planetR * 0.12f * (0.7f + 0.3f * pres[lyPad]);
        my = juce::jmax (my, mr + 14.0f);
        g.setColour (col::layer[lyPad].interpolatedWith (juce::Colours::white, 0.5f).withAlpha (0.4f + 0.5f * pres[lyPad]));
        g.fillEllipse (mx - mr, my - mr, mr * 2, mr * 2);
    }
    // asteroid dust (Texture)
    {
        const float a = pres[lyTexture] * (0.3f + glow[lyTexture] * 1.5f);
        for (int i = 0; i < 50 && a > 0.01f; ++i)
        {
            const float x = std::fmod (w * hashF ((uint32_t) i, 501) + mt * (8.0f + 6.0f * hashF ((uint32_t) i, 502)), w);
            const float y = h * (0.55f + 0.4f * hashF ((uint32_t) i, 503)) + std::sin (t * 0.4f + (float) i) * 3.0f;
            g.setColour (juce::Colour (0xffb7a99a).withAlpha (juce::jlimit (0.0f, 0.8f, a * 0.8f)));
            const float s = 1.5f + 2.5f * hashF ((uint32_t) i, 504);
            g.fillEllipse (x, y, s, s * 0.8f);
        }
    }
}

// ---- Nebula: drifting gas clouds (one colour per layer), a slowly turning spiral galaxy, filaments, stars
void Landscape::paintNebula (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float w = b.getWidth(), h = b.getHeight(), t = (float) f.time;
    g.fillAll (juce::Colour (0xff020208));
    for (size_t i = 0; i < stars.size(); ++i)
    {
        const float tw = 0.5f + 0.5f * std::sin (t * twinkle[i] + (float) i);
        g.setColour (juce::Colours::white.withAlpha (0.25f + 0.5f * tw));
        g.fillEllipse (stars[i].x * w, stars[i].y * h, starSize[i] * 0.9f, starSize[i] * 0.9f);
    }
    const auto c = b.getCentre();
    // gas clouds orbit slowly around the centre
    for (int i = 0; i < 9; ++i)
    {
        const int l = i % kNumLayers;
        const float a0 = kTwoPi * hashF ((uint32_t) f.seed, (uint32_t) i + 600) + f.evoTime * 0.004f * (i % 2 ? 1.0f : -1.0f);
        const float rad = w * (0.12f + 0.28f * hashF ((uint32_t) f.seed, (uint32_t) i + 610));
        const float x = c.x + std::cos (a0) * rad, y = c.y + std::sin (a0) * rad * 0.55f;
        const float sz = w * (0.16f + 0.12f * hashF ((uint32_t) f.seed, (uint32_t) i + 620)) * (0.7f + 0.3f * pres[(size_t) l] + 0.2f * glow[(size_t) l]);
        const auto colr = col::layer[l].interpolatedWith (i % 3 == 0 ? juce::Colour (0xffff4fa0) : col::accent3, 0.35f);
        softBlob (g, x, y, sz, sz * 0.6f, colr.withAlpha (0.10f + 0.22f * pres[(size_t) l] * (0.6f + f.light)));
    }
    // filaments (Shimmer)
    const float fa = pres[lyShimmer] * (0.3f + glow[lyShimmer]);
    for (int k = 0; k < 5 && fa > 0.01f; ++k)
    {
        juce::Path fil;
        const float y0 = h * (0.2f + 0.15f * (float) k);
        fil.startNewSubPath (0, y0);
        fil.cubicTo (w * 0.3f, y0 + std::sin (t * 0.1f + (float) k) * h * 0.2f, w * 0.6f, y0 - std::cos (t * 0.08f + (float) k) * h * 0.2f, w, y0 + h * 0.05f);
        g.setColour (col::accent.withAlpha (0.10f * fa));
        g.strokePath (fil, juce::PathStrokeType (3.0f + (float) k));
        g.setColour (juce::Colours::white.withAlpha (0.10f * fa));
        g.strokePath (fil, juce::PathStrokeType (0.8f));
    }
    // spiral galaxy (Drone core, Pad arms, Texture stars)
    {
        const float rot = f.evoTime * 0.01f + t * 0.01f;
        const float R = h * 0.42f;
        softBlob (g, c.x, c.y, R * 0.35f * (1.0f + 0.3f * glow[lyDrone]), R * 0.2f, juce::Colour (0xfffff0d0).withAlpha (0.45f + 0.3f * pres[lyDrone]));
        for (int i = 0; i < 420; ++i)
        {
            const float u = hashF ((uint32_t) i, 701), arm = (float) (i % 2) * kPi;
            const float rr = R * std::sqrt (u);
            const float ang = arm + rot + u * 7.5f + (hashF ((uint32_t) i, 702) - 0.5f) * 0.6f;
            const float x = c.x + std::cos (ang) * rr, y = c.y + std::sin (ang) * rr * 0.45f;
            const auto pc = u < 0.3f ? juce::Colour (0xffffe6b0) : (i % 3 ? col::layer[lyPad] : col::accent);
            g.setColour (pc.withAlpha ((0.25f + 0.5f * pres[lyPad]) * (1.0f - u * 0.6f) * (0.6f + 0.4f * std::sin (t * 1.3f + (float) i))));
            g.fillRect (x, y, 1.6f, 1.6f);
        }
    }
    // cosmic dust (Wind) drifting
    for (int i = 0; i < 40 && pres[lyWind] > 0.02f; ++i)
    {
        const float x = std::fmod (w * hashF ((uint32_t) i, 801) + f.musTime * 6.0f, w), y = h * hashF ((uint32_t) i, 802);
        g.setColour (col::layer[lyWind].withAlpha (0.25f * pres[lyWind]));
        g.fillEllipse (x, y, 1.4f, 1.4f);
    }
}

// ---- Neon Rain: smoggy sky glow, arcology, three skyline layers, neon signs, searchlights, flying cars, rain, fog
void Landscape::paintNeon (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float w = b.getWidth(), h = b.getHeight(), t = (float) f.time, lt = f.light;
    // smog: dark navy above, orange / magenta glow of the city below
    const auto glowC = juce::Colour (0xffc2552f).interpolatedWith (juce::Colour (0xff8a2b7a), 0.4f + 0.3f * std::sin (t * 0.05f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff04060f), 0, 0, glowC.withAlpha (1.0f).darker (1.0f - 0.6f * lt), 0, h * 0.8f, false));
    g.fillRect (b);
    // searchlights (Shimmer)
    const float sa = pres[lyShimmer] * (0.25f + glow[lyShimmer] * 1.2f);
    for (int k = 0; k < 3 && sa > 0.01f; ++k)
    {
        const float bx = w * (0.2f + 0.3f * (float) k), ang = std::sin (t * (0.11f + 0.04f * (float) k) + (float) k * 2.0f) * 0.5f;
        juce::Path beam;
        beam.addTriangle (bx, h * 0.85f, bx + std::sin (ang - 0.05f) * h * 1.4f, h * 0.85f - std::cos (ang - 0.05f) * h * 1.4f,
                          bx + std::sin (ang + 0.05f) * h * 1.4f, h * 0.85f - std::cos (ang + 0.05f) * h * 1.4f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8f4ff).withAlpha (0.16f * sa), bx, h * 0.85f, juce::Colour (0xffd8f4ff).withAlpha (0.0f), bx, 0, false));
        g.fillPath (beam);
    }
    // arcology pyramid in the distance (Drone), with tiny lights and a burning flare
    {
        const float ax = arcology.x, base = h * 0.8f, ah = h * 0.42f * (0.85f + 0.15f * pres[lyDrone]);
        juce::Path pyr;
        pyr.addTriangle (ax - ah * 0.9f, base, ax + ah * 0.9f, base, ax, base - ah);
        g.setColour (juce::Colour (0xff0d1020).interpolatedWith (glowC, 0.12f));
        g.fillPath (pyr);
        for (int i = 0; i < 90; ++i)
        {
            const float u = hashF ((uint32_t) i, 901), v = hashF ((uint32_t) i, 902);
            const float y = base - ah * u, half = ah * 0.9f * (1.0f - u);
            g.setColour (juce::Colour (0xffffd59a).withAlpha (0.3f + 0.4f * glow[lyDrone] * (0.5f + 0.5f * std::sin (t + (float) i))));
            g.fillRect (ax - half + 2.0f * half * v, y, 1.5f, 1.2f);
        }
        const float fl = 0.5f + 0.5f * std::sin (t * 3.1f) * std::sin (t * 1.7f);
        softBlob (g, ax - ah * 0.55f, base - ah * 0.35f, 14.0f + 10.0f * fl, 10.0f + 6.0f * fl, juce::Colour (0xffffa040).withAlpha (0.5f * fl * (0.4f + pres[lyDrone])));
    }
    // skyline layers, neon signs between them
    for (int layer = 0; layer < 3; ++layer)
    {
        if (skyline[(size_t) layer].isValid())
        {
            const float drift = std::fmod (f.musTime * (1.0f + 2.0f * (float) layer), (float) getWidth());
            g.setOpacity (layer == 0 ? 0.85f : 1.0f);
            g.drawImageAt (skyline[(size_t) layer], (int) -drift, 0);
            g.drawImageAt (skyline[(size_t) layer], (int) (w - drift), 0);
            g.setOpacity (1.0f);
        }
        for (auto& s : signs)
        {
            if (s.layer != layer) continue;
            const float drift = std::fmod (f.musTime * (1.0f + 2.0f * (float) layer), (float) getWidth());
            const float flick = std::sin (t * 9.0f + s.phase) > 0.97f ? 0.3f : 1.0f;
            const float lvl = flick * (0.55f + 0.45f * pres[lyPad]) * (0.7f + 0.3f * glow[lyPad]);
            for (float off : { -drift, w - drift })
            {
                auto r = s.r.translated (off, 0);
                softBlob (g, r.getCentreX(), r.getCentreY(), r.getWidth() * 1.6f + 10.0f, r.getHeight() * 1.6f + 10.0f, s.c.withAlpha (0.18f * lvl));
                g.setColour (s.c.withAlpha (0.9f * lvl));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.5f * lvl));
                g.fillRoundedRectangle (r.reduced (r.getWidth() * 0.3f, r.getHeight() * 0.3f), 1.0f);
            }
        }
    }
    // a few flying cars always cruise (Density), plus the ones launched by bells (sparks kind 2)
    for (int i = 0; i < (int) (2 + 4 * f.density); ++i)
    {
        const float sp = 30.0f + 40.0f * hashF ((uint32_t) i, 1001), dir = i % 2 ? 1.0f : -1.0f;
        const float x = dir > 0 ? std::fmod (t * sp + w * hashF ((uint32_t) i, 1002), w + 80.0f) - 40.0f : w - std::fmod (t * sp + w * hashF ((uint32_t) i, 1002), w + 80.0f) + 40.0f;
        const float y = h * (0.2f + 0.35f * hashF ((uint32_t) i, 1003)) + std::sin (t * 0.5f + (float) i) * 4.0f;
        g.setColour (juce::Colour (0xffff5a5a).withAlpha (0.8f));
        g.fillEllipse (x, y, 2.5f, 2.5f);
        g.setColour (juce::Colour (0xffd8f4ff).withAlpha (0.8f));
        g.fillEllipse (x + 5.0f * dir, y, 2.0f, 2.0f);
    }
    // fog (Wind)
    if (pres[lyWind] > 0.02f)
        for (int i = 0; i < 6; ++i)
        {
            const float mx = std::fmod ((float) i * w * 0.27f + f.musTime * (8.0f + 4.0f * (float) i), w * 1.6f) - w * 0.3f;
            softBlob (g, mx, h * (0.78f + 0.03f * (float) (i % 3)), w * 0.25f, h * 0.06f, glowC.brighter (0.4f).withAlpha (0.10f * pres[lyWind] * (0.6f + glow[lyWind])));
        }
    // rain
    {
        const float amount = juce::jlimit (0.0f, 1.0f, f.rain + 0.25f * pres[lyWind]);
        const int n = (int) (amount * 380.0f);
        g.setColour (juce::Colour (0xffbcd6ff).withAlpha (0.18f + 0.15f * amount));
        for (int i = 0; i < n; ++i)
        {
            const float sp = 0.9f + 0.5f * hashF ((uint32_t) i, 1101);
            const float x = std::fmod (w * hashF ((uint32_t) i, 1102) + t * 60.0f * sp, w + 40.0f) - 20.0f;
            const float y = std::fmod (h * hashF ((uint32_t) i, 1103) + t * 520.0f * sp, h + 30.0f) - 30.0f;
            g.drawLine (x, y, x - 3.5f, y + 14.0f * sp, 0.8f);
        }
    }
    // street-level glow
    g.setGradientFill (juce::ColourGradient (glowC.withAlpha (0.0f), 0, h * 0.75f, glowC.withAlpha (0.35f), 0, h, false));
    g.fillRect (b.withTop (h * 0.75f));
}

void Landscape::paintSparks (juce::Graphics& g)
{
    for (auto& s : sparks)
    {
        if (s.kind == 2) // flying light with a trail
        {
            const float dir = s.vx > 0 ? -1.0f : 1.0f;
            g.setGradientFill (juce::ColourGradient (s.c.withAlpha (0.8f), s.x, s.y, s.c.withAlpha (0.0f), s.x + dir * 50.0f, s.y, false));
            g.drawLine (s.x, s.y, s.x + dir * 50.0f, s.y, 1.5f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (s.x - 2, s.y - 2, 4, 4);
            continue;
        }
        const float a = juce::jlimit (0.0f, 1.0f, s.life * 1.4f) * pres[lyBells] + 0.2f * s.life;
        if (s.kind == 1) // comet
        {
            const float len = 60.0f;
            const float nx = -s.vx / std::sqrt (s.vx * s.vx + s.vy * s.vy), ny = -s.vy / std::sqrt (s.vx * s.vx + s.vy * s.vy);
            g.setGradientFill (juce::ColourGradient (s.c.withAlpha (a), s.x, s.y, s.c.withAlpha (0.0f), s.x + nx * len, s.y + ny * len, false));
            g.drawLine (s.x, s.y, s.x + nx * len, s.y + ny * len, 2.0f);
        }
        g.setGradientFill (juce::ColourGradient (s.c.withAlpha (0.45f * a), s.x, s.y, s.c.withAlpha (0.0f), s.x + s.size * 7, s.y, true));
        g.fillEllipse (s.x - s.size * 7, s.y - s.size * 7, s.size * 14, s.size * 14);
        g.setColour (s.c.withAlpha (a));
        g.fillEllipse (s.x - s.size * 0.5f, s.y - s.size * 0.5f, s.size, s.size);
        g.drawLine (s.x - s.size * 2.5f, s.y, s.x + s.size * 2.5f, s.y, 0.8f);
        g.drawLine (s.x, s.y - s.size * 2.5f, s.x, s.y + s.size * 2.5f, 0.8f);
    }
}

void Landscape::paintOverlay (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float t = (float) f.time;
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
    auto leg = b.reduced (16, 12).removeFromBottom (16);
    const float lw = juce::jmin (100.0f, leg.getWidth() / kNumLayers);
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
