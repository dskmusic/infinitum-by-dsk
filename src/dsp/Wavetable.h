#pragma once
#include "Common.h"
#include <memory>
#include <vector>

namespace gx
{
// Band-limited, mip-mapped wavetable. Each frame stores kLevels versions; level L keeps
// at most (kMaxHarm >> L) harmonics in max(64, kBaseLen >> L) samples (+1 guard sample).
class Wavetable
{
public:
    static constexpr int kBaseLen = 2048, kMaxHarm = 512, kLevels = 10;

    // Time-domain frames of any length (resampled to kBaseLen), normalised as a whole.
    static std::unique_ptr<Wavetable> fromFrames (const std::vector<std::vector<float>>& frames, juce::String name);
    // Harmonic amplitude spectra (index 1 = fundamental), phases all sine.
    static std::unique_ptr<Wavetable> fromSpectra (const std::vector<std::vector<float>>& spectra, juce::String name);
    // Import from audio: split into cycles of 'cycleLen' samples (2048 = Serum-style).
    static std::unique_ptr<Wavetable> fromAudio (const float* data, int numSamples, int cycleLen, juce::String name);

    int numFrames() const { return frames; }
    const juce::String& getName() const { return name; }

    static int levelFor (float maxHarmonics)
    {
        int L = 0;
        while (L < kLevels - 1 && (float) (kMaxHarm >> L) > maxHarmonics) ++L;
        return L;
    }
    // phase in cycles [0,1), pos 0..1 across frames
    float read (float phase, float pos, int level) const
    {
        const float fp = juce::jlimit (0.0f, 1.0f, pos) * (float) (frames - 1);
        const int f0 = (int) fp;
        const int f1 = juce::jmin (f0 + 1, frames - 1);
        const float ft = fp - (float) f0;
        const int n = len[(size_t) level];
        const float x = phase * (float) n;
        const int i = juce::jlimit (0, n - 1, (int) x);
        const float t = x - (float) i;
        const float* a = data.data() + (size_t) f0 * stride + (size_t) offset[(size_t) level];
        const float* b = data.data() + (size_t) f1 * stride + (size_t) offset[(size_t) level];
        const float va = a[i] + t * (a[i + 1] - a[i]);
        const float vb = b[i] + t * (b[i + 1] - b[i]);
        return va + ft * (vb - va);
    }
    // Raw level-0 frame for display
    const float* frameData (int f) const { return data.data() + (size_t) juce::jlimit (0, frames - 1, f) * stride; }
    // Original level-0 frames (time-domain) for serialisation
    std::vector<std::vector<float>> exportFrames() const;

private:
    Wavetable() = default;
    void build (const std::vector<std::vector<float>>& spectraRe, const std::vector<std::vector<float>>& spectraIm);
    int frames = 0;
    size_t stride = 0;
    std::array<int, kLevels> len {}, offset {};
    std::vector<float> data;
    juce::String name;
};

// Built-in procedural tables, shared by all instances (built once, read-only).
const Wavetable& builtinTable (int index);
int numBuiltinTables();

// Harmonic spectra used by the Spectral Mutation oscillator (archetype "genomes").
constexpr int kGenomeHarm = 96, kNumArchetypes = 8;
const std::array<std::array<float, kGenomeHarm>, kNumArchetypes>& genomeArchetypes();
}
