#include "Params.h"
#include <map>

namespace gx
{
using Range = juce::NormalisableRange<float>;

static Range logRange (float lo, float hi)
{
    const float ratio = hi / lo;
    return Range (lo, hi,
        [lo, ratio] (float, float, float x) { return lo * std::pow (ratio, x); },
        [lo, ratio] (float, float, float v) { return std::log (v / lo) / std::log (ratio); },
        [] (float, float, float v) { return v; });
}
static Range lin (float lo, float hi, float step = 0.0f) { return Range (lo, hi, step); }
static Range skewed (float lo, float hi, float centre) { Range r (lo, hi); r.setSkewForCentre (centre); return r; }

const juce::StringArray& divisionNames()
{
    static const juce::StringArray n { "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                       "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1/1", "2/1", "4/1" };
    return n;
}
double divisionBeats (int i)
{
    static const double b[] { 1.0 / 16, 1.0 / 12, 1.0 / 8, 1.0 / 6, 1.0 / 4, 3.0 / 8, 1.0 / 3, 1.0 / 2, 3.0 / 4,
                              2.0 / 3, 1.0, 1.5, 4.0 / 3, 2.0, 3.0, 4.0, 8.0, 16.0 };
    return b[juce::jlimit (0, (int) std::size (b) - 1, i)];
}
const juce::StringArray& fxNames()
{
    static const juce::StringArray n { "Distortion", "Chorus", "Flanger", "Phaser", "Delay", "Reverb",
                                       "Compressor", "EQ", "Bitcrusher", "Widener", "Shimmer", "Granular Smear" };
    return n;
}
const juce::StringArray& wavetableNames()
{
    static const juce::StringArray n { "Basic Shapes", "PWM", "Harmonic Sweep", "Odd Sweep", "Formant Vowels", "Organ Drawbars",
                                       "Bell Partials", "Sync Sweep", "Fold Sweep", "Tanh Drive", "Comb Teeth", "Glass",
                                       "Digital Steps", "Choir", "Metallic", "Phase Distort", "Ripple", "Spectral Tilt",
                                       "Hollow", "Noise Morph", "Grit", "Chebyshev", "Brass Formant", "Pluck Body", "User" };
    return n;
}
const juce::StringArray& chordNames()
{
    static const juce::StringArray n { "Off", "Octave", "Fifth", "Power", "Major", "Minor", "Sus2", "Sus4",
                                       "Maj7", "Min7", "Dom7", "Add9", "Min9", "Stack 4ths" };
    return n;
}
const std::vector<int>& chordIntervals (int mode)
{
    static const std::vector<std::vector<int>> c { {}, { 12 }, { 7 }, { 7, 12 }, { 4, 7 }, { 3, 7 }, { 2, 7 }, { 5, 7 },
                                                   { 4, 7, 11 }, { 3, 7, 10 }, { 4, 7, 10 }, { 4, 7, 14 }, { 3, 7, 10, 14 }, { 5, 10, 15 } };
    return c[(size_t) juce::jlimit (0, (int) c.size() - 1, mode)];
}

const juce::StringArray& sourceNames()
{
    static const juce::StringArray n { "None", "Env 1", "Env 2", "Env 3", "Env 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8",
                                       "Velocity", "Aftertouch", "Mod Wheel", "Pitch Bend", "Key", "Random", "Step Seq", "Slide" };
    return n;
}
const juce::StringArray& sourceIds()
{
    static const juce::StringArray n { "none", "env1", "env2", "env3", "env4", "lfo1", "lfo2", "lfo3", "lfo4",
                                       "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8",
                                       "velocity", "aftertouch", "modwheel", "pitchbend", "key", "random", "stepseq", "slide" };
    return n;
}

namespace
{
struct Builder
{
    std::vector<ParamDef> defs;
    juce::String group;
    Builder() { defs.resize ((size_t) P::numParams); }

    ParamDef& put (int idx, juce::String id, juce::String name, ParamKind k, Range r, float def, juce::String unit = {})
    {
        auto& d = defs[(size_t) idx];
        jassert (d.index < 0); // filled twice
        d.index = idx; d.id = std::move (id); d.name = std::move (name); d.kind = k;
        d.range = r; d.def = def; d.unit = std::move (unit); d.group = group;
        return d;
    }
    void f (int i, juce::String id, juce::String n, Range r, float def, juce::String u = {}, bool dest = false)
    { put (i, id, n, pkFloat, r, def, u).destIndex = dest ? 1 : 0; }
    void fm (int i, juce::String id, juce::String n, Range r, float def, juce::String u = {}) { f (i, id, n, r, def, u, true); }
    void in (int i, juce::String id, juce::String n, int lo, int hi, int def, juce::String u = {})
    { put (i, id, n, pkInt, Range ((float) lo, (float) hi, 1.0f), (float) def, u); }
    void b (int i, juce::String id, juce::String n, bool def) { put (i, id, n, pkBool, Range (0, 1, 1), def ? 1.0f : 0.0f); }
    void c (int i, juce::String id, juce::String n, const juce::StringArray& ch, int def)
    { put (i, id, n, pkChoice, Range (0, (float) ch.size() - 1, 1), (float) def).choices = ch; }
};

std::vector<ParamDef> build()
{
    using namespace P;
    Builder B;
    B.group = "global";
    B.f (master, "master", "Master", lin (-48.0f, 24.0f), -6.0f, "dB");
    B.in (polyphony, "polyphony", "Polyphony", 1, kMaxVoices, 16);
    B.c (playMode, "play_mode", "Play Mode", { "Poly", "Mono", "Legato" }, 0);
    B.f (glide, "glide", "Glide", skewed (0.0f, 2.0f, 0.25f), 0.0f, "s");
    B.b (glideLegato, "glide_legato", "Glide Legato Only", false);
    B.in (uniVoices, "uni_voices", "Unison Voices", 1, kMaxUnison, 1);
    B.fm (uniDetune, "uni_detune", "Unison Detune", lin (0, 1), 0.25f);
    B.fm (uniSpread, "uni_spread", "Unison Spread", lin (0, 1), 0.7f);
    B.f (uniBlend, "uni_blend", "Unison Blend", lin (0, 1), 0.75f);
    B.in (bendRange, "bend_range", "Bend Range", 0, 48, 2, "st");
    B.f (velSens, "vel_sens", "Velocity Sensitivity", lin (0, 1), 0.6f);
    B.b (mpeOn, "mpe", "MPE", false);
    B.c (filtRouting, "filter_routing", "Filter Routing", { "Serial", "Parallel" }, 0);
    B.fm (filtBalance, "filter_balance", "Filter Balance", lin (0, 1), 0.5f);
    B.group = "macros";
    for (int i = 0; i < kNumMacros; ++i)
        B.fm (macro1 + i, "macro" + juce::String (i + 1), "Macro " + juce::String (i + 1), lin (0, 1), 0.0f);

    B.group = "arp";
    B.b (arpOn, "arp_on", "Arp On", false);
    B.c (arpMode, "arp_mode", "Arp Mode", { "Up", "Down", "Up-Down", "Down-Up", "Random", "As Played", "Chord", "Converge" }, 0);
    B.in (arpOct, "arp_oct", "Arp Octaves", 1, 4, 1);
    B.c (arpRate, "arp_rate", "Arp Rate", divisionNames(), 4);
    B.f (arpGate, "arp_gate", "Arp Gate", lin (0.05f, 1.0f), 0.6f);
    B.f (arpSwing, "arp_swing", "Arp Swing", lin (0.0f, 0.75f), 0.0f);
    B.b (arpLatch, "arp_latch", "Arp Latch", false);
    B.in (arpSteps, "arp_steps", "Arp Steps", 1, kMaxArpSteps, 8);
    B.group = "gate";
    B.b (tgOn, "tg_on", "Trance Gate On", false);
    B.c (tgRate, "tg_rate", "Trance Gate Rate", divisionNames(), 4);
    B.in (tgSteps, "tg_steps", "Trance Gate Steps", 1, kMaxGateSteps, 16);
    B.fm (tgDepth, "tg_depth", "Trance Gate Depth", lin (0, 1), 1.0f);
    B.f (tgAttack, "tg_attack", "Trance Gate Attack", skewed (0.1f, 50.0f, 5.0f), 2.0f, "ms");
    B.f (tgRelease, "tg_release", "Trance Gate Release", skewed (0.1f, 200.0f, 20.0f), 15.0f, "ms");
    B.group = "seq";
    B.c (seqRate, "seq_rate", "Step Seq Rate", divisionNames(), 4);
    B.in (seqSteps, "seq_steps", "Step Seq Steps", 1, kMaxSeqSteps, 16);
    B.f (seqGlide, "seq_glide", "Step Seq Glide", lin (0, 1), 0.0f);
    B.group = "global";
    B.c (chordMode, "chord", "Chord", chordNames(), 0);
    B.f (strum, "strum", "Strum", skewed (0.0f, 200.0f, 40.0f), 0.0f, "ms");
    B.f (humanize, "humanize", "Humanize", lin (0, 1), 0.0f);

    for (int o = 0; o < kNumOsc; ++o)
    {
        const juce::String p = "osc" + juce::String (o + 1) + "_", n = "Osc " + juce::String (o + 1) + " ";
        B.group = "osc" + juce::String (o + 1);
        B.b (osc (o, oOn), p + "on", n + "On", o == 0);
        B.c (osc (o, oType), p + "type", n + "Type", { "Wavetable", "Analog", "FM", "Noise / Sample", "Spectral Mutation" }, o == 0 ? 1 : 0);
        B.fm (osc (o, oLevel), p + "level", n + "Level", lin (0, 1), 0.7f);
        B.fm (osc (o, oPan), p + "pan", n + "Pan", lin (-1, 1), 0.0f);
        B.in (osc (o, oOct), p + "oct", n + "Octave", -3, 3, 0);
        B.in (osc (o, oSemi), p + "semi", n + "Semitone", -12, 12, 0, "st");
        B.fm (osc (o, oFine), p + "fine", n + "Fine", lin (-100, 100), 0.0f, "ct");
        B.c (osc (o, oTable), p + "table", n + "Wavetable", wavetableNames(), 0);
        B.fm (osc (o, oMorph), p + "morph", n + "Morph", lin (0, 1), 0.66f);
        B.fm (osc (o, oShape), p + "shape", n + "Shape", lin (0, 1), 0.5f);
        B.fm (osc (o, oSync), p + "sync", n + "Sync", lin (1, 8), 1.0f, "x");
        B.fm (osc (o, oFold), p + "fold", n + "Fold", lin (0, 1), 0.0f);
        B.c (osc (o, oFmAlgo), p + "fm_algo", n + "FM Algorithm", { "4>3>2>1", "(3+4)>2>1", "2+(4>3)>1", "(2+3+4)>1", "2>1 + 4>3", "4>(1+2+3)", "2>1 +3 +4", "1+2+3+4" }, 0);
        B.fm (osc (o, oFmR2), p + "fm_r2", n + "FM Ratio 2", lin (0.25f, 16.0f, 0.25f), 2.0f);
        B.fm (osc (o, oFmR3), p + "fm_r3", n + "FM Ratio 3", lin (0.25f, 16.0f, 0.25f), 3.0f);
        B.fm (osc (o, oFmR4), p + "fm_r4", n + "FM Ratio 4", lin (0.25f, 16.0f, 0.25f), 1.0f);
        B.fm (osc (o, oFmI2), p + "fm_i2", n + "FM Level 2", lin (0, 1), 0.4f);
        B.fm (osc (o, oFmI3), p + "fm_i3", n + "FM Level 3", lin (0, 1), 0.0f);
        B.fm (osc (o, oFmI4), p + "fm_i4", n + "FM Level 4", lin (0, 1), 0.0f);
        B.fm (osc (o, oMutRate), p + "mut_rate", n + "Mutation Rate", lin (0, 1), 0.3f);
        B.c (osc (o, oNoise), p + "noise", n + "Noise Type", { "White", "Pink", "Brown", "Crackle", "Digital", "Sample" }, 0);
        B.b (osc (o, oRandPhase), p + "rand_phase", n + "Random Phase", true);
    }
    const juce::StringArray ftypes { "LP 12", "LP 24", "Ladder 24", "HP 12", "HP 24", "Band Pass", "Notch", "Comb +", "Comb -", "Formant" };
    for (int i = 0; i < kNumFilters; ++i)
    {
        const juce::String p = "filter" + juce::String (i + 1) + "_", n = "Filter " + juce::String (i + 1) + " ";
        B.group = "filter" + juce::String (i + 1);
        B.b (filt (i, fOn), p + "on", n + "On", i == 0);
        B.c (filt (i, fType), p + "type", n + "Type", ftypes, i == 0 ? 2 : 3);
        B.fm (filt (i, fCutoff), p + "cutoff", n + "Cutoff", logRange (20.0f, 20000.0f), i == 0 ? 4000.0f : 40.0f, "Hz");
        B.fm (filt (i, fReso), p + "reso", n + "Resonance", lin (0, 1), 0.2f);
        B.fm (filt (i, fDrive), p + "drive", n + "Drive", lin (0, 1), 0.0f);
        B.f (filt (i, fKey), p + "key", n + "Key Track", lin (0, 1), 0.3f);
        B.fm (filt (i, fEnv), p + "env", n + "Env Amount", lin (-1, 1), 0.0f);
    }
    for (int i = 0; i < kNumEnv; ++i)
    {
        const juce::String p = "env" + juce::String (i + 1) + "_", n = "Env " + juce::String (i + 1) + " ";
        B.group = "env" + juce::String (i + 1);
        B.f (env (i, eDelay), p + "delay", n + "Delay", skewed (0.0f, 4.0f, 0.3f), 0.0f, "s");
        B.fm (env (i, eAttack), p + "attack", n + "Attack", skewed (0.0f, 10.0f, 0.5f), 0.003f, "s");
        B.f (env (i, eHold), p + "hold", n + "Hold", skewed (0.0f, 4.0f, 0.3f), 0.0f, "s");
        B.fm (env (i, eDecay), p + "decay", n + "Decay", skewed (0.001f, 10.0f, 0.8f), 0.4f, "s");
        B.fm (env (i, eSustain), p + "sustain", n + "Sustain", lin (0, 1), i == 0 ? 0.8f : 0.0f);
        B.fm (env (i, eRelease), p + "release", n + "Release", skewed (0.001f, 12.0f, 1.0f), 0.3f, "s");
        B.f (env (i, eACurve), p + "a_curve", n + "Attack Curve", lin (-1, 1), 0.0f);
        B.f (env (i, eDCurve), p + "d_curve", n + "Decay Curve", lin (-1, 1), 0.5f);
        B.f (env (i, eRCurve), p + "r_curve", n + "Release Curve", lin (-1, 1), 0.5f);
    }
    for (int i = 0; i < kNumLfo; ++i)
    {
        const juce::String p = "lfo" + juce::String (i + 1) + "_", n = "LFO " + juce::String (i + 1) + " ";
        B.group = "lfo" + juce::String (i + 1);
        B.c (lfo (i, lShape), p + "shape", n + "Shape", { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Smooth Random", "Custom" }, 0);
        B.fm (lfo (i, lRate), p + "rate", n + "Rate", logRange (0.01f, 40.0f), 2.0f, "Hz");
        B.b (lfo (i, lSync), p + "sync", n + "Sync", false);
        B.c (lfo (i, lDiv), p + "div", n + "Division", divisionNames(), 10);
        B.b (lfo (i, lRetrig), p + "retrig", n + "Retrigger", true);
        B.f (lfo (i, lFade), p + "fade", n + "Fade In", skewed (0.0f, 8.0f, 1.0f), 0.0f, "s");
        B.f (lfo (i, lPhase), p + "phase", n + "Phase", lin (0, 1), 0.0f);
    }
    B.group = "matrix";
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const juce::String p = "mod" + juce::String (i + 1) + "_", n = "Mod " + juce::String (i + 1) + " ";
        B.c (mod (i, mSrc), p + "src", n + "Source", sourceNames(), 0);
        B.c (mod (i, mDst), p + "dst", n + "Destination", { "None" }, 0); // choices filled below
        B.f (mod (i, mAmt), p + "amt", n + "Amount", lin (-1, 1), 0.0f);
    }

    B.group = "fx.dist";
    B.b (distOn, "dist_on", "Distortion On", false);
    B.c (distType, "dist_type", "Distortion Type", { "Soft", "Hard", "Fold", "Tube", "Rectify" }, 0);
    B.fm (distDrive, "dist_drive", "Distortion Drive", lin (0, 1), 0.3f);
    B.fm (distTone, "dist_tone", "Distortion Tone", lin (0, 1), 0.6f);
    B.fm (distMix, "dist_mix", "Distortion Mix", lin (0, 1), 1.0f);
    B.group = "fx.chorus";
    B.b (choOn, "chorus_on", "Chorus On", false);
    B.fm (choRate, "chorus_rate", "Chorus Rate", logRange (0.05f, 8.0f), 0.6f, "Hz");
    B.fm (choDepth, "chorus_depth", "Chorus Depth", lin (0, 1), 0.4f);
    B.fm (choMix, "chorus_mix", "Chorus Mix", lin (0, 1), 0.4f);
    B.group = "fx.flanger";
    B.b (flaOn, "flanger_on", "Flanger On", false);
    B.fm (flaRate, "flanger_rate", "Flanger Rate", logRange (0.02f, 8.0f), 0.2f, "Hz");
    B.fm (flaDepth, "flanger_depth", "Flanger Depth", lin (0, 1), 0.6f);
    B.fm (flaFb, "flanger_fb", "Flanger Feedback", lin (-0.95f, 0.95f), 0.5f);
    B.fm (flaMix, "flanger_mix", "Flanger Mix", lin (0, 1), 0.5f);
    B.group = "fx.phaser";
    B.b (phaOn, "phaser_on", "Phaser On", false);
    B.fm (phaRate, "phaser_rate", "Phaser Rate", logRange (0.02f, 8.0f), 0.3f, "Hz");
    B.fm (phaDepth, "phaser_depth", "Phaser Depth", lin (0, 1), 0.7f);
    B.fm (phaFb, "phaser_fb", "Phaser Feedback", lin (-0.95f, 0.95f), 0.4f);
    B.fm (phaMix, "phaser_mix", "Phaser Mix", lin (0, 1), 0.5f);
    B.group = "fx.delay";
    B.b (dlyOn, "delay_on", "Delay On", false);
    B.c (dlyDiv, "delay_div", "Delay Time", divisionNames(), 8);
    B.fm (dlyFb, "delay_fb", "Delay Feedback", lin (0, 0.95f), 0.35f);
    B.fm (dlyTone, "delay_tone", "Delay Tone", lin (0, 1), 0.6f);
    B.b (dlyPing, "delay_ping", "Delay Ping-Pong", true);
    B.fm (dlyMix, "delay_mix", "Delay Mix", lin (0, 1), 0.25f);
    B.group = "fx.reverb";
    B.b (revOn, "reverb_on", "Reverb On", false);
    B.fm (revSize, "reverb_size", "Reverb Size", lin (0, 1), 0.6f);
    B.fm (revDamp, "reverb_damp", "Reverb Damping", lin (0, 1), 0.4f);
    B.fm (revWidth, "reverb_width", "Reverb Width", lin (0, 1), 1.0f);
    B.fm (revMix, "reverb_mix", "Reverb Mix", lin (0, 1), 0.25f);
    B.group = "fx.comp";
    B.b (cmpOn, "comp_on", "Compressor On", false);
    B.f (cmpThresh, "comp_thresh", "Comp Threshold", lin (-48, 0), -18.0f, "dB");
    B.f (cmpRatio, "comp_ratio", "Comp Ratio", skewed (1, 20, 4), 3.0f, ":1");
    B.f (cmpAttack, "comp_attack", "Comp Attack", skewed (0.1f, 100.0f, 10.0f), 5.0f, "ms");
    B.f (cmpRelease, "comp_release", "Comp Release", skewed (10.0f, 1000.0f, 150.0f), 120.0f, "ms");
    B.f (cmpMakeup, "comp_makeup", "Comp Makeup", lin (0, 24), 3.0f, "dB");
    B.group = "fx.eq";
    B.b (eqOn, "eq_on", "EQ On", false);
    B.f (eqLoF, "eq_lo_f", "EQ Low Freq", logRange (20, 1000), 120.0f, "Hz");
    B.f (eqLoG, "eq_lo_g", "EQ Low Gain", lin (-18, 18), 0.0f, "dB");
    B.f (eqM1F, "eq_m1_f", "EQ Mid 1 Freq", logRange (60, 8000), 500.0f, "Hz");
    B.f (eqM1G, "eq_m1_g", "EQ Mid 1 Gain", lin (-18, 18), 0.0f, "dB");
    B.f (eqM2F, "eq_m2_f", "EQ Mid 2 Freq", logRange (200, 16000), 2500.0f, "Hz");
    B.f (eqM2G, "eq_m2_g", "EQ Mid 2 Gain", lin (-18, 18), 0.0f, "dB");
    B.f (eqHiF, "eq_hi_f", "EQ High Freq", logRange (1000, 20000), 8000.0f, "Hz");
    B.f (eqHiG, "eq_hi_g", "EQ High Gain", lin (-18, 18), 0.0f, "dB");
    B.f (eqQ, "eq_q", "EQ Mid Q", skewed (0.2f, 8.0f, 1.0f), 0.9f);
    B.group = "fx.crush";
    B.b (bitOn, "crush_on", "Bitcrusher On", false);
    B.fm (bitBits, "crush_bits", "Crush Bits", lin (2, 16), 10.0f);
    B.fm (bitRate, "crush_rate", "Crush Downsample", lin (1, 32), 1.0f, "x");
    B.fm (bitMix, "crush_mix", "Crush Mix", lin (0, 1), 1.0f);
    B.group = "fx.widener";
    B.b (widOn, "wide_on", "Widener On", false);
    B.fm (widWidth, "wide_width", "Width", lin (0, 2), 1.3f);
    B.f (widBassMono, "wide_bass", "Bass Mono Freq", logRange (20, 500), 120.0f, "Hz");
    B.group = "fx.shimmer";
    B.b (shmOn, "shimmer_on", "Shimmer On", false);
    B.fm (shmSize, "shimmer_size", "Shimmer Size", lin (0, 1), 0.7f);
    B.fm (shmAmount, "shimmer_amount", "Shimmer Amount", lin (0, 1), 0.5f);
    B.fm (shmMix, "shimmer_mix", "Shimmer Mix", lin (0, 1), 0.3f);
    B.group = "fx.granular";
    B.b (grnOn, "granular_on", "Granular On", false);
    B.fm (grnSize, "granular_size", "Grain Size", skewed (10.0f, 500.0f, 80.0f), 90.0f, "ms");
    B.fm (grnDensity, "granular_density", "Grain Density", lin (0, 1), 0.5f);
    B.fm (grnSpread, "granular_spread", "Smear", lin (0, 1), 0.5f);
    B.fm (grnPitch, "granular_pitch", "Grain Pitch Jitter", lin (0, 1), 0.1f);
    B.fm (grnMix, "granular_mix", "Granular Mix", lin (0, 1), 0.35f);

    // Assign destination indices in parameter order (after "none")
    int next = 1;
    for (auto& d : B.defs)
    {
        jassert (d.index >= 0); // every slot filled
        if (d.destIndex) d.destIndex = next++;
    }
    return B.defs;
}
} // namespace

static std::vector<DestDef> buildDests (const std::vector<ParamDef>& defs);

struct Tables
{
    std::vector<ParamDef> defs;
    std::vector<DestDef> dests;
    std::map<juce::String, int> byId, destById;
    Tables()
    {
        defs = build();
        dests = buildDests (defs);
        juce::StringArray dn;
        for (auto& d : dests) dn.add (d.name);
        for (int i = 0; i < kNumModSlots; ++i)
        {
            auto& d = defs[(size_t) P::mod (i, P::mDst)];
            d.choices = dn;
            d.range = Range (0, (float) dn.size() - 1, 1);
        }
        for (auto& d : defs) byId[d.id] = d.index;
        for (int i = 0; i < (int) dests.size(); ++i) destById[dests[(size_t) i].id] = i;
    }
};
static const Tables& tables() { static const Tables t; return t; }

const std::vector<ParamDef>& paramDefs() { return tables().defs; }
const std::vector<DestDef>& destDefs() { return tables().dests; }
int paramIndex (const juce::String& id) { auto& m = tables().byId; auto it = m.find (id); return it == m.end() ? -1 : it->second; }
int destIndex (const juce::String& id) { auto& m = tables().destById; auto it = m.find (id); return it == m.end() ? 0 : it->second; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& d : paramDefs())
    {
        const juce::ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case pkFloat:
                l.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, d.range, d.def,
                         juce::AudioParameterFloatAttributes().withLabel (d.unit)));
                break;
            case pkInt:
                l.add (std::make_unique<juce::AudioParameterInt> (pid, d.name, (int) d.range.start, (int) d.range.end, (int) d.def,
                         juce::AudioParameterIntAttributes().withLabel (d.unit)));
                break;
            case pkBool: l.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f)); break;
            case pkChoice: l.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, d.choices, (int) d.def)); break;
        }
    }
    return l;
}

// destDefs is completed lazily after paramDefs exist (avoids init-order recursion).
static std::vector<DestDef> buildDests (const std::vector<ParamDef>& defs)
{
    std::vector<DestDef> v;
    v.push_back ({ "none", "None", -1, -1 });
    for (auto& d : defs)
        if (d.destIndex > 0) v.push_back ({ d.id, d.name, d.index, -1 });
    const char* vid[] { "pitch", "pitch_osc1", "pitch_osc2", "pitch_osc3", "pitch_osc4", "volume", "pan" };
    const char* vn[] { "Pitch (All)", "Osc 1 Pitch", "Osc 2 Pitch", "Osc 3 Pitch", "Osc 4 Pitch", "Volume", "Pan" };
    for (int i = 0; i < vdNumVirtual; ++i) v.push_back ({ vid[i], vn[i], -1, i });
    return v;
}
} // namespace gx
