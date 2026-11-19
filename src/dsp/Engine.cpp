#include "Engine.h"

namespace gx
{
namespace
{
constexpr int kMutPeriod = 256;
constexpr float kCutOct = 9.965784f; // log2(1000): octaves spanned by the cutoff range

struct UniLayout { int n = 1; float detune[kMaxUnison] {}, panL[kMaxUnison] {}, panR[kMaxUnison] {}, gain[kMaxUnison] {}; };

inline void panGains (float pan, float& l, float& r)
{
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    l = std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;
    r = std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.41421356f;
}

UniLayout makeUnison (int n, float detune, float spread, float blend)
{
    UniLayout u;
    u.n = juce::jlimit (1, kMaxUnison, n);
    float sum = 0;
    for (int i = 0; i < u.n; ++i)
    {
        const float off = u.n == 1 ? 0.0f : -1.0f + 2.0f * (float) i / (float) (u.n - 1);
        u.detune[i] = (off + 0.07f * std::sin ((float) i * 1.7f) * (u.n > 1 ? 1.0f : 0.0f)) * detune * 50.0f; // cents
        panGains (off * spread, u.panL[i], u.panR[i]);
        u.gain[i] = (u.n == 1 || std::abs (off) < 0.34f) ? 1.0f : blend;
        sum += u.gain[i] * u.gain[i];
    }
    const float nrm = 1.0f / std::sqrt (juce::jmax (1.0e-6f, sum));
    for (int i = 0; i < u.n; ++i) u.gain[i] *= nrm;
    return u;
}

// FM algorithm table: modulator bitmask per operator (bit j = op j modulates), carrier mask.
struct Algo { uint8_t mods[4]; uint8_t carriers; };
const Algo kAlgos[8] {
    { { 0b0010, 0b0100, 0b1000, 0 }, 0b0001 }, // 4>3>2>1
    { { 0b0010, 0b1100, 0, 0 }, 0b0001 },      // (3+4)>2>1
    { { 0b0110, 0, 0b1000, 0 }, 0b0001 },      // 2+(4>3)>1
    { { 0b1110, 0, 0, 0 }, 0b0001 },           // (2+3+4)>1
    { { 0b0010, 0, 0b1000, 0 }, 0b0101 },      // 2>1 + 4>3
    { { 0b1000, 0b1000, 0b1000, 0 }, 0b0111 }, // 4>(1+2+3)
    { { 0b0010, 0, 0, 0 }, 0b1101 },           // 2>1 +3 +4
    { { 0, 0, 0, 0 }, 0b1111 },                // additive
};
} // namespace

Engine::Engine() {}

void Engine::prepare (double sampleRate, int mb)
{
    sr = sampleRate;
    maxBlock = juce::jmax (16, mb);
    voices.clear();
    voices.resize ((size_t) kMaxVoices);
    for (int i = 0; i < kMaxVoices; ++i) voices[(size_t) i].rng = Rng (1000u + (uint32_t) i * 7919u);
    busL.assign ((size_t) maxBlock, 0.0f);
    busR.assign ((size_t) maxBlock, 0.0f);
    const auto& defs = paramDefs();
    destOfParam.assign (defs.size(), 0);
    for (auto& d : defs) destOfParam[(size_t) d.index] = d.destIndex;
    normBase.assign (destDefs().size(), 0.0f);
    jassert ((int) destDefs().size() <= kMaxDests);
    gp.assign (defs.size(), 0.0f);
    fftBuf.assign (1024, 0.0f);
    activeDests.reserve (kNumModSlots);
    rhythm.prepare (sr);
    fx.prepare (sr, kFxBlock);
    masterGain.setTime (0.02f, sr);
    reset();
}

void Engine::reset()
{
    for (auto& v : voices) { v.active = false; for (auto& e : v.env) e.kill(); }
    rhythm.reset();
    fx.reset();
    monoCount = 0;
    modWheel = aftertouch = pitchBend = slideGlobal = 0;
    sustainPedal = false;
    lastPlayedNote = -1;
    gateGain = 1;
    dcL = dcR = dcXL = dcXR = 0;
}

void Engine::allNotesOff()
{
    for (auto& v : voices) if (v.active) { for (auto& e : v.env) e.noteOff(); v.released = true; }
    monoCount = 0;
}

// ---------------------------------------------------------------- MIDI
void Engine::handleMidi (const juce::MidiMessage& m)
{
    const int ch = m.getChannel();
    const bool mpe = p[P::mpeOn] > 0.5f && ch != 1;
    if (m.isNoteOn()) inputNoteOn (m.getNoteNumber(), m.getFloatVelocity(), ch);
    else if (m.isNoteOff()) inputNoteOff (m.getNoteNumber(), ch);
    else if (m.isPitchWheel())
    {
        const float b = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        if (mpe) { for (auto& v : voices) if (v.active && v.channel == ch) v.bend = b * 48.0f; }
        else pitchBend = b;
    }
    else if (m.isChannelPressure())
    {
        const float a = (float) m.getChannelPressureValue() / 127.0f;
        if (mpe) { for (auto& v : voices) if (v.active && v.channel == ch) v.pressure = a; }
        else aftertouch = a;
    }
    else if (m.isAftertouch())
    {
        for (auto& v : voices) if (v.active && v.note == m.getNoteNumber()) v.pressure = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float val = (float) m.getControllerValue() / 127.0f;
        if (cc == 1) modWheel = val;
        else if (cc == 74) { if (mpe) { for (auto& v : voices) if (v.active && v.channel == ch) v.slide = val; } else slideGlobal = val; }
        else if (cc == 64)
        {
            sustainPedal = val >= 0.5f;
            if (! sustainPedal)
                for (auto& v : voices)
                    if (v.active && v.sustained) { v.sustained = false; v.released = true; for (auto& e : v.env) e.noteOff(); }
        }
        else if (cc == 120 || cc == 123) { rhythm.allOff (*this); if (cc == 120) for (auto& v : voices) { v.active = false; for (auto& e : v.env) e.kill(); } }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) rhythm.allOff (*this);
}

void Engine::inputNoteOn (int note, float vel, int ch) { rhythm.noteOn (*this, gp.data(), note, vel, ch); }
void Engine::inputNoteOff (int note, int ch) { rhythm.noteOff (*this, gp.data(), note, ch); }

void Engine::voiceNoteOn (int note, float vel, int ch)
{
    tele.lastNote.store (note, std::memory_order_relaxed);
    tele.lastVelocity.store (vel, std::memory_order_relaxed);
    lastVelocity = vel;
    const int mode = (int) p[P::playMode];
    if (mode == pmPoly)
    {
        const int poly = juce::jlimit (1, kMaxVoices, (int) p[P::polyphony]);
        for (int i = poly; i < kMaxVoices; ++i)
            if (voices[(size_t) i].active && ! voices[(size_t) i].released) { voices[(size_t) i].released = true; for (auto& e : voices[(size_t) i].env) e.noteOff(); }
        Voice* target = nullptr;
        for (int i = 0; i < poly && ! target; ++i)
            if (voices[(size_t) i].active && voices[(size_t) i].note == note && voices[(size_t) i].channel == ch) target = &voices[(size_t) i];
        for (int i = 0; i < poly && ! target; ++i)
            if (! voices[(size_t) i].active) target = &voices[(size_t) i];
        if (! target)
        {
            float best = 1e9f;
            for (int i = 0; i < poly; ++i) // quietest released voice
            {
                auto& v = voices[(size_t) i];
                if (v.released && v.env[0].value < best) { best = v.env[0].value; target = &v; }
            }
            if (! target)
            {
                int oldest = INT_MAX;
                for (int i = 0; i < poly; ++i)
                    if (voices[(size_t) i].age < oldest) { oldest = voices[(size_t) i].age; target = &voices[(size_t) i]; }
            }
        }
        startVoice (*target, note, vel, ch, false);
        return;
    }
    // mono / legato
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[(size_t) i] == note) { for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1]; --monoCount; break; }
    if (monoCount < (int) monoStack.size()) monoStack[(size_t) monoCount++] = note;
    auto& v = voices[0];
    const bool overlapping = v.active && ! v.released && monoCount > 1;
    for (int i = 1; i < kMaxVoices; ++i) if (voices[(size_t) i].active) { voices[(size_t) i].released = true; for (auto& e : voices[(size_t) i].env) e.noteOff(); }
    if (mode == pmLegato && overlapping)
    {
        v.note = note;
        v.targetNote = (float) note;
        if (p[P::glide] < 0.0005f) v.curNote = (float) note;
        lastPlayedNote = (float) note;
    }
    else startVoice (v, note, vel, ch, overlapping);
}

