#include "Visuals.h"
#include "../dsp/Wavetable.h"
#include "../dsp/Envelope.h"

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
    f.energy += (juce::jmin (1.0f, f.rms * 4.0f) - f.energy) * (float) juce::jmin (1.0, dt * 6.0);
    f.time += dt;
    auto& t = proc.telemetry();
    f.voiceMask = t.voiceMask.load(); f.activeVoices = t.activeVoices.load(); f.lastNote = t.lastNote.load();
    f.arpStep = t.arpStep.load(); f.gateStep = t.gateStep.load(); f.seqStep = t.seqStep.load();
    f.lastVelocity = t.lastVelocity.load(); f.gateLevel = t.gateLevel.load(); f.seqValue = t.seqValue.load();
    for (int i = 0; i < kNumEnv; ++i) f.env[(size_t) i] = t.env[(size_t) i].load();
    for (int i = 0; i < kNumLfo; ++i) f.lfo[(size_t) i] = t.lfo[(size_t) i].load();
    for (int i = 0; i < kNumOsc; ++i) f.morph[(size_t) i] = t.oscMorph[(size_t) i].load();
    for (int i = 0; i < kNumFilters; ++i) f.cutoff[(size_t) i] = t.cutoff[(size_t) i].load();
    for (int i = 0; i < kMaxVoices; ++i) f.voiceLevel[(size_t) i] = t.voiceLevel[(size_t) i].load();
    for (int i = 0; i < kNumMacros; ++i) f.macro[(size_t) i] = proc.getParam (P::macro1 + i);
}

// ---------------------------------------------------------------- Scope
void Scope::tick (const Frame& f)
{
    t = f.time;
    energy = f.energy;
    // trigger on a rising zero crossing for a stable picture
    int start = Frame::kScope - 1024;
    for (int i = Frame::kScope - 1024; i > 600; --i)
        if (f.scope[(size_t) i - 1] < 0 && f.scope[(size_t) i] >= 0) { start = i - 400; break; }
    // responsive auto-gain: instant on louder signals, slow release (~1.5 s) so dynamics and decays stay visible
    float peak = 0.0f;
    for (int i = 0; i < 512; ++i) peak = juce::jmax (peak, std::abs (f.scope[(size_t) (start + i)]));
    if (peak > dispPeak) dispPeak = peak;
    else dispPeak += (peak - dispPeak) * 0.012f;
    const float g = juce::jlimit (1.0f, 4.0f, 0.85f / juce::jmax (dispPeak, 0.02f));
    levelDb += (juce::Decibels::gainToDecibels (f.rms, -90.0f) - levelDb) * 0.25f;
    for (int i = 0; i < 512; ++i)
    {
        float v = juce::jlimit (-1.1f, 1.1f, f.scope[(size_t) (start + i)] * g);
        if (f.rms < 1.0e-4f) // idle: slow organic drift
            v = 0.08f * std::sin ((float) i * 0.045f + (float) t * 1.7f) * std::sin ((float) t * 0.37f + (float) i * 0.011f);
        trace[(size_t) i] = v;
    }
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    if (trail.isNull() || trail.getWidth() != getWidth())
        trail = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
    {
        juce::Graphics tg (trail);
        trail.multiplyAllAlphas (0.62f); // phosphor decay
        juce::Path p;
        const float w = r.getWidth() - 8, h = r.getHeight() * 0.42f, cy = r.getCentreY();
        for (int i = 0; i < 512; ++i)
        {
            const float x = 4 + w * (float) i / 511.0f, y = cy - trace[(size_t) i] * h;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        tg.setColour (col::phosphor.withAlpha (0.12f));
        tg.strokePath (p, juce::PathStrokeType (4.0f));
        tg.setColour (col::phosphor.withAlpha (0.9f));
        tg.strokePath (p, juce::PathStrokeType (1.6f));
    }
    g.drawImageAt (trail, 0, 0);
    g.setColour (col::phosphor.withAlpha (0.55f));
    g.setFont (monoFont (10.0f));
    g.drawText (label, r.reduced (8, 4), juce::Justification::topLeft);
    g.drawText (levelDb <= -89.0f ? juce::String ("-inf dB") : juce::String (levelDb, 1) + " dB", r.reduced (8, 4), juce::Justification::topRight);
}

// ---------------------------------------------------------------- Spectrum
void Spectrum::tick (const Frame& f)
{
    for (int i = 0; i < Frame::kBands; ++i)
    {
        const float v = f.spectrum[(size_t) i];
        bands[(size_t) i] = v > bands[(size_t) i] ? v : bands[(size_t) i] * 0.86f + v * 0.14f;
        peaks[(size_t) i] = juce::jmax (bands[(size_t) i], peaks[(size_t) i] - 0.006f);
    }
    repaint();
}

void Spectrum::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (6, 8);
    const float bw = in.getWidth() / Frame::kBands;
    for (int i = 0; i < Frame::kBands; ++i)
    {
        const float h = bands[(size_t) i] * in.getHeight();
        const auto c = col::accent2.interpolatedWith (col::accent, (float) i / Frame::kBands);
        g.setColour (c.withAlpha (0.85f));
        g.fillRect (in.getX() + i * bw + 0.5f, in.getBottom() - h, juce::jmax (1.0f, bw - 1.2f), h);
        g.setColour (c.brighter (0.6f));
        g.fillRect (in.getX() + i * bw + 0.5f, in.getBottom() - peaks[(size_t) i] * in.getHeight() - 1.5f, juce::jmax (1.0f, bw - 1.2f), 1.5f);
    }
    g.setColour (col::phosphor.withAlpha (0.55f));
    g.setFont (monoFont (10.0f));
    g.drawText (L ("ESPECTRO", "SPECTRUM"), r.reduced (8, 4), juce::Justification::topLeft);
}

