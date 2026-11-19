#include "Engine.h"

namespace gx
{
namespace
{
inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline float hashUni (uint32_t a, uint32_t b) { return (float) (hash32 (hash32 (a * 0x9E3779B1u) ^ (b + 0x85ebca6bu)) >> 8) * (1.0f / 16777216.0f); }
inline float smoothstep (float a, float b, float x) { const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a)); return t * t * (3 - 2 * t); }
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float saw (float& ph, float inc)
{
    ph += inc;
    if (ph >= 1.0f) ph -= 1.0f;
    return 2.0f * ph - 1.0f - polyBlep (ph, inc);
}
inline float coefFor (float seconds, double rate) { return seconds <= 0 ? 0.0f : std::exp (-1.0f / (float) (seconds * rate)); }
inline void panGains (float pan, float& l, float& r) // equal power, pan -1..1
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * kPi * 0.25f;
    l = std::cos (a) * 1.41421356f; r = std::sin (a) * 1.41421356f;
}
// Scale degree (any integer, may exceed the scale length) -> semitones from the root.
inline int degreeSemis (const std::vector<int>& s, int d)
{
    const int m = (int) s.size();
    const int oct = d >= 0 ? d / m : -((-d + m - 1) / m);
    return s[(size_t) (d - oct * m)] + 12 * oct;
}
} // namespace

// ================================================================ setup
void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = mb;
    fftBuf.assign (1024, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        grnBuf[c].init ((int) (sr * 5.0));
        dly[c].init ((int) (sr * 2.5));
        for (auto& s : shift[c]) s.prepare (sr);
    }
    shm.prepare (sr);
    rev.prepare (sr);
    for (auto* s : { &widthS, &warmthS, &dlyMixS, &revMixS, &masterS }) s->setTime (0.05f, sr);
    limRel = coefFor (0.15f, sr);
    reset (seed);
}

void Engine::reset (uint32_t s)
{
    seed = s == 0 ? 1u : s;
    ctrlLeft = 0;
    musT = evoT = sessionT = 0;
    runGain = 0; runInc = 0;
    silentFor = 0;
    idle = true;
    for (size_t i = 0; i < evoFreq.size(); ++i)
    {
        static const float mult[3] { 1.0f, 1.6180339f, 2.6457513f };
        evoFreq[i] = (1.0f / 120.0f) * mult[i % 3] * (0.72f + 0.56f * hashUni (seed, (uint32_t) i * 3 + 1));
        evoPhase[i] = kTwoPi * hashUni (seed, (uint32_t) i * 3 + 2);
    }
    presence.fill (0); gainNow.fill (0); gainInc.fill (0); toneEff.fill (0.5f); energyAcc.fill (0);
    keyOverride = -1; curDegree = -1; curRoot = -1; chordSlot = -1; bellSlot = -1;
    // drone
    mut = MutOsc();
    mut.rng = Rng (seed * 7919u + 3u);
    for (int k = 0; k < kGenomeHarm; ++k)
    {
        mut.dom[k] = mut.target[k] = mut.rng.uni();
        mut.speed[k] = 0.3f + mut.rng.uni();
        mut.phase[k] = mut.rng.uni();
    }
    for (int i = 0; i < 5; ++i) dronePh[i] = hashUni (seed, 100u + (uint32_t) i);
    for (auto& f : droneF) f.reset();
    droneBreath = 1;
    // pad
    for (int v = 0; v < kPadVoices; ++v)
    {
        auto& vc = pad[(size_t) v];
        vc = PadVoice();
        for (int k = 0; k < 4; ++k) vc.ph[k] = hashUni (seed, 200u + (uint32_t) (v * 4 + k));
        vc.midi = v >= kChordVoices;
        vc.attackCoef = coefFor (vc.midi ? 0.25f : 2.2f, sr);
        vc.releaseCoef = coefFor (vc.midi ? 2.5f : 3.0f, sr);
        vc.pan = vc.midi ? 0.0f : ((float) v / (kChordVoices - 1) * 2.0f - 1.0f) * 0.7f;
    }
    padAge = 0;
    // texture
    for (auto& b : grnBuf) b.clear();
    for (auto& g : grains) g = Grain();
    grnTimer = 0;
    grnRng = Rng (seed * 31u + 555u);
    // shimmer
    shm.clear();
    for (auto& c : shift) for (auto& ps : c) ps.clear();
    shmFb[0] = shmFb[1] = 0;
    for (auto& b : shmDamp) b.reset();
    // wind
    for (auto& c : pink) for (auto& v : c) v = 0;
    for (auto& c : windF) for (auto& f : c) f.reset();
    windRng = Rng (seed * 13u + 31337u);
    windGust = 0;
    // bells
    for (auto& b : bells) b = Bell();
    bellRng = Rng (seed * 17u + 7u);
    // master
    for (auto& d : dly) d.clear();
    dlyLp[0] = dlyLp[1] = 0;
    rev.clear();
    for (int c = 0; c < 2; ++c) { lowCutF[c].reset(); highCutF[c].reset(); tiltF[c].reset(); dcX[c] = dcY[c] = 0; }
    limGain = 1;
    for (int i = 0; i < kChordVoices; ++i) chordNotes[(size_t) i] = 0;
}

