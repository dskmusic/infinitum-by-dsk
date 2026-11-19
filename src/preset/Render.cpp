#include "Render.h"

namespace gx
{
bool renderPreset (const Preset& preset, const juce::MidiBuffer& midi, double sr, int bs,
                   juce::AudioBuffer<float>& out, const std::atomic<bool>* cancel)
{
    auto eng = std::make_unique<Engine>();
    eng->prepare (sr, bs);
    std::array<std::unique_ptr<Wavetable>, kNumOsc> owned;
    std::array<const Wavetable*, kNumOsc> ut {};
    for (int o = 0; o < kNumOsc; ++o)
        if (! preset.wavetables[(size_t) o].empty())
        {
            owned[(size_t) o] = Wavetable::fromFrames (preset.wavetables[(size_t) o], preset.wavetableNames[(size_t) o]);
            ut[(size_t) o] = owned[(size_t) o].get();
        }
    std::unique_ptr<SampleData> smp;
    if (! preset.sample.empty())
    {
        smp = std::make_unique<SampleData>();
        smp->data = preset.sample; smp->sampleRate = preset.sampleRate; smp->rootNote = preset.sampleRoot;
    }
    auto pats = std::make_unique<Patterns> (preset.patterns);
    Transport tr;
    tr.bpm = 120.0;
    tr.playing = true;
    juce::AudioBuffer<float> block (2, bs);
    for (int pos = 0; pos < out.getNumSamples(); pos += bs)
    {
        if (cancel != nullptr && cancel->load()) return false;
        const int n = juce::jmin (bs, out.getNumSamples() - pos);
        block.setSize (2, n, false, false, true);
        juce::MidiBuffer m;
        m.addEvents (midi, pos, n, -pos);
        tr.ppq = (double) pos / sr * 2.0;
        eng->process (block, m, preset.params.data(), *pats, tr, ut, smp.get());
        for (int c = 0; c < juce::jmin (2, out.getNumChannels()); ++c) out.copyFrom (c, pos, block, c, 0, n);
    }
    return true;
}

juce::AudioBuffer<float> renderNote (const Preset& preset, int note, float vel, double seconds, double releaseAt, double sr)
{
    juce::AudioBuffer<float> out (2, (int) (seconds * sr));
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
    m.addEvent (juce::MidiMessage::noteOff (1, note), (int) (releaseAt * sr));
    renderPreset (preset, m, sr, 256, out);
    return out;
}
}
