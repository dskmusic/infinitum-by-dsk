#pragma once
#include "../preset/Preset.h"
#include "../Lang.h"

namespace gx
{
struct SfzOptions
{
    int stepSemis = 3, lowNote = 24, highNote = 96;
    int velLayers = 2;                 // 1..8
    double sampleRate = 44100.0;
    int bitDepth = 24;                 // 16 or 24
    double noteSeconds = 2.0;          // held part
    double releaseTail = 1.0;          // recorded after note-off
    bool loop = true;                  // loop sustained sounds
    double crossfadeSeconds = 0.05;
};

struct SfzResult
{
    bool ok = false, cancelled = false;
    juce::String error;
    juce::File sfzFile;
    int numSamples = 0;
    juce::StringArray warnings;
};

class SfzExporter
{
public:
    // Things the SFZ cannot reproduce (static samples): returned as human-readable warnings.
    static juce::StringArray unrepresentable (const Preset& p);
    // Writes <parentDir>/<preset name>/<name>.sfz + samples/*.wav
    static SfzResult exportPreset (const Preset& p, const SfzOptions& o, const juce::File& parentDir,
                                   const std::atomic<bool>* cancel = nullptr, std::function<void (float)> progress = {});
    static juce::String noteName (int midi);
    // Sanity checker used by tests: contiguous key/velocity coverage, files exist, loops in range.
    static juce::String verify (const juce::File& sfzFile);
};
}