void Engine::voiceNoteOff (int note, int ch)
{
    const int mode = (int) p[P::playMode];
    if (mode == pmPoly)
    {
        for (auto& v : voices)
            if (v.active && ! v.released && v.note == note && v.channel == ch)
            {
                if (sustainPedal) v.sustained = true;
                else { v.released = true; for (auto& e : v.env) e.noteOff(); }
            }
        return;
    }
    for (int i = 0; i < monoCount; ++i)
        if (monoStack[(size_t) i] == note) { for (int j = i; j < monoCount - 1; ++j) monoStack[(size_t) j] = monoStack[(size_t) j + 1]; --monoCount; break; }
    auto& v = voices[0];
    if (! v.active || v.released) return;
    if (monoCount > 0)
    {
        const int top = monoStack[(size_t) monoCount - 1];
        if (top != v.note) { v.note = top; v.targetNote = (float) top; if (p[P::glide] < 0.0005f) v.curNote = (float) top; }
    }
    else if (v.note == note || true)
    {
        if (sustainPedal) v.sustained = true;
        else { v.released = true; for (auto& e : v.env) e.noteOff(); }
    }
}

void Engine::startVoice (Voice& v, int note, float vel, int ch, bool legato)
{
    const bool wasSounding = v.active && v.env[0].value > 1.0e-4f;
    const float glideT = p[P::glide];
    const bool glideOk = glideT > 0.0005f && (p[P::glideLegato] < 0.5f || legato) && lastPlayedNote >= 0;
    v.curNote = glideOk ? ((int) p[P::playMode] != pmPoly && wasSounding ? v.curNote : lastPlayedNote) : (float) note;
    v.targetNote = (float) note;
    lastPlayedNote = (float) note;
    v.active = true; v.released = false; v.sustained = false;
    v.note = note; v.channel = ch; v.velocity = vel; v.age = ++ageCounter;
    v.randomVal = v.rng.bi(); v.pressure = 0; v.slide = slideGlobal; v.bend = 0; v.timeOn = 0;
    for (int i = 0; i < kNumEnv; ++i) { v.env[(size_t) i].setParams (p, P::env (i, 0)); v.env[(size_t) i].noteOn (wasSounding); }
    for (int i = 0; i < kNumLfo; ++i)
    {
        auto& l = v.lfo[(size_t) i];
        if (p[P::lfo (i, P::lRetrig)] > 0.5f) { l.phase = p[P::lfo (i, P::lPhase)]; l.rng = Rng (v.rng.next()); l.wrapped(); l.wrapped(); }
    }
    if (! wasSounding)
    {
        v.ampPrev = 0;
        for (int o = 0; o < kNumOsc; ++o)
        {
            auto& r = v.osc[(size_t) o];
            const bool rp = p[P::osc (o, P::oRandPhase)] > 0.5f;
            for (int u = 0; u < kMaxUnison; ++u)
            {
                r.ph[u] = rp ? v.rng.uni() : 0.0f; r.mph[u] = r.ph[u]; r.prev[u] = 0; r.smp[u] = 0; r.fb[u] = 0;
                for (int k = 0; k < 4; ++k) r.fm[u][k] = rp ? v.rng.uni() : 0.0f;
            }
            auto& m = r.mut;
            m.rng = Rng (v.rng.next() ^ (uint32_t) (note * 2654435761u));
            for (int k = 0; k < kGenomeHarm; ++k) { m.dom[k] = m.rng.uni(); m.target[k] = m.rng.uni(); m.speed[k] = 0.3f + m.rng.uni(); m.phase[k] = m.rng.uni(); }
            m.counter = -1; // force rebuild of both tables on first render
        }
        for (auto& f : v.filt) for (auto& c : f) c.reset();
        v.cutoffSmooth.fill (-1.0f);
    }
}

