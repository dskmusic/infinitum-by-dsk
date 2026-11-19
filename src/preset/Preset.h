#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../dsp/Common.h"

class InfinitumProcessor;

namespace gx
{
constexpr int kPresetFormatVersion = 1;

struct PresetMeta
{
    juce::String uuid, name = "Init", category = "Lead", subcategory, author = "DSK Music", notes;
    juce::StringArray tags;
    bool favorite = false, factory = false;
    int rating = 0, uses = 0;
    juce::Time created, modified;
    juce::String folder;      // user sub-folder (relative), empty = root
    juce::File file;          // user preset file (empty for factory presets)
};

// Full preset content (everything needed to reproduce a sound).
struct Preset
{
    PresetMeta meta;
    std::vector<float> params;                 // plain values, indexed like paramDefs()
    Patterns patterns;
    std::array<std::vector<std::vector<float>>, kNumOsc> wavetables; // user tables (empty = none)
    std::array<juce::String, kNumOsc> wavetableNames;
    std::vector<float> sample; double sampleRate = 44100; int sampleRoot = 60; juce::String sampleName;

    Preset();                                   // = init patch (all defaults)
    juce::var toVar (bool includeMeta = true) const;
    static Preset fromVar (const juce::var& v); // migrates older versions, ignores unknown ids
    juce::String toJson() const;
    static bool fromJson (const juce::String& json, Preset& out);
    bool saveTo (const juce::File& f) const;
    static bool loadFrom (const juce::File& f, Preset& out);

    static Preset capture (InfinitumProcessor& p);
    void applyTo (InfinitumProcessor& p) const;

    // structural similarity 0..1 (1 = identical), used for duplicate detection
    float similarity (const Preset& other) const;
};

// Category tree shared by the generator, the manager and the factory bank.
const juce::StringArray& categories();
const juce::StringArray& subcategories (const juce::String& category);
const juce::StringArray& characterTags();
}
