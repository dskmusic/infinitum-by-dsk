#pragma once
#include "../preset/Preset.h"
#include <set>

namespace gx
{
struct GenRequest
{
    juce::String category = "Lead";
    juce::String subcategory;            // empty = pick one musically
    float brightness = 0.5f, movement = 0.5f, aggression = 0.35f, space = 0.4f, weirdness = 0.2f;
    uint32_t seed = 1;
    std::set<juce::String> locks;        // "osc", "filter", "env", "mod", "fx", "rhythm"
    const Preset* base = nullptr;        // source for locked sections
};

// "How interesting does it sound" metrics, measured on a rendered note/chord.
struct Interest
{
    float flux = 0;        // short-term spectral movement (dB per frame, ~0.3 static .. 6+ very busy)
    float evolution = 0;   // long-term change: start vs end of the hold (dB)
    float width = 0;       // stereo width: side/mid RMS ratio (0 mono .. ~1 very wide)
    float richness = 0;    // share of the 100 Hz-12 kHz bands within 36 dB of the loudest (0..1)
    float attackMs = 0;    // time to reach 90% of the loudest 50 ms window
    float score = 0;       // category-weighted interest score (higher = better), filled by the generator
};

struct Validation
{
    bool ok = false;
    float rmsDb = -100, peakDb = -100, dc = 0, sustainRatio = 0, tailDb = -100, centroidHz = 0, flatness = 0;
    juce::String reason;
    Interest interest;                   // measured on the validation render (v1.1)
};


// Rule-based, musically informed preset generator with offline validation.
class Generator
{
public:
    // Generates a validated preset; regenerates (new sub-seed) until it passes (max 'maxAttempts').
    // v1.1: collects up to 'pickBest' valid candidates and keeps the most interesting one (category-aware score).
    static Preset generate (const GenRequest& req, int* attempts = nullptr, Validation* result = nullptr, int maxAttempts = 24, int pickBest = 6);
    static float interestScore (const Preset& p, const Validation& v);
    static Preset mutate (const Preset& p, float amount, uint32_t seed, const std::set<juce::String>& locks = {});
    static Preset blend (const Preset& a, const Preset& b, float t, uint32_t seed);

    // Renders the preset, checks it and corrects the master level (target RMS, peak <= -1 dBFS).
    static Validation validate (Preset& p, bool fixLevel = true);

    // Measures movement / width / richness / attack of a rendered buffer over its first 'holdSec' seconds.
    static Interest measureInterest (const juce::AudioBuffer<float>& b, double sampleRate, double holdSec);

    static juce::StringArray deriveTags (const Preset& p, const GenRequest& req);
    static juce::String makeName (const juce::String& category, const juce::String& sub, Rng& rng);

    // Builds one unvalidated candidate (exposed for tests).
    static Preset candidate (const GenRequest& req, uint32_t seed);
    static int testNote (const juce::String& category);
};

// Copies every parameter of the given lock groups from 'src' into 'dst'.
void applyLocks (Preset& dst, const Preset& src, const std::set<juce::String>& locks);
bool paramInLock (const ParamDef& d, const juce::String& lock);
}
