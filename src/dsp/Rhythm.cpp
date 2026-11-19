#include "Rhythm.h"
#include "Engine.h"
#include <algorithm>

namespace gx
{

void Rhythm::reset()
{
    numHeld = physicalDown = numPending = arpSeqLen = arpNoteIdx = 0;
    lastArpStep = lastGateStep = lastSeqStep = -1;
    internalBeat = curBeat = 0;
    gateTarget = 1; seqValue = seqSmooth = 0;
    arpStep = gateStep = seqStep = -1;
    arpDirty = true;
}

void Rhythm::schedule (double atBeat, int note, float vel, int ch, bool on)
{
    if (numPending < (int) pending.size())
        pending[(size_t) numPending++] = { atBeat, note, vel, ch, on };
}

void Rhythm::noteOn (Engine& e, const float* p, int note, float vel, int ch)
{
    const bool arp = p[P::arpOn] > 0.5f;
    if (physicalDown == 0 && numHeld == 0)
    {
        // restart the free-running clock so patterns start on the first note
        for (int i = 0; i < numPending; ++i) pending[(size_t) i].atBeat -= internalBeat;
        curBeat -= internalBeat;
        internalBeat = 0;
        lastArpStep = -1;
    }
    ++physicalDown;

    const int mode = (int) p[P::chordMode];
    chordUsed[(size_t) (note & 127)] = mode;
    int notes[8]; int count = 0;
    notes[count++] = note;
    for (int iv : chordIntervals (mode))
        if (count < 8 && note + iv < 128) notes[count++] = note + iv;

    if (arp)
    {
        if (p[P::arpLatch] > 0.5f && physicalDown == 1) numHeld = 0;
        for (int i = 0; i < count; ++i)
        {
            bool dup = false;
            for (int h = 0; h < numHeld; ++h) dup |= held[(size_t) h].note == notes[i];
            if (! dup && numHeld < (int) held.size()) held[(size_t) numHeld++] = { notes[i], vel, orderCounter++ };
        }
        arpDirty = true;
        return;
    }
    const double strumBeats = p[P::strum] * 0.001 * (lastBpm / 60.0);
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && strumBeats > 1.0e-6) schedule (curBeat + strumBeats * i, notes[i], vel, ch, true);
        else e.voiceNoteOn (notes[i], vel, ch);
    }
}

void Rhythm::noteOff (Engine& e, const float* p, int note, int ch)
{
    physicalDown = juce::jmax (0, physicalDown - 1);
    int notes[8]; int count = 0;
    notes[count++] = note;
    for (int iv : chordIntervals (chordUsed[(size_t) (note & 127)]))
        if (count < 8 && note + iv < 128) notes[count++] = note + iv;

    if (p[P::arpOn] > 0.5f)
    {
        if (p[P::arpLatch] > 0.5f) return;
        for (int i = 0; i < count; ++i)
            for (int h = 0; h < numHeld; ++h)
                if (held[(size_t) h].note == notes[i]) { held[(size_t) h] = held[(size_t) --numHeld]; break; }
        arpDirty = true;
        return;
    }
    for (int i = 0; i < count; ++i)
    {
        for (int k = 0; k < numPending; ++k)
            if (pending[(size_t) k].on && pending[(size_t) k].note == notes[i]) { pending[(size_t) k] = pending[(size_t) --numPending]; --k; }
        e.voiceNoteOff (notes[i], ch);
    }
}

void Rhythm::allOff (Engine& e)
{
    numPending = numHeld = physicalDown = 0;
    e.allNotesOff();
}

void Rhythm::rebuildArpOrder (const float* p)
{
    arpDirty = false;
    std::array<Held, 32> s = held;
    const int mode = (int) p[P::arpMode];
    if (mode == 5) std::sort (s.begin(), s.begin() + numHeld, [] (auto& a, auto& b) { return a.order < b.order; });
    else std::sort (s.begin(), s.begin() + numHeld, [] (auto& a, auto& b) { return a.note < b.note; });
    const int oct = juce::jlimit (1, 4, (int) p[P::arpOct]);
    int base[128]; int nb = 0;
    for (int o = 0; o < oct; ++o)
        for (int i = 0; i < numHeld && nb < 128; ++i)
            if (s[(size_t) i].note + 12 * o < 128) base[nb++] = s[(size_t) i].note + 12 * o;
    arpSeqLen = 0;
    auto push = [this] (int n) { if (arpSeqLen < 128) arpSeq[(size_t) arpSeqLen++] = n; };
    switch (mode)
    {
        case 1: for (int i = nb - 1; i >= 0; --i) push (base[i]); break;                       // down
        case 2: for (int i = 0; i < nb; ++i) push (base[i]); for (int i = nb - 2; i > 0; --i) push (base[i]); break; // up-down
        case 3: for (int i = nb - 1; i >= 0; --i) push (base[i]); for (int i = 1; i < nb - 1; ++i) push (base[i]); break; // down-up
        case 7: for (int lo = 0, hi = nb - 1; lo <= hi; ++lo, --hi) { push (base[lo]); if (hi != lo) push (base[hi]); } break; // converge
        default: for (int i = 0; i < nb; ++i) push (base[i]); break;                            // up / random / as played / chord
    }
}