// ================================================================ evolution / harmony
float Engine::evoNoise (int ch, double t) const
{
    const size_t b = (size_t) juce::jlimit (0, kNumLayers * 4 - 1, ch) * 3;
    const double s = 0.55 * std::sin ((double) kTwoPi * evoFreq[b] * t + evoPhase[b])
                   + 0.30 * std::sin ((double) kTwoPi * evoFreq[b + 1] * t + evoPhase[b + 1])
                   + 0.15 * std::sin ((double) kTwoPi * evoFreq[b + 2] * t + evoPhase[b + 2]);
    return (float) (0.5 + 0.5 * s);
}

int Engine::degreeRaw (int64_t slot) const
{
    if (slot <= 0) return 0;
    const auto& s = scaleSteps ((int) pv (P::scale));
    const int m = (int) s.size();
    const float harmony = pv (P::harmony);
    // weights: tonic and the "pillar" degrees first; colour degrees grow with Harmony
    float w[8] {};
    for (int d = 0; d < m; ++d) w[d] = 0.6f + 0.8f * harmony;
    w[0] = 3.0f;
    if (m == 7) { w[3] = 2.0f; w[4] = 1.6f; w[5] = 1.8f; w[2] = 1.2f; w[1] = 0.4f + 0.8f * harmony; w[6] = 0.2f + 0.6f * harmony; }
    float total = 0;
    for (int d = 0; d < m; ++d) total += w[d];
    float u = hashUni (seed ^ 0xC0FFEEu, (uint32_t) (slot * 7 + 1)) * total;
    for (int d = 0; d < m; ++d) { u -= w[d]; if (u <= 0) return d; }
    return 0;
}

int Engine::chordDegreeFor (int64_t slot) const
{
    if (pv (P::harmony) < 0.12f) return 0;
    const int m = (int) scaleSteps ((int) pv (P::scale)).size();
    int d = degreeRaw (slot);
    if (slot > 0 && d == degreeRaw (slot - 1)) d = (d + (m == 7 ? 3 : 2)) % m; // never the same chord twice in a row
    return d;
}

void Engine::updateChord (bool force)
{
    const double period = 6.0 * std::pow (8.0, 1.0 - (double) pv (P::chordRate));
    const int64_t slot = (int64_t) std::floor (musT / period);
    rootPc = keyOverride >= 0 ? keyOverride : (int) pv (P::key);
    if (! force && slot == chordSlot && rootPc == curRoot) return;
    chordSlot = slot;
    curRoot = rootPc;
    const auto& s = scaleSteps ((int) pv (P::scale));
    const float harmony = pv (P::harmony);
    const int d = chordDegreeFor (slot);
    curDegree = d;
    int tones[5];
    int k = 0;
    if (harmony < 0.25f) { tones[k++] = degreeSemis (s, d); tones[k++] = degreeSemis (s, d) + 7; }  // open fifths
    else
    {
        tones[k++] = degreeSemis (s, d);
        tones[k++] = degreeSemis (s, d + 2);
        tones[k++] = degreeSemis (s, d + 4);
        if (harmony > 0.45f) tones[k++] = degreeSemis (s, d + 6);
        if (harmony > 0.80f) tones[k++] = degreeSemis (s, d + 8);
    }
    const int base = 12 * ((int) pv (P::octave) + 2) + rootPc;
    for (int i = 0; i < kChordVoices; ++i)
    {
        int n = base + tones[i % k] + 12 * (i / k);
        while (n > base + 26) n -= 12;   // keep the pad inside ~2 octaves
        chordNotes[(size_t) i] = n;
    }
    std::sort (chordNotes.begin(), chordNotes.end());
    for (int i = 0; i < kChordVoices; ++i) pad[(size_t) i].target = (float) chordNotes[(size_t) i];
    if (force) for (int i = 0; i < kChordVoices; ++i) pad[(size_t) i].note = pad[(size_t) i].target;
    tele.rootPc.store (rootPc);
    tele.chordDegree.store (d);
    for (int i = 0; i < kChordVoices; ++i) tele.chordNotes[(size_t) i].store (chordNotes[(size_t) i]);
}