// ---------------------------------------------------------------- modulation
float Engine::pv (const Voice& v, int param) const
{
    const int d = destOfParam[(size_t) param];
    if (d == 0 || v.modN[d] == 0.0f) return p[param];
    const auto& def = paramDefs()[(size_t) param];
    return def.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normBase[(size_t) d] + v.modN[d]));
}

void Engine::computeMods (Voice& v, int n)
{
    float src[msNumSources] {};
    for (int i = 0; i < kNumEnv; ++i) src[msEnv1 + i] = v.env[(size_t) i].value;
    for (int i = 0; i < kNumLfo; ++i)
    {
        float val;
        if (p[P::lfo (i, P::lRetrig)] > 0.5f)
        {
            const float hz = p[P::lfo (i, P::lSync)] > 0.5f ? (float) (curBpm / 60.0 / divisionBeats ((int) p[P::lfo (i, P::lDiv)]))
                                                            : pv (v, P::lfo (i, P::lRate));
            val = v.lfo[(size_t) i].tick ((int) p[P::lfo (i, P::lShape)], hz * (float) n / (float) sr, pat->lfoTable[(size_t) i]);
        }
        else val = globalLfo[(size_t) i];
        const float fade = p[P::lfo (i, P::lFade)];
        if (fade > 0.001f) val *= juce::jmin (1.0f, v.timeOn / fade);
        src[msLfo1 + i] = val;
    }
    for (int i = 0; i < kNumMacros; ++i) src[msMacro1 + i] = p[P::macro1 + i];
    src[msVelocity] = v.velocity;
    src[msAftertouch] = juce::jmax (aftertouch, v.pressure);
    src[msModWheel] = modWheel;
    src[msPitchBend] = pitchBend;
    src[msKey] = ((float) v.note - 60.0f) / 60.0f;
    src[msRandom] = v.randomVal;
    src[msStepSeq] = globalSrc[msStepSeq];
    src[msSlide] = juce::jmax (v.slide, slideGlobal);
    v.lastLfo = { src[msLfo1], src[msLfo2], src[msLfo3], src[msLfo4] };

    std::fill (v.modN, v.modN + numDests, 0.0f);
    for (int s = 0; s < kNumModSlots; ++s)
        if (slotSrc[(size_t) s] > 0 && slotDst[(size_t) s] > 0)
            v.modN[slotDst[(size_t) s]] += slotAmt[(size_t) s] * src[slotSrc[(size_t) s]];
}

void Engine::updateGlobalLfos (int n, double beat)
{
    for (int i = 0; i < kNumLfo; ++i)
    {
        auto& l = globalLfoState[(size_t) i];
        const int shape = (int) p[P::lfo (i, P::lShape)];
        if (p[P::lfo (i, P::lSync)] > 0.5f)
        {
            const double beats = divisionBeats ((int) p[P::lfo (i, P::lDiv)]);
            const float ph = (float) (beat / beats - std::floor (beat / beats));
            const float np = wrap01 (ph + p[P::lfo (i, P::lPhase)]);
            if (np < l.phase - 0.5f) l.wrapped();
            l.phase = np;
            globalLfo[(size_t) i] = l.value (shape, np, pat->lfoTable[(size_t) i]);
        }
        else
        {
            const float hz = gp[(size_t) P::lfo (i, P::lRate)];
            globalLfo[(size_t) i] = l.tick (shape, hz * (float) n / (float) sr, pat->lfoTable[(size_t) i]);
        }
    }
}