// ---------------------------------------------------------------- VuMeter
void VuMeter::tick (const Frame& f)
{
    l = juce::jmax (f.peakL, l * 0.85f); r = juce::jmax (f.peakR, r * 0.85f);
    hl = juce::jmax (l, hl - 0.01f); hr = juce::jmax (r, hr - 0.01f);
    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawScreen (g, b);
    auto in = b.reduced (5, 8);
    auto bar = [&] (juce::Rectangle<float> rr, float v, float hold)
    {
        const float db = juce::Decibels::gainToDecibels (v, -60.0f), hdb = juce::Decibels::gainToDecibels (hold, -60.0f);
        const int segs = 24;
        for (int s = 0; s < segs; ++s)
        {
            const float th = -60.0f + 63.0f * (float) s / segs;
            const auto c = th > -3 ? col::danger : th > -12 ? col::accent : col::accent2;
            const float y = rr.getBottom() - (s + 1) * rr.getHeight() / segs;
            g.setColour (db > th ? c : c.withAlpha (0.12f));
            g.fillRect (rr.getX(), y + 1, rr.getWidth(), rr.getHeight() / segs - 2);
        }
        const float hy = rr.getBottom() - juce::jlimit (0.0f, 1.0f, (hdb + 60.0f) / 63.0f) * rr.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (rr.getX(), hy, rr.getWidth(), 1.5f);
    };
    auto lr = in.removeFromLeft (in.getWidth() * 0.5f).reduced (2, 0);
    bar (lr, l, hl);
    bar (in.reduced (2, 0), r, hr);
}

// ---------------------------------------------------------------- LedMatrix
void LedMatrix::tick (const Frame& fr) { f = fr; t = fr.time; repaint(); }

void LedMatrix::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    drawScreen (g, b);
    auto in = b.reduced (10, 8);
    g.setFont (monoFont (9.5f));
    const float rowH = in.getHeight() / 6.0f;
    auto row = [&] (const char* name, int count, auto levelFn, juce::Colour c)
    {
        auto rr = in.removeFromTop (rowH);
        g.setColour (col::phosphor.withAlpha (0.5f));
        g.drawText (name, rr.removeFromLeft (44), juce::Justification::centredLeft);
        const float step = rr.getWidth() / (float) count;
        const float rad = juce::jmin (step * 0.32f, rowH * 0.28f);
        for (int i = 0; i < count; ++i) drawLed (g, { rr.getX() + step * (i + 0.5f), rr.getCentreY() }, rad, c, levelFn (i));
    };
    const float idle = f.activeVoices == 0 ? 1.0f : 0.0f;
    row ("VOX", 32, [&] (int i) { return juce::jmax (f.voiceLevel[(size_t) i], idle * 0.12f * (0.5f + 0.5f * std::sin ((float) t * 1.3f + i * 0.7f))); }, col::accent2);
    row ("ARP", 16, [&] (int i) { return i == f.arpStep ? 1.0f : 0.06f; }, col::accent);
    row ("GATE", 32, [&] (int i) { return i == f.gateStep ? f.gateLevel : 0.05f; }, col::accent3);
    row ("SEQ", 32, [&] (int i) { return i == f.seqStep ? 0.5f + 0.5f * std::abs (f.seqValue) : 0.05f; }, col::accent);
    row ("ENV", 16, [&] (int i) { return f.env[(size_t) (i / 4)] > (float) (i % 4) / 4.0f ? 0.4f + 0.6f * f.env[(size_t) (i / 4)] : 0.05f; }, col::accent2);
    row ("LFO", 16, [&] (int i) { const float v = 0.5f + 0.5f * f.lfo[(size_t) (i / 4)]; return v > (float) (i % 4) / 4.0f ? v : 0.05f; }, col::accent3);
}

