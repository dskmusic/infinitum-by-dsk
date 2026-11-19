// Infinitum unit tests (run with: InfinitumTool test)
#include "Tests.h"
#include <set>

namespace gxtest
{
int failures = 0, checks = 0;

void check (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("  FAIL: %s\n", what.toRawUTF8()); }
}

Stats analyse (const juce::AudioBuffer<float>& b)
{
    Stats s;
    double sum = 0, sq = 0;
    const int n = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* d = b.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (d[i])) { s.finite = false; continue; }
            s.peak = juce::jmax (s.peak, std::abs (d[i]));
            sum += d[i]; sq += (double) d[i] * d[i];
        }
    }
    const double total = (double) n * b.getNumChannels();
    s.dc = (float) std::abs (sum / juce::jmax (1.0, total));
    s.rms = (float) std::sqrt (sq / juce::jmax (1.0, total));
    return s;
}

void setParam (InfinitumProcessor& p, const juce::String& id, float plain)
{
    const int i = gx::paramIndex (id);
    jassert (i >= 0);
    if (i >= 0) p.setParamPlain (i, plain);
}

juce::AudioBuffer<float> renderNote (InfinitumProcessor& p, int note, double seconds, double releaseAt, float vel, double sr)
{
    juce::AudioBuffer<float> out (2, (int) (seconds * sr));
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
    m.addEvent (juce::MidiMessage::noteOff (1, note), (int) (releaseAt * sr));
    p.renderOffline (out, m, sr, 256);
    return out;
}

static void testParams()
{
    std::puts ("[params]");
    const auto& defs = gx::paramDefs();
    check ((int) defs.size() == gx::P::numParams, "param count");
    std::set<juce::String> ids;
    for (int i = 0; i < (int) defs.size(); ++i)
    {
        check (defs[(size_t) i].index == i, "index filled " + juce::String (i));
        check (ids.insert (defs[(size_t) i].id).second, "unique id " + defs[(size_t) i].id);
        check (defs[(size_t) i].range.start <= defs[(size_t) i].def && defs[(size_t) i].def <= defs[(size_t) i].range.end, "default in range " + defs[(size_t) i].id);
    }
    check ((int) gx::destDefs().size() <= gx::kMaxDests, "dest count fits");
    InfinitumProcessor p;
    check ((int) p.getParameters().size() == gx::P::numParams, "host-visible parameter count");
}

static void testRender()
{
    std::puts ("[render]");
    InfinitumProcessor p;
    auto b = renderNote (p, 60, 1.5, 1.0);
    auto s = analyse (b);
    check (s.finite, "default patch finite");
    check (s.peak > 0.01f && s.peak < 1.5f, "default patch level " + juce::String (s.peak));
    check (s.dc < 0.01f, "default patch DC " + juce::String (s.dc));
    // release tail decays
    const float* d = b.getReadPointer (0);
    float tail = 0;
    for (int i = b.getNumSamples() - 2000; i < b.getNumSamples(); ++i) tail = juce::jmax (tail, std::abs (d[i]));
    check (tail < 0.01f, "release decays " + juce::String (tail));

    auto b2 = renderNote (p, 60, 1.5, 1.0);
    bool same = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) same &= b.getSample (c, i) == b2.getSample (c, i);
    check (same, "offline render deterministic");
}

static void testOscillators()
{
    std::puts ("[oscillators]");
    for (int type = 0; type < 5; ++type)
        for (int uni : { 1, 5 })
        {
            InfinitumProcessor p;
            setParam (p, "osc1_type", (float) type);
            setParam (p, "uni_voices", (float) uni);
            setParam (p, "osc1_noise", 1);
            setParam (p, "osc1_fold", 0.3f);
            setParam (p, "osc1_sync", type == 1 ? 3.0f : 1.0f);
            setParam (p, "osc1_fm_i3", 0.5f);
            for (int note : { 24, 60, 100 })
            {
                auto s = analyse (renderNote (p, note, 0.6, 0.4));
                const juce::String tag = "osc type " + juce::String (type) + " uni " + juce::String (uni) + " note " + juce::String (note);
                check (s.finite, tag + " finite");
                check (s.rms > 0.002f, tag + " audible rms=" + juce::String (s.rms));
                check (s.peak < 2.0f, tag + " peak=" + juce::String (s.peak));
            }
        }
    for (int t = 0; t < gx::numBuiltinTables(); ++t)
    {
        InfinitumProcessor p;
        setParam (p, "osc1_type", 0);
        setParam (p, "osc1_table", (float) t);
        auto s = analyse (renderNote (p, 48, 0.4, 0.3));
        check (s.finite && s.rms > 0.002f, "wavetable " + gx::wavetableNames()[t]);
    }
}

