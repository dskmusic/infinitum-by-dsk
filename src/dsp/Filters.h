#pragma once
#include "Common.h"

namespace gx
{
// Zero-delay-feedback state variable filter (Simper/TPT).
struct Svf
{
    float ic1 = 0, ic2 = 0, k = 2, a1 = 1, a2 = 0, a3 = 0;
    void set (float g, float kk) { k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    void reset() { ic1 = ic2 = 0; }
    // returns lp; bp/hp via refs
    inline float tick (float v0, float& bp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        bp = v1;
        hp = v0 - k * v1 - v2;
        return v2;
    }
};

// Nonlinear ZDF 4-pole ladder.
struct Ladder
{
    float s[4] {}, G = 0, g = 0, k = 0;
    void set (float gg, float res) { g = gg; G = g / (1.0f + g); k = res * 3.9f; }
    void reset() { for (auto& v : s) v = 0; }
    inline float tick (float x)
    {
        const float inv = 1.0f / (1.0f + g);
        const float b1 = s[0] * inv, b2 = s[1] * inv, b3 = s[2] * inv, b4 = s[3] * inv;
        const float S = G * G * G * b1 + G * G * b2 + G * b3 + b4;
        const float G4 = G * G * G * G;
        float u = (x * (1.0f + k * 0.35f) - k * S) / (1.0f + k * G4);
        u = fastTanh (u);
        float y = G * u + b1; s[0] = 2.0f * y - s[0];
        float y2 = G * y + b2; s[1] = 2.0f * y2 - s[1];
        float y3 = G * y2 + b3; s[2] = 2.0f * y3 - s[2];
        float y4 = G * y3 + b4; s[3] = 2.0f * y4 - s[3];
        return y4;
    }
};

struct Comb
{
    static constexpr int kSize = 8192;
    float buf[kSize] {};
    int w = 0;
    float delay = 100, fb = 0, damp = 0, lp = 0;
    void reset() { std::fill (std::begin (buf), std::end (buf), 0.0f); lp = 0; }
    inline float tick (float x)
    {
        float r = (float) w - delay;
        if (r < 0) r += kSize;
        const int i = (int) r;
        const float t = r - (float) i;
        const float d = buf[i & (kSize - 1)] * (1 - t) + buf[(i + 1) & (kSize - 1)] * t;
        lp = d + damp * (lp - d);
        const float y = x + fb * lp;
        buf[w] = fastTanh (y * 0.7f) * 1.428f;
        w = (w + 1) & (kSize - 1);
        return y * 0.6f;
    }
};

// One filter slot, one channel: holds state for every type so type switches are click-free enough.
struct FilterChannel
{
    Svf a, b, f1, f2, f3;
    Ladder ladder;
    Comb comb;
    int type = ftLp12;
    float drive = 0, driveGain = 1, driveComp = 1;
    float fg[3] {}, fk = 1; // formant

    void reset() { a.reset(); b.reset(); f1.reset(); f2.reset(); f3.reset(); ladder.reset(); comb.reset(); }

    // cutoff Hz, reso 0..1, vowel 0..1 (formant only)
    void set (int t, float cutoff, float reso, float drv, double sr)
    {
        type = t;
        const float nyq = (float) sr * 0.49f;
        cutoff = juce::jlimit (16.0f, nyq * 0.95f, cutoff);
        const float g = std::tan (kPi * cutoff / (float) sr);
        const float k = 2.0f - 1.96f * reso;
        drive = drv;
        driveGain = 1.0f + drv * drv * 12.0f;
        driveComp = 1.0f / (1.0f + drv * 1.5f);
        switch (t)
        {
            case ftLp12: case ftHp12: case ftBp: case ftNotch: a.set (g, k); break;
            case ftLp24: case ftHp24: a.set (g, 1.85f); b.set (g, k); break;
            case ftLadder: ladder.set (g, reso); break;
            case ftCombPos: case ftCombNeg:
                comb.delay = juce::jlimit (2.0f, (float) Comb::kSize - 4, (float) sr / cutoff);
                comb.fb = (t == ftCombPos ? 1.0f : -1.0f) * (0.2f + 0.78f * reso);
                comb.damp = 0.15f + 0.5f * (1.0f - reso);
                break;
            case ftFormant:
            {
                static const float F[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
                const float vowel = juce::jlimit (0.0f, 1.0f, std::log (cutoff / 20.0f) / std::log (1000.0f)) * 4.0f;
                const int v0 = juce::jmin (3, (int) vowel);
                const float vt = vowel - (float) v0;
                const float q = 2.0f - 1.6f * reso; // lower k = narrower
                Svf* s[3] { &f1, &f2, &f3 };
                for (int j = 0; j < 3; ++j)
                {
                    const float fr = juce::jmin (nyq * 0.9f, F[v0][j] + (F[v0 + 1][j] - F[v0][j]) * vt);
                    s[j]->set (std::tan (kPi * fr / (float) sr), q * (j == 0 ? 0.5f : 0.35f));
                }
                fk = q;
                break;
            }
            default: break;
        }
    }

    inline float tick (float x)
    {
        if (drive > 0.001f) x = fastTanh (x * driveGain) * driveComp * (1.0f + drive);
        float bp, hp;
        switch (type)
        {
            case ftLp12: return a.tick (x, bp, hp);
            case ftHp12: a.tick (x, bp, hp); return hp;
            case ftBp: a.tick (x, bp, hp); return bp * a.k;
            case ftNotch: { const float lp = a.tick (x, bp, hp); return lp + hp; }
            case ftLp24: { const float l1 = a.tick (x, bp, hp); return b.tick (l1, bp, hp); }
            case ftHp24: { a.tick (x, bp, hp); const float h1 = hp; b.tick (h1, bp, hp); return hp; }
            case ftLadder: return ladder.tick (x);
            case ftCombPos: case ftCombNeg: return comb.tick (x);
            case ftFormant:
            {
                float b1, b2, b3;
                f1.tick (x, b1, hp); f2.tick (x, b2, hp); f3.tick (x, b3, hp);
                return (b1 * 1.0f + b2 * 0.7f + b3 * 0.45f) * 1.6f;
            }
            default: return x;
        }
    }
};

// RBJ biquad (transposed direct form II) for EQ / tone shaping.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float tick (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void setNorm (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0); }
    void peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void shelf (double sr, double f, double db, bool high)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
        if (high) setNorm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq), (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        else setNorm (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq), (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void lowpass (double sr, double f, double q = 0.7071)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * juce::jmin (f, sr * 0.45) / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass (double sr, double f, double q = 0.7071)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * juce::jmin (f, sr * 0.45) / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
};
}
