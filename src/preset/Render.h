#pragma once
#include "Preset.h"
#include "../dsp/Engine.h"

namespace gx
{
// Renders a preset with a private engine (no APVTS, no host notifications). Deterministic.
// Safe from any thread. 'cancel' (optional) aborts early and returns false.
bool renderPreset (const Preset& preset, const juce::MidiBuffer& midi, double sampleRate, int blockSize,
                   juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel = nullptr);

// Convenience: one note (velocity 0..1), released at 'releaseAt' seconds.
juce::AudioBuffer<float> renderNote (const Preset& preset, int note, float velocity, double seconds,
                                     double releaseAt, double sampleRate = 48000.0);
}
