#include "Effects.h"

namespace gx
{
// ---------------------------------------------------------------- FDN reverb
void Fdn::prepare (double s)
{
    sr = s;
    for (auto& l : line) l.init ((int) (sr * 0.32));
    for (auto& a : ap) a.init ((int) (sr * 0.03));
    clear();
    set (0.5f, 0.3f);
}
void Fdn::clear()
{
    for (auto& l : line) l.clear();
    for (auto& a : ap) a.clear();
    lp.fill (0);
}
void Fdn::set (float size, float dampAmt)
{
    static const float base[8] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
    static const float apb[4] { 4.7f, 3.6f, 12.7f, 9.3f };
    const float scale = 0.45f + size * 1.75f;
    const float rt60 = 0.35f + size * size * 9.0f;
    for (int i = 0; i < 8; ++i)
    {
        len[(size_t) i] = base[i] * scale * 0.001f * (float) sr;
        g[(size_t) i] = std::pow (10.0f, -3.0f * len[(size_t) i] / (rt60 * (float) sr));
    }
    for (int i = 0; i < 4; ++i) apLen[(size_t) i] = apb[i] * 0.001f * (float) sr;
    damp = 0.05f + dampAmt * 0.85f;
}
void Fdn::process (float inL, float inR, float& outL, float& outR)
{
    // input diffusion
    float x = (inL + inR) * 0.5f;
    for (int i = 0; i < 4; ++i)
    {
        const float d = ap[(size_t) i].read (apLen[(size_t) i]);
        const float v = x + 0.6f * d;
        ap[(size_t) i].push (v);
        x = d - 0.6f * v;
    }
    modPh += 0.7f / (float) sr;
    if (modPh > 1) modPh -= 1;
    const float m = fastSin (modPh) * 6.0f;
    float y[8];
    for (int i = 0; i < 8; ++i)
    {
        const float rd = line[(size_t) i].read (len[(size_t) i] + (i == 1 ? m : i == 5 ? -m : 0.0f));
        lp[(size_t) i] = rd + damp * (lp[(size_t) i] - rd);
        y[i] = lp[(size_t) i] * g[(size_t) i];
    }
    // Hadamard 8x8 (fast Walsh-Hadamard), normalised
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h * 2)
            for (int j = i; j < i + h; ++j) { const float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
    const float nrm = 0.35355339f;
    for (int i = 0; i < 8; ++i) line[(size_t) i].push (y[i] * nrm + (i & 1 ? inR : inL) * 0.25f + x * 0.25f);
    outL = (lp[0] - lp[2] + lp[4] - lp[6]) * 0.5f;
    outR = (lp[1] - lp[3] + lp[5] - lp[7]) * 0.5f;
}

float PitchShifter::process (float x, float ratio)
{
    d.push (x);
    phase += (1.0f - ratio) / win;
    phase -= std::floor (phase);
    const float p2 = wrap01 (phase + 0.5f);
    const float g1 = 0.5f - 0.5f * std::cos (kTwoPi * phase), g2 = 0.5f - 0.5f * std::cos (kTwoPi * p2);
    return d.read (phase * win + 2.0f) * g1 + d.read (p2 * win + 2.0f) * g2;
}

// ---------------------------------------------------------------- Effects
void Effects::prepare (double sampleRate, int mb)
{
    sr = sampleRate; maxBlock = mb;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) mb);
    for (int c = 0; c < 2; ++c)
    {
        choD[c].init ((int) (sr * 0.05));
        flaD[c].init ((int) (sr * 0.02));
        dlyD[c].init ((int) (sr * 6.0));
        shift[c].prepare (sr);
        grnBuf[c].init ((int) (sr * 4.0));
    }
    rev.prepare (sr);
    shm.prepare (sr);
    for (auto& m : mixS) m.setTime (0.015f, sr);
    reset();
}

void Effects::reset()
{
    if (os) os->reset();
    for (int c = 0; c < 2; ++c)
    {
        choD[c].clear(); flaD[c].clear(); dlyD[c].clear(); shift[c].clear(); grnBuf[c].clear();
        distLp[c] = distDc[c] = flaFbS[c] = phaFbS[c] = dlyLp[c] = dlyHp[c] = shmFb[c] = crushHold[c] = 0;
        for (auto& z : phaZ[c]) z = 0;
        for (auto& b : eqF[(size_t) c]) b.reset();
        wideLp[c].reset();
    }
    rev.clear(); shm.clear();
    compEnv = 0; dlySmooth = -1; crushCount = 0;
    for (auto& g : grains) g.on = false;
    eqCache.fill (-1e9f);
    wasOn.fill (false);
}