// ---------------------------------------------------------------- CircuitBus
CircuitBus::CircuitBus() { setInterceptsMouseClicks (false, false); }

void CircuitBus::resized()
{
    traces.clear();
    const auto b = getLocalBounds().toFloat();
    for (int i = 0; i < 14; ++i)
    {
        Trace t;
        float x = rng.nextFloat() * b.getWidth(), y = (i % 2 == 0) ? b.getY() + 2 : b.getBottom() - 2;
        t.path.startNewSubPath (x, y);
        for (int k = 0; k < 5; ++k)
        {
            if (k % 2 == 0) y = juce::jlimit (b.getY() + 2, b.getBottom() - 2, y + (rng.nextFloat() - 0.5f) * b.getHeight());
            else x = juce::jlimit (b.getX() + 2, b.getRight() - 2, x + (rng.nextFloat() - 0.5f) * b.getWidth() * 0.5f);
            t.path.lineTo (x, y);
        }
        t.length = t.path.getLength();
        t.phase = rng.nextFloat();
        t.speed = 0.08f + rng.nextFloat() * 0.25f;
        t.colour = i % 3 == 0 ? col::accent : (i % 3 == 1 ? col::accent2 : col::accent3);
        traces.push_back (t);
    }
}

void CircuitBus::tick (const Frame& f)
{
    energy = f.energy;
    for (auto& t : traces) { t.phase += t.speed * (0.25f + 2.5f * energy) / 60.0f; if (t.phase > 1) t.phase -= 1; }
    repaint();
}

void CircuitBus::paint (juce::Graphics& g)
{
    for (auto& t : traces)
    {
        g.setColour (t.colour.withAlpha (0.07f));
        g.strokePath (t.path, juce::PathStrokeType (1.2f));
        const auto p = t.path.getPointAlongPath (t.phase * t.length);
        g.setColour (t.colour.withAlpha (0.2f + 0.6f * energy));
        g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (p));
        g.setColour (t.colour.withAlpha (0.08f + 0.2f * energy));
        g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (p));
    }
}

// ---------------------------------------------------------------- OscView
OscView::OscView (InfinitumProcessor& p, int o) : proc (p), osc (o) { startTimerHz (4); rebuild (proc.getParam (P::osc (o, P::oMorph))); }

