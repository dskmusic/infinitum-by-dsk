#pragma once
#include "Common.h"
#include "Envelope.h"
#include "Filters.h"
#include "Wavetable.h"
#include "Rhythm.h"
#include "Effects.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace gx
{
struct SampleData
{
    std::vector<float> data; // mono
    double sampleRate = 44100.0;
    int rootNote = 60;
    juce::String name;
};

struct Transport
{
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
};

// Values published to the UI (relaxed atomics, written once per block).
struct Telemetry
{
    std::atomic<uint32_t> voiceMask { 0 };
    std::atomic<int> activeVoices { 0 }, lastNote { -1 }, arpStep { -1 }, gateStep { -1 }, seqStep { -1 };
    std::atomic<float> lastVelocity { 0 }, gateLevel { 1 }, seqValue { 0 };
    std::array<std::atomic<float>, kNumEnv> env {};
    std::array<std::atomic<float>, kNumLfo> lfo {};
    std::array<std::atomic<float>, kNumOsc> oscMorph {};
    std::array<std::atomic<float>, kNumFilters> cutoff {};
    std::array<std::atomic<int>, kMaxVoices> voiceNote {};
    std::array<std::atomic<float>, kMaxVoices> voiceLevel {};
    std::array<std::atomic<float>, kNumMacros> macro {};
};

constexpr int kMaxDests = 256, kFxBlock = 512;

struct MutationState
{
    float table[2][513] {};
    int cur = 0, counter = 0;
    float dom[kGenomeHarm] {}, target[kGenomeHarm] {}, speed[kGenomeHarm] {}, phase[kGenomeHarm] {};
    Rng rng { 99 };
};

struct OscRuntime
{
    float ph[kMaxUnison] {}, mph[kMaxUnison] {}, prev[kMaxUnison] {};
    float fm[kMaxUnison][4] {}, fb[kMaxUnison] {};
    float smp[kMaxUnison] {};
    float pink[7] {}, brown = 0, color = 0, crackle = 0, digVal = 0, digPh = 0;
    MutationState mut;
};

class Engine;

struct Voice
{
    bool active = false, released = false, sustained = false;
    int note = 60, channel = 1, age = 0;
    float velocity = 1, curNote = 60, targetNote = 60, randomVal = 0, pressure = 0, slide = 0, bend = 0;
    float timeOn = 0, ampPrev = 0;
    std::array<Envelope, kNumEnv> env;
    std::array<LfoState, kNumLfo> lfo;
    std::array<OscRuntime, kNumOsc> osc;
    std::array<std::array<FilterChannel, 2>, kNumFilters> filt;
    std::array<float, kNumFilters> cutoffSmooth {};
    float modN[kMaxDests] {}; // normalized modulation per destination index
    std::array<float, kNumLfo> lastLfo {};
    Rng rng { 1 };
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const float* params,
                  const Patterns& pat, const Transport& tr, const std::array<const Wavetable*, kNumOsc>& userTables,
                  const SampleData* sample);
    void allNotesOff();
    Telemetry tele;
    double getSampleRate() const { return sr; }

    // note pipeline entry points (used by Rhythm)
    void voiceNoteOn (int note, float vel, int channel);
    void voiceNoteOff (int note, int channel);

private:
    friend class Rhythm;
    void handleMidi (const juce::MidiMessage& m);
    void inputNoteOn (int note, float vel, int channel);
    void inputNoteOff (int note, int channel);
    void startVoice (Voice& v, int note, float vel, int channel, bool legato);
    void renderVoice (Voice& v, float* L, float* R, int n);
    void computeMods (Voice& v, int n);
    float pv (const Voice& v, int param) const; // modulated plain value
    void rebuildMutation (Voice& v, int o, float f0, float morph, float chaos, float rate, bool both);
    void updateGlobalLfos (int n, double beat);

    double sr = 48000.0;
    int maxBlock = 512;
    std::vector<Voice> voices;
    std::vector<float> busL, busR;
    const float* p = nullptr;
    const Patterns* pat = nullptr;
    std::array<const Wavetable*, kNumOsc> tables {};
    const SampleData* sample = nullptr;

    // global controller state
    float modWheel = 0, aftertouch = 0, pitchBend = 0, slideGlobal = 0;
    bool sustainPedal = false;
    int ageCounter = 0;
    float lastPlayedNote = -1;
    std::array<int, 64> monoStack {};
    int monoCount = 0;

    // mod matrix per block
    std::array<int, kNumModSlots> slotSrc {}, slotDst {};
    std::array<float, kNumModSlots> slotAmt {};
    std::vector<float> normBase;     // per destination
    std::vector<int> destOfParam;    // param -> dest index
    std::vector<int> activeDests;
    std::array<float, kNumLfo> globalLfo {};
    std::array<LfoState, kNumLfo> globalLfoState {};
    float globalSrc[msNumSources] {};
    std::vector<float> gp;           // globally-modulated parameter view (FX, rhythm)
    int numDests = 1, destDefsFirstVirtual = 1;
    double curBpm = 120.0;
    float lastVelocity = 0.8f;
    bool nanReset = false;
    float dcL = 0, dcR = 0, dcXL = 0, dcXR = 0;

    juce::dsp::FFT mutFft { 9 };
    std::vector<float> fftBuf;
    Rng rng { 4242 };

    Rhythm rhythm;
    Effects fx;
    Smooth masterGain;
    float gateGain = 1;
};
}
