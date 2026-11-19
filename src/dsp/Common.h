#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "../Params.h"

namespace gx
{
constexpr float kTwoPi = 6.283185307179586f;
constexpr float kPi = 3.141592653589793f;
constexpr int kControlBlock = 16; // modulation/control rate granularity in samples

inline float noteToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }
inline float dbToGain (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float fastTanh (float x)
{
    x = juce::jlimit (-4.5f, 4.5f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float wrap01 (float x) { return x - std::floor (x); }

// Fast sine via 2048-point table with linear interpolation; phase in cycles.
struct SineTable
{
    static constexpr int N = 2048;
    float t[N + 1];
    SineTable() { for (int i = 0; i <= N; ++i) t[i] = std::sin (kTwoPi * (float) i / N); }
    static const SineTable& get() { static const SineTable s; return s; }
};
inline float fastSin (float phaseCycles)
{
    const float p = wrap01 (phaseCycles) * SineTable::N;
    const int i = (int) p;
    const float f = p - (float) i;
    const float* t = SineTable::get().t;
    return t[i] + f * (t[i + 1] - t[i]);
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bi() { return uni() * 2.0f - 1.0f; }                          // [-1,1)
};

// One-pole smoother toward a target (per-sample).
struct Smooth
{
    float v = 0, coef = 0.0f;
    void setTime (float seconds, double sr) { coef = seconds <= 0 ? 0.0f : std::exp (-1.0f / (float) (seconds * sr)); }
    float step (float target) { v = target + coef * (v - target); return v; }
};

// Arp/gate/seq/LFO-table data edited on the message thread, read by the audio thread through a seqlock.
struct ArpStep { float on = 1, vel = 1, gate = 1, ratchet = 1, prob = 1, transpose = 0; };
struct Patterns
{
    std::array<ArpStep, kMaxArpSteps> arp {};
    std::array<float, kMaxGateSteps> gate {};
    std::array<float, kMaxSeqSteps> seq {};
    std::array<std::array<float, kLfoTableSize>, kNumLfo> lfoTable {};
    std::array<int, kNumFx> fxOrder {};
    Patterns()
    {
        for (int i = 0; i < kMaxGateSteps; ++i) gate[(size_t) i] = (i % 2 == 0) ? 1.0f : 0.0f;
        for (int i = 0; i < kMaxSeqSteps; ++i) seq[(size_t) i] = 0.0f;
        for (auto& t : lfoTable)
            for (int i = 0; i < kLfoTableSize; ++i) t[(size_t) i] = std::sin (kTwoPi * (float) i / kLfoTableSize);
        for (int i = 0; i < kNumFx; ++i) fxOrder[(size_t) i] = i;
    }
};

class SharedPatterns
{
public:
    void write (const Patterns& p)
    {
        seq.fetch_add (1, std::memory_order_acq_rel);
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy ((void*) &data, &p, sizeof (Patterns));
        std::atomic_thread_fence (std::memory_order_release);
        seq.fetch_add (1, std::memory_order_acq_rel);
    }
    // Returns false (keeping 'out' unchanged) if a write was in progress for too long.
    bool read (Patterns& out) const
    {
        for (int tries = 0; tries < 8; ++tries)
        {
            const auto s0 = seq.load (std::memory_order_acquire);
            if (s0 & 1u) continue;
            std::memcpy ((void*) &tmp, &data, sizeof (Patterns));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_acquire) == s0) { std::memcpy ((void*) &out, &tmp, sizeof (Patterns)); return true; }
        }
        return false;
    }
    uint32_t version() const { return seq.load (std::memory_order_acquire); }

private:
    Patterns data;
    mutable Patterns tmp;
    std::atomic<uint32_t> seq { 0 };
};
} // namespace gx