void Rhythm::arpStepFire (Engine& e, const float* p, const Patterns& pt, int k, double stepBeat, double L)
{
    const int steps = juce::jlimit (1, kMaxArpSteps, (int) p[P::arpSteps]);
    const int idx = k % steps;
    arpStep = idx;
    const auto& st = pt.arp[(size_t) idx];
    const float h = p[P::humanize];
    if (st.on < 0.5f || rng.uni() > st.prob || rng.uni() < h * 0.1f) return;
    if (arpDirty) rebuildArpOrder (p);
    if (arpSeqLen == 0) return;

    int r = juce::jlimit (1, 4, (int) std::round (st.ratchet));
    if (rng.uni() < h * 0.15f) r = juce::jmin (4, r * 2);
    const double gateLen = juce::jmax (0.01, L * p[P::arpGate] * st.gate / r);
    const int mode = (int) p[P::arpMode];
    const int tr = (int) std::round (st.transpose);

    auto play = [&] (int note, float vel)
    {
        note = juce::jlimit (0, 127, note + tr);
        for (int j = 0; j < r; ++j)
        {
            const double on = stepBeat + j * L / r;
            const float v = juce::jlimit (0.05f, 1.0f, vel * st.vel * (1.0f - h * 0.3f * rng.uni()) * (j > 0 ? 0.85f : 1.0f));
            if (on <= curBeat + 1.0e-9) e.voiceNoteOn (note, v, 1); else schedule (on, note, v, 1, true);
            schedule (on + gateLen, note, 0, 1, false);
        }
    };
    if (mode == 6)
    {
        for (int i = 0; i < numHeld; ++i) play (held[(size_t) i].note, held[(size_t) i].vel);
        return;
    }
    const int sel = mode == 4 ? (int) (rng.next() % (uint32_t) arpSeqLen) : (arpNoteIdx++ % arpSeqLen);
    const int note = arpSeq[(size_t) sel];
    float vel = 0.8f;
    for (int i = 0; i < numHeld; ++i) if (held[(size_t) i].note % 12 == note % 12) vel = held[(size_t) i].vel;
    play (note, vel);
}

void Rhythm::tick (Engine& e, const float* p, const Patterns& pt, const double beatStart, double bps, int n)
{
    curBeat = beatStart;
    lastBpm = bps * e.getSampleRate() * 60.0;
    const double beatEnd = beatStart + bps * n;

    // ---- arpeggiator
    const bool arpOn = p[P::arpOn] > 0.5f;
    if (wasArpOn && ! arpOn)
    {
        for (int i = 0; i < numPending; ++i)
            if (! pending[(size_t) i].on) e.voiceNoteOff (pending[(size_t) i].note, pending[(size_t) i].ch);
        numPending = 0; numHeld = 0; arpStep = -1;
    }
    wasArpOn = arpOn;
    if (arpOn && numHeld > 0)
    {
        const double L = divisionBeats ((int) p[P::arpRate]);
        const double swing = p[P::arpSwing] * L * 0.5;
        auto stepStart = [&] (long long k) { return (double) k * L + ((k & 1) ? swing : 0.0); };
        if (lastArpStep < 0 || stepStart (lastArpStep) > beatEnd) lastArpStep = (long long) std::floor (beatStart / L) - 1;
        for (int guard = 0; guard < 4 && stepStart (lastArpStep + 1) <= beatEnd; ++guard)
        {
            ++lastArpStep;
            arpStepFire (e, p, pt, (int) (lastArpStep % 1000000), juce::jmax (beatStart, stepStart (lastArpStep)), L);
        }
    }
    else if (arpOn) { lastArpStep = -1; arpStep = -1; }

    // ---- scheduled notes (strum, arp gates/ratchets)
    for (int i = 0; i < numPending; ++i)
    {
        auto ev = pending[(size_t) i];
        if (ev.atBeat <= beatEnd)
        {
            pending[(size_t) i] = pending[(size_t) --numPending]; --i;
            if (ev.on) e.voiceNoteOn (ev.note, ev.vel, ev.ch); else e.voiceNoteOff (ev.note, ev.ch);
        }
    }

    // ---- trance gate
    if (p[P::tgOn] > 0.5f)
    {
        const double L = divisionBeats ((int) p[P::tgRate]);
        const int steps = juce::jlimit (1, kMaxGateSteps, (int) p[P::tgSteps]);
        const long long k = (long long) std::floor (beatStart / L);
        gateStep = (int) (((k % steps) + steps) % steps);
        gateTarget = 1.0f - p[P::tgDepth] * (1.0f - juce::jlimit (0.0f, 1.0f, pt.gate[(size_t) gateStep]));
    }
    else { gateTarget = 1.0f; gateStep = -1; }

    // ---- step sequencer (modulation source)
    {
        const double L = divisionBeats ((int) p[P::seqRate]);
        const int steps = juce::jlimit (1, kMaxSeqSteps, (int) p[P::seqSteps]);
        const long long k = (long long) std::floor (beatStart / L);
        seqStep = (int) (((k % steps) + steps) % steps);
        const float target = pt.seq[(size_t) seqStep];
        const float g = p[P::seqGlide];
        if (g < 0.001f) seqSmooth = target;
        else seqSmooth += (target - seqSmooth) * (float) (1.0 - std::exp (-(bps * n) / (g * L * 0.5)));
        seqValue = seqSmooth;
    }
}
}