// ================================================================ drone: spectral mutation (from Genesix)
void Engine::rebuildMutation (float f0, float morph, float chaos, float rate, float tilt)
{
    auto& m = mut;
    const auto& arch = genomeArchetypes();
    const int maxK = juce::jlimit (1, kGenomeHarm - 1, (int) ((float) sr * 0.45f / juce::jmax (1.0f, f0)));
    const float pos = juce::jlimit (0.0f, 1.0f, morph) * (kNumArchetypes - 1);
    const int a = juce::jmin (kNumArchetypes - 2, (int) pos);
    const float t = pos - (float) a;
    const float dt = (float) kMutPeriod / (float) sr;
    const float r = 0.03f + rate * rate * 6.0f;
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int k = 1; k <= maxK; ++k)
    {
        m.dom[k] += (m.target[k] - m.dom[k]) * juce::jmin (1.0f, m.speed[k] * r * dt);
        if (std::abs (m.target[k] - m.dom[k]) < 0.03f || m.rng.uni() < r * dt * 0.25f) m.target[k] = m.rng.uni();
        const float cross = juce::jlimit (0.0f, 1.0f, t + (m.dom[k] - 0.5f) * (0.6f + chaos));
        const float base = arch[(size_t) a][(size_t) k] + (arch[(size_t) a + 1][(size_t) k] - arch[(size_t) a][(size_t) k]) * cross;
        const float gene = juce::jmax (0.0f, 1.0f + chaos * 2.5f * (m.dom[k] - 0.5f));
        const float amp = base * gene * std::pow ((float) k, -tilt);
        m.phase[k] = wrap01 (m.phase[k] + chaos * 0.01f * (m.dom[k] - 0.5f));
        const float ph = m.phase[k] * kTwoPi;
        fftBuf[(size_t) k * 2] = amp * std::sin (ph);
        fftBuf[(size_t) k * 2 + 1] = -amp * std::cos (ph);
    }
    mutFft.performRealOnlyInverseTransform (fftBuf.data());
    float peak = 1.0e-6f;
    for (int i = 0; i < kMutTable; ++i) peak = juce::jmax (peak, std::abs (fftBuf[(size_t) i]));
    const int dst = 1 - m.cur;
    for (int i = 0; i < kMutTable; ++i) m.table[dst][i] = fftBuf[(size_t) i] / peak;
    m.table[dst][kMutTable] = m.table[dst][0];
    m.cur = dst;
    m.counter = 0;
}

// ================================================================ events
void Engine::triggerBell (int64_t slot)
{
    const auto& s = scaleSteps ((int) pv (P::scale));
    const float tone = toneEff[lyBells];
    Bell* b = nullptr;
    for (auto& x : bells) if (! x.on) { b = &x; break; }
    if (b == nullptr) { b = &bells[0]; for (auto& x : bells) if (x.amp < b->amp) b = &x; }
    const uint32_t h = (uint32_t) slot;
    // mostly chord tones, sometimes a scale colour note; two upper octaves
    const int deg = (hashUni (seed ^ 0xBE11u, h * 3 + 1) < 0.7f ? curDegree + 2 * (int) (hashUni (seed, h * 3 + 2) * 3.0f)
                                                                 : (int) (hashUni (seed, h * 3 + 2) * (float) s.size()));
    const int note = 12 * ((int) pv (P::octave) + 4) + rootPc + degreeSemis (s, deg) + (hashUni (seed, h * 3 + 3) < 0.35f ? 12 : 0);
    const float f = noteToHz ((float) note, a4);
    const float ratio = tone < 0.5f ? 2.0f + tone * 2.0f : 3.5f + (tone - 0.5f) * 7.0f;
    b->on = true;
    b->ph = b->mph = 0;
    b->inc = f / (float) sr;
    b->minc = f * ratio / (float) sr;
    b->idx = 0.6f + tone * 3.0f;
    b->idxCoef = coefFor (0.25f + 0.6f * (1.0f - tone), sr);
    b->amp = 0.18f + 0.12f * bellRng.uni();
    b->ampCoef = coefFor (1.6f + 3.5f * pv (P::space), sr);
    const float pan = (bellRng.uni() * 2.0f - 1.0f) * 0.8f;
    panGains (pan, b->panL, b->panR);
    tele.bellCount.fetch_add (1);
    tele.lastBellNote.store (note);
    tele.lastBellPan.store (pan);
}

void Engine::spawnGrain()
{
    Grain* g = nullptr;
    for (auto& x : grains) if (! x.on) { g = &x; break; }
    if (g == nullptr) return;
    static const float ratios[6] { 1.0f, 2.0f, 1.5f, 0.5f, 3.0f, 4.0f };
    const float tone = grnTone;
    const int maxIdx = juce::jlimit (1, 6, 2 + (int) (tone * 4.5f));
    const int idx = juce::jmin (maxIdx - 1, (int) (std::pow (grnRng.uni(), 1.6f - tone) * (float) maxIdx));
    float inc = ratios[idx];
    if (grnRng.uni() < 0.12f * grnChar) inc = -inc; // occasional reversed grain
    g->len = grnLen * (0.7f + 0.6f * grnRng.uni());
    g->inc = inc;
    const float bufLen = (float) grnBuf[0].mask;
    float pos = 0.05f * (float) sr + grnRng.uni() * (float) sr * (0.5f + 2.5f * grnChar);
    if (inc > 1.0f) pos = juce::jmax (pos, g->len * (inc - 1.0f) + 64.0f);
    if (inc < 0.0f) pos = juce::jmin (pos, bufLen - g->len * (1.0f - inc) - 64.0f);
    g->pos = juce::jlimit (4.0f, bufLen - 4.0f, pos);
    g->t = 0;
    panGains ((grnRng.uni() * 2.0f - 1.0f) * 0.85f, g->panL, g->panR);
    g->on = true;
}

