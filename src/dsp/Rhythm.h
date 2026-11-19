#pragma once
#include "Common.h"

namespace gx
{
class Engine;

// Chord/strum, arpeggiator, trance gate and step sequencer clocks.
class Rhythm
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset();

    // Input notes (after MIDI parsing). Handles chord expansion, strum and arp routing.
    void noteOn (Engine& e, const float* p, int note, float vel, int ch);
    void noteOff (Engine& e, const float* p, int note, int ch);
    void allOff (Engine& e);

    // Advance one control sub-block of n samples.
    void tick (Engine& e, const float* p, const Patterns& pat, const double beatStart, double beatsPerSample, int n);

    double clockBeat() const { return internalBeat; }
    void advanceClock (double beats) { internalBeat += beats; }

    float gateTarget = 1.0f, seqValue = 0.0f;
    int arpStep = -1, gateStep = -1, seqStep = -1;

private:
    struct Held { int note; float vel; int order; };
    struct Pending { double atBeat; int note; float vel; int ch; bool on; };

    void rebuildArpOrder (const float* p);
    void arpStepFire (Engine& e, const float* p, const Patterns& pat, int stepIndex, double stepBeat, double stepLen);
    void schedule (double atBeat, int note, float vel, int ch, bool on);

    double sr = 48000.0, internalBeat = 0.0;
    std::array<Held, 32> held {};
    int numHeld = 0, physicalDown = 0, orderCounter = 0;
    std::array<int, 128> arpSeq {};
    int arpSeqLen = 0, arpNoteIdx = 0;
    bool arpDirty = true, wasArpOn = false;
    long long lastArpStep = -1, lastGateStep = -1, lastSeqStep = -1;
    std::array<Pending, 128> pending {};
    int numPending = 0;
    double curBeat = 0;
    Rng rng { 31337 };
    float seqSmooth = 0;
    double lastBpm = 120.0;
    std::array<int, 128> chordUsed {};
};
}
