#pragma once
#include "Common.h"

namespace gx
{
// c in -1..1: >0 fast start (exponential-like), <0 slow start
inline float curveWarp (float t, float c)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    if (std::abs (c) < 0.01f) return t;
    return c > 0 ? 1.0f - std::pow (1.0f - t, 1.0f + 4.0f * c) : std::pow (t, 1.0f - 4.0f * c);
}

// DAHDSR envelope evaluated at control rate.
struct Envelope
{
    enum Stage { Idle, Delay, Attack, Hold, Decay, Sustain, Release };
    Stage stage = Idle;
    float t = 0, value = 0, start = 0;
    float delay = 0, attack = 0.01f, hold = 0, decay = 0.3f, sustain = 1, release = 0.3f, ac = 0, dc = 0.5f, rc = 0.5f;

    void setParams (const float* p, int base)
    {
        using namespace P;
        delay = p[base + eDelay]; attack = p[base + eAttack]; hold = p[base + eHold];
        decay = p[base + eDecay]; release = p[base + eRelease];
        ac = p[base + eACurve]; dc = p[base + eDCurve]; rc = p[base + eRCurve];
    }
    void setSustain (float s) { sustain = juce::jlimit (0.0f, 1.0f, s); }
    void noteOn (bool fromCurrent) { start = fromCurrent ? value : 0.0f; if (! fromCurrent) value = 0; t = 0; stage = delay > 0.0005f ? Delay : Attack; }
    void noteOff() { if (stage != Idle && stage != Release) { start = value; t = 0; stage = Release; } }
    void kill() { stage = Idle; value = 0; }
    bool active() const { return stage != Idle; }

    // advance by dt seconds; returns new value
    float advance (float dt)
    {
        auto next = [this] (Stage s) { stage = s; t = 0; };
        switch (stage)
        {
            case Idle: value = 0; break;
            case Delay: t += dt; if (t >= delay) next (Attack); break;
            case Attack:
                if (attack < 0.0005f) { value = 1; next (hold > 0.0005f ? Hold : Decay); break; }
                t += dt / attack;
                value = start + (1.0f - start) * curveWarp (t, ac);
                if (t >= 1) { value = 1; next (hold > 0.0005f ? Hold : Decay); }
                break;
            case Hold: value = 1; t += dt; if (t >= hold) next (Decay); break;
            case Decay:
                t += dt / juce::jmax (0.0005f, decay);
                value = sustain + (1.0f - sustain) * (1.0f - curveWarp (t, dc));
                if (t >= 1) { value = sustain; next (Sustain); }
                break;
            case Sustain: value = sustain; break;
            case Release:
                t += dt / juce::jmax (0.0005f, release);
                value = start * (1.0f - curveWarp (t, rc));
                if (t >= 1 || value < 1.0e-5f) { value = 0; next (Idle); }
                break;
        }
        return value;
    }
};

// LFO waveform evaluation with random state.
struct LfoState
{
    float phase = 0, r0 = 0, r1 = 0, sh = 0;
    Rng rng { 777 };
    void wrapped() { r0 = r1; r1 = rng.bi(); sh = rng.bi(); }
    // Advance phase by inc cycles; returns value -1..1
    float tick (int shape, float inc, const std::array<float, kLfoTableSize>& table)
    {
        phase += inc;
        if (phase >= 1.0f) { phase -= std::floor (phase); wrapped(); }
        return value (shape, phase, table);
    }
    float value (int shape, float ph, const std::array<float, kLfoTableSize>& table) const
    {
        switch (shape)
        {
            case lsSine: return fastSin (ph);
            case lsTri: return 1.0f - 4.0f * std::abs (ph - 0.5f) ;
            case lsSawUp: return 2.0f * ph - 1.0f;
            case lsSawDown: return 1.0f - 2.0f * ph;
            case lsSquare: return ph < 0.5f ? 1.0f : -1.0f;
            case lsSampleHold: return sh;
            case lsSmoothRandom: { const float w = 0.5f - 0.5f * std::cos (kPi * ph); return r0 + (r1 - r0) * w; }
            case lsCustom:
            {
                const float x = ph * kLfoTableSize;
                const int i = (int) x % kLfoTableSize;
                const float f = x - std::floor (x);
                return table[(size_t) i] + f * (table[(size_t) ((i + 1) % kLfoTableSize)] - table[(size_t) i]);
            }
            default: return 0;
        }
    }
};
}