void OscView::rebuild (float morph)
{
    using namespace P;
    const int type = (int) proc.getParam (P::osc (osc, oType));
    const float shape = proc.getParam (P::osc (osc, oShape)), sync = proc.getParam (P::osc (osc, oSync)), fold = proc.getParam (P::osc (osc, oFold));
    Rng rng (5);
    for (int i = 0; i < 256; ++i)
    {
        const float ph = (float) i / 256.0f, sp = wrap01 (ph * sync);
        float y = 0;
        switch (type)
        {
            case otWavetable:
            {
                const int ti = (int) proc.getParam (P::osc (osc, oTable));
                const Wavetable* u = proc.getUserWavetable (osc);
                const Wavetable& T = ti >= numBuiltinTables() && u != nullptr ? *u : builtinTable (ti >= numBuiltinTables() ? 0 : ti);
                y = T.read (sp, morph, 2);
                break;
            }
            case otAnalog:
            {
                const float seg = morph * 3, pw = 0.05f + 0.9f * shape;
                const float s = std::sin (kTwoPi * sp), t = -(1 - 4 * std::abs (sp - 0.5f)), w = 2 * sp - 1, q = sp < pw ? 1.0f : -1.0f;
                y = seg < 1 ? s + (t - s) * seg : seg < 2 ? t + (w - t) * (seg - 1) : w + (q - w) * (seg - 2);
                break;
            }
            case otFM:
            {
                const float r2 = proc.getParam (P::osc (osc, oFmR2)), i2 = proc.getParam (P::osc (osc, oFmI2));
                const float r3 = proc.getParam (P::osc (osc, oFmR3)), i3 = proc.getParam (P::osc (osc, oFmI3));
                const float sc = 0.3f + morph * 1.05f;
                const float m3 = std::sin (kTwoPi * ph * r3) * i3 * i3 * 6 * sc;
                const float m2 = std::sin (kTwoPi * ph * r2 + m3) * i2 * i2 * 6 * sc;
                y = std::sin (kTwoPi * ph + m2);
                break;
            }
            case otNoise: y = rng.bi() * (0.3f + 0.7f * morph); break;
            default: // spectral mutation: blend of archetypes with a slow animated drift
            {
                const auto& a = genomeArchetypes();
                const float pos = morph * (kNumArchetypes - 1);
                const int k0 = juce::jmin (kNumArchetypes - 2, (int) pos);
                const float tt = pos - (float) k0;
                const float drift = (float) nowSeconds() * 0.4f;
                for (int k = 1; k < 24; ++k)
                    y += (a[(size_t) k0][(size_t) k] * (1 - tt) + a[(size_t) k0 + 1][(size_t) k] * tt) * (1.0f + 0.4f * shape * std::sin (drift * (0.5f + k * 0.13f)))
                         * std::sin (kTwoPi * k * sp);
                y *= 0.7f;
                break;
            }
        }
        if (fold > 0.001f) y = y + (std::sin (y * (1 + fold * 5) * kPi * 0.5f) - y) * juce::jmin (1.0f, fold * 4);
        wave[(size_t) i] = y;
    }
    float pk = 0.01f;
    for (float v : wave) pk = juce::jmax (pk, std::abs (v));
    for (float& v : wave) v /= pk;
    lastMorph = morph;
    repaint();
}

void OscView::tick (const Frame& f)
{
    const float m = f.activeVoices > 0 ? f.morph[(size_t) osc] : proc.getParam (P::osc (osc, P::oMorph));
    if (std::abs (m - lastMorph) > 0.004f || (int) proc.getParam (P::osc (osc, P::oType)) == otMutation) rebuild (m);
}