void Engine::handleMidi (const juce::MidiMessage& m)
{
    const int mode = (int) pv (P::midiMode);
    if (mode == mmOff) return;
    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (mode == mmKey || mode == mmKeyPlay) keyOverride = note % 12;
        if (mode == mmPlay || mode == mmKeyPlay)
        {
            PadVoice* v = nullptr;
            for (int i = kChordVoices; i < kPadVoices; ++i) if (pad[(size_t) i].midiNote == note) v = &pad[(size_t) i];
            if (v == nullptr) for (int i = kChordVoices; i < kPadVoices; ++i) if (pad[(size_t) i].midiNote < 0 && pad[(size_t) i].gain < 0.002f) { v = &pad[(size_t) i]; break; }
            if (v == nullptr) { v = &pad[kChordVoices]; for (int i = kChordVoices; i < kPadVoices; ++i) if (pad[(size_t) i].age < v->age) v = &pad[(size_t) i]; }
            if (v->gain < 0.01f) v->note = (float) note;
            v->target = (float) note;
            v->midiNote = note;
            v->gainTarget = 0.35f + 0.65f * m.getFloatVelocity();
            v->age = ++padAge;
        }
    }
    else if (m.isNoteOff())
    {
        for (int i = kChordVoices; i < kPadVoices; ++i)
            if (pad[(size_t) i].midiNote == m.getNoteNumber()) { pad[(size_t) i].midiNote = -1; pad[(size_t) i].gainTarget = 0; }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (int i = kChordVoices; i < kPadVoices; ++i) { pad[(size_t) i].midiNote = -1; pad[(size_t) i].gainTarget = 0; }
    }
}