void Effects::process (float* L, float* R, int n, const float* p, const std::array<int, kNumFx>& order, double bpm)
{
    static const int onParam[kNumFx] { P::distOn, P::choOn, P::flaOn, P::phaOn, P::dlyOn, P::revOn, P::cmpOn, P::eqOn, P::bitOn, P::widOn, P::shmOn, P::grnOn };
    for (int i = 0; i < kNumFx; ++i)
    {
        const int fx = juce::jlimit (0, kNumFx - 1, order[(size_t) i]);
        const bool on = p[onParam[fx]] > 0.5f;
        if (on && ! wasOn[(size_t) fx])
        {
            // clear stale tails when an effect is switched on
            if (fx == fxDelay) { dlyD[0].clear(); dlyD[1].clear(); dlySmooth = -1; }
            if (fx == fxReverb) rev.clear();
            if (fx == fxShimmer) { shm.clear(); shmFb[0] = shmFb[1] = 0; }
            if (fx == fxGranular) { grnBuf[0].clear(); grnBuf[1].clear(); for (auto& g : grains) g.on = false; }
            mixS[fx].v = 0;
        }
        wasOn[(size_t) fx] = on;
        if (! on) continue;
        switch (fx)
        {
            case fxDist: dist (L, R, n, p); break;
            case fxChorus: chorus (L, R, n, p); break;
            case fxFlanger: flanger (L, R, n, p); break;
            case fxPhaser: phaser (L, R, n, p); break;
            case fxDelay: delay (L, R, n, p, bpm); break;
            case fxReverb: reverb (L, R, n, p); break;
            case fxComp: comp (L, R, n, p); break;
            case fxEq: eq (L, R, n, p); break;
            case fxCrush: crush (L, R, n, p); break;
            case fxWidener: widen (L, R, n, p); break;
            case fxShimmer: shimmer (L, R, n, p); break;
            case fxGranular: granular (L, R, n, p); break;
            default: break;
        }
    }
}

void Effects::dist (float* L, float* R, int n, const float* p)
{
    const int type = (int) p[P::distType];
    const float drive = p[P::distDrive];
    const float g = 1.0f + drive * drive * 40.0f;
    const float comp = 1.0f / (1.0f + drive * 2.0f);
    float* chans[2] { L, R };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
    // keep dry for mix
    float dryL[2048], dryR[2048];
    const int m = juce::jmin (n, 2048);
    std::memcpy (dryL, L, sizeof (float) * (size_t) m);
    std::memcpy (dryR, R, sizeof (float) * (size_t) m);
    auto up = os->processSamplesUp (block);
    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        float* d = up.getChannelPointer (c);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            const float x = d[i] * g;
            float y;
            switch (type)
            {
                case 1: y = juce::jlimit (-1.0f, 1.0f, x); break;
                case 2: y = fastSin (x * 0.25f); break;
                case 3: y = x > 0 ? fastTanh (x) : fastTanh (x * 0.6f) * 0.9f; break;
                case 4: y = fastTanh (std::abs (x) * 1.2f) * 1.2f - 0.5f; break;
                default: y = fastTanh (x); break;
            }
            d[i] = y * (type == 1 || type == 2 ? comp * 1.5f : 1.0f);
        }
    }
    os->processSamplesDown (block);
    const float toneHz = 700.0f * std::pow (28.0f, p[P::distTone]);
    const float a = std::exp (-kTwoPi * toneHz / (float) sr);
    auto& mix = mixS[fxDist];
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.step (p[P::distMix]);
        distLp[0] = L[i] + a * (distLp[0] - L[i]);
        distLp[1] = R[i] + a * (distLp[1] - R[i]);
        const float dl = i < m ? dryL[i] : L[i], dr = i < m ? dryR[i] : R[i];
        L[i] = dl + (distLp[0] * comp * 1.4f - dl) * w;
        R[i] = dr + (distLp[1] * comp * 1.4f - dr) * w;
    }
    // remove DC introduced by the rectifier
    if (type == 4)
        for (int i = 0; i < n; ++i) { distDc[0] += 0.001f * (L[i] - distDc[0]); distDc[1] += 0.001f * (R[i] - distDc[1]); L[i] -= distDc[0]; R[i] -= distDc[1]; }
}