static void testFilters()
{
    std::puts ("[filters]");
    for (int t = 0; t < 10; ++t)
        for (float reso : { 0.0f, 0.95f })
        {
            InfinitumProcessor p;
            setParam (p, "filter1_type", (float) t);
            setParam (p, "filter1_cutoff", 1200.0f);
            setParam (p, "filter1_reso", reso);
            setParam (p, "filter1_drive", 0.5f);
            setParam (p, "filter1_env", 0.4f);
            auto s = analyse (renderNote (p, 48, 0.6, 0.4));
            const juce::String tag = "filter " + juce::String (t) + " reso " + juce::String (reso);
            check (s.finite, tag + " finite");
            check (s.rms > 0.0005f, tag + " audible");
            check (s.peak < 3.0f, tag + " bounded " + juce::String (s.peak));
        }
}

static void testFxAndRhythm()
{
    std::puts ("[fx + rhythm]");
    InfinitumProcessor p;
    for (auto id : { "dist_on", "chorus_on", "flanger_on", "phaser_on", "delay_on", "reverb_on", "comp_on", "eq_on", "crush_on", "wide_on", "shimmer_on", "granular_on" })
        setParam (p, id, 1);
    setParam (p, "eq_lo_g", 6);
    setParam (p, "arp_on", 1);
    setParam (p, "tg_on", 1);
    setParam (p, "chord", 4);
    setParam (p, "mod1_src", (float) gx::msLfo1);
    setParam (p, "mod1_dst", (float) gx::destIndex ("filter1_cutoff"));
    setParam (p, "mod1_amt", 0.5f);
    setParam (p, "mod2_src", (float) gx::msStepSeq);
    setParam (p, "mod2_dst", (float) gx::destIndex ("reverb_mix"));
    setParam (p, "mod2_amt", 0.3f);
    auto s = analyse (renderNote (p, 57, 3.0, 2.0));
    check (s.finite, "all fx + arp + gate finite");
    check (s.rms > 0.001f, "all fx audible");
    check (s.peak < 4.01f, "all fx bounded");
}

static void testRealtimeConditions()
{
    std::puts ("[realtime: sample rates / block sizes / state]");
    InfinitumProcessor p;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int bs : { 1, 7, 64, 512, 4096 })
        {
            p.prepareToPlay (sr, bs);
            juce::AudioBuffer<float> b (2, bs);
            bool ok = true;
            for (int k = 0; k < juce::jmax (4, 4096 / bs); ++k)
            {
                juce::MidiBuffer m;
                if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
                p.processBlock (b, m);
                ok &= analyse (b).finite;
            }
            check (ok, "sr " + juce::String (sr) + " bs " + juce::String (bs));
        }
    setParam (p, "filter1_cutoff", 777.0f);
    setParam (p, "osc2_type", 4);
    auto pats = p.getPatterns();
    pats.seq[3] = 0.5f;
    p.setPatterns (pats);
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    InfinitumProcessor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    check (std::abs (q.getParam (gx::paramIndex ("filter1_cutoff")) - 777.0f) < 0.5f, "state: cutoff restored");
    check ((int) q.getParam (gx::paramIndex ("osc2_type")) == 4, "state: osc type restored");
    check (q.getPatterns().seq[3] == 0.5f, "state: patterns restored");
}

static void testStress()
{
    std::puts ("[stress: note floods, modes, pedal, MPE]");
    for (int mode = 0; mode < 3; ++mode)
    {
        InfinitumProcessor p;
        setParam (p, "play_mode", (float) mode);
        setParam (p, "polyphony", 4);
        setParam (p, "glide", 0.05f);
        setParam (p, "mpe", 1);
        setParam (p, "uni_voices", 3);
        p.prepareToPlay (48000, 128);
        juce::Random r (mode + 1);
        bool ok = true;
        juce::AudioBuffer<float> b (2, 128);
        for (int k = 0; k < 600; ++k)
        {
            juce::MidiBuffer m;
            for (int e = 0; e < 6; ++e)
            {
                const int pos = r.nextInt (128), note = 20 + r.nextInt (90), ch = 1 + r.nextInt (16);
                switch (r.nextInt (6))
                {
                    case 0: case 1: m.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) (1 + r.nextInt (127))), pos); break;
                    case 2: m.addEvent (juce::MidiMessage::noteOff (ch, note), pos); break;
                    case 3: m.addEvent (juce::MidiMessage::controllerEvent (1, 64, r.nextBool() ? 127 : 0), pos); break;
                    case 4: m.addEvent (juce::MidiMessage::pitchWheel (ch, r.nextInt (16384)), pos); break;
                    default: m.addEvent (juce::MidiMessage::channelPressureChange (ch, r.nextInt (128)), pos); break;
                }
            }
            if (k == 500) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            p.processBlock (b, m);
            ok &= analyse (b).finite && analyse (b).peak < 4.01f;
        }
        check (ok, "stress mode " + juce::String (mode));
    }
}

int runAll()
{
    testStress();
    testParams();
    testRender();
    testOscillators();
    testFilters();
    testFxAndRhythm();
    testRealtimeConditions();
    for (auto& t : extraTests()) t();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

std::vector<std::function<void()>>& extraTests() { static std::vector<std::function<void()>> v; return v; }
}