// ================================================================ control tick
void Engine::control()
{
    const float dt = (float) kCtrl / (float) sr;
    const bool frozen = pv (P::freeze) > 0.5f;
    const float evoRate = 0.15f * std::pow (30.0f, pv (P::evolution));
    if (running && ! frozen) { musT += dt; evoT += dt * evoRate; }
    if (running) sessionT += dt;
    a4 = (int) pv (P::tuning) == 1 ? 432.0f : 440.0f;
    lightMul = std::pow (2.0f, (pv (P::light) - 0.5f) * 3.0f);
    updateChord (curDegree < 0);

    // ---- layer presence (smooth gates over slow incommensurate noise) and evolving tone
    const float density = pv (P::density);
    const float evoDepth = 0.35f + 0.65f * pv (P::evolution);
    const float gainCoef = coefFor (1.2f, sr / kCtrl);
    for (int l = 0; l < kNumLayers; ++l)
    {
        const float pres = juce::jlimit (0.0f, 1.0f, lp (l, P::lPresence) + (density - 0.5f) * 0.5f);
        float gate;
        if (pres >= 0.99f) gate = 1.0f;
        else if (pres <= 0.01f) gate = 0.0f;
        else
        {
            const float th = 1.0f - pres;
            gate = smoothstep (th - 0.14f, th + 0.14f, evoNoise (l * 4, evoT));
        }
        presence[(size_t) l] = gate + gainCoef * (presence[(size_t) l] - gate);
        const float on = lp (l, P::lOn) > 0.5f ? 1.0f : 0.0f;
        const float lvl = lp (l, P::lLevel);
        const float target = on * lvl * lvl * presence[(size_t) l] * (l == lyDrone ? droneBreath : 1.0f);
        gainInc[(size_t) l] = (target - gainNow[(size_t) l]) / (float) kCtrl;
        toneEff[(size_t) l] = juce::jlimit (0.0f, 1.0f, lp (l, P::lTone) + (evoNoise (l * 4 + 1, evoT) - 0.5f) * 0.5f * evoDepth
                                                        + (pv (P::light) - 0.5f) * 0.3f);
        tele.presence[(size_t) l].store (presence[(size_t) l] * on);
    }

    const int oct = (int) pv (P::octave);
    // ---- drone
    {
        const float droneNote = (float) (12 * (oct + 1) + rootPc);
        const float f0 = noteToHz (droneNote, a4);
        const float ch = lp (lyDrone, P::lChar);
        const float tone = toneEff[lyDrone];
        if (mut.counter >= kMutPeriod)
            rebuildMutation (f0 * 1.5f, 0.1f + 0.8f * evoNoise (lyDrone * 4 + 2, evoT), ch, 0.2f + 0.8f * ch, (1.0f - tone) * 0.9f);
        const float det = 0.0008f + ch * 0.0025f;
        droneInc[0] = f0 * (1 - det) / (float) sr;
        droneInc[1] = f0 * (1 + det) / (float) sr;
        droneInc[2] = f0 * 1.4983f * (1 + det * 0.7f) / (float) sr;
        droneInc[3] = f0 * 1.4983f * (1 - det * 0.7f) / (float) sr;
        droneInc[4] = f0 * 0.5f / (float) sr;
        const float cut = juce::jlimit (80.0f, (float) sr * 0.45f, 220.0f * std::pow (2.0f, tone * 6.0f) * lightMul);
        for (auto& f : droneF) f.set (std::tan (kPi * cut / (float) sr), 1.3f);
        droneBreath = 0.75f + 0.25f * evoNoise (lyDrone * 4 + 3, evoT * 3.0);
    }
    // ---- pad
    {
        const float ch = lp (lyPad, P::lChar);
        const float glide = coefFor (1.5f + 2.5f * (1.0f - pv (P::chordRate)), sr / kCtrl);
        const float cents = 3.0f + ch * 20.0f;
        const float tone = toneEff[lyPad];
        for (int v = 0; v < kPadVoices; ++v)
        {
            auto& pvc = pad[(size_t) v];
            pvc.note = pvc.target + glide * (pvc.note - pvc.target);
            if (! pvc.midi) pvc.gainTarget = 1.0f;
            const float f = noteToHz (pvc.note, a4);
            const float d[4] { -cents, cents * 0.15f, cents, -1200.0f };
            for (int k = 0; k < 4; ++k) pvc.inc[k] = juce::jmin (0.45f, f * std::exp2 (d[k] / 1200.0f) / (float) sr);
            const float wob = evoNoise (lyPad * 4 + 2, evoT * 2.0 + v * 17.0);
            const float cut = juce::jlimit (60.0f, (float) sr * 0.45f, f * (1.5f + 10.0f * tone * tone) * lightMul * (0.75f + 0.5f * wob));
            for (auto& flt : pvc.f) flt.set (std::tan (kPi * cut / (float) sr), 1.5f);
        }
    }
    // ---- texture
    {
        grnTone = toneEff[lyTexture];
        grnChar = lp (lyTexture, P::lChar);
        grnLen = (0.08f + 0.6f * grnChar) * (float) sr;
        grnInterval = (0.03f + 0.22f * grnChar) * (float) sr * (1.4f - density * 0.8f);
        grnRecord = ! frozen;
    }
    // ---- shimmer
    {
        shmBlend = lp (lyShimmer, P::lChar);
        const float space = pv (P::space);
        shm.set (0.6f + 0.4f * space, 0.25f + 0.5f * (1.0f - toneEff[lyShimmer]), frozen ? 3.0f : 1.0f);
        shmGainFb = 0.35f + 0.3f * space + (frozen ? 0.2f : 0.0f);
        for (auto& b : shmDamp) b.lowpass (sr, 2500.0 + 9000.0 * toneEff[lyShimmer] * juce::jmin (1.5f, lightMul));
    }
    // ---- wind
    {
        const float tone = toneEff[lyWind], ch = lp (lyWind, P::lChar);
        const float gust = evoNoise (lyWind * 4 + 2, musT * 9.0);
        windGust = 0.25f + 0.75f * std::pow (gust, 1.0f + ch * 2.0f);
        const float c1 = juce::jlimit (60.0f, (float) sr * 0.4f, 250.0f * std::pow (2.0f, tone * 4.0f) * lightMul * (0.6f + 0.8f * gust));
        const float k = 1.6f - 1.4f * ch;
        for (int c = 0; c < 2; ++c)
        {
            windF[c][0].set (std::tan (kPi * c1 * (c ? 1.07f : 0.94f) / (float) sr), k);
            windF[c][1].set (std::tan (kPi * juce::jmin ((float) sr * 0.4f, c1 * 2.6f) / (float) sr), k * 1.3f);
        }
    }
    // ---- bells: one chance per 1/8 s slot of musical time
    {
        const int64_t slot = (int64_t) std::floor (musT * 8.0);
        if (slot != bellSlot)
        {
            const bool fresh = bellSlot < 0;
            bellSlot = slot;
            const float ch = lp (lyBells, P::lChar);
            const float prob = (0.015f + 0.3f * ch * ch) * (0.3f + 1.4f * density) * presence[lyBells];
            if (! fresh && running && lp (lyBells, P::lOn) > 0.5f && hashUni (seed ^ 0xB0B0u, (uint32_t) slot) < prob) triggerBell (slot);
        }
    }
    // ---- master section (cached coefficient updates)
    {
        const float lc = pv (P::lowCut), hc = pv (P::highCut), tilt = (pv (P::light) - 0.5f) * 8.0f;
        if (lc != cacheLow) { cacheLow = lc; for (auto& b : lowCutF) b.highpass (sr, lc); }
        if (hc != cacheHigh) { cacheHigh = hc; for (auto& b : highCutF) b.lowpass (sr, hc); }
        if (tilt != cacheTilt) { cacheTilt = tilt; for (auto& b : tiltF) b.shelf (sr, 2500.0, tilt, true); }
        const float space = pv (P::space), damp = pv (P::revDamp), fr = frozen ? 1.0f : 0.0f;
        if (space != cacheRev || damp != cacheDamp || fr != cacheFreeze)
        {
            cacheRev = space; cacheDamp = damp; cacheFreeze = fr;
            rev.set (0.35f + 0.75f * space, damp, frozen ? 2.5f : 1.0f);
        }
        dlySamples = juce::jlimit (16.0f, (float) dly[0].mask - 16.0f, pv (P::dlyTime) * 0.001f * (float) sr);
    }
    tele.evoTime.store ((float) evoT);
    tele.musTime.store ((float) musT);
}