void Effects::chorus (float* L, float* R, int n, const float* p)
{
    const float rate = p[P::choRate], depth = p[P::choDepth] * 0.006f * (float) sr, base = 0.011f * (float) sr;
    auto& mix = mixS[fxChorus];
    for (int i = 0; i < n; ++i)
    {
        choPh += rate / (float) sr; if (choPh >= 1) choPh -= 1;
        const float w = mix.step (p[P::choMix]);
        choD[0].push (L[i]); choD[1].push (R[i]);
        const float l = choD[0].read (base + depth * (0.5f + 0.5f * fastSin (choPh))) * 0.6f + choD[1].read (base * 1.3f + depth * (0.5f + 0.5f * fastSin (choPh + 0.33f))) * 0.4f;
        const float r = choD[1].read (base + depth * (0.5f + 0.5f * fastSin (choPh + 0.25f))) * 0.6f + choD[0].read (base * 1.3f + depth * (0.5f + 0.5f * fastSin (choPh + 0.58f))) * 0.4f;
        L[i] += (l - L[i]) * w * 0.9f;
        R[i] += (r - R[i]) * w * 0.9f;
    }
}

void Effects::flanger (float* L, float* R, int n, const float* p)
{
    const float rate = p[P::flaRate], depth = p[P::flaDepth], fb = p[P::flaFb];
    auto& mix = mixS[fxFlanger];
    for (int i = 0; i < n; ++i)
    {
        flaPh += rate / (float) sr; if (flaPh >= 1) flaPh -= 1;
        const float w = mix.step (p[P::flaMix]);
        float* io[2] { &L[i], &R[i] };
        for (int c = 0; c < 2; ++c)
        {
            const float mod = 0.5f + 0.5f * fastSin (flaPh + c * 0.25f);
            const float d = (0.0005f + depth * 0.0055f * mod) * (float) sr;
            flaD[c].push (*io[c] + fastTanh (flaFbS[c] * fb));
            const float y = flaD[c].read (d);
            flaFbS[c] = y;
            *io[c] = *io[c] * (1 - w * 0.5f) + y * w * 0.7f;
        }
    }
}

void Effects::phaser (float* L, float* R, int n, const float* p)
{
    const float rate = p[P::phaRate], depth = p[P::phaDepth], fb = p[P::phaFb];
    auto& mix = mixS[fxPhaser];
    for (int i = 0; i < n; ++i)
    {
        phaPh += rate / (float) sr; if (phaPh >= 1) phaPh -= 1;
        const float w = mix.step (p[P::phaMix]);
        float* io[2] { &L[i], &R[i] };
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = 0.5f + 0.5f * fastSin (phaPh + c * 0.2f);
            const float fc = 180.0f * std::pow (22.0f, lfo * depth + (1 - depth) * 0.3f);
            const float t = std::tan (kPi * juce::jmin (fc, (float) sr * 0.45f) / (float) sr);
            const float a = (t - 1) / (t + 1);
            float x = *io[c] + phaFbS[c] * fb;
            for (int s = 0; s < 6; ++s)
            {
                const float y = a * x + phaZ[c][s];
                phaZ[c][s] = x - a * y;
                x = y;
            }
            phaFbS[c] = fastTanh (x);
            *io[c] = *io[c] + (x - *io[c]) * w * 0.5f;
        }
    }
}