// ---------------------------------------------------------------- oscillators
void Engine::rebuildMutation (Voice& v, int o, float f0, float morph, float chaos, float rate, bool both)
{
    auto& m = v.osc[(size_t) o].mut;
    const auto& arch = genomeArchetypes();
    const int maxK = juce::jlimit (1, kGenomeHarm - 1, (int) ((float) sr * 0.45f / juce::jmax (1.0f, f0)));
    const float pos = juce::jlimit (0.0f, 1.0f, morph) * (kNumArchetypes - 1);
    const int a = juce::jmin (kNumArchetypes - 2, (int) pos);
    const float t = pos - (float) a;
    const float dt = (float) kMutPeriod / (float) sr;
    const float r = 0.05f + rate * rate * 20.0f;
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int k = 1; k <= maxK; ++k)
    {
        // genetic drift: each partial ("gene") wanders toward a target, which mutates over time
        m.dom[k] += (m.target[k] - m.dom[k]) * juce::jmin (1.0f, m.speed[k] * r * dt);
        if (std::abs (m.target[k] - m.dom[k]) < 0.03f || m.rng.uni() < r * dt * 0.25f) m.target[k] = m.rng.uni();
        const float cross = juce::jlimit (0.0f, 1.0f, t + (m.dom[k] - 0.5f) * (0.6f + chaos));
        const float base = arch[(size_t) a][(size_t) k] + (arch[(size_t) a + 1][(size_t) k] - arch[(size_t) a][(size_t) k]) * cross;
        const float gene = juce::jmax (0.0f, 1.0f + chaos * 3.0f * (m.dom[k] - 0.5f));
        const float amp = base * gene;
        m.phase[k] = wrap01 (m.phase[k] + chaos * 0.01f * (m.dom[k] - 0.5f));
        const float ph = m.phase[k] * kTwoPi;
        fftBuf[(size_t) k * 2] = amp * std::sin (ph);
        fftBuf[(size_t) k * 2 + 1] = -amp * std::cos (ph);
    }
    mutFft.performRealOnlyInverseTransform (fftBuf.data());
    float peak = 1.0e-6f;
    for (int i = 0; i < 512; ++i) peak = juce::jmax (peak, std::abs (fftBuf[(size_t) i]));
    const int dst = both ? m.cur : 1 - m.cur;
    for (int i = 0; i < 512; ++i) m.table[dst][i] = fftBuf[(size_t) i] / peak;
    m.table[dst][512] = m.table[dst][0];
    if (both) { std::memcpy (m.table[1 - m.cur], m.table[m.cur], sizeof (m.table[0])); m.counter = kMutPeriod; }
    else { m.cur = dst; m.counter = 0; }
}