// ================================================================ audio
void Engine::renderChunk (float* L, float* R, int n)
{
    const float sources = runGain > 0.0f || running ? 1.0f : 0.0f;
    const bool frozen = pv (P::freeze) > 0.5f;
    const float shimIn = frozen ? 0.25f : 1.0f;
    const float dlyFb = pv (P::dlyFb), dlyToneC = 0.15f + 0.8f * pv (P::dlyTone);
    const float runTarget = running ? 1.0f : 0.0f;
    const float fadeIn = juce::jmax (0.05f, pv (P::fadeIn)), fadeOut = juce::jmax (0.05f, pv (P::fadeOut));
    runInc = running ? 1.0f / (fadeIn * (float) sr) : -1.0f / (fadeOut * (float) sr);
    const auto& tbl = mut.table;
    const float droneOn = lp (lyDrone, P::lOn), padOnF = lp (lyPad, P::lOn);
    // texture and shimmer listen to drone + pad even when those layers are muted in the mix
    const bool feed = (lp (lyTexture, P::lOn) > 0.5f && gainNow[lyTexture] > 1.0e-5f) || (lp (lyShimmer, P::lOn) > 0.5f && gainNow[lyShimmer] > 1.0e-5f);
    const float windCh = lp (lyWind, P::lChar);

    for (int i = 0; i < n; ++i)
    {
        runGain = juce::jlimit (0.0f, 1.0f, runGain + runInc);
        if ((runInc > 0 && runGain >= runTarget) || (runInc < 0 && runGain <= runTarget)) runGain = runTarget;
        for (int l = 0; l < kNumLayers; ++l) gainNow[(size_t) l] += gainInc[(size_t) l];
        const float rg = runGain * runGain * (3.0f - 2.0f * runGain); // S-curve fade

        float dl = 0, dr = 0, padL = 0, padR = 0, midiL = 0, midiR = 0, feedL = 0, feedR = 0;
        // ---- drone
        if (sources > 0 && ((droneOn > 0.5f && gainNow[lyDrone] > 1.0e-5f) || feed))
        {
            const float xf = juce::jmin (1.0f, (float) mut.counter / (float) kMutPeriod);
            auto rd = [&] (float ph)
            {
                const float x = ph * kMutTable;
                const int k = (int) x;
                const float t = x - (float) k;
                const float a = tbl[1 - mut.cur][k] + t * (tbl[1 - mut.cur][k + 1] - tbl[1 - mut.cur][k]);
                const float b = tbl[mut.cur][k] + t * (tbl[mut.cur][k + 1] - tbl[mut.cur][k]);
                return a + xf * (b - a);
            };
            for (int k = 0; k < 5; ++k) { dronePh[k] += droneInc[k]; if (dronePh[k] >= 1.0f) dronePh[k] -= 1.0f; }
            float xl = rd (dronePh[0]) + 0.45f * rd (dronePh[2]);
            float xr = rd (dronePh[1]) + 0.45f * rd (dronePh[3]);
            const float sub = 0.4f * fastSin (dronePh[4]);
            float bp, hp;
            xl = droneF[0].tick (xl, bp, hp) + sub;
            xr = droneF[1].tick (xr, bp, hp) + sub;
            xl *= 0.32f; xr *= 0.32f;
            feedL = xl * 0.6f; feedR = xr * 0.6f;
            const float g = gainNow[lyDrone] * droneOn;
            dl = xl * g; dr = xr * g;
        }
        ++mut.counter;
        // ---- pad (chord voices + MIDI voices)
        for (int v = 0; v < kPadVoices; ++v)
        {
            auto& pvc = pad[(size_t) v];
            const float tgt = pvc.midi ? pvc.gainTarget : pvc.gainTarget * (sources > 0 && (padOnF > 0.5f || feed) ? 1.0f : 0.0f);
            pvc.gain = tgt + (tgt > pvc.gain ? pvc.attackCoef : pvc.releaseCoef) * (pvc.gain - tgt);
            if (pvc.gain < 1.0e-5f) continue;
            const float s0 = saw (pvc.ph[0], pvc.inc[0]), s1 = saw (pvc.ph[1], pvc.inc[1]), s2 = saw (pvc.ph[2], pvc.inc[2]);
            pvc.ph[3] += pvc.inc[3]; if (pvc.ph[3] >= 1.0f) pvc.ph[3] -= 1.0f;
            const float sub = 0.5f * fastSin (pvc.ph[3]);
            float bp, hp;
            const float l = pvc.f[0].tick (s0 + 0.6f * s1, bp, hp) + sub;
            const float r = pvc.f[1].tick (s2 + 0.6f * s1, bp, hp) + sub;
            float pl, pr;
            panGains (pvc.pan, pl, pr);
            if (pvc.midi) { midiL += l * pl * pvc.gain; midiR += r * pr * pvc.gain; }
            else { padL += l * pl * pvc.gain; padR += r * pr * pvc.gain; }
        }
        padL *= 0.075f; padR *= 0.075f;
        feedL += padL * 0.6f; feedR += padR * 0.6f;
        const float padG = gainNow[lyPad] * (padOnF > 0.5f ? 1.0f : 0.0f);
        padL *= padG; padR *= padG;
        midiL *= 0.09f; midiR *= 0.09f;

        // ---- texture: granular cloud over drone + pad (+ MIDI)
        float tl = 0, tr = 0;
        if (grnRecord) { grnBuf[0].push (feedL + midiL); grnBuf[1].push (feedR + midiR); }
        if (sources > 0 && lp (lyTexture, P::lOn) > 0.5f && gainNow[lyTexture] > 1.0e-5f)
        {
            grnTimer -= 1.0f;
            if (grnTimer <= 0) { grnTimer = grnInterval * (0.6f + 0.8f * grnRng.uni()); spawnGrain(); }
        }
        int active = 0;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            ++active;
            const float env = 0.5f - 0.5f * std::cos (kTwoPi * g.t / g.len);
            const float d = juce::jlimit (2.0f, (float) grnBuf[0].mask - 4.0f, g.pos);
            tl += grnBuf[0].read (d) * env * g.panL;
            tr += grnBuf[1].read (d) * env * g.panR;
            g.pos += 1.0f - g.inc;
            g.t += 1.0f;
            if (g.t >= g.len) g.on = false;
        }
        {
            const float tg = gainNow[lyTexture] * 2.2f / std::sqrt ((float) juce::jmax (4, active));
            tl *= tg; tr *= tg;
        }

        // ---- wind
        float wl = 0, wr = 0;
        if (sources > 0 && lp (lyWind, P::lOn) > 0.5f && gainNow[lyWind] > 1.0e-5f)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float w = windRng.bi();
                auto& pk = pink[c];
                pk[0] = 0.99765f * pk[0] + w * 0.0990460f;
                pk[1] = 0.96300f * pk[1] + w * 0.2965164f;
                pk[2] = 0.57000f * pk[2] + w * 1.0526913f;
                const float pn = (pk[0] + pk[1] + pk[2] + w * 0.1848f) * 0.18f;
                float bp1, bp2, hp;
                windF[c][0].tick (pn, bp1, hp);
                windF[c][1].tick (pn, bp2, hp);
                const float y = (bp1 + 0.55f * bp2) * (1.0f + windCh * 1.5f);
                (c == 0 ? wl : wr) = y;
            }
            const float g = gainNow[lyWind] * windGust * 0.9f;
            wl *= g; wr *= g;
        }

        // ---- bells
        float bl = 0, br = 0;
        for (auto& b : bells)
        {
            if (! b.on) continue;
            b.mph += b.minc; if (b.mph >= 1.0f) b.mph -= 1.0f;
            b.ph += b.inc; if (b.ph >= 1.0f) b.ph -= 1.0f;
            const float y = fastSin (b.ph + b.idx * 0.159f * fastSin (b.mph)) * b.amp;
            b.idx *= b.idxCoef;
            b.amp *= b.ampCoef;
            bl += y * b.panL; br += y * b.panR;
            if (b.amp < 1.0e-5f) b.on = false;
        }
        {
            const float g = juce::jmax (gainNow[lyBells], 0.0f) * 0.9f;
            bl *= g; br *= g;
        }

        // ---- shimmer: pitch-shifted feedback reverb fed by the tonal layers
        float sl = 0, sr2 = 0;
        {
            const float send = gainNow[lyShimmer] * (lp (lyShimmer, P::lOn) > 0.5f ? 1.0f : 0.0f);
            const float inL = (feedL + tl * 0.5f) * send * shimIn * rg + midiL * send * shimIn;
            const float inR = (feedR + tr * 0.5f) * send * shimIn * rg + midiR * send * shimIn;
            if (send > 1.0e-5f || std::abs (shmFb[0]) + std::abs (shmFb[1]) > 1.0e-6f)
            {
                const float oct0 = shift[0][0].process (shmFb[0], 2.0f), oct1 = shift[1][0].process (shmFb[1], 2.0f);
                const float fif0 = shift[0][1].process (shmFb[0], 3.0f), fif1 = shift[1][1].process (shmFb[1], 3.0f);
                const float fl = oct0 + shmBlend * (fif0 - oct0), fr = oct1 + shmBlend * (fif1 - oct1);
                float l, r;
                shm.process (inL * 2.0f + fl * shmGainFb, inR * 2.0f + fr * shmGainFb, l, r);
                shmFb[0] = shmDamp[0].tick (fastTanh (l));
                shmFb[1] = shmDamp[1].tick (fastTanh (r));
                sl = l * 0.9f; sr2 = r * 0.9f;
            }
        }

        // energy per layer (UI)
        energyAcc[lyDrone] += dl * dl + dr * dr; energyAcc[lyPad] += padL * padL + padR * padR;
        energyAcc[lyTexture] += tl * tl + tr * tr; energyAcc[lyShimmer] += sl * sl + sr2 * sr2;
        energyAcc[lyWind] += wl * wl + wr * wr; energyAcc[lyBells] += bl * bl + br * br;

        // ---- mix: layers (faded by the transport) + MIDI voices
        float xl = (dl + padL + tl + wl + bl) * rg + sl + midiL;
        float xr = (dr + padR + tr + wr + br) * rg + sr2 + midiR;

        // ---- delay (bells + texture send), cross feedback
        {
            const float mix = dlyMixS.step (pv (P::dlyMix));
            const float inL = (bl + tl * 0.4f) * rg * mix, inR = (br + tr * 0.4f) * rg * mix;
            const float yl = dly[0].read (dlySamples), yr = dly[1].read (dlySamples * 1.333f);
            dlyLp[0] += dlyToneC * (yl - dlyLp[0]);
            dlyLp[1] += dlyToneC * (yr - dlyLp[1]);
            dly[0].push (fastTanh (inL + dlyLp[1] * dlyFb));
            dly[1].push (fastTanh (inR + dlyLp[0] * dlyFb));
            xl += yl * 0.8f; xr += yr * 0.8f;
        }
        // ---- warmth (gentle tape-like saturation)
        {
            const float w = warmthS.step (pv (P::warmth));
            if (w > 0.001f)
            {
                const float drive = 1.0f + w * 3.0f;
                xl = xl + w * (fastTanh (xl * drive) / drive * 1.2f - xl);
                xr = xr + w * (fastTanh (xr * drive) / drive * 1.2f - xr);
            }
        }
        // ---- reverb (Space)
        {
            const float mix = revMixS.step (0.12f + 0.68f * pv (P::space));
            float rl, rr;
            rev.process (xl, xr, rl, rr);
            xl = xl * (1.0f - mix * 0.55f) + rl * mix * 1.1f;
            xr = xr * (1.0f - mix * 0.55f) + rr * mix * 1.1f;
        }
        // ---- tone + width
        for (int c = 0; c < 2; ++c)
        {
            float& x = c == 0 ? xl : xr;
            x = tiltF[c].tick (highCutF[c].tick (lowCutF[c].tick (x)));
            const float y = x - dcX[c] + 0.9995f * dcY[c]; // DC blocker
            dcX[c] = x; dcY[c] = y; x = y;
        }
        {
            const float w = widthS.step (pv (P::width));
            const float m = 0.5f * (xl + xr), s = 0.5f * (xl - xr) * w;
            xl = m + s; xr = m - s;
        }
        // ---- master + safety limiter
        const float mg = masterS.step (dbToGain (pv (P::master)));
        xl *= mg; xr *= mg;
        const float pk = juce::jmax (std::abs (xl), std::abs (xr));
        const float want = pk > 0.89f ? 0.89f / pk : 1.0f;
        limGain = want < limGain ? want : want + limRel * (limGain - want);
        xl = fastTanh (xl * limGain * 1.05f) * 0.95f;
        xr = fastTanh (xr * limGain * 1.05f) * 0.95f;
        if (! std::isfinite (xl) || ! std::isfinite (xr)) { xl = xr = 0; }
        L[i] = xl; R[i] = xr;
    }
}