void Effects::delay (float* L, float* R, int n, const float* p, double bpm)
{
    const double secs = juce::jlimit (0.005, 5.9, divisionBeats ((int) p[P::dlyDiv]) * 60.0 / juce::jmax (20.0, bpm));
    const float target = (float) (secs * sr);
    if (dlySmooth < 0) dlySmooth = target;
    const float fb = p[P::dlyFb];
    const bool ping = p[P::dlyPing] > 0.5f;
    const float lpHz = 1200.0f * std::pow (14.0f, p[P::dlyTone]);
    const float a = std::exp (-kTwoPi * lpHz / (float) sr), hpA = std::exp (-kTwoPi * 90.0f / (float) sr);
    auto& mix = mixS[fxDelay];
    for (int i = 0; i < n; ++i)
    {
        dlySmooth += (target - dlySmooth) * 0.0005f;
        const float w = mix.step (p[P::dlyMix]);
        float yl = dlyD[0].read (dlySmooth), yr = dlyD[1].read (dlySmooth);
        dlyLp[0] = yl + a * (dlyLp[0] - yl); dlyLp[1] = yr + a * (dlyLp[1] - yr);
        dlyHp[0] = dlyLp[0] + hpA * (dlyHp[0] - dlyLp[0]); dlyHp[1] = dlyLp[1] + hpA * (dlyHp[1] - dlyLp[1]);
        const float fl = dlyLp[0] - dlyHp[0], fr = dlyLp[1] - dlyHp[1];
        if (ping)
        {
            dlyD[0].push ((L[i] + R[i]) * 0.5f + fastTanh (fr * fb));
            dlyD[1].push (fastTanh (fl * fb));
        }
        else
        {
            dlyD[0].push (L[i] + fastTanh (fl * fb));
            dlyD[1].push (R[i] + fastTanh (fr * fb));
        }
        L[i] += yl * w;
        R[i] += yr * w;
    }
}

void Effects::reverb (float* L, float* R, int n, const float* p)
{
    rev.set (p[P::revSize], p[P::revDamp]);
    const float width = p[P::revWidth];
    auto& mix = mixS[fxReverb];
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.step (p[P::revMix]);
        float l, r;
        rev.process (L[i], R[i], l, r);
        const float m = (l + r) * 0.5f, s = (l - r) * 0.5f * width;
        L[i] = L[i] * (1 - w * 0.5f) + (m + s) * w;
        R[i] = R[i] * (1 - w * 0.5f) + (m - s) * w;
    }
}

void Effects::comp (float* L, float* R, int n, const float* p)
{
    const float th = p[P::cmpThresh], ratio = p[P::cmpRatio], mk = dbToGain (p[P::cmpMakeup]);
    const float at = std::exp (-1.0f / (p[P::cmpAttack] * 0.001f * (float) sr));
    const float rl = std::exp (-1.0f / (p[P::cmpRelease] * 0.001f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float lvl = juce::jmax (std::abs (L[i]), std::abs (R[i]));
        compEnv = lvl > compEnv ? lvl + at * (compEnv - lvl) : lvl + rl * (compEnv - lvl);
        const float db = 20.0f * std::log10 (compEnv + 1.0e-9f);
        const float over = db - th, knee = 6.0f;
        float red = 0;
        if (over > knee * 0.5f) red = over * (1 - 1 / ratio);
        else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; red = (1 - 1 / ratio) * x * x / (2 * knee); }
        const float g = dbToGain (-red) * mk;
        L[i] *= g; R[i] *= g;
    }
}

void Effects::eq (float* L, float* R, int n, const float* p)
{
    const float v[9] { p[P::eqLoF], p[P::eqLoG], p[P::eqM1F], p[P::eqM1G], p[P::eqM2F], p[P::eqM2G], p[P::eqHiF], p[P::eqHiG], p[P::eqQ] };
    bool changed = false;
    for (int i = 0; i < 9; ++i) changed |= v[i] != eqCache[(size_t) i];
    if (changed)
    {
        for (int i = 0; i < 9; ++i) eqCache[(size_t) i] = v[i];
        for (int c = 0; c < 2; ++c)
        {
            eqF[(size_t) c][0].shelf (sr, v[0], v[1], false);
            eqF[(size_t) c][1].peak (sr, v[2], v[8], v[3]);
            eqF[(size_t) c][2].peak (sr, juce::jmin ((double) v[4], sr * 0.45), v[8], v[5]);
            eqF[(size_t) c][3].shelf (sr, juce::jmin ((double) v[6], sr * 0.45), v[7], true);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        for (int b = 0; b < 4; ++b) { if (std::abs (v[b * 2 + 1]) > 0.01f) { l = eqF[0][(size_t) b].tick (l); r = eqF[1][(size_t) b].tick (r); } }
        L[i] = l; R[i] = r;
    }
}

void Effects::crush (float* L, float* R, int n, const float* p)
{
    const float q = std::exp2 (p[P::bitBits] - 1.0f), ds = p[P::bitRate];
    auto& mix = mixS[fxCrush];
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.step (p[P::bitMix]);
        crushCount += 1.0f;
        if (crushCount >= ds) { crushCount -= ds; crushHold[0] = std::round (L[i] * q) / q; crushHold[1] = std::round (R[i] * q) / q; }
        L[i] += (crushHold[0] - L[i]) * w;
        R[i] += (crushHold[1] - R[i]) * w;
    }
}