// ---------------------------------------------------------------- voice render
void Engine::renderVoice (Voice& v, float* outL, float* outR, int n)
{
    const float dt = (float) n / (float) sr;
    v.timeOn += dt;
    for (int i = 0; i < kNumEnv; ++i)
    {
        auto& e = v.env[(size_t) i];
        e.setParams (p, P::env (i, 0));
        e.attack = pv (v, P::env (i, P::eAttack));
        e.decay = pv (v, P::env (i, P::eDecay));
        e.release = pv (v, P::env (i, P::eRelease));
        e.setSustain (pv (v, P::env (i, P::eSustain)));
    }
    const float a0 = v.ampPrev;
    for (auto& e : v.env) e.advance (dt);
    const float a1 = v.env[0].value;
    computeMods (v, n);

    // pitch
    if (v.curNote != v.targetNote)
    {
        const float g = p[P::glide];
        if (g < 0.0005f) v.curNote = v.targetNote;
        else
        {
            v.curNote += (v.targetNote - v.curNote) * (1.0f - std::exp (-dt / (g * 0.3f)));
            if (std::abs (v.targetNote - v.curNote) < 0.001f) v.curNote = v.targetNote;
        }
    }
    const int vd = destDefsFirstVirtual;
    const float bend = pitchBend * p[P::bendRange] + v.bend;
    const float pitchAll = v.modN[vd + vdPitchAll] * 24.0f;
    const UniLayout uni = makeUnison ((int) p[P::uniVoices], pv (v, P::uniDetune), pv (v, P::uniSpread), p[P::uniBlend]);

    float vl[kControlBlock] {}, vr[kControlBlock] {};
    float tmp[kControlBlock];
    const float nyq = (float) sr * 0.5f;

    for (int o = 0; o < kNumOsc; ++o)
    {
        using namespace P;
        if (p[osc (o, oOn)] < 0.5f) continue;
        auto& rt = v.osc[(size_t) o];
        const int type = (int) p[osc (o, oType)];
        const float level = pv (v, osc (o, oLevel));
        if (level < 1.0e-4f) continue;
        float opl, opr;
        panGains (pv (v, osc (o, oPan)), opl, opr);
        const float morph = pv (v, osc (o, oMorph)), shape = pv (v, osc (o, oShape));
        const float sync = juce::jlimit (1.0f, 8.0f, pv (v, osc (o, oSync))), fold = pv (v, osc (o, oFold));
        const float note = v.curNote + 12.0f * p[osc (o, oOct)] + p[osc (o, oSemi)] + pv (v, osc (o, oFine)) * 0.01f
                           + bend + pitchAll + v.modN[vd + vdPitchOsc1 + o] * 24.0f;
        const float f0 = noteToHz (juce::jlimit (-24.0f, 150.0f, note));
        const float lvl = level * 0.5f;

        auto addOut = [&] (const float* s, float gl, float gr)
        {
            if (fold > 0.001f)
            {
                const float fg = 1.0f + fold * 5.0f, mixF = juce::jmin (1.0f, fold * 4.0f);
                for (int i = 0; i < n; ++i) { const float f = fastSin (s[i] * fg * 0.25f); vl[i] += (s[i] + (f - s[i]) * mixF) * gl; vr[i] += (s[i] + (f - s[i]) * mixF) * gr; }
            }
            else for (int i = 0; i < n; ++i) { vl[i] += s[i] * gl; vr[i] += s[i] * gr; }
        };

        if (type == otNoise)
        {
            const int nt = (int) p[osc (o, oNoise)];
            const float colorHz = 150.0f * std::pow (120.0f, morph);
            const float ca = std::exp (-kTwoPi * juce::jmin (colorHz, nyq * 0.9f) / (float) sr);
            for (int i = 0; i < n; ++i)
            {
                float x = 0;
                const float w = v.rng.bi();
                switch (nt)
                {
                    case ntPink:
                    {
                        float* b = rt.pink;
                        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f;
                        b[2] = 0.96900f * b[2] + w * 0.1538520f; b[3] = 0.86650f * b[3] + w * 0.3104856f;
                        b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
                        x = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f) * 0.11f; b[6] = w * 0.115926f;
                        break;
                    }
                    case ntBrown: rt.brown = juce::jlimit (-1.0f, 1.0f, rt.brown * 0.998f + w * 0.06f); x = rt.brown * 2.5f; break;
                    case ntCrackle:
                        if (v.rng.uni() < 0.0003f + shape * 0.004f) rt.crackle = v.rng.bi();
                        rt.crackle *= 0.93f; x = rt.crackle; break;
                    case ntDigital:
                        rt.digPh += f0 * (1.0f + shape * 15.0f) / (float) sr;
                        if (rt.digPh >= 1.0f) { rt.digPh -= std::floor (rt.digPh); rt.digVal = v.rng.uni() < 0.5f ? -1.0f : 1.0f; }
                        x = rt.digVal * 0.7f; break;
                    case ntSample:
                        if (sample != nullptr && ! sample->data.empty())
                        {
                            const float ratio = std::exp2 ((note - (float) sample->rootNote) / 12.0f) * (float) (sample->sampleRate / sr);
                            const int len = (int) sample->data.size();
                            float& pos = rt.smp[0];
                            if (pos < (float) (len - 1))
                            {
                                const int k = (int) pos; const float t = pos - (float) k;
                                x = sample->data[(size_t) k] * (1 - t) + sample->data[(size_t) juce::jmin (k + 1, len - 1)] * t;
                                pos += ratio;
                                if (shape > 0.5f && pos >= (float) (len - 1)) pos = 0; // loop
                            }
                        }
                        break;
                    default: x = w * 0.6f; break;
                }
                if (nt <= ntBrown) { rt.color = x + ca * (rt.color - x); x = morph > 0.97f ? x : rt.color * (1.0f + (1.0f - morph) * 1.5f); }
                tmp[i] = x;
            }
            addOut (tmp, opl * lvl, opr * lvl);
            continue;
        }

        if (type == otMutation)
        {
            auto& m = rt.mut;
            const float rate = pv (v, osc (o, oMutRate));
            if (m.counter < 0) rebuildMutation (v, o, f0, morph, shape, rate, true);
            else if (m.counter >= kMutPeriod) rebuildMutation (v, o, f0, morph, shape, rate, false);
        }

        for (int u = 0; u < uni.n; ++u)
        {
            const float inc = juce::jmin (0.45f, f0 * std::exp2 (uni.detune[u] * (1.0f / 1200.0f)) / (float) sr);
            float& ph = rt.ph[u];
            switch (type)
            {
                case otWavetable:
                {
                    const int ti = (int) p[osc (o, oTable)];
                    const Wavetable* wt = ti >= numBuiltinTables() ? tables[(size_t) o] : nullptr;
                    const Wavetable& T = wt != nullptr ? *wt : builtinTable (ti >= numBuiltinTables() ? 0 : ti);
                    const int L = Wavetable::levelFor (0.5f / (inc * sync) * 0.95f);
                    for (int i = 0; i < n; ++i)
                    {
                        ph += inc; if (ph >= 1.0f) ph -= 1.0f;
                        tmp[i] = T.read (sync > 1.001f ? wrap01 (ph * sync) : ph, morph, L);
                    }
                    break;
                }
                case otAnalog:
                {
                    const float seg = juce::jlimit (0.0f, 3.0f, morph * 3.0f);
                    float wS = 0, wT = 0, wW = 0, wQ = 0;
                    if (seg < 1) { wS = 1 - seg; wT = seg; } else if (seg < 2) { wT = 2 - seg; wW = seg - 1; } else { wW = 3 - seg; wQ = seg - 2; }
                    const float pw = 0.05f + 0.9f * shape;
                    auto naive = [=] (float x)
                    {
                        float y = 0;
                        if (wS > 0) y += wS * fastSin (x);
                        if (wT > 0) y += wT * (1.0f - 4.0f * std::abs (x - 0.5f)) * -1.0f;
                        if (wW > 0) y += wW * (2.0f * x - 1.0f);
                        if (wQ > 0) y += wQ * (x < pw ? 1.0f : -1.0f);
                        return y;
                    };
                    const float sinc = inc * sync;
                    float& mp = rt.mph[u];
                    float& prev = rt.prev[u];
                    for (int i = 0; i < n; ++i)
                    {
                        float cp = 0, cc = 0;
                        bool synced = false;
                        const float old = ph;
                        if (sync > 1.001f)
                        {
                            mp += inc;
                            if (mp >= 1.0f)
                            {
                                mp -= 1.0f;
                                const float t = juce::jlimit (0.0f, 1.0f, mp / inc);
                                const float before = wrap01 (old + sinc * (1.0f - t));
                                const float after = wrap01 (mp * sync);
                                const float J = naive (after) - naive (before);
                                cc -= 0.5f * J * (1 - t) * (1 - t);
                                cp += 0.5f * J * t * t;
                                ph = after;
                                synced = true;
                            }
                        }
                        if (! synced)
                        {
                            ph += sinc;
                            if (ph >= 1.0f)
                            {
                                ph -= 1.0f;
                                const float t = ph / sinc;
                                const float J = -2.0f * wW + 2.0f * wQ;
                                cc -= 0.5f * J * (1 - t) * (1 - t);
                                cp += 0.5f * J * t * t;
                                if (wQ > 0 && ph >= pw) { const float t2 = (ph - pw) / sinc; const float J2 = -2.0f * wQ; cc -= 0.5f * J2 * (1 - t2) * (1 - t2); cp += 0.5f * J2 * t2 * t2; }
                            }
                            else if (wQ > 0 && old < pw && ph >= pw)
                            {
                                const float t = (ph - pw) / sinc, J = -2.0f * wQ;
                                cc -= 0.5f * J * (1 - t) * (1 - t);
                                cp += 0.5f * J * t * t;
                            }
                        }
                        const float y = naive (ph) + cc;
                        tmp[i] = prev + cp;
                        prev = y;
                    }
                    break;
                }
                case otFM:
                {
                    const Algo& al = kAlgos[juce::jlimit (0, 7, (int) p[osc (o, oFmAlgo)])];
                    const float ratio[4] { 1.0f, pv (v, osc (o, oFmR2)), pv (v, osc (o, oFmR3)), pv (v, osc (o, oFmR4)) };
                    const float scale = (0.3f + morph * 1.05f) * juce::jlimit (0.15f, 1.0f, 2500.0f / f0);
                    const float lv[4] { 1.0f, pv (v, osc (o, oFmI2)), pv (v, osc (o, oFmI3)), pv (v, osc (o, oFmI4)) };
                    float idx[4];
                    for (int k = 0; k < 4; ++k) idx[k] = lv[k] * lv[k] * 6.0f * scale / kTwoPi; // radians -> cycles
                    const float fbAmt = shape * shape * 0.25f;
                    float incs[4];
                    for (int k = 0; k < 4; ++k) incs[k] = juce::jmin (0.49f, inc * ratio[k]);
                    int nc = 0; for (int k = 0; k < 4; ++k) nc += (al.carriers >> k) & 1;
                    const float cg = 1.0f / (float) juce::jmax (1, nc);
                    auto& op = rt.fm[u];
                    for (int i = 0; i < n; ++i)
                    {
                        float out[4];
                        for (int k = 3; k >= 0; --k)
                        {
                            op[k] += incs[k]; if (op[k] >= 1.0f) op[k] -= 1.0f;
                            float pm = 0;
                            for (int j = k + 1; j < 4; ++j) if ((al.mods[k] >> j) & 1) pm += out[j] * idx[j];
                            if (k == 3) pm += rt.fb[u] * fbAmt;
                            out[k] = fastSin (op[k] + pm);
                        }
                        rt.fb[u] = (rt.fb[u] + out[3]) * 0.5f;
                        float s = 0;
                        for (int k = 0; k < 4; ++k) if ((al.carriers >> k) & 1) s += out[k] * (k == 0 ? 1.0f : lv[k]);
                        tmp[i] = s * cg;
                    }
                    break;
                }
                case otMutation:
                {
                    auto& m = rt.mut;
                    const float* nt = m.table[m.cur];
                    const float* ot = m.table[1 - m.cur];
                    for (int i = 0; i < n; ++i)
                    {
                        ph += inc; if (ph >= 1.0f) ph -= 1.0f;
                        const float rp = (sync > 1.001f ? wrap01 (ph * sync) : ph) * 512.0f;
                        const int k = (int) rp; const float t = rp - (float) k;
                        const float a = nt[k] + t * (nt[k + 1] - nt[k]);
                        const float b = ot[k] + t * (ot[k + 1] - ot[k]);
                        const float x = juce::jmin (1.0f, (float) (m.counter + i) / (float) kMutPeriod);
                        tmp[i] = b + (a - b) * x;
                    }
                    break;
                }
                default: std::fill (tmp, tmp + n, 0.0f); break;
            }
            addOut (tmp, uni.panL[u] * uni.gain[u] * opl * lvl, uni.panR[u] * uni.gain[u] * opr * lvl);
        }
        if (type == otMutation) rt.mut.counter += n;
    }

    // ---- filters
    const bool parallel = p[P::filtRouting] > 0.5f;
    const float bal = pv (v, P::filtBalance);
    bool filtOn[kNumFilters];
    for (int f = 0; f < kNumFilters; ++f)
    {
        using namespace P;
        filtOn[f] = p[filt (f, fOn)] > 0.5f;
        if (! filtOn[f]) continue;
        const int dCut = destOfParam[(size_t) filt (f, fCutoff)];
        float norm = normBase[(size_t) dCut] + v.modN[dCut];
        norm += pv (v, filt (f, fEnv)) * v.env[1].value;
        norm += p[filt (f, fKey)] * ((v.curNote - 60.0f) / 12.0f) / kCutOct;
        norm = juce::jlimit (0.0f, 1.0f, norm);
        float& cs = v.cutoffSmooth[(size_t) f];
        cs = cs < 0 ? norm : cs + (norm - cs) * 0.5f;
        const float hz = 20.0f * std::pow (1000.0f, cs);
        tele.cutoff[(size_t) f].store (hz, std::memory_order_relaxed);
        const float reso = pv (v, filt (f, fReso)), drv = pv (v, filt (f, fDrive));
        const int t = (int) p[filt (f, fType)];
        v.filt[(size_t) f][0].set (t, hz, reso, drv, sr);
        v.filt[(size_t) f][1].set (t, hz, reso, drv, sr);
    }
    if (filtOn[0] || filtOn[1])
    {
        for (int i = 0; i < n; ++i)
        {
            float l = vl[i], r = vr[i];
            if (! parallel)
            {
                for (int f = 0; f < kNumFilters; ++f)
                    if (filtOn[f]) { l = v.filt[(size_t) f][0].tick (l); r = v.filt[(size_t) f][1].tick (r); }
            }
            else
            {
                const float l1 = filtOn[0] ? v.filt[0][0].tick (l) : l, r1 = filtOn[0] ? v.filt[0][1].tick (r) : r;
                const float l2 = filtOn[1] ? v.filt[1][0].tick (l) : l, r2 = filtOn[1] ? v.filt[1][1].tick (r) : r;
                l = l1 * (1 - bal) + l2 * bal; r = r1 * (1 - bal) + r2 * bal;
            }
            vl[i] = l; vr[i] = r;
        }
    }

    // ---- amp
    const float vs = p[P::velSens];
    const float velGain = (1.0f - vs) + vs * v.velocity * v.velocity;
    const float vol = juce::jlimit (0.0f, 2.0f, 1.0f + v.modN[vd + vdVolume]) * velGain;
    float pl = 1, pr = 1;
    if (v.modN[vd + vdPan] != 0.0f) panGains (v.modN[vd + vdPan], pl, pr);
    for (int i = 0; i < n; ++i)
    {
        const float a = (a0 + (a1 - a0) * (float) (i + 1) / (float) n) * vol;
        outL[i] += vl[i] * a * pl;
        outR[i] += vr[i] * a * pr;
    }
    v.ampPrev = a1;
    if (! v.env[0].active()) v.active = false;
}