void Engine::process (float* L, float* R, int n, const juce::MidiBuffer& midi, const float* params, const HostInfo& host)
{
    p = params;
    const uint32_t s = (uint32_t) pv (P::seed);
    // ---- transport: DAW playback resyncs (fresh, reproducible start); internal Play keeps flowing
    if (host.hostPlaying)
    {
        const int64_t pos = host.hasPosition ? host.hostSample : expectedHost;
        if (! wasHost || std::llabs (pos - expectedHost) > 64 || s != seed)
        {
            reset (s);
            musT = (double) juce::jmax ((int64_t) 0, pos) / sr;
            evoT = musT * (double) (0.15f * std::pow (30.0f, pv (P::evolution)));
        }
        expectedHost = pos + n;
    }
    else if (s != seed && ! running && runGain <= 0.0f)
        reset (s);
    if (host.internalPlay && ! wasInternal && ! host.hostPlaying) sessionT = 0;
    wasHost = host.hostPlaying;
    wasInternal = host.internalPlay;
    running = host.hostPlaying || host.internalPlay;

    // ---- idle: nothing playing and every tail has died -> output silence, skip the DSP
    bool midiPending = ! midi.isEmpty();
    bool voices = false;
    for (int i = kChordVoices; i < kPadVoices; ++i) voices |= pad[(size_t) i].gain > 1.0e-5f || pad[(size_t) i].gainTarget > 0;
    if (! running && runGain <= 0.0f && ! voices && ! midiPending && silentFor > 8.0)
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        tele.running.store (false);
        tele.runGain.store (0);
        return;
    }

    int pos = 0;
    auto it = midi.begin();
    while (pos < n)
    {
        while (it != midi.end() && (*it).samplePosition <= pos) { handleMidi ((*it).getMessage()); ++it; }
        if (ctrlLeft == 0)
        {
            control();
            ctrlLeft = kCtrl;
            for (int l = 0; l < kNumLayers; ++l)
            {
                const float e = std::sqrt (energyAcc[(size_t) l] / (2.0f * kCtrl));
                tele.energy[(size_t) l].store (e);
                energyAcc[(size_t) l] = 0;
            }
        }
        int chunk = juce::jmin (n - pos, ctrlLeft);
        if (it != midi.end()) chunk = juce::jmin (chunk, juce::jmax (1, (*it).samplePosition - pos));
        renderChunk (L + pos, R + pos, chunk);
        ctrlLeft -= chunk;
        pos += chunk;
    }
    for (; it != midi.end(); ++it) handleMidi ((*it).getMessage());

    float peak = 0;
    for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
    silentFor = peak > 1.0e-5f || running ? 0.0 : silentFor + (double) n / sr;

    tele.running.store (running);
    tele.runGain.store (runGain);
    tele.sessionElapsed.store ((float) sessionT);
    const int timerMin = (int) pv (P::timer);
    if (timerMin > 0 && host.internalPlay && ! host.hostPlaying && sessionT >= timerMin * 60.0 - pv (P::fadeOut))
        tele.timerExpired.store (true);
}
}