void Effects::widen (float* L, float* R, int n, const float* p)
{
    const float width = p[P::widWidth];
    if (p[P::widBassMono] != wideCacheF) { wideCacheF = p[P::widBassMono]; for (auto& b : wideLp) b.lowpass (sr, wideCacheF); }
    for (int i = 0; i < n; ++i)
    {
        const float m = (L[i] + R[i]) * 0.5f;
        float s = (L[i] - R[i]) * 0.5f;
        const float sLow = wideLp[0].tick (s);
        s = (s - sLow) * width;
        L[i] = m + s; R[i] = m - s;
    }
}

void Effects::shimmer (float* L, float* R, int n, const float* p)
{
    shm.set (0.55f + p[P::shmSize] * 0.45f, 0.35f);
    const float amt = p[P::shmAmount] * 0.65f;
    auto& mix = mixS[fxShimmer];
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.step (p[P::shmMix]);
        const float sl = shift[0].process (shmFb[0], 2.0f), sr2 = shift[1].process (shmFb[1], 2.0f);
        float l, r;
        shm.process (L[i] + sl * amt, R[i] + sr2 * amt, l, r);
        shmFb[0] = fastTanh (l); shmFb[1] = fastTanh (r);
        L[i] = L[i] * (1 - w * 0.4f) + l * w;
        R[i] = R[i] * (1 - w * 0.4f) + r * w;
    }
}

void Effects::granular (float* L, float* R, int n, const float* p)
{
    const float size = p[P::grnSize] * 0.001f * (float) sr;
    const float density = p[P::grnDensity], spread = p[P::grnSpread], jitter = p[P::grnPitch];
    const float interval = juce::jmax (32.0f, size * (1.1f - density) * 0.5f);
    auto& mix = mixS[fxGranular];
    for (int i = 0; i < n; ++i)
    {
        const float w = mix.step (p[P::grnMix]);
        grnTimer -= 1.0f;
        if (grnTimer <= 0)
        {
            grnTimer = interval * (0.6f + 0.8f * rng.uni());
            for (auto& g : grains)
                if (! g.on)
                {
                    g.on = true; g.t = 0; g.len = size * (0.7f + 0.6f * rng.uni());
                    const float semis = jitter * 12.0f * rng.bi();
                    g.inc = std::exp2 (semis / 12.0f) * (rng.uni() < spread * 0.25f ? -1.0f : 1.0f);
                    g.pos = g.len * 2.0f + 8.0f + rng.uni() * spread * (float) sr * 1.5f;
                    const float pan = rng.bi() * 0.8f;
                    g.panL = std::cos ((pan + 1) * kPi * 0.25f) * 1.414f; g.panR = std::sin ((pan + 1) * kPi * 0.25f) * 1.414f;
                    break;
                }
        }
        float gl = 0, gr = 0;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float env = 0.5f - 0.5f * std::cos (kTwoPi * g.t / g.len);
            const float d = juce::jlimit (2.0f, (float) grnBuf[0].mask - 4.0f, g.pos);
            gl += grnBuf[0].read (d) * env * g.panL;
            gr += grnBuf[1].read (d) * env * g.panR;
            g.pos += 1.0f - g.inc; // moving read head
            g.t += 1.0f;
            if (g.t >= g.len) g.on = false;
        }
        const float norm = 0.35f;
        grnBuf[0].push (L[i] + fastTanh (gl * spread * 0.3f));
        grnBuf[1].push (R[i] + fastTanh (gr * spread * 0.3f));
        L[i] = L[i] * (1 - w * 0.5f) + gl * norm * w;
        R[i] = R[i] * (1 - w * 0.5f) + gr * norm * w;
    }
}
}