void OscView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    const bool on = proc.getParam (P::osc (osc, P::oOn)) > 0.5f;
    juce::Path p;
    const float h = r.getHeight() * 0.4f;
    for (int i = 0; i < 256; ++i)
    {
        const float x = r.getX() + 4 + (r.getWidth() - 8) * (float) i / 255.0f, y = r.getCentreY() - wave[(size_t) i] * h;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    const auto c = on ? col::phosphor : col::textDim;
    g.setColour (c.withAlpha (0.2f));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

// ---------------------------------------------------------------- FilterView
static float filterMag (int type, float f, float fc, float reso)
{
    // analogue prototype magnitude (good enough for display)
    const float w = f / fc;
    const float q = 0.5f + reso * 9.5f;
    const float re = 1 - w * w, im = w / q;
    const float den = std::sqrt (re * re + im * im) + 1e-6f;
    switch (type)
    {
        case ftLp12: return 1 / den;
        case ftLp24: case ftLadder: { const float d2 = std::sqrt ((1 - w * w) * (1 - w * w) + w * w * 2.0f) + 1e-6f; return 1 / (den * d2) * (type == ftLadder ? 1.0f : 1.0f); }
        case ftHp12: return w * w / den;
        case ftHp24: { const float d2 = std::sqrt ((1 - w * w) * (1 - w * w) + w * w * 2.0f) + 1e-6f; return w * w * w * w / (den * d2); }
        case ftBp: return (w / q) / den * 1.5f;
        case ftNotch: return std::abs (1 - w * w) / den;
        case ftCombPos: case ftCombNeg:
        {
            const float g = 0.2f + 0.78f * reso, ph = kTwoPi * f / fc;
            const float s = type == ftCombPos ? 1.0f : -1.0f;
            return 0.6f / std::sqrt (1 + g * g - 2 * s * g * std::cos (ph));
        }
        case ftFormant:
        {
            float m = 0;
            for (float ff : { 0.0f, 1.6f, 3.4f }) { const float c2 = fc * (0.6f + ff); const float ww = f / c2; m += 0.6f / (std::abs (ww - 1 / ww) * q + 1); }
            return m;
        }
        default: return 1;
    }
}

void FilterView::tick (const Frame& f)
{
    bool changed = false;
    for (int i = 0; i < kNumFilters; ++i)
    {
        const float c = f.activeVoices > 0 ? f.cutoff[(size_t) i] : proc.getParam (P::filt (i, P::fCutoff));
        if (std::abs (c - cut[(size_t) i]) > 0.5f) { cut[(size_t) i] = c; changed = true; }
    }
    if (changed) repaint();
}

void FilterView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (6);
    const juce::Colour cs[2] { col::accent, col::accent2 };
    for (int fi = 0; fi < kNumFilters; ++fi)
    {
        if (proc.getParam (P::filt (fi, P::fOn)) < 0.5f) continue;
        const int type = (int) proc.getParam (P::filt (fi, P::fType));
        const float fc = cut[(size_t) fi] > 0 ? cut[(size_t) fi] : proc.getParam (P::filt (fi, P::fCutoff));
        const float reso = proc.getParam (P::filt (fi, P::fReso));
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
            const float db = juce::jlimit (-36.0f, 24.0f, juce::Decibels::gainToDecibels (filterMag (type, f, fc, reso)));
            const float x = in.getX() + in.getWidth() * (float) i / 200.0f, y = in.getY() + in.getHeight() * (1.0f - (db + 36.0f) / 60.0f);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        juce::Path fill (p);
        fill.lineTo (in.getRight(), in.getBottom()); fill.lineTo (in.getX(), in.getBottom()); fill.closeSubPath();
        g.setColour (cs[fi].withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (cs[fi]);
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }
}

// ---------------------------------------------------------------- EnvView
void EnvView::tick (const Frame& f) { if (std::abs (f.env[(size_t) e] - level) > 0.003f) { level = f.env[(size_t) e]; repaint(); } }

void EnvView::paint (juce::Graphics& g)
{
    using namespace P;
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (6, 8);
    const float dl = proc.getParam (env (e, eDelay)), at = proc.getParam (env (e, eAttack)), ho = proc.getParam (env (e, eHold));
    const float de = proc.getParam (env (e, eDecay)), su = proc.getParam (env (e, eSustain)), re = proc.getParam (env (e, eRelease));
    const float ac = proc.getParam (env (e, eACurve)), dc = proc.getParam (env (e, eDCurve)), rc = proc.getParam (env (e, eRCurve));
    const float sustainLen = 0.25f * (dl + at + ho + de + re) + 0.1f;
    const float total = dl + at + ho + de + sustainLen + re + 1e-3f;
    auto X = [&] (float t) { return in.getX() + in.getWidth() * t / total; };
    auto Y = [&] (float v) { return in.getBottom() - in.getHeight() * v; };
    juce::Path p;
    p.startNewSubPath (X (0), Y (0));
    p.lineTo (X (dl), Y (0));
    for (int i = 1; i <= 24; ++i) p.lineTo (X (dl + at * i / 24.0f), Y (curveWarp (i / 24.0f, ac)));
    p.lineTo (X (dl + at + ho), Y (1));
    for (int i = 1; i <= 24; ++i) p.lineTo (X (dl + at + ho + de * i / 24.0f), Y (su + (1 - su) * (1 - curveWarp (i / 24.0f, dc))));
    const float rs = dl + at + ho + de + sustainLen;
    p.lineTo (X (rs), Y (su));
    for (int i = 1; i <= 24; ++i) p.lineTo (X (rs + re * i / 24.0f), Y (su * (1 - curveWarp (i / 24.0f, rc))));
    juce::Path fill (p);
    fill.lineTo (X (total), Y (0)); fill.closeSubPath();
    g.setColour (col::accent.withAlpha (0.12f + 0.2f * level));
    g.fillPath (fill);
    g.setColour (col::accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setColour (col::phosphor);
    g.fillRect (in.getX(), Y (level) - 1, in.getWidth(), 1.0f);
}

// ---------------------------------------------------------------- LfoView
void LfoView::tick (const Frame& f) { if (std::abs (f.lfo[(size_t) l] - value) > 0.005f) { value = f.lfo[(size_t) l]; repaint(); } }

void LfoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (6, 8);
    LfoState st;
    st.r0 = -0.6f; st.r1 = 0.7f; st.sh = 0.4f;
    const int shape = (int) proc.getParam (P::lfo (l, P::lShape));
    const auto& table = proc.getPatterns().lfoTable[(size_t) l];
    juce::Path p;
    for (int i = 0; i <= 200; ++i)
    {
        const float ph = (float) i / 200.0f;
        float v = st.value (shape, ph, table);
        if (shape == lsSampleHold) v = std::sin ((float) std::floor (ph * 8) * 2.1f);
        const float x = in.getX() + in.getWidth() * ph, y = in.getCentreY() - v * in.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col::accent3.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    g.setColour (col::accent3);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setColour (col::phosphor);
    g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre ({ in.getRight() - 6, in.getCentreY() - value * in.getHeight() * 0.45f }));
    if (shape == lsCustom)
    {
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        g.drawText (L ("dibuja con el ratón", "draw with the mouse"), in, juce::Justification::bottomLeft);
    }
}

void LfoView::draw (const juce::MouseEvent& e)
{
    if ((int) proc.getParam (P::lfo (l, P::lShape)) != lsCustom) return;
    auto in = getLocalBounds().toFloat().reduced (6, 8);
    auto pats = proc.getPatterns();
    auto& t = pats.lfoTable[(size_t) l];
    const int x = juce::jlimit (0, kLfoTableSize - 1, (int) ((e.position.x - in.getX()) / in.getWidth() * kLfoTableSize));
    const float v = juce::jlimit (-1.0f, 1.0f, (in.getCentreY() - e.position.y) / (in.getHeight() * 0.45f));
    const int a = lastX < 0 ? x : juce::jmin (x, lastX), b = lastX < 0 ? x : juce::jmax (x, lastX);
    for (int i = a; i <= b; ++i) t[(size_t) i] = v;
    lastX = x;
    proc.setPatterns (pats);
    repaint();
}

// ---------------------------------------------------------------- StepGrid
StepGrid::StepGrid (InfinitumProcessor& p, Getter g, int ms, float l, float h, int sp, int k)
    : proc (p), get (std::move (g)), maxSteps (ms), stepsParam (sp), kind (k), lo (l), hi (h) {}

void StepGrid::tick (const Frame& f)
{
    const int s = kind == 0 ? f.arpStep : kind == 1 ? f.gateStep : f.seqStep;
    if (s != play) { play = s; repaint(); }
}

void StepGrid::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawScreen (g, r);
    auto in = r.reduced (4, label.isEmpty() ? 4.0f : 14.0f);
    if (label.isNotEmpty())
    {
        g.setColour (col::phosphor.withAlpha (0.6f));
        g.setFont (monoFont (9.5f));
        g.drawText (label, r.reduced (6, 2), juce::Justification::topLeft);
    }
    const int steps = juce::jlimit (1, maxSteps, (int) proc.getParam (stepsParam));
    auto pats = proc.getPatterns();
    const float w = in.getWidth() / (float) maxSteps;
    const bool bip = lo < 0;
    for (int i = 0; i < maxSteps; ++i)
    {
        const float v = (get (pats, i) - lo) / (hi - lo);
        auto cell = juce::Rectangle<float> (in.getX() + i * w + 1, in.getY(), w - 2, in.getHeight());
        const bool active = i < steps;
        g.setColour (i % 4 == 0 ? col::panelLo.brighter (0.15f) : col::panelLo);
        g.fillRect (cell);
        auto c = (i == play ? colour.brighter (0.5f) : colour).withAlpha (active ? 0.9f : 0.2f);
        g.setColour (c);
        if (bip)
        {
            const float mid = cell.getCentreY(), y = cell.getBottom() - v * cell.getHeight();
            g.fillRect (cell.getX(), juce::jmin (mid, y), cell.getWidth(), std::abs (y - mid) + 1);
        }
        else g.fillRect (cell.withTop (cell.getBottom() - v * cell.getHeight()));
        if (i == play) { g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawRect (cell, 1.0f); }
    }
}

void StepGrid::edit (const juce::MouseEvent& e)
{
    auto in = getLocalBounds().toFloat().reduced (4, label.isEmpty() ? 4.0f : 14.0f);
    const int i = juce::jlimit (0, maxSteps - 1, (int) ((e.position.x - in.getX()) / in.getWidth() * maxSteps));
    float v = juce::jlimit (0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight());
    v = lo + v * (hi - lo);
    if (quantize) v = std::round (v);
    if (e.mods.isRightButtonDown()) v = lo < 0 ? 0.0f : lo;
    auto pats = proc.getPatterns();
    get (pats, i) = v;
    proc.setPatterns (pats);
    repaint();
}
}