// ---------------------------------------------------------------- block
void Engine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const float* params,
                      const Patterns& pt, const Transport& tr, const std::array<const Wavetable*, kNumOsc>& userTables,
                      const SampleData* smp)
{
    p = params; pat = &pt; tables = userTables; sample = smp;
    const int N = out.getNumSamples();
    out.clear();
    if (out.getNumChannels() < 2) return;
    float* L = out.getWritePointer (0);
    float* R = out.getWritePointer (1);

    // matrix setup
    const auto& defs = paramDefs();
    const auto& dests = destDefs();
    numDests = (int) dests.size();
    destDefsFirstVirtual = numDests - vdNumVirtual;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        slotSrc[(size_t) s] = juce::jlimit (0, msNumSources - 1, (int) p[P::mod (s, P::mSrc)]);
        slotDst[(size_t) s] = juce::jlimit (0, numDests - 1, (int) p[P::mod (s, P::mDst)]);
        slotAmt[(size_t) s] = p[P::mod (s, P::mAmt)];
    }
    for (int d = 1; d < destDefsFirstVirtual; ++d)
    {
        const int pi = dests[(size_t) d].param;
        normBase[(size_t) d] = defs[(size_t) pi].range.convertTo0to1 (p[pi]);
    }
    std::copy (p, p + P::numParams, gp.begin());

    curBpm = tr.bpm > 1.0 ? tr.bpm : 120.0;
    const double bps = curBpm / 60.0 / sr;
    auto it = midi.begin();
    const auto end = midi.end();

    for (int pos = 0; pos < N;)
    {
        const int n = juce::jmin (kControlBlock, N - pos);
        while (it != end && (*it).samplePosition < pos + n)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        const double beat = tr.playing ? tr.ppq + pos * bps : rhythm.clockBeat();

        // global sources (for FX / global destinations)
        updateGlobalLfos (n, beat);
        const Voice* newest = nullptr;
        for (auto& v : voices) if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
        for (int i = 0; i < kNumEnv; ++i) globalSrc[msEnv1 + i] = newest ? newest->env[(size_t) i].value : 0.0f;
        for (int i = 0; i < kNumLfo; ++i) globalSrc[msLfo1 + i] = globalLfo[(size_t) i];
        for (int i = 0; i < kNumMacros; ++i) globalSrc[msMacro1 + i] = p[P::macro1 + i];
        globalSrc[msVelocity] = lastVelocity;
        globalSrc[msAftertouch] = aftertouch;
        globalSrc[msModWheel] = modWheel;
        globalSrc[msPitchBend] = pitchBend;
        globalSrc[msKey] = lastPlayedNote >= 0 ? (lastPlayedNote - 60.0f) / 60.0f : 0.0f;
        globalSrc[msRandom] = 0;
        globalSrc[msSlide] = slideGlobal;
        for (int s = 0; s < kNumModSlots; ++s) // reset modded globals to base first
            if (slotDst[(size_t) s] > 0 && slotDst[(size_t) s] < destDefsFirstVirtual)
                gp[(size_t) dests[(size_t) slotDst[(size_t) s]].param] = p[dests[(size_t) slotDst[(size_t) s]].param];
        for (int s = 0; s < kNumModSlots; ++s)
        {
            const int d = slotDst[(size_t) s];
            if (slotSrc[(size_t) s] == 0 || d == 0 || d >= destDefsFirstVirtual) continue;
            const int pi = dests[(size_t) d].param;
            const float cur = defs[(size_t) pi].range.convertTo0to1 (gp[(size_t) pi]);
            gp[(size_t) pi] = defs[(size_t) pi].range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, cur + slotAmt[(size_t) s] * globalSrc[slotSrc[(size_t) s]]));
        }

        rhythm.tick (*this, gp.data(), pt, beat, bps, n);
        if (! tr.playing) rhythm.advanceClock (bps * n);
        globalSrc[msStepSeq] = rhythm.seqValue;

        float* bl = L + pos;
        float* br = R + pos;
        for (auto& v : voices)
            if (v.active) renderVoice (v, bl, br, n);

        // trance gate
        const float atk = std::exp (-1.0f / (juce::jmax (0.1f, p[P::tgAttack]) * 0.001f * (float) sr));
        const float rel = std::exp (-1.0f / (juce::jmax (0.1f, p[P::tgRelease]) * 0.001f * (float) sr));
        const float target = rhythm.gateTarget;
        if (target < 0.9999f || gateGain < 0.9999f)
            for (int i = 0; i < n; ++i)
            {
                gateGain = target + (target > gateGain ? atk : rel) * (gateGain - target);
                bl[i] *= gateGain; br[i] *= gateGain;
            }
        pos += n;
    }

    // DC blocker (PWM / asymmetric shapes) before the effects
    for (int i = 0; i < N; ++i)
    {
        const float l = L[i], r = R[i];
        dcL = l - dcXL + 0.9995f * dcL; dcXL = l; L[i] = dcL;
        dcR = r - dcXR + 0.9995f * dcR; dcXR = r; R[i] = dcR;
    }
    for (int pos = 0; pos < N; pos += kFxBlock)
        fx.process (L + pos, R + pos, juce::jmin (kFxBlock, N - pos), gp.data(), pt.fxOrder, curBpm);

    const float mg = dbToGain (p[P::master]);
    for (int i = 0; i < N; ++i)
    {
        const float g = masterGain.step (mg);
        float l = L[i] * g, r = R[i] * g;
        if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0; nanReset = true; }
        L[i] = juce::jlimit (-4.0f, 4.0f, l);
        R[i] = juce::jlimit (-4.0f, 4.0f, r);
    }
    if (nanReset) { nanReset = false; fx.reset(); for (auto& v : voices) for (auto& f : v.filt) for (auto& c : f) c.reset(); }

    // telemetry
    uint32_t mask = 0; int count = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        tele.voiceNote[(size_t) i].store (v.active ? v.note : -1, std::memory_order_relaxed);
        tele.voiceLevel[(size_t) i].store (v.active ? v.env[0].value : 0.0f, std::memory_order_relaxed);
        if (v.active) { mask |= 1u << i; ++count; }
    }
    tele.voiceMask.store (mask, std::memory_order_relaxed);
    tele.activeVoices.store (count, std::memory_order_relaxed);
    const Voice* newest = nullptr;
    for (auto& v : voices) if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
    for (int i = 0; i < kNumEnv; ++i) tele.env[(size_t) i].store (newest ? newest->env[(size_t) i].value : 0.0f, std::memory_order_relaxed);
    for (int i = 0; i < kNumLfo; ++i) tele.lfo[(size_t) i].store (newest && p[P::lfo (i, P::lRetrig)] > 0.5f ? newest->lastLfo[(size_t) i] : globalLfo[(size_t) i], std::memory_order_relaxed);
    for (int i = 0; i < kNumMacros; ++i) tele.macro[(size_t) i].store (p[P::macro1 + i], std::memory_order_relaxed);
    for (int o = 0; o < kNumOsc; ++o) tele.oscMorph[(size_t) o].store (newest ? pv (*newest, P::osc (o, P::oMorph)) : p[P::osc (o, P::oMorph)], std::memory_order_relaxed);
    tele.arpStep.store (p[P::arpOn] > 0.5f ? rhythm.arpStep : -1, std::memory_order_relaxed);
    tele.gateStep.store (rhythm.gateStep, std::memory_order_relaxed);
    tele.seqStep.store (rhythm.seqStep, std::memory_order_relaxed);
    tele.gateLevel.store (gateGain, std::memory_order_relaxed);
    tele.seqValue.store (rhythm.seqValue, std::memory_order_relaxed);
}
}
