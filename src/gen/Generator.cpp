#include "Generator.h"
#include "../preset/Render.h"
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <array>
#include <algorithm>

namespace gx
{
// ======================================================================= builder helpers
namespace
{
struct B
{
    Preset& p;
    Rng& r;
    const GenRequest& q;
    juce::String sub;

    int idx (const char* id) const { const int i = paramIndex (id); jassert (i >= 0); return i; }
    void set (const char* id, float v) { const int i = idx (id); if (i >= 0) p.params[(size_t) i] = paramDefs()[(size_t) i].range.snapToLegalValue (v); }
    void set (const juce::String& id, float v) { set (id.toRawUTF8(), v); }
    float get (const char* id) const { return p.params[(size_t) idx (id)]; }
    float rr (float a, float b) { return a + (b - a) * r.uni(); }
    bool chance (float pr) { return r.uni() < pr; }
    int pick (int n) { return (int) (r.next() % (uint32_t) juce::jmax (1, n)); }
    template <typename T> T pickOf (std::initializer_list<T> l) { auto it = l.begin(); std::advance (it, pick ((int) l.size())); return *it; }
    // log interpolation between lo and hi driven by a 0..1 control (with jitter)
    float scaleLog (float lo, float hi, float ctl, float jitter = 0.15f) { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, ctl + rr (-jitter, jitter))); }
    float scaleLin (float lo, float hi, float ctl, float jitter = 0.12f) { return lo + (hi - lo) * juce::jlimit (0.0f, 1.0f, ctl + rr (-jitter, jitter)); }

    juce::String o (int i, const char* f) const { return "osc" + juce::String (i) + "_" + f; }
    juce::String fl (int i, const char* f) const { return "filter" + juce::String (i) + "_" + f; }
    juce::String e (int i, const char* f) const { return "env" + juce::String (i) + "_" + f; }
    juce::String l (int i, const char* f) const { return "lfo" + juce::String (i) + "_" + f; }

    void osc (int i, int type, float level, int oct = 0, int semi = 0, float fine = 0)
    {
        set (o (i, "on"), 1); set (o (i, "type"), (float) type); set (o (i, "level"), level);
        set (o (i, "oct"), (float) oct); set (o (i, "semi"), (float) semi); set (o (i, "fine"), fine);
    }
    void analog (int i, float morph, float level, int oct = 0, float pw = 0.5f) { osc (i, otAnalog, level, oct); set (o (i, "morph"), morph); set (o (i, "shape"), pw); }
    void wt (int i, int table, float morph, float level, int oct = 0) { osc (i, otWavetable, level, oct); set (o (i, "table"), (float) table); set (o (i, "morph"), morph); }
    void filter (int i, int type, float cutoff, float reso, float env = 0, float key = 0.3f, float drive = 0)
    {
        set (fl (i, "on"), 1); set (fl (i, "type"), (float) type); set (fl (i, "cutoff"), cutoff);
        set (fl (i, "reso"), reso); set (fl (i, "env"), env); set (fl (i, "key"), key); set (fl (i, "drive"), drive);
    }
    void env (int i, float a, float d, float s, float rel, float ac = 0.2f, float dc = 0.5f, float rc = 0.5f)
    {
        set (e (i, "attack"), a); set (e (i, "decay"), d); set (e (i, "sustain"), s); set (e (i, "release"), rel);
        set (e (i, "a_curve"), ac); set (e (i, "d_curve"), dc); set (e (i, "r_curve"), rc);
    }
    void lfo (int i, int shape, float rate, bool retrig = true, float fade = 0, int syncDiv = -1)
    {
        set (l (i, "shape"), (float) shape); set (l (i, "rate"), rate); set (l (i, "retrig"), retrig ? 1.0f : 0.0f); set (l (i, "fade"), fade);
        set (l (i, "sync"), syncDiv >= 0 ? 1.0f : 0.0f);
        if (syncDiv >= 0) set (l (i, "div"), (float) syncDiv);
    }
    bool mod (const char* src, const juce::String& dst, float amt)
    {
        const int s = sourceIds().indexOf (src), d = destIndex (dst);
        if (s <= 0 || d <= 0) { jassertfalse; return false; }
        for (int k = 0; k < kNumModSlots; ++k)
            if ((int) p.params[(size_t) P::mod (k, P::mSrc)] == 0)
            {
                p.params[(size_t) P::mod (k, P::mSrc)] = (float) s;
                p.params[(size_t) P::mod (k, P::mDst)] = (float) d;
                p.params[(size_t) P::mod (k, P::mAmt)] = juce::jlimit (-1.0f, 1.0f, amt);
                return true;
            }
        return false;
    }
    void fxOn (const char* onId) { set (onId, 1); }

    // ---- v1.1 sound-design helpers
    bool on (const juce::String& id) const { return get (id.toRawUTF8()) > 0.5f; }
    int freeOsc() const { for (int i = 2; i <= 4; ++i) if (! on (o (i, "on"))) return i; return 0; }
    int oscCount() const { int n = 0; for (int i = 1; i <= 4; ++i) n += on (o (i, "on")); return n; }
    bool hasMod (const char* src, const juce::String& dst) const
    {
        const int s = sourceIds().indexOf (src), d = destIndex (dst);
        for (int k = 0; k < kNumModSlots; ++k)
            if ((int) p.params[(size_t) P::mod (k, P::mSrc)] == s && (int) p.params[(size_t) P::mod (k, P::mDst)] == d) return true;
        return false;
    }
    // adds a route only if the destination exists and it is not already there (never asserts)
    bool modIf (const char* src, const juce::String& dst, float amt)
    {
        if (destIndex (dst) <= 0 || sourceIds().indexOf (src) <= 0 || hasMod (src, dst)) return false;
        return mod (src, dst, amt);
    }
    int freeLfo() const
    {
        for (int i = 1; i <= kNumLfo; ++i)
        {
            const int s = sourceIds().indexOf ("lfo" + juce::String (i));
            bool used = false;
            for (int k = 0; k < kNumModSlots; ++k) used |= (int) p.params[(size_t) P::mod (k, P::mSrc)] == s;
            if (! used) return i;
        }
        return 0;
    }
    bool envUsed (int i) const
    {
        const int s = sourceIds().indexOf ("env" + juce::String (i));
        for (int k = 0; k < kNumModSlots; ++k) if ((int) p.params[(size_t) P::mod (k, P::mSrc)] == s) return true;
        return false;
    }
    void formant2 (float vowel, float balance)   // parallel formant filter (vocal colour)
    {
        set ("filter2_on", 1); set ("filter2_type", (float) ftFormant); set ("filter2_cutoff", vowel); set ("filter2_reso", rr (0.3f, 0.45f));
        set ("filter_routing", 1); set ("filter_balance", balance);
    }
};

// Division indices (see divisionNames)
enum Div { d64 = 0, d32T, d32, d16T, d16, d16D, d8T, d8, d8D, d4T, d4, d4D, d2T, d2, d2D, d1 };
// Wavetable indices (see wavetableNames)
enum Tbl { tBasic, tPwm, tHarm, tOdd, tVowel, tOrgan, tBell, tSync, tFold, tTanh, tComb, tGlass, tDigital, tChoir, tMetal, tPhaseDist,
           tRipple, tTilt, tHollow, tNoise, tGrit, tCheby, tBrass, tPluck };

// ---- sound archetypes ------------------------------------------------------------------
void bass (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    const bool mono = sub == "Acid" || sub == "Reese" || b.chance (0.55f);
    b.set ("play_mode", mono ? (b.chance (0.5f) ? (float) pmLegato : (float) pmMono) : (float) pmPoly);
    b.set ("polyphony", 8);
    b.env (1, b.rr (0.0005f, 0.004f), b.rr (0.3f, 1.0f), b.rr (0.75f, 1.0f), b.rr (0.04f, 0.16f), 0.4f);
    b.set ("filter1_key", b.rr (0.2f, 0.5f));
    if (sub == "Sub")
    {
        b.analog (1, b.rr (0.0f, 0.18f), 0.85f);
        b.analog (2, 0.66f, b.rr (0.08f, 0.22f) * (0.5f + q.brightness));
        b.filter (1, ftLp24, b.scaleLog (180, 900, q.brightness), b.rr (0.05f, 0.2f), 0.1f, 0.4f);
    }
    else if (sub == "Reese")
    {
        b.analog (1, 0.66f, 0.6f);
        b.analog (2, 0.66f, 0.6f, 0, 0.5f);
        b.set ("osc2_fine", b.rr (9, 20) * (b.chance (0.5f) ? 1 : -1));
        b.analog (3, 0.0f, 0.55f, -1);
        b.set ("uni_voices", (float) (2 + b.pick (2))); b.set ("uni_detune", b.rr (0.08f, 0.18f)); b.set ("uni_spread", b.rr (0.1f, 0.3f));
        b.filter (1, ftLp24, b.scaleLog (300, 1800, q.brightness), b.rr (0.1f, 0.3f), 0.1f, 0.3f, b.rr (0.1f, 0.4f) + q.aggression * 0.3f);
        b.lfo (1, lsSine, b.rr (0.08f, 0.35f), false);
        b.mod ("lfo1", "filter1_cutoff", b.rr (0.06f, 0.14f) * (0.5f + q.movement));
        b.mod ("lfo1", "osc2_fine", b.rr (0.03f, 0.08f));
    }
    else if (sub == "Acid")
    {
        b.analog (1, b.chance (0.6f) ? 0.66f : 1.0f, 0.8f, 0, b.rr (0.4f, 0.55f));
        b.filter (1, ftLadder, b.scaleLog (180, 800, q.brightness), b.rr (0.62f, 0.86f), b.rr (0.4f, 0.7f), 0.3f, b.rr (0.1f, 0.4f));
        b.env (2, 0.001f, b.rr (0.1f, 0.32f), 0.0f, 0.1f);
        b.set ("play_mode", (float) pmLegato); b.set ("glide", b.rr (0.04f, 0.12f)); b.set ("glide_legato", 1);
        b.mod ("velocity", "filter1_env", b.rr (0.15f, 0.3f));
        if (q.aggression > 0.3f) { b.fxOn ("dist_on"); b.set ("dist_type", (float) b.pickOf ({ 0, 3 })); b.set ("dist_drive", b.scaleLin (0.15f, 0.5f, q.aggression)); }
    }
    else if (sub == "Wobble")
    {
        b.wt (1, b.pickOf ({ (int) tBasic, (int) tGrit, (int) tSync, (int) tFold, (int) tTanh }), b.rr (0.3f, 0.7f), 0.7f);
        b.analog (2, 0.0f, 0.55f, -1);
        b.filter (1, ftLp24, b.scaleLog (250, 900, q.brightness), b.rr (0.25f, 0.5f), 0.0f, 0.3f, 0.2f);
        b.lfo (1, b.pickOf ({ (int) lsSine, (int) lsTri, (int) lsSawDown }), 2, true, 0, b.pickOf ({ (int) d8, (int) d4, (int) d8T, (int) d16 }));
        b.mod ("lfo1", "filter1_cutoff", b.rr (0.3f, 0.5f));
        b.mod ("lfo1", "osc1_morph", b.rr (0.15f, 0.35f));
        b.fxOn ("dist_on"); b.set ("dist_drive", b.scaleLin (0.15f, 0.55f, q.aggression));
    }
    else if (sub == "FM")
    {
        b.osc (1, otFM, 0.75f);
        b.set ("osc1_fm_algo", (float) b.pickOf ({ 0, 4 }));
        b.set ("osc1_fm_r2", b.pickOf ({ 0.5f, 1.0f, 2.0f })); b.set ("osc1_fm_i2", b.scaleLin (0.25f, 0.6f, q.brightness));
        b.set ("osc1_fm_r3", 1.0f); b.set ("osc1_fm_i3", b.rr (0.0f, 0.25f));
        b.set ("osc1_morph", 0.5f);
        b.analog (2, 0.0f, 0.5f, -1);
        b.filter (1, ftLp12, b.scaleLog (700, 4000, q.brightness), 0.1f, 0.0f, 0.3f);
        b.env (3, 0.001f, b.rr (0.08f, 0.3f), 0.0f, 0.1f);
        b.mod ("env3", "osc1_fm_i2", b.rr (0.2f, 0.4f));
    }
    else if (sub == "Growl")
    {
        // talking, vowel-like mid bass: grit wavetable + formant moving in time + clean sub
        b.set ("play_mode", (float) pmLegato); b.set ("glide", b.rr (0.03f, 0.08f)); b.set ("glide_legato", 1);
        b.wt (1, b.pickOf ({ (int) tGrit, (int) tFold, (int) tTanh, (int) tVowel }), b.rr (0.3f, 0.6f), 0.7f);
        b.analog (2, 0.0f, 0.5f, -1);
        b.filter (1, ftLp24, b.scaleLog (500, 2200, q.brightness), b.rr (0.2f, 0.4f), 0.0f, 0.3f, b.rr (0.2f, 0.4f));
        b.formant2 (b.rr (200, 500), b.rr (0.45f, 0.65f));
        b.lfo (1, b.pickOf ({ (int) lsSine, (int) lsTri, (int) lsSmoothRandom }), 2, true, 0, b.pickOf ({ (int) d8, (int) d4, (int) d8T, (int) d4D }));
        b.mod ("lfo1", "filter2_cutoff", b.rr (0.25f, 0.45f));
        b.mod ("lfo1", "osc1_morph", b.rr (0.2f, 0.4f));
        b.fxOn ("dist_on"); b.set ("dist_type", (float) b.pickOf ({ 0, 2, 3 })); b.set ("dist_drive", b.scaleLin (0.2f, 0.6f, q.aggression)); b.set ("dist_mix", b.rr (0.5f, 0.9f));
    }
    else if (sub == "Pluck")
    {
        b.analog (1, b.pickOf ({ 0.66f, 1.0f }), 0.8f, 0, b.rr (0.3f, 0.5f));
        b.analog (2, 0.0f, 0.4f, -1);
        b.filter (1, ftLadder, b.scaleLog (250, 1200, q.brightness), b.rr (0.15f, 0.4f), b.rr (0.4f, 0.65f), 0.3f);
        b.env (1, 0.001f, b.rr (0.25f, 0.6f), b.rr (0.0f, 0.2f), b.rr (0.08f, 0.2f));
        b.env (2, 0.001f, b.rr (0.12f, 0.3f), 0.0f, 0.1f);
    }
    else // Synth
    {
        b.analog (1, b.pickOf ({ 0.66f, 0.85f, 1.0f }), 0.75f, 0, b.rr (0.3f, 0.5f));
        b.analog (2, 0.0f, b.rr (0.4f, 0.6f), -1);
        b.set ("uni_voices", (float) (1 + b.pick (2))); b.set ("uni_detune", b.rr (0.05f, 0.15f)); b.set ("uni_spread", 0.2f);
        b.filter (1, ftLadder, b.scaleLog (250, 2200, q.brightness), b.rr (0.1f, 0.35f), b.rr (0.2f, 0.5f), 0.3f, q.aggression * 0.4f);
        b.env (2, 0.001f, b.rr (0.15f, 0.5f), b.rr (0.0f, 0.3f), 0.15f);
    }
    if (b.get ("uni_voices") > 1.5f) { b.fxOn ("wide_on"); b.set ("wide_width", b.rr (1.0f, 1.4f)); b.set ("wide_bass", b.rr (120, 200)); }
    if (q.aggression > 0.55f) { b.fxOn ("comp_on"); b.set ("comp_thresh", b.rr (-20, -10)); b.set ("comp_ratio", b.rr (3, 6)); }
}

void lead (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.set ("play_mode", sub == "Poly" ? (float) pmPoly : (b.chance (0.6f) ? (float) pmLegato : (float) pmMono));
    b.set ("glide", sub == "Poly" ? 0.0f : b.rr (0.02f, 0.1f));
    b.set ("glide_legato", 1);
    b.env (1, b.rr (0.001f, 0.02f), b.rr (0.3f, 1.0f), b.rr (0.75f, 1.0f), b.rr (0.1f, 0.35f));
    if (sub == "Supersaw")
    {
        b.analog (1, 0.66f, 0.7f);
        b.set ("uni_voices", (float) b.pickOf ({ 5, 7, 8 })); b.set ("uni_detune", b.rr (0.22f, 0.38f)); b.set ("uni_spread", b.rr (0.7f, 0.95f)); b.set ("uni_blend", b.rr (0.6f, 0.85f));
        b.analog (2, 0.66f, 0.25f, 1);
        b.filter (1, ftLp12, b.scaleLog (3500, 14000, q.brightness), 0.1f, 0.1f, 0.3f);
    }
    else if (sub == "Sync")
    {
        b.analog (1, 0.66f, 0.75f);
        b.set ("osc1_sync", b.rr (1.5f, 3.0f));
        b.env (3, 0.001f, b.rr (0.3f, 1.2f), b.rr (0.0f, 0.3f), 0.3f);
        b.mod ("env3", "osc1_sync", b.rr (0.2f, 0.45f));
        b.analog (2, 1.0f, 0.3f, -1, 0.45f);
        b.filter (1, ftLp24, b.scaleLog (2500, 11000, q.brightness), b.rr (0.05f, 0.2f), 0.1f, 0.4f);
    }
    else if (sub == "FM")
    {
        b.osc (1, otFM, 0.75f);
        b.set ("osc1_fm_algo", (float) b.pickOf ({ 0, 1, 2, 3 }));
        b.set ("osc1_fm_r2", b.pickOf ({ 1.0f, 2.0f, 3.0f })); b.set ("osc1_fm_i2", b.scaleLin (0.3f, 0.65f, q.brightness));
        b.set ("osc1_fm_r3", b.pickOf ({ 1.0f, 2.0f, 4.0f })); b.set ("osc1_fm_i3", b.rr (0.1f, 0.4f));
        b.set ("osc1_fm_r4", b.pickOf ({ 1.0f, 0.5f })); b.set ("osc1_fm_i4", b.rr (0.0f, 0.3f));
        b.set ("osc1_shape", b.rr (0.0f, 0.4f));
        b.filter (1, ftLp12, b.scaleLog (3000, 12000, q.brightness), 0.1f, 0.0f, 0.3f);
    }
    else if (sub == "Soft")
    {
        b.analog (1, b.rr (0.25f, 0.42f), 0.8f);
        b.analog (2, 0.0f, 0.25f, 1);
        b.filter (1, ftLp12, b.scaleLog (1800, 6000, q.brightness), 0.1f, 0.1f, 0.4f);
        b.set ("env1_attack", b.rr (0.02f, 0.08f));
    }
    else if (sub == "Poly")
    {
        b.wt (1, b.pickOf ({ (int) tBasic, (int) tHarm, (int) tTilt, (int) tSync, (int) tBrass }), b.scaleLin (0.4f, 0.95f, q.brightness), 0.65f);
        b.analog (2, 0.66f, 0.4f, 0); b.set ("osc2_fine", b.rr (5, 12));
        b.set ("uni_voices", (float) (2 + b.pick (3))); b.set ("uni_detune", b.rr (0.12f, 0.25f)); b.set ("uni_spread", 0.6f);
        b.filter (1, ftLp24, b.scaleLog (2000, 9000, q.brightness), b.rr (0.1f, 0.3f), b.rr (0.1f, 0.3f), 0.4f);
        b.env (2, 0.002f, b.rr (0.2f, 0.6f), 0.4f, 0.3f);
    }
    else if (sub == "Anthem")
    {
        // stadium lead: wide supersaw + octave layer + body, opens with the mod wheel
        b.set ("play_mode", (float) pmPoly); b.set ("glide", 0.0f);
        b.analog (1, 0.66f, 0.65f);
        b.set ("uni_voices", (float) b.pickOf ({ 6, 7, 8 })); b.set ("uni_detune", b.rr (0.25f, 0.36f)); b.set ("uni_spread", b.rr (0.85f, 1.0f)); b.set ("uni_blend", b.rr (0.65f, 0.85f));
        b.analog (2, 0.66f, b.rr (0.25f, 0.35f), 1);
        b.wt (3, b.pickOf ({ (int) tHarm, (int) tBrass, (int) tTilt }), b.rr (0.4f, 0.8f), b.rr (0.25f, 0.35f), -1);
        b.filter (1, ftLp12, b.scaleLog (3000, 11000, q.brightness), 0.12f, 0.15f, 0.35f);
        b.env (2, 0.005f, b.rr (0.4f, 1.0f), 0.6f, 0.4f);
        b.fxOn ("reverb_on"); b.set ("reverb_size", b.rr (0.7f, 0.9f)); b.set ("reverb_mix", b.scaleLin (0.15f, 0.3f, q.space));
        b.fxOn ("wide_on"); b.set ("wide_width", b.rr (1.2f, 1.5f));
    }
    else if (sub == "Vocal")
    {
        // talking lead: vowel table swept slowly + parallel formant, expressive glide
        b.set ("play_mode", (float) pmLegato); b.set ("glide", b.rr (0.04f, 0.1f));
        b.wt (1, b.pickOf ({ (int) tVowel, (int) tChoir }), b.rr (0.2f, 0.8f), 0.7f);
        b.analog (2, 0.66f, 0.3f); b.set ("osc2_fine", b.rr (4, 9));
        b.filter (1, ftLp24, b.scaleLog (2500, 9000, q.brightness), 0.12f, 0.1f, 0.4f);
        b.formant2 (b.rr (250, 700), b.rr (0.35f, 0.5f));
        b.lfo (1, lsSmoothRandom, b.rr (0.3f, 1.2f), false);
        b.mod ("lfo1", "osc1_morph", b.rr (0.25f, 0.45f));
        b.mod ("lfo1", "filter2_cutoff", b.rr (0.15f, 0.3f));
    }
    else // Mono
    {
        b.analog (1, 0.66f, 0.7f);
        b.analog (2, b.pickOf ({ 0.66f, 1.0f }), 0.35f, b.pickOf ({ 0, 1 }), 0.4f);
        b.set ("osc2_fine", b.rr (-8, 8));
        b.filter (1, ftLadder, b.scaleLog (1500, 9000, q.brightness), b.rr (0.15f, 0.4f), b.rr (0.15f, 0.4f), 0.4f, q.aggression * 0.5f);
        b.env (2, 0.002f, b.rr (0.2f, 0.7f), 0.5f, 0.3f);
    }
    // expressive vibrato (delayed) + mod wheel brightness
    b.lfo (2, lsSine, b.rr (4.8f, 6.2f), true, b.rr (0.3f, 0.9f));
    b.mod ("lfo2", "pitch", b.rr (0.006f, 0.012f));
    b.mod ("modwheel", "filter1_cutoff", b.rr (0.15f, 0.3f));
    if (q.space > 0.15f) { b.fxOn ("delay_on"); b.set ("delay_div", (float) b.pickOf ({ (int) d8D, (int) d4, (int) d8 })); b.set ("delay_mix", b.scaleLin (0.08f, 0.3f, q.space)); b.set ("delay_fb", b.rr (0.25f, 0.45f)); }
    if (q.aggression > 0.5f) { b.fxOn ("dist_on"); b.set ("dist_drive", b.scaleLin (0.1f, 0.5f, q.aggression)); b.set ("dist_mix", b.rr (0.4f, 0.9f)); }
}

void pad (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.set ("polyphony", 12);
    b.env (1, b.scaleLin (0.25f, 2.5f, 1.0f - q.aggression), b.rr (1.0f, 3.0f), b.rr (0.7f, 1.0f), b.scaleLin (1.0f, 4.5f, q.space), -0.2f, 0.4f, 0.5f);
    b.set ("uni_voices", (float) (3 + b.pick (5))); b.set ("uni_detune", b.rr (0.12f, 0.28f)); b.set ("uni_spread", b.rr (0.7f, 1.0f));
    if (sub == "Evolving")
    {
        b.wt (1, b.pickOf ({ (int) tHarm, (int) tVowel, (int) tTilt, (int) tChoir, (int) tGlass, (int) tRipple, (int) tComb }), b.rr (0.2f, 0.6f), 0.6f);
        b.osc (2, otMutation, 0.45f); b.set ("osc2_morph", b.rr (0.1f, 0.9f)); b.set ("osc2_mut_rate", b.scaleLin (0.15f, 0.6f, q.movement)); b.set ("osc2_shape", b.rr (0.2f, 0.6f));
        b.lfo (1, lsSmoothRandom, b.rr (0.05f, 0.2f), false);
        b.mod ("lfo1", "osc1_morph", b.scaleLin (0.2f, 0.5f, q.movement));
        b.lfo (2, lsSine, b.rr (0.03f, 0.12f), false);
        b.mod ("lfo2", "filter1_cutoff", b.rr (0.06f, 0.15f));
        b.filter (1, ftLp12, b.scaleLog (1500, 8000, q.brightness), b.rr (0.05f, 0.25f), 0.0f, 0.2f);
    }
    else if (sub == "Choir")
    {
        b.wt (1, b.pickOf ({ (int) tChoir, (int) tVowel }), b.rr (0.1f, 0.9f), 0.7f);
        b.analog (2, 0.66f, 0.2f);
        b.lfo (1, lsSmoothRandom, b.rr (0.08f, 0.25f), false);
        b.mod ("lfo1", "osc1_morph", b.scaleLin (0.15f, 0.4f, q.movement));
        b.filter (1, ftLp12, b.scaleLog (2500, 9000, q.brightness), 0.1f);
        b.lfo (2, lsSine, b.rr (4.5f, 5.5f), true, 1.0f);
        b.mod ("lfo2", "pitch", 0.006f);
    }
    else if (sub == "Gated")
    {
        b.analog (1, 0.66f, 0.65f);
        b.wt (2, b.pickOf ({ (int) tHarm, (int) tTilt }), b.rr (0.4f, 0.8f), 0.45f);
        b.filter (1, ftLp24, b.scaleLog (1500, 7000, q.brightness), 0.15f);
        b.set ("env1_attack", b.rr (0.01f, 0.2f));
        b.set ("tg_on", 1); b.set ("tg_rate", (float) b.pickOf ({ (int) d16, (int) d16, (int) d8 })); b.set ("tg_depth", b.rr (0.75f, 1.0f));
        b.set ("tg_steps", 16); b.set ("tg_attack", b.rr (1, 5)); b.set ("tg_release", b.rr (8, 40));
        static const uint16_t pats[] { 0b1011011010110110, 0b1110111011101110, 0b1010101011011010, 0b1101101101101101, 0b1001101010011010, 0b1111011101110111 };
        const uint16_t pat = pats[b.pick (6)];
        for (int i = 0; i < kMaxGateSteps; ++i) b.p.patterns.gate[(size_t) i] = ((pat >> (15 - (i % 16))) & 1) ? (b.chance (0.25f) ? 0.7f : 1.0f) : 0.0f;
    }
    else if (sub == "Cinematic")
    {
        // trailer-style swell: choir/glass table + detuned saw + evolving mutation layer, slow bloom
        b.set ("env1_attack", b.rr (0.8f, 2.5f));
        b.wt (1, b.pickOf ({ (int) tChoir, (int) tHarm, (int) tGlass, (int) tVowel }), b.rr (0.2f, 0.7f), 0.55f);
        b.analog (2, 0.66f, 0.4f); b.set ("osc2_fine", b.rr (6, 12));
        b.osc (3, otMutation, 0.3f); b.set ("osc3_mut_rate", b.scaleLin (0.15f, 0.5f, q.movement)); b.set ("osc3_morph", b.rr (0.2f, 0.8f)); b.set ("osc3_oct", 1);
        b.filter (1, ftLp24, b.scaleLog (800, 4500, q.brightness), b.rr (0.1f, 0.25f), 0.0f, 0.25f);
        b.lfo (1, lsSmoothRandom, b.rr (0.04f, 0.12f), false);
        b.mod ("lfo1", "osc1_morph", b.scaleLin (0.2f, 0.45f, q.movement));
        b.fxOn ("shimmer_on"); b.set ("shimmer_mix", b.scaleLin (0.12f, 0.3f, q.space)); b.set ("shimmer_amount", b.rr (0.35f, 0.6f));
    }
    else if (sub == "Dark")
    {
        b.analog (1, b.pickOf ({ 0.66f, 0.4f }), 0.7f);
        b.analog (2, 0.66f, 0.5f, -1); b.set ("osc2_fine", b.rr (-10, 10));
        b.filter (1, ftLp24, b.scaleLog (350, 1400, q.brightness * 0.6f), b.rr (0.1f, 0.3f), 0.1f, 0.2f);
        b.lfo (1, lsSine, b.rr (0.05f, 0.2f), false);
        b.mod ("lfo1", "filter1_cutoff", b.rr (0.05f, 0.12f) * (0.5f + q.movement));
    }
    else if (sub == "Bright")
    {
        b.wt (1, b.pickOf ({ (int) tGlass, (int) tHarm, (int) tTilt, (int) tBell }), b.rr (0.6f, 1.0f), 0.6f);
        b.analog (2, 0.66f, 0.35f, 1);
        b.filter (1, ftLp12, b.scaleLog (5000, 15000, q.brightness), 0.08f);
        b.lfo (1, lsTri, b.rr (0.1f, 0.4f), false);
        b.mod ("lfo1", "osc1_morph", b.rr (0.1f, 0.25f) * (0.4f + q.movement));
        if (q.space > 0.5f) { b.fxOn ("shimmer_on"); b.set ("shimmer_mix", b.rr (0.12f, 0.3f)); b.set ("shimmer_amount", b.rr (0.3f, 0.6f)); }
    }
    else // Warm
    {
        b.analog (1, 0.66f, 0.6f);
        b.analog (2, b.rr (0.4f, 0.66f), 0.5f); b.set ("osc2_fine", b.rr (6, 14));
        b.filter (1, ftLp24, b.scaleLog (900, 4000, q.brightness), b.rr (0.05f, 0.2f), 0.15f, 0.2f);
        b.env (2, b.rr (0.4f, 1.5f), b.rr (1, 3), 0.5f, 2.0f);
        b.fxOn ("chorus_on"); b.set ("chorus_mix", b.rr (0.3f, 0.5f));
    }
    b.fxOn ("reverb_on"); b.set ("reverb_size", b.scaleLin (0.6f, 0.95f, q.space)); b.set ("reverb_mix", b.scaleLin (0.18f, 0.45f, q.space)); b.set ("reverb_damp", b.rr (0.2f, 0.6f));
    if (q.space > 0.55f) { b.fxOn ("delay_on"); b.set ("delay_div", (float) b.pickOf ({ (int) d4D, (int) d2, (int) d4 })); b.set ("delay_mix", b.rr (0.1f, 0.22f)); b.set ("delay_fb", b.rr (0.3f, 0.55f)); }
}

void pluck (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.env (1, 0.0005f, b.rr (0.25f, 0.8f), 0.0f, b.rr (0.2f, 0.5f), 0.3f, 0.6f, 0.5f);
    if (sub == "Bell")
    {
        if (b.chance (0.6f))
        {
            b.osc (1, otFM, 0.7f);
            b.set ("osc1_fm_algo", (float) b.pickOf ({ 4, 6, 0 }));
            b.set ("osc1_fm_r2", b.pickOf ({ 3.5f, 4.0f, 7.0f, 2.75f })); b.set ("osc1_fm_i2", b.scaleLin (0.25f, 0.6f, q.brightness));
            b.set ("osc1_fm_r3", b.pickOf ({ 1.0f, 2.0f })); b.set ("osc1_fm_r4", b.pickOf ({ 5.0f, 9.0f, 14.0f })); b.set ("osc1_fm_i3", b.rr (0.2f, 0.5f)); b.set ("osc1_fm_i4", b.rr (0.1f, 0.3f));
            b.env (3, 0.001f, b.rr (0.2f, 0.8f), 0.0f, 0.3f);
            b.mod ("env3", "osc1_fm_i2", b.rr (0.15f, 0.3f));
        }
        else b.wt (1, b.pickOf ({ (int) tBell, (int) tGlass, (int) tMetal }), b.rr (0.2f, 0.8f), 0.7f);
        b.env (1, 0.0005f, b.rr (1.0f, 2.8f), 0.0f, b.rr (0.8f, 2.0f), 0.3f, 0.7f, 0.6f);
        b.set ("filter1_on", 0);
    }
    else if (sub == "Mallet")
    {
        b.osc (1, otFM, 0.75f);
        b.set ("osc1_fm_algo", 0); b.set ("osc1_fm_r2", b.pickOf ({ 4.0f, 3.0f, 2.0f })); b.set ("osc1_fm_i2", b.scaleLin (0.2f, 0.45f, q.brightness));
        b.env (3, 0.0005f, b.rr (0.04f, 0.12f), 0.0f, 0.1f);
        b.mod ("env3", "osc1_fm_i2", 0.3f);
        b.env (1, 0.0005f, b.rr (0.3f, 0.8f), 0.0f, b.rr (0.2f, 0.5f), 0.3f, 0.7f);
        b.filter (1, ftLp12, b.scaleLog (2500, 9000, q.brightness), 0.1f, 0.0f, 0.5f);
    }
    else if (sub == "Synth")
    {
        b.wt (1, b.pickOf ({ (int) tPluck, (int) tHarm, (int) tSync, (int) tDigital }), b.rr (0.2f, 0.8f), 0.7f);
        b.env (3, 0.0005f, b.rr (0.15f, 0.4f), 0.0f, 0.2f);
        b.mod ("env3", "osc1_morph", b.rr (0.2f, 0.4f));
        b.filter (1, ftLp24, b.scaleLog (800, 4000, q.brightness), b.rr (0.1f, 0.3f), b.rr (0.35f, 0.6f), 0.4f);
        b.env (2, 0.0005f, b.rr (0.1f, 0.35f), 0.0f, 0.2f);
    }
    else // Short
    {
        b.analog (1, b.pickOf ({ 0.66f, 1.0f, 0.85f }), 0.7f, 0, b.rr (0.3f, 0.5f));
        b.analog (2, 0.66f, 0.35f, 1); b.set ("osc2_fine", b.rr (-7, 7));
        b.set ("uni_voices", (float) (1 + b.pick (3))); b.set ("uni_detune", b.rr (0.08f, 0.2f));
        b.filter (1, ftLadder, b.scaleLog (500, 2500, q.brightness), b.rr (0.1f, 0.35f), b.rr (0.45f, 0.75f), 0.5f);
        b.env (2, 0.0005f, b.rr (0.1f, 0.3f), 0.0f, 0.2f);
    }
    b.fxOn ("delay_on"); b.set ("delay_div", (float) b.pickOf ({ (int) d8D, (int) d8, (int) d16D })); b.set ("delay_mix", b.scaleLin (0.1f, 0.3f, q.space)); b.set ("delay_fb", b.rr (0.25f, 0.5f));
    b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.08f, 0.3f, q.space)); b.set ("reverb_size", b.rr (0.4f, 0.8f));
}

void keys (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.set ("vel_sens", b.rr (0.6f, 0.9f));
    if (sub == "Organ")
    {
        b.wt (1, (int) tOrgan, b.rr (0.0f, 1.0f), 0.7f);
        b.env (1, 0.004f, 0.3f, 1.0f, b.rr (0.04f, 0.12f));
        b.set ("vel_sens", 0.1f);
        b.set ("filter1_on", 0);
        if (b.chance (0.5f)) { b.fxOn ("phaser_on"); b.set ("phaser_rate", b.rr (0.3f, 1.2f)); b.set ("phaser_mix", 0.35f); }
        else { b.fxOn ("chorus_on"); b.set ("chorus_rate", b.rr (0.6f, 1.5f)); b.set ("chorus_mix", 0.45f); }
        if (q.aggression > 0.45f) { b.fxOn ("dist_on"); b.set ("dist_type", 3); b.set ("dist_drive", b.rr (0.1f, 0.35f)); }
    }
    else if (sub == "Clav")
    {
        b.analog (1, 1.0f, 0.7f, 0, b.rr (0.08f, 0.2f));
        b.filter (1, b.chance (0.5f) ? ftBp : ftLp24, b.scaleLog (800, 3000, q.brightness), b.rr (0.2f, 0.45f), b.rr (0.3f, 0.5f), 0.6f);
        b.env (2, 0.0005f, b.rr (0.1f, 0.3f), 0.1f, 0.1f);
        b.env (1, 0.0005f, b.rr (0.4f, 0.9f), b.rr (0.2f, 0.4f), 0.08f);
        b.fxOn ("phaser_on"); b.set ("phaser_mix", b.rr (0.2f, 0.4f));
    }
    else if (sub == "Bell")
    {
        b.wt (1, b.pickOf ({ (int) tBell, (int) tGlass }), b.rr (0.2f, 0.7f), 0.6f);
        b.analog (2, 0.0f, 0.35f);
        b.env (1, 0.001f, b.rr (1.2f, 3.0f), b.rr (0.1f, 0.3f), b.rr (0.6f, 1.5f), 0.3f, 0.7f);
        b.filter (1, ftLp12, b.scaleLog (3000, 12000, q.brightness), 0.05f);
    }
    else if (sub == "Digital")
    {
        b.wt (1, b.pickOf ({ (int) tDigital, (int) tPwm, (int) tPhaseDist }), b.rr (0.2f, 0.8f), 0.65f);
        b.env (1, 0.001f, b.rr (0.8f, 2.0f), b.rr (0.2f, 0.5f), b.rr (0.3f, 0.8f));
        b.filter (1, ftLp24, b.scaleLog (2000, 8000, q.brightness), 0.1f, 0.2f, 0.5f);
        b.env (2, 0.001f, b.rr (0.3f, 0.8f), 0.3f, 0.5f);
    }
    else // Electric
    {
        b.osc (1, otFM, 0.75f);
        b.set ("osc1_fm_algo", 6); // 2>1 +3 +4
        b.set ("osc1_fm_r2", 1.0f); b.set ("osc1_fm_i2", b.scaleLin (0.2f, 0.42f, q.brightness));
        b.set ("osc1_fm_r3", 14.0f); b.set ("osc1_fm_i3", b.rr (0.04f, 0.12f) * (0.5f + q.brightness));
        b.set ("osc1_fm_r4", 1.0f); b.set ("osc1_fm_i4", 0.0f);
        b.env (3, 0.0005f, b.rr (0.2f, 0.6f), 0.0f, 0.3f);
        b.mod ("env3", "osc1_fm_i2", b.rr (0.1f, 0.25f));
        b.mod ("velocity", "osc1_fm_i2", b.rr (0.1f, 0.2f));
        b.env (1, 0.001f, b.rr (1.2f, 3.0f), b.rr (0.15f, 0.35f), b.rr (0.25f, 0.6f), 0.3f, 0.6f);
        b.set ("filter1_on", 0);
        b.fxOn (b.chance (0.5f) ? "chorus_on" : "phaser_on");
        b.set ("chorus_mix", 0.35f); b.set ("phaser_mix", 0.3f);
    }
    b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.08f, 0.28f, q.space)); b.set ("reverb_size", b.rr (0.35f, 0.7f));
}

void arp (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    // sound: tight pluck / bell
    B inner { b.p, b.r, b.q, b.chance (0.3f) ? "Bell" : (b.chance (0.5f) ? "Synth" : "Short") };
    pluck (inner);
    b.env (1, 0.0005f, b.rr (0.15f, 0.45f), b.rr (0.0f, 0.2f), b.rr (0.1f, 0.3f), 0.3f, 0.6f);
    b.set ("arp_on", 1);
    b.set ("arp_rate", (float) (sub == "Trance" ? (int) d16 : b.pickOf ({ (int) d16, (int) d16, (int) d8, (int) d16T })));
    b.set ("arp_mode", (float) (sub == "Classic" ? b.pickOf ({ 0, 2, 3 }) : sub == "Trance" ? b.pickOf ({ 0, 2 }) : sub == "Evolving" ? b.pickOf ({ 4, 7, 2 }) : 5));
    b.set ("arp_oct", (float) (1 + b.pick (sub == "Sequence" ? 1 : 3)));
    b.set ("arp_gate", b.rr (0.35f, 0.75f));
    b.set ("arp_swing", sub == "Classic" ? b.rr (0.0f, 0.25f) : 0.0f);
    const int steps = b.pickOf ({ 8, 16, 16, 12 });
    b.set ("arp_steps", (float) steps);
    for (int i = 0; i < kMaxArpSteps; ++i)
    {
        auto& st = b.p.patterns.arp[(size_t) i];
        st = {};
        st.vel = (i % 4 == 0) ? 1.0f : (i % 2 == 0 ? 0.78f : 0.62f);
        st.gate = b.chance (0.2f) ? 1.6f : 1.0f;
        if (sub == "Sequence")
        {
            st.on = b.chance (0.78f) || i == 0 ? 1.0f : 0.0f;
            st.transpose = (float) b.pickOf ({ 0, 0, 0, 12, 7, -5, 3, 5 });
        }
        if (sub == "Evolving") st.prob = b.chance (0.3f) ? b.rr (0.5f, 0.85f) : 1.0f;
        if (b.chance (0.12f + q.movement * 0.15f)) st.ratchet = 2;
    }
    if (sub == "Evolving")
    {
        b.lfo (3, lsSine, b.rr (0.05f, 0.18f), false);
        b.mod ("lfo3", "filter1_cutoff", b.rr (0.12f, 0.25f));
        b.set ("humanize", b.rr (0.1f, 0.35f));
    }
    b.fxOn ("delay_on"); b.set ("delay_div", (float) b.pickOf ({ (int) d8D, (int) d16D, (int) d4T })); b.set ("delay_mix", b.scaleLin (0.12f, 0.3f, q.space));
}

void strings (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    if (sub == "Pizzicato")
    {
        b.analog (1, 0.66f, 0.7f);
        b.set ("uni_voices", 2); b.set ("uni_detune", 0.1f);
        b.filter (1, ftLp24, b.scaleLog (900, 3000, q.brightness), 0.1f, 0.4f, 0.5f);
        b.env (2, 0.0005f, b.rr (0.08f, 0.2f), 0.0f, 0.1f);
        b.env (1, 0.001f, b.rr (0.2f, 0.4f), 0.0f, b.rr (0.2f, 0.4f));
    }
    else if (sub == "Solo")
    {
        b.set ("play_mode", (float) pmLegato); b.set ("glide", b.rr (0.03f, 0.08f)); b.set ("glide_legato", 1);
        b.analog (1, 0.66f, 0.75f);
        b.filter (1, ftLp24, b.scaleLog (2000, 6000, q.brightness), 0.15f, 0.1f, 0.4f);
        b.set ("filter2_on", 1); b.set ("filter2_type", (float) ftFormant); b.set ("filter2_cutoff", b.rr (150, 400)); b.set ("filter2_reso", 0.4f);
        b.set ("filter_routing", 1); b.set ("filter_balance", 0.35f);
        b.env (1, b.rr (0.06f, 0.2f), 1.0f, 0.9f, b.rr (0.3f, 0.7f));
    }
    else if (sub == "Hybrid")
    {
        // orchestral section + pulsing synth layer (rhythmic ostinato feel through the trance gate)
        b.analog (1, 0.66f, 0.6f);
        b.wt (2, b.pickOf ({ (int) tPwm, (int) tHarm, (int) tBrass }), b.rr (0.3f, 0.7f), 0.4f);
        b.analog (3, 0.0f, 0.3f, -1);
        b.set ("uni_voices", (float) (4 + b.pick (3))); b.set ("uni_detune", b.rr (0.12f, 0.22f)); b.set ("uni_spread", b.rr (0.8f, 1.0f));
        b.filter (1, ftLp24, b.scaleLog (1800, 6500, q.brightness), b.rr (0.1f, 0.25f), 0.15f, 0.3f);
        b.env (1, b.rr (0.05f, 0.2f), 1.2f, 0.85f, b.rr (0.5f, 1.2f), -0.2f);
        b.set ("tg_on", 1); b.set ("tg_rate", (float) b.pickOf ({ (int) d16, (int) d8 })); b.set ("tg_depth", b.rr (0.35f, 0.6f));
        b.set ("tg_steps", 16); b.set ("tg_attack", b.rr (2, 6)); b.set ("tg_release", b.rr (20, 60));
        static const uint16_t pats[] { 0b1011101110111011, 0b1110111011101110, 0b1011011010110110, 0b1111111011111110 };
        const uint16_t pat = pats[b.pick (4)];
        for (int i = 0; i < kMaxGateSteps; ++i) b.p.patterns.gate[(size_t) i] = ((pat >> (15 - (i % 16))) & 1) ? 1.0f : 0.25f;
        b.fxOn ("chorus_on"); b.set ("chorus_mix", b.rr (0.2f, 0.35f));
    }
    else
    {
        b.analog (1, 0.66f, 0.6f);
        if (sub == "Synth") b.wt (2, (int) tPwm, b.rr (0.3f, 0.7f), 0.45f); else b.analog (2, 0.66f, 0.3f, 1);
        b.set ("uni_voices", (float) (4 + b.pick (3))); b.set ("uni_detune", b.rr (0.12f, 0.22f)); b.set ("uni_spread", b.rr (0.75f, 1.0f));
        b.filter (1, ftLp24, b.scaleLog (2200, 7000, q.brightness), 0.1f, 0.1f, 0.3f);
        b.env (1, b.rr (0.15f, 0.6f), 1.2f, 0.85f, b.rr (0.6f, 1.6f), -0.2f);
        b.fxOn ("chorus_on"); b.set ("chorus_mix", b.rr (0.2f, 0.4f));
    }
    b.lfo (2, lsSine, b.rr (4.8f, 5.8f), true, b.rr (0.6f, 1.4f));
    b.mod ("lfo2", "pitch", b.rr (0.005f, 0.009f));
    b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.15f, 0.4f, q.space)); b.set ("reverb_size", b.rr (0.6f, 0.9f));
}

void brass (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.analog (1, 0.66f, 0.65f);
    b.analog (2, b.pickOf ({ 0.66f, 1.0f }), 0.4f, 0, 0.4f); b.set ("osc2_fine", b.rr (4, 10));
    b.set ("uni_voices", (float) (1 + b.pick (3))); b.set ("uni_detune", b.rr (0.08f, 0.16f));
    b.filter (1, ftLp24, b.scaleLog (600, 1800, q.brightness), b.rr (0.05f, 0.2f), b.rr (0.35f, 0.55f), 0.5f, q.aggression * 0.3f);
    if (sub == "Stab")
    {
        b.env (2, 0.005f, b.rr (0.15f, 0.3f), 0.1f, 0.2f);
        b.env (1, 0.002f, b.rr (0.25f, 0.45f), 0.0f, b.rr (0.15f, 0.3f));
    }
    else
    {
        b.env (2, b.rr (0.03f, 0.1f), b.rr (0.3f, 0.8f), b.rr (0.35f, 0.6f), 0.3f, 0.3f);
        b.env (1, b.rr (0.02f, 0.06f), 0.8f, 0.85f, b.rr (0.15f, 0.35f));
    }
    if (sub == "Horn") { b.set ("filter2_on", 1); b.set ("filter2_type", (float) ftFormant); b.set ("filter2_cutoff", b.rr (100, 300)); b.set ("filter2_reso", 0.35f); b.set ("filter_routing", 1); b.set ("filter_balance", 0.3f); }
    if (sub == "Trailer")
    {
        // huge low brass: saws an octave apart, slow "blat" swell, drive and a big hall
        b.analog (3, 0.66f, 0.45f, -1); b.set ("osc3_fine", b.rr (-6, 6));
        b.set ("uni_voices", (float) (3 + b.pick (3))); b.set ("uni_detune", b.rr (0.12f, 0.2f)); b.set ("uni_spread", b.rr (0.5f, 0.8f));
        b.set ("filter1_cutoff", b.scaleLog (350, 1100, q.brightness)); b.set ("filter1_env", b.rr (0.5f, 0.7f));
        b.env (2, b.rr (0.12f, 0.35f), b.rr (0.8f, 1.6f), b.rr (0.4f, 0.6f), 0.5f, 0.3f);
        b.env (1, b.rr (0.05f, 0.15f), 1.0f, 0.9f, b.rr (0.4f, 0.8f));
        b.formant2 (b.rr (120, 260), b.rr (0.2f, 0.3f));
        b.fxOn ("dist_on"); b.set ("dist_type", 0.0f); b.set ("dist_drive", b.scaleLin (0.1f, 0.35f, q.aggression)); b.set ("dist_mix", b.rr (0.3f, 0.6f));
        b.set ("reverb_size", b.rr (0.8f, 0.95f));
    }
    b.mod ("velocity", "filter1_cutoff", b.rr (0.1f, 0.2f));
    b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.1f, 0.3f, q.space));
}

void fx (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    if (sub == "Riser" || sub == "Downer")
    {
        b.analog (1, 0.66f, 0.55f);
        b.set ("uni_voices", 6); b.set ("uni_detune", 0.3f);
        b.osc (2, otNoise, 0.35f); b.set ("osc2_noise", (float) ntWhite); b.set ("osc2_morph", 0.8f);
        b.filter (1, ftLp24, b.scaleLog (400, 1500, q.brightness), 0.35f, 0.0f, 0.2f);
        if (sub == "Riser")
        {
            b.env (3, b.rr (1.5f, 3.0f), 1.0f, 1.0f, 1.0f, -0.4f);
            b.mod ("env3", "pitch", b.rr (0.35f, 0.5f));
            b.mod ("env3", "filter1_cutoff", b.rr (0.35f, 0.5f));
        }
        else
        {
            b.env (3, 0.001f, b.rr (1.2f, 2.5f), 0.0f, 0.5f, 0.0f, 0.3f);
            b.mod ("env3", "pitch", b.rr (0.35f, 0.5f));
            b.mod ("env3", "filter1_cutoff", b.rr (0.3f, 0.5f));
        }
        b.env (1, 0.01f, 3.0f, 1.0f, b.rr (0.5f, 1.5f));
    }
    else if (sub == "Noise")
    {
        b.osc (1, otNoise, 0.7f); b.set ("osc1_noise", (float) b.pickOf ({ ntWhite, ntPink, ntDigital })); b.set ("osc1_morph", b.rr (0.5f, 1.0f));
        b.filter (1, b.pickOf ({ (int) ftCombPos, (int) ftBp, (int) ftFormant }), b.scaleLog (300, 3000, q.brightness), b.rr (0.5f, 0.85f), 0.0f, 0.5f);
        b.lfo (1, lsSmoothRandom, b.rr (0.2f, 1.5f), false);
        b.mod ("lfo1", "filter1_cutoff", b.rr (0.2f, 0.4f));
        b.env (1, b.rr (0.05f, 0.5f), 1.0f, 1.0f, b.rr (0.5f, 1.5f));
    }
    else if (sub == "Impact")
    {
        b.analog (1, 0.0f, 0.85f, -1);
        b.osc (2, otNoise, 0.45f); b.set ("osc2_noise", (float) ntPink);
        b.env (3, 0.0005f, b.rr (0.08f, 0.25f), 0.0f, 0.2f);
        b.mod ("env3", "pitch", b.rr (0.6f, 0.9f));
        b.env (1, 0.0005f, b.rr (0.8f, 1.6f), 0.0f, 0.8f, 0.3f, 0.7f);
        b.filter (1, ftLp24, b.scaleLog (800, 5000, q.brightness), 0.1f, 0.3f, 0.0f);
        b.fxOn ("dist_on"); b.set ("dist_drive", b.rr (0.2f, 0.5f));
    }
    else // Texture
    {
        b.osc (1, otMutation, 0.6f); b.set ("osc1_mut_rate", b.rr (0.4f, 0.9f)); b.set ("osc1_shape", b.rr (0.4f, 0.9f)); b.set ("osc1_morph", b.rr (0, 1));
        b.wt (2, b.pickOf ({ (int) tNoise, (int) tMetal, (int) tComb, (int) tGrit }), b.rr (0, 1), 0.4f);
        b.lfo (1, lsSampleHold, b.rr (2.0f, 8.0f), false);
        b.mod ("lfo1", "osc2_morph", b.rr (0.2f, 0.5f));
        b.filter (1, ftLp12, b.scaleLog (1500, 8000, q.brightness), 0.2f);
        b.env (1, b.rr (0.1f, 0.8f), 1.0f, 1.0f, b.rr (0.8f, 2.0f));
        b.fxOn ("granular_on"); b.set ("granular_mix", b.rr (0.25f, 0.5f));
    }
    b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.2f, 0.45f, q.space)); b.set ("reverb_size", b.rr (0.6f, 0.95f));
    if (b.chance (0.5f)) { b.fxOn ("delay_on"); b.set ("delay_mix", b.rr (0.1f, 0.25f)); }
}

void drums (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.set ("vel_sens", 0.75f);
    b.set ("filter1_on", 0);
    if (sub == "Kick")
    {
        b.analog (1, 0.0f, 0.95f, -1);
        b.env (3, 0.0005f, b.rr (0.03f, 0.08f), 0.0f, 0.05f, 0.0f, 0.6f);
        b.mod ("env3", "pitch", b.rr (0.85f, 1.0f));
        b.env (1, 0.0005f, b.rr (0.25f, 0.6f), 0.0f, 0.06f, 0.0f, 0.4f);
        b.osc (2, otNoise, 0.0f); b.set ("osc2_noise", (float) ntWhite);
        b.env (4, 0.0005f, 0.012f, 0.0f, 0.01f);
        b.mod ("env4", "osc2_level", b.rr (0.08f, 0.2f) * (0.4f + q.brightness));
        if (q.aggression > 0.4f) { b.fxOn ("dist_on"); b.set ("dist_drive", b.scaleLin (0.1f, 0.4f, q.aggression)); }
    }
    else if (sub == "Snare")
    {
        b.analog (1, 0.3f, 0.55f, -1); b.set ("osc1_semi", (float) b.pick (5));
        b.env (3, 0.0005f, b.rr (0.02f, 0.05f), 0.0f, 0.05f);
        b.mod ("env3", "pitch", b.rr (0.2f, 0.35f));
        b.osc (2, otNoise, 0.7f); b.set ("osc2_noise", (float) ntWhite); b.set ("osc2_morph", b.scaleLin (0.6f, 1.0f, q.brightness));
        b.filter (1, ftHp12, b.rr (150, 300), 0.1f, 0.0f, 0.0f);
        b.env (1, 0.0005f, b.rr (0.12f, 0.28f), 0.0f, 0.08f, 0.0f, 0.6f);
    }
    else if (sub == "Hat")
    {
        b.osc (1, otNoise, 1.0f); b.set ("osc1_noise", (float) ntWhite); b.set ("osc1_morph", 1.0f);
        b.osc (2, otFM, 0.3f); b.set ("osc2_fm_r2", 3.5f); b.set ("osc2_fm_i2", 0.8f); b.set ("osc2_oct", 3);
        b.filter (1, b.chance (0.5f) ? ftHp12 : ftHp24, b.scaleLog (3000, 8000, q.brightness), 0.25f, 0.0f, 0.0f);
        const bool open = b.chance (0.35f);
        b.env (1, 0.0005f, open ? b.rr (0.25f, 0.5f) : b.rr (0.03f, 0.1f), 0.0f, open ? 0.2f : 0.04f, 0.0f, 0.7f);
    }
    else if (sub == "Tom")
    {
        b.analog (1, 0.1f, 0.9f, -1); b.set ("osc1_semi", (float) b.pick (8));
        b.env (3, 0.0005f, b.rr (0.08f, 0.2f), 0.0f, 0.1f);
        b.mod ("env3", "pitch", b.rr (0.25f, 0.45f));
        b.env (1, 0.0005f, b.rr (0.3f, 0.7f), 0.0f, 0.1f, 0.0f, 0.5f);
    }
    else // Perc
    {
        b.osc (1, otFM, 0.75f);
        b.set ("osc1_fm_algo", (float) b.pickOf ({ 0, 3 })); b.set ("osc1_fm_r2", b.pickOf ({ 3.5f, 5.0f, 7.25f, 2.25f })); b.set ("osc1_fm_i2", b.rr (0.4f, 0.75f));
        b.set ("osc1_fm_r3", b.pickOf ({ 1.5f, 2.75f })); b.set ("osc1_fm_i3", b.rr (0.0f, 0.4f));
        b.env (1, 0.0005f, b.rr (0.08f, 0.3f), 0.0f, 0.08f, 0.0f, 0.6f);
        b.env (3, 0.0005f, b.rr (0.02f, 0.06f), 0.0f, 0.05f);
        b.mod ("env3", "pitch", b.rr (0.05f, 0.2f));
    }
    if (q.space > 0.3f) { b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.05f, 0.25f, q.space)); b.set ("reverb_size", b.rr (0.2f, 0.6f)); }
}

void ambient (B& b)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    b.set ("polyphony", 12);
    b.env (1, b.rr (1.0f, 4.0f), 3.0f, 1.0f, b.rr (2.5f, 6.0f), -0.4f);
    if (sub == "Drone")
    {
        b.osc (1, otMutation, 0.55f); b.set ("osc1_mut_rate", b.rr (0.1f, 0.35f)); b.set ("osc1_morph", b.rr (0, 1));
        b.wt (2, b.pickOf ({ (int) tVowel, (int) tHarm, (int) tTilt, (int) tRipple }), b.rr (0, 1), 0.45f, -1);
        b.lfo (1, lsSmoothRandom, b.rr (0.03f, 0.1f), false);
        b.mod ("lfo1", "osc2_morph", b.rr (0.2f, 0.4f));
        b.filter (1, ftLp12, b.scaleLog (800, 5000, q.brightness), 0.15f);
    }
    else if (sub == "Texture")
    {
        b.osc (1, otNoise, 0.35f); b.set ("osc1_noise", (float) ntPink);
        b.wt (2, b.pickOf ({ (int) tGlass, (int) tChoir, (int) tComb }), b.rr (0, 1), 0.5f);
        b.filter (1, ftCombPos, b.scaleLog (150, 900, q.brightness), b.rr (0.6f, 0.85f), 0.0f, 1.0f);
        b.lfo (1, lsSmoothRandom, b.rr (0.05f, 0.2f), false);
        b.mod ("lfo1", "osc2_morph", 0.3f);
        b.fxOn ("granular_on"); b.set ("granular_mix", b.rr (0.25f, 0.5f)); b.set ("granular_spread", b.rr (0.4f, 0.9f));
    }
    else if (sub == "Cathedral")
    {
        // sacred space: organ/choir tables, mutation shimmer layer, endless shimmer reverb
        b.wt (1, b.pickOf ({ (int) tOrgan, (int) tChoir, (int) tHarm }), b.rr (0.2f, 0.8f), 0.55f);
        b.wt (2, b.pickOf ({ (int) tChoir, (int) tGlass }), b.rr (0, 1), 0.35f, 1);
        b.osc (3, otMutation, 0.25f); b.set ("osc3_mut_rate", b.rr (0.1f, 0.3f)); b.set ("osc3_morph", b.rr (0, 1));
        b.filter (1, ftLp12, b.scaleLog (2000, 8000, q.brightness), 0.1f);
        b.lfo (1, lsSmoothRandom, b.rr (0.03f, 0.08f), false);
        b.mod ("lfo1", "osc2_morph", b.rr (0.25f, 0.45f));
        b.set ("uni_voices", 3); b.set ("uni_detune", b.rr (0.08f, 0.15f)); b.set ("uni_spread", 1.0f);
    }
    else // Soundscape
    {
        b.wt (1, b.pickOf ({ (int) tChoir, (int) tVowel, (int) tGlass, (int) tHarm }), b.rr (0, 1), 0.6f);
        b.analog (2, 0.0f, 0.35f, -1);
        b.set ("uni_voices", 4); b.set ("uni_detune", b.rr (0.1f, 0.2f)); b.set ("uni_spread", 1.0f);
        b.lfo (1, lsSine, b.rr (0.02f, 0.08f), false);
        b.mod ("lfo1", "osc1_morph", 0.35f);
        b.filter (1, ftLp12, b.scaleLog (1500, 7000, q.brightness), 0.1f);
    }
    b.fxOn ("reverb_on"); b.set ("reverb_size", b.rr (0.85f, 1.0f)); b.set ("reverb_mix", b.scaleLin (0.3f, 0.55f, q.space));
    if (q.space > 0.4f) { b.fxOn ("shimmer_on"); b.set ("shimmer_mix", b.scaleLin (0.1f, 0.35f, q.space)); }
    b.fxOn ("delay_on"); b.set ("delay_div", (float) b.pickOf ({ (int) d2, (int) d4D })); b.set ("delay_mix", b.rr (0.1f, 0.2f)); b.set ("delay_fb", b.rr (0.4f, 0.6f));
}

// ---- v1.1: layering with a role per oscillator --------------------------------------------
thread_local int gAirOsc = 0; // osc slot used as "air" layer in the current candidate (for macro 1)

void addLayers (B& b, const juce::String& cat)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    gAirOsc = 0;
    if (cat == "Drums/Perc" || cat == "FX") return;
    const bool sustainedCat = cat == "Pad" || cat == "Strings" || cat == "Ambient" || cat == "Brass" || cat == "Lead";
    // AIR: a quiet, bright layer an octave up gives size and sheen
    if ((sustainedCat || (cat == "Keys" && (sub == "Digital" || sub == "Bell")) || (cat == "Pluck" && sub == "Bell"))
        && cat != "Bass" && b.chance (cat == "Lead" ? 0.4f : 0.65f))
        if (const int i = b.freeOsc())
        {
            b.wt (i, b.pickOf ({ (int) tGlass, (int) tHarm, (int) tChoir, (int) tBell }), b.rr (0.4f, 1.0f), b.rr (0.1f, 0.18f) * (0.6f + q.brightness * 0.6f), 1);
            b.set (b.o (i, "fine"), b.rr (-7, 7));
            b.set (b.o (i, "pan"), b.rr (-0.35f, 0.35f));
            gAirOsc = i;
        }
    // WEIGHT: a clean sine sub under basses and big sustained sounds (keeps the low end solid)
    bool hasLow = false;
    for (int i = 1; i <= 4; ++i) hasLow |= b.on (b.o (i, "on")) && b.get (b.o (i, "oct").toRawUTF8()) < -0.5f;
    if (! hasLow && (cat == "Bass" ? b.chance (0.6f) : (sub == "Cinematic" || sub == "Trailer" || sub == "Hybrid" || (cat == "Pad" && b.chance (0.25f)))))
        if (const int i = b.freeOsc())
            b.analog (i, 0.0f, cat == "Bass" ? b.rr (0.35f, 0.5f) : b.rr (0.15f, 0.25f), -1);
    // MOTION: a low-level spectral-mutation layer that keeps the timbre alive
    if ((cat == "Pad" || cat == "Ambient" || cat == "Strings" || (cat == "Keys" && sub == "Digital") || (cat == "Lead" && sub == "Soft"))
        && b.chance (0.3f + 0.4f * q.movement))
        if (const int i = b.freeOsc())
        {
            b.osc (i, otMutation, b.rr (0.15f, 0.28f));
            b.set (b.o (i, "mut_rate"), b.scaleLin (0.1f, 0.5f, q.movement));
            b.set (b.o (i, "morph"), b.rr (0.1f, 0.9f));
            b.set (b.o (i, "shape"), b.rr (0.2f, 0.5f));
        }
}

// ---- v1.1: Infinitum signature features where they shine ---------------------------------------
void signature (B& b, const juce::String& cat)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    // Shimmer on big sustained sounds
    if ((cat == "Pad" || cat == "Ambient" || cat == "Strings" || (cat == "Keys" && sub == "Bell") || (cat == "Pluck" && sub == "Bell"))
        && ! b.on ("shimmer_on") && q.space > 0.3f && b.chance (0.5f))
    { b.fxOn ("shimmer_on"); b.set ("shimmer_mix", b.scaleLin (0.08f, 0.25f, q.space)); b.set ("shimmer_amount", b.rr (0.3f, 0.6f)); }
    // Granular Smear on textures and evolving sounds
    if ((cat == "Ambient" || cat == "FX" || sub == "Evolving" || sub == "Cinematic" || (cat == "Arp" && sub == "Evolving"))
        && ! b.on ("granular_on") && b.chance (0.3f + 0.3f * q.movement))
    { b.fxOn ("granular_on"); b.set ("granular_mix", b.rr (0.1f, 0.28f)); b.set ("granular_spread", b.rr (0.4f, 0.9f)); }
    // Formant colour on vocal-ish sounds (only if filter 2 is free)
    if (! b.on ("filter2_on") && ((cat == "Lead" && (sub == "Mono" || sub == "Soft")) || sub == "Choir" || (cat == "Strings" && sub == "Ensemble") || (cat == "Pad" && sub == "Warm"))
        && b.chance (0.35f))
        b.formant2 (b.rr (250, 800), b.rr (0.2f, 0.35f));
    // Comb resonance on bells and mallets (metallic body)
    if (! b.on ("filter2_on") && cat == "Pluck" && (sub == "Bell" || sub == "Mallet") && b.chance (0.35f))
    {
        b.set ("filter2_on", 1); b.set ("filter2_type", (float) ftCombPos); b.set ("filter2_cutoff", b.rr (300, 1200));
        b.set ("filter2_reso", b.rr (0.4f, 0.65f)); b.set ("filter2_key", 1.0f); b.set ("filter_routing", 1); b.set ("filter_balance", b.rr (0.15f, 0.3f));
    }
    // Classic keys motion: stereo tremolo (electric piano / clav) or a slow phaser (organ)
    if (cat == "Keys" && (sub == "Electric" || sub == "Clav" || sub == "Organ") && b.chance (0.75f))
        if (const int l = b.freeLfo())
        {
            const auto src = ("lfo" + juce::String (l)).toStdString();
            if (sub == "Organ") { b.lfo (l, lsSine, b.rr (0.5f, 1.2f), false); b.fxOn ("phaser_on"); b.set ("phaser_mix", b.rr (0.2f, 0.35f)); b.modIf (src.c_str(), "phaser_mix", 0.15f); }
            else { b.lfo (l, lsSine, b.rr (3.5f, 6.0f), false); b.modIf (src.c_str(), "osc1_pan", b.rr (0.35f, 0.6f)); if (b.on ("osc2_on")) b.modIf (src.c_str(), "osc2_pan", -b.rr (0.35f, 0.6f)); }
        }
    // Width for the narrow families (leads, keys, plucks); basses stay mono-compatible
    if ((cat == "Lead" || cat == "Keys" || cat == "Pluck" || cat == "Arp") && ! b.on ("wide_on") && ! b.on ("chorus_on") && b.chance (0.55f))
    {
        if (b.chance (0.5f)) { b.fxOn ("chorus_on"); b.set ("chorus_mix", b.rr (0.18f, 0.32f)); }
        else { b.fxOn ("wide_on"); b.set ("wide_width", b.rr (1.15f, 1.45f)); b.set ("wide_bass", b.rr (150, 250)); }
    }
}

// ---- v1.1: long-form motion ("epic" bloom) - uses free envelopes/LFOs and slots only ----------
void epicMotion (B& b, const juce::String& cat)
{
    const auto& q = b.q;
    const auto sub = b.sub;
    const bool big = cat == "Pad" || cat == "Ambient" || cat == "Strings" || cat == "Brass" || (cat == "Lead" && (sub == "Anthem" || sub == "Supersaw"));
    if (! big || cat == "Drums/Perc") return;
    // slow bloom: env 4 opens filter and timbre over several seconds
    const float bloomChance = cat == "Pad" || cat == "Ambient" || sub == "Cinematic" || sub == "Trailer" ? 0.75f : 0.45f;
    if (! b.envUsed (4) && b.chance (bloomChance))
    {
        b.env (4, b.scaleLin (1.5f, 7.0f, q.movement), 2.0f, 1.0f, 3.0f, -0.4f);
        b.modIf ("env4", "filter1_cutoff", b.rr (0.12f, 0.25f));
        b.modIf ("env4", "osc1_morph", b.rr (0.08f, 0.18f));
    }
    // very slow drift: an LFO wandering over the timbre (seconds to tens of seconds per cycle)
    if (const int l = b.freeLfo(); l > 0 && b.chance (0.6f))
    {
        b.lfo (l, lsSmoothRandom, b.rr (0.03f, 0.12f), false);
        const auto src = ("lfo" + juce::String (l)).toStdString();
        b.modIf (src.c_str(), b.on ("osc2_on") ? "osc2_morph" : "osc1_morph", b.rr (0.1f, 0.25f));
        if (b.on ("filter2_on")) b.modIf (src.c_str(), "filter2_cutoff", b.rr (0.08f, 0.18f));
    }
}

// ---- cross-category modifiers -----------------------------------------------------------
void applyCharacter (B& b, const juce::String& cat)
{
    const auto& q = b.q;
    const bool tonal = cat != "Drums/Perc";
    // Movement: macro-style motion on timbre
    if (tonal && q.movement > 0.55f && cat != "Arp")
    {
        int lfoIdx = 4;
        b.lfo (lfoIdx, b.pickOf ({ (int) lsSine, (int) lsTri, (int) lsSmoothRandom }), b.scaleLog (0.08f, 3.0f, q.movement), false);
        const char* targets[] { "osc1_morph", "filter1_cutoff", "osc1_shape" };
        b.mod ("lfo4", targets[b.pick (3)], b.scaleLin (0.05f, 0.25f, q.movement));
        if (q.movement > 0.75f && b.get ("chorus_on") < 0.5f && b.chance (0.5f)) { b.fxOn ("phaser_on"); b.set ("phaser_mix", b.rr (0.2f, 0.4f)); }
    }
    // Step sequencer as a rhythmic modulation source (filter / timbre steps)
    if (tonal && q.movement > 0.5f && cat != "Drums/Perc" && b.chance (0.3f + 0.3f * q.movement))
    {
        b.set ("seq_rate", (float) b.pickOf ({ (int) d16, (int) d8, (int) d16 }));
        const int steps = b.pickOf ({ 8, 16, 16 });
        b.set ("seq_steps", (float) steps);
        b.set ("seq_glide", b.chance (0.5f) ? b.rr (0.1f, 0.5f) : 0.0f);
        static const float shapes[4][8] { { 1, -0.2f, 0.5f, -0.6f, 0.8f, -0.4f, 0.2f, -1 }, { -1, -0.5f, 0, 0.5f, 1, 0.5f, 0, -0.5f },
                                          { 1, -1, 1, -1, 0.3f, -0.3f, 0.6f, -0.6f }, { 0.8f, 0.8f, -0.3f, 0.4f, -0.8f, 0.1f, 0.6f, -0.5f } };
        const int sh = b.pick (4);
        for (int i = 0; i < kMaxSeqSteps; ++i) b.p.patterns.seq[(size_t) i] = juce::jlimit (-1.0f, 1.0f, shapes[sh][i % 8] + b.rr (-0.15f, 0.15f));
        b.mod ("stepseq", b.chance (0.6f) ? "filter1_cutoff" : "osc1_morph", b.scaleLin (0.12f, 0.3f, q.movement));
        if (b.get ("filter1_on") < 0.5f) b.filter (1, ftLp12, 6000, 0.15f);
    }
    // Weirdness (v1.1): controlled originality - musical surprises, never noise for its own sake
    if (q.weirdness > 0.4f && tonal)
    {
        const int w = b.pick (6);
        if (w == 0) { if (const int i = b.freeOsc()) { b.osc (i, otMutation, b.scaleLin (0.2f, 0.4f, q.weirdness)); b.set (b.o (i, "mut_rate"), b.rr (0.3f, 0.8f)); b.set (b.o (i, "shape"), q.weirdness); } }
        if (w == 1) { b.fxOn ("granular_on"); b.set ("granular_mix", b.scaleLin (0.1f, 0.3f, q.weirdness)); b.set ("granular_pitch", b.rr (0.0f, 0.3f)); }
        if (w == 2) // harmonic-but-unusual FM colour layer (fifths, octave-and-fifth)
            if (const int i = b.freeOsc())
            {
                b.osc (i, otFM, b.rr (0.15f, 0.3f));
                b.set (b.o (i, "fm_algo"), (float) b.pickOf ({ 0, 1, 4 }));
                b.set (b.o (i, "fm_r2"), b.pickOf ({ 1.5f, 3.0f, 0.5f, 2.0f })); b.set (b.o (i, "fm_i2"), b.scaleLin (0.2f, 0.5f, q.weirdness));
            }
        if (w == 3) { b.set ("osc1_fold", b.scaleLin (0.1f, 0.35f, q.weirdness)); }
        if (w == 4) b.modIf ("key", "osc1_morph", b.rr (0.2f, 0.45f));                    // timbre changes across the keyboard
        if (w == 5 && ! b.on ("filter2_on")) { b.formant2 (b.rr (250, 800), b.rr (0.25f, 0.4f)); b.modIf ("velocity", "filter2_cutoff", b.rr (0.2f, 0.4f)); }
        if (q.weirdness > 0.85f && b.chance (0.5f)) { b.fxOn ("crush_on"); b.set ("crush_bits", b.rr (8, 12)); b.set ("crush_rate", b.rr (1, 3)); b.set ("crush_mix", b.rr (0.15f, 0.35f)); }
        if (q.weirdness > 0.75f) b.modIf ("random", "osc1_morph", b.rr (0.1f, 0.25f));
    }
    // Aggression: harmonics & dynamics
    if (q.aggression > 0.7f && b.get ("dist_on") < 0.5f && tonal && cat != "Pad" && cat != "Ambient")
    { b.fxOn ("dist_on"); b.set ("dist_type", (float) b.pickOf ({ 0, 3, 2 })); b.set ("dist_drive", b.scaleLin (0.1f, 0.4f, q.aggression)); b.set ("dist_mix", b.rr (0.4f, 0.8f)); }
    // Space: always at least a touch of air for melodic sounds
    if (q.space > 0.6f && b.get ("reverb_on") < 0.5f && cat != "Bass") { b.fxOn ("reverb_on"); b.set ("reverb_mix", b.scaleLin (0.1f, 0.3f, q.space)); }
    if (q.space > 0.7f && cat != "Bass" && cat != "Drums/Perc") { b.fxOn ("wide_on"); b.set ("wide_width", b.rr (1.2f, 1.6f)); }
    // Macros (v1.1): each one changes something musically clear
    // 1 Brightness: filter + air layer
    b.mod ("macro1", "filter1_cutoff", 0.35f);
    if (gAirOsc) b.modIf ("macro1", b.o (gAirOsc, "level"), 0.3f);
    // 2 Timbre: main and second oscillator shape
    b.mod ("macro2", "osc1_morph", 0.3f);
    if (b.on ("osc2_on") && b.get ("osc2_type") != (float) otNoise) b.modIf ("macro2", "osc2_morph", 0.25f);
    // 3 Space: reverb, delay, shimmer together
    if (b.on ("reverb_on")) b.modIf ("macro3", "reverb_mix", 0.3f);
    if (b.on ("delay_on")) b.modIf ("macro3", "delay_mix", 0.2f);
    if (b.on ("shimmer_on")) b.modIf ("macro3", "shimmer_mix", 0.2f);
    // 4 Character: resonance plus drive (or wavefold when there is no distortion)
    b.mod ("macro4", "filter1_reso", 0.3f);
    if (b.on ("dist_on")) b.modIf ("macro4", "dist_drive", 0.3f); else if (tonal) b.modIf ("macro4", "osc1_fold", 0.25f);
    // Mod wheel: a big, playable "open up" morph on every tonal sound
    if (tonal)
    {
        b.modIf ("modwheel", "filter1_cutoff", b.rr (0.2f, 0.35f));
        b.modIf ("modwheel", "osc1_morph", b.rr (0.12f, 0.25f));
        if (b.on ("reverb_on")) b.modIf ("modwheel", "reverb_mix", b.rr (0.1f, 0.2f));
    }
}

void fxOrder (B& b)
{
    // musically sensible signal chain: drive -> tone -> modulation -> time -> dynamics -> width
    b.p.patterns.fxOrder = { fxDist, fxCrush, fxEq, fxComp, fxPhaser, fxFlanger, fxChorus, fxGranular, fxDelay, fxShimmer, fxReverb, fxWidener };
}
} // namespace

// ======================================================================= locks
bool paramInLock (const ParamDef& d, const juce::String& lock)
{
    const auto& g = d.group;
    if (lock == "osc") return g.startsWith ("osc") || d.id.startsWith ("uni_");
    if (lock == "filter") return g.startsWith ("filter") || d.id == "filter_routing" || d.id == "filter_balance";
    if (lock == "env") return g.startsWith ("env");
    if (lock == "mod") return g.startsWith ("lfo") || g == "matrix" || g == "macros";
    if (lock == "fx") return g.startsWith ("fx.");
    if (lock == "rhythm") return g == "arp" || g == "gate" || g == "seq" || d.id == "chord" || d.id == "strum";
    return false;
}

void applyLocks (Preset& dst, const Preset& src, const std::set<juce::String>& locks)
{
    if (locks.empty()) return;
    for (auto& d : paramDefs())
        for (auto& l : locks)
            if (paramInLock (d, l)) dst.params[(size_t) d.index] = src.params[(size_t) d.index];
    if (locks.count ("rhythm")) { dst.patterns.arp = src.patterns.arp; dst.patterns.gate = src.patterns.gate; dst.patterns.seq = src.patterns.seq; }
    if (locks.count ("fx")) dst.patterns.fxOrder = src.patterns.fxOrder;
    if (locks.count ("mod")) dst.patterns.lfoTable = src.patterns.lfoTable;
    if (locks.count ("osc")) { dst.wavetables = src.wavetables; dst.wavetableNames = src.wavetableNames; dst.sample = src.sample; dst.sampleRate = src.sampleRate; dst.sampleRoot = src.sampleRoot; }
}

// ======================================================================= candidate
int Generator::testNote (const juce::String& c)
{
    if (c == "Bass") return 36;
    if (c == "Drums/Perc") return 48;
    if (c == "Lead" || c == "Pluck") return 64;
    if (c == "Ambient") return 55;
    return 60;
}

Preset Generator::candidate (const GenRequest& req, uint32_t seed)
{
    Preset p;
    Rng rng (seed * 2654435761u + 12345u);
    B b { p, rng, req, req.subcategory };
    // neutral start
    b.set ("osc1_on", 0); b.set ("filter1_on", 0); b.set ("filter2_on", 0); b.set ("master", 0);
    b.env (1, 0.003f, 0.5f, 0.8f, 0.3f);
    b.env (2, 0.003f, 0.4f, 0.0f, 0.3f);
    for (int i = 1; i <= 4; ++i) b.set (b.o (i, "rand_phase"), 1);
    auto subs = subcategories (req.category);
    if (b.sub.isEmpty() && ! subs.isEmpty()) b.sub = subs[b.pick (subs.size())];
    const auto& c = req.category;
    if (c == "Bass") bass (b);
    else if (c == "Lead") lead (b);
    else if (c == "Pad") pad (b);
    else if (c == "Pluck") pluck (b);
    else if (c == "Keys") keys (b);
    else if (c == "Arp") arp (b);
    else if (c == "Strings") strings (b);
    else if (c == "Brass") brass (b);
    else if (c == "FX") fx (b);
    else if (c == "Drums/Perc") drums (b);
    else ambient (b);
    if (b.get ("osc1_on") < 0.5f && b.get ("osc2_on") < 0.5f) b.analog (1, 0.66f, 0.7f); // safety
    addLayers (b, c);          // v1.1: air / weight / motion layers
    signature (b, c);          // v1.1: shimmer, granular, formant, comb, width where they shine
    applyCharacter (b, c);     // sliders + macros + mod wheel
    epicMotion (b, c);         // v1.1: slow bloom and drift on big sustained sounds
    fxOrder (b);
    // gentle humanisation of pitch-free details
    for (int i = 1; i <= 4; ++i)
        if (b.get (b.o (i, "on").toRawUTF8()) > 0.5f && b.get (b.o (i, "type").toRawUTF8()) == (float) otAnalog && b.chance (0.3f))
            b.set (b.o (i, "rand_phase"), c == "Bass" || c == "Drums/Perc" ? 0.0f : 1.0f);
    if (req.base != nullptr) applyLocks (p, *req.base, req.locks);
    p.meta.category = c;
    p.meta.subcategory = b.sub;
    p.meta.author = "Infinitum AI";
    p.meta.name = makeName (c, b.sub, rng);
    p.meta.tags = deriveTags (p, req);
    p.meta.notes = juce::String::formatted ("Generated: seed %u, brightness %.2f, movement %.2f, aggression %.2f, space %.2f, weirdness %.2f",
                                            seed, req.brightness, req.movement, req.aggression, req.space, req.weirdness);
    return p;
}

Preset Generator::generate (const GenRequest& req, int* attempts, Validation* result, int maxAttempts, int pickBest)
{
    Preset best, fallback;
    Validation bestV, fallbackV;
    float bestScore = -1e9f;
    int valid = 0;
    for (int a = 0; a < maxAttempts && valid < juce::jmax (1, pickBest); ++a)
    {
        auto p = candidate (req, req.seed + (uint32_t) a * 7919u);
        auto v = validate (p, true);
        if (attempts) *attempts = a + 1;
        if (v.ok)
        {
            ++valid;
            v.interest.score = interestScore (p, v);
            if (v.interest.score > bestScore) { bestScore = v.interest.score; best = p; bestV = v; }
        }
        else if (a == 0 || v.rmsDb > fallbackV.rmsDb) { fallback = p; fallbackV = v; }
    }
    if (valid > 0) { if (result) *result = bestV; return best; }
    if (result) *result = fallbackV;
    return fallback;
}

// v1.1: how interesting a VALID candidate sounds for its category (higher is better, ~0..1.3).
// Targets are ranges, not "more is better": too static loses, too busy (chaos) loses too.
float Generator::interestScore (const Preset& p, const Validation& v)
{
    const auto& in = v.interest;
    const auto& cat = p.meta.category;
    const auto& sub = p.meta.subcategory;
    auto band = [] (float x, float lo, float hi, float soft) { return x < lo ? 1.0f - juce::jmin (1.0f, (lo - x) / soft) : x > hi ? 1.0f - juce::jmin (1.0f, (x - hi) / soft) : 1.0f; };
    const bool big = cat == "Pad" || cat == "Ambient" || cat == "Strings";
    const bool decaying = cat == "Pluck" || cat == "Keys" || cat == "Drums/Perc" || sub == "Stab" || sub == "Pizzicato";
    float mv = cat == "Drums/Perc" ? 0.5f
             : big ? band (in.flux, 1.8f, 4.5f, 2.0f)
             : cat == "Arp" ? band (in.flux, 2.0f, 7.0f, 3.0f)
             : cat == "FX" ? band (in.flux, 1.5f, 6.0f, 3.0f)
             : decaying ? band (in.flux, 0.8f, 3.5f, 1.5f) : band (in.flux, 1.2f, 3.5f, 1.5f);
    float ev = decaying ? 0.5f : (big || cat == "FX") ? band (in.evolution, 2.5f, 7.0f, 2.5f) : band (in.evolution, 1.0f, 6.0f, 2.0f);
    float wd = (cat == "Bass" || cat == "Drums/Perc") ? band (in.width, 0.0f, 0.25f, 0.3f) : band (in.width, 0.35f, 0.95f, 0.35f);
    float rc = cat == "Bass" ? band (in.richness, 0.4f, 0.85f, 0.3f) : band (in.richness, 0.6f, 0.95f, 0.3f);
    float s = 0.35f * mv + 0.25f * ev + 0.2f * wd + 0.2f * rc;
    // never reward chaos or noise
    if (in.flux > 8.0f && cat != "Drums/Perc") s -= 0.4f;
    const bool noiseOk = cat == "FX" || cat == "Drums/Perc" || cat == "Ambient";
    if (! noiseOk && v.flatness > 0.35f) s -= 0.3f;
    // small bonus for depth: layers and Infinitum signature features
    auto val = [&] (const char* id) { return p.params[(size_t) paramIndex (id)]; };
    int oscs = 0; bool sig = false;
    for (int i = 0; i < kNumOsc; ++i)
        if (p.params[(size_t) P::osc (i, P::oOn)] > 0.5f) { ++oscs; sig |= p.params[(size_t) P::osc (i, P::oType)] == (float) otMutation; }
    sig |= val ("shimmer_on") > 0.5f || val ("granular_on") > 0.5f
           || (val ("filter2_on") > 0.5f && (val ("filter2_type") == (float) ftFormant || val ("filter2_type") == (float) ftCombPos));
    if (oscs >= 3) s += 0.05f;
    if (sig) s += 0.08f;
    return s;
}

// ======================================================================= validation
static Validation validateOnce (Preset& p, bool fixLevel, float& missingGainDb);

Validation Generator::validate (Preset& p, bool fixLevel)
{
    float missing = 0;
    auto v = validateOnce (p, fixLevel, missing);
    if (fixLevel && v.reason == "too quiet" && missing > 0)
    {
        // not enough master range: raise oscillator levels (bounded) and measure again
        float maxLevel = 0.01f;
        for (int o = 0; o < kNumOsc; ++o)
            if (p.params[(size_t) P::osc (o, P::oOn)] > 0.5f) maxLevel = juce::jmax (maxLevel, p.params[(size_t) P::osc (o, P::oLevel)]);
        const float k = juce::jmin (juce::Decibels::decibelsToGain (missing + 1.0f), 1.0f / maxLevel);
        for (int o = 0; o < kNumOsc; ++o) p.params[(size_t) P::osc (o, P::oLevel)] = juce::jmin (1.0f, p.params[(size_t) P::osc (o, P::oLevel)] * k);
        v = validateOnce (p, fixLevel, missing);
    }
    return v;
}

static Validation validateOnce (Preset& p, bool fixLevel, float& missingGainDb)
{
    Validation v;
    missingGainDb = 0;
    const auto& cat = p.meta.category;
    const double sr = 44100.0;
    const bool longHold = cat == "Pad" || cat == "Ambient" || cat == "Strings" || cat == "FX" || cat == "Brass";
    const double hold = longHold ? 2.2 : 1.3;
    const double total = hold + 2.6;
    const int note = Generator::testNote (cat);
    const bool chord = p.params[(size_t) P::arpOn] > 0.5f || cat == "Pad" || cat == "Keys" || cat == "Strings";
    juce::AudioBuffer<float> out (2, (int) (total * sr));
    juce::MidiBuffer m;
    const int notes[3] { note, note + (chord ? 4 : 0), note + (chord ? 7 : 0) };
    const int numNotes = chord ? 3 : 1;
    for (int i = 0; i < numNotes; ++i)
    {
        m.addEvent (juce::MidiMessage::noteOn (1, notes[i], 0.8f), 0);
        m.addEvent (juce::MidiMessage::noteOff (1, notes[i]), (int) (hold * sr));
    }
    renderPreset (p, m, sr, 256, out);
    v.interest = Generator::measureInterest (out, sr, hold);

    const int n = out.getNumSamples();
    const float* L = out.getReadPointer (0);
    const float* R = out.getReadPointer (1);
    double sum = 0;
    float peak = 0;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { v.reason = "NaN/Inf"; return v; }
        peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i]));
        sum += L[i] + R[i];
    }
    v.dc = (float) std::abs (sum / (2.0 * n));
    // windowed RMS (100 ms)
    const int win = (int) (0.1 * sr);
    std::vector<float> w;
    for (int s = 0; s + win <= n; s += win)
    {
        double sq = 0;
        for (int i = s; i < s + win; ++i) sq += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        w.push_back ((float) std::sqrt (sq / win));
    }
    float loud = 0;
    for (size_t i = 0; i + 2 < w.size(); ++i) loud = juce::jmax (loud, (w[i] + w[i + 1] + w[i + 2]) / 3.0f);
    v.rmsDb = juce::Decibels::gainToDecibels (loud, -120.0f);
    v.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
    const int holdEnd = juce::jlimit (1, (int) w.size() - 1, (int) (hold / 0.1) - 1);
    float maxW = 1.0e-9f;
    for (int i = 0; i <= holdEnd; ++i) maxW = juce::jmax (maxW, w[(size_t) i]);
    v.sustainRatio = w[(size_t) holdEnd] / maxW;
    v.tailDb = juce::Decibels::gainToDecibels (w.back(), -120.0f) - v.rmsDb;
    // runaway = still loud at the end AND not decaying over the last second
    const float before = w[w.size() > 11 ? w.size() - 11 : 0];
    const bool decaying = w.back() < before * 0.8f;

    // spectrum of the loudest part of the hold window
    juce::dsp::FFT fft (12);
    std::vector<float> buf (8192, 0.0f);
    const int start = (int) (juce::jmin (0.4, hold * 0.4) * sr);
    for (int i = 0; i < 4096 && start + i < n; ++i)
        buf[(size_t) i] = 0.5f * (L[start + i] + R[start + i]) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / 4096.0f));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double num = 0, den = 0, logSum = 0, linSum = 0;
    int bins = 0;
    for (int k = 2; k < 2048; ++k)
    {
        const double mag = buf[(size_t) k] + 1e-9;
        const double f = k * sr / 4096.0;
        num += f * mag; den += mag;
        if (f > 100 && f < 12000) { logSum += std::log (mag); linSum += mag; ++bins; }
    }
    v.centroidHz = (float) (num / juce::jmax (1e-9, den));
    v.flatness = bins > 0 ? (float) (std::exp (logSum / bins) / (linSum / bins)) : 0.0f;

    const bool noiseOk = cat == "FX" || cat == "Drums/Perc" || cat == "Ambient";
    const auto sub = p.meta.subcategory;
    const bool sustained = (cat == "Bass" && sub != "Pluck") || cat == "Lead" || cat == "Pad" || (cat == "Strings" && sub != "Pizzicato")
                           || (cat == "Brass" && sub != "Stab") || (cat == "Keys" && sub == "Organ") || cat == "Ambient";
    if (v.rmsDb < -60.0f) v.reason = "silent";
    else if (v.dc > 0.02f) v.reason = "DC offset";
    else if (sustained && v.sustainRatio < 0.12f && p.params[(size_t) P::tgOn] < 0.5f) v.reason = "no sustain";
    else if (v.tailDb > -30.0f && ! decaying) v.reason = "runaway tail";
    else if (! noiseOk && v.flatness > 0.45f) v.reason = "too noisy";
    else if (! noiseOk && v.centroidHz > 7500.0f) v.reason = "harsh";
    else if (v.centroidHz < 40.0f) v.reason = "mud";

    // level correction (master is post-FX and linear)
    static const std::map<juce::String, float> target { { "Bass", -15 }, { "Lead", -16 }, { "Pad", -18 }, { "Pluck", -17 }, { "Keys", -17 }, { "Arp", -17 },
                                                        { "Strings", -18 }, { "Brass", -16 }, { "FX", -18 }, { "Drums/Perc", -14 }, { "Ambient", -20 } };
    if (fixLevel && v.reason.isEmpty())
    {
        const float t = target.count (cat) ? target.at (cat) : -17.0f;
        // decaying sounds are normalised by peak (their RMS window is not representative)
        const bool decaying = ! sustained && p.params[(size_t) P::tgOn] < 0.5f;
        float gain = decaying ? juce::jmin (-2.0f - v.peakDb, t + 4.0f - v.rmsDb) : t - v.rmsDb;
        gain = juce::jmin (gain, -1.0f - v.peakDb);
        auto& master = p.params[(size_t) P::master];
        const float newMaster = master + gain;
        if (newMaster > 24.0f) { v.reason = "too quiet"; missingGainDb = newMaster - 24.0f; }
        else
        {
            master = juce::jlimit (-48.0f, 24.0f, newMaster);
            v.rmsDb += gain; v.peakDb += gain;
        }
    }
    v.ok = v.reason.isEmpty();
    return v;
}

// ======================================================================= interest metrics
Interest Generator::measureInterest (const juce::AudioBuffer<float>& b, double sr, double holdSec)
{
    Interest r;
    const int n = juce::jmin (b.getNumSamples(), (int) (holdSec * sr));
    const float* L = b.getReadPointer (0);
    const float* R = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
    if (n < 4096) return r;
    // attack: first 50 ms window reaching 90 % of the loudest one
    const int aw = (int) (0.05 * sr);
    std::vector<float> wr;
    for (int s = 0; s + aw <= n; s += aw / 2)
    {
        double e = 0;
        for (int i = s; i < s + aw; ++i) e += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        wr.push_back ((float) std::sqrt (e / aw));
    }
    const float mx = wr.empty() ? 0.0f : *std::max_element (wr.begin(), wr.end());
    for (size_t i = 0; i < wr.size(); ++i) if (wr[i] >= 0.9f * mx) { r.attackMs = (float) (i * (aw / 2) / sr * 1000.0); break; }
    // stereo width
    double mid = 0, side = 0;
    for (int i = 0; i < n; ++i) { const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]); mid += m * m; side += s * s; }
    r.width = (float) std::sqrt (side / juce::jmax (1e-12, mid));
    // band spectra per frame (32 log bands, 100 Hz - 12 kHz)
    constexpr int order = 11, N = 1 << order, kBands = 32;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2);
    std::vector<std::array<float, kBands>> frames;
    const int startS = (int) (0.05 * sr);
    for (int s = startS; s + N <= n; s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (L[s + i] + R[s + i]) * (0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        std::array<float, kBands> band {};
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * sr / N;
            if (f < 100 || f >= 12000) continue;
            const int bi = juce::jlimit (0, kBands - 1, (int) (std::log (f / 100.0) / std::log (120.0) * kBands));
            band[(size_t) bi] += buf[(size_t) k] * buf[(size_t) k];
        }
        for (auto& v : band) v = 10.0f * std::log10 (v + 1e-12f);
        frames.push_back (band);
    }
    if (frames.size() < 3) return r;
    // ignore near-silent frames (decays) for the movement figures
    float loud = -300;
    for (auto& f : frames) loud = juce::jmax (loud, *std::max_element (f.begin(), f.end()));
    double flux = 0; int fluxN = 0;
    for (size_t i = 1; i < frames.size(); ++i)
    {
        if (*std::max_element (frames[i].begin(), frames[i].end()) < loud - 40) continue;
        double d = 0;
        for (int k = 0; k < kBands; ++k) d += std::abs (juce::jmax (frames[i][(size_t) k], loud - 60) - juce::jmax (frames[i - 1][(size_t) k], loud - 60));
        flux += d / kBands; ++fluxN;
    }
    r.flux = fluxN > 0 ? (float) (flux / fluxN) : 0.0f;
    const size_t q = juce::jmax ((size_t) 1, frames.size() / 4);
    std::array<float, kBands> a {}, z {};
    for (size_t i = 0; i < q; ++i)
        for (int k = 0; k < kBands; ++k) { a[(size_t) k] += frames[i][(size_t) k] / q; z[(size_t) k] += frames[frames.size() - 1 - i][(size_t) k] / q; }
    // compare spectral SHAPE (level-normalised), so a plain decay is not "evolution"
    const float ma = *std::max_element (a.begin(), a.end()), mz = *std::max_element (z.begin(), z.end());
    double ev = 0;
    for (int k = 0; k < kBands; ++k) ev += std::abs (juce::jmax (a[(size_t) k] - ma, -50.0f) - juce::jmax (z[(size_t) k] - mz, -50.0f));
    r.evolution = (float) (ev / kBands);
    const auto& mf = frames[frames.size() / 2];
    const float top = *std::max_element (mf.begin(), mf.end());
    int rich = 0;
    for (auto v : mf) rich += v > top - 36.0f;
    r.richness = (float) rich / kBands;
    return r;
}

// ======================================================================= mutate / blend
Preset Generator::mutate (const Preset& src, float amount, uint32_t seed, const std::set<juce::String>& locks)
{
    Preset p = src;
    Rng rng (seed ^ 0xA5A5A5A5u);
    const auto& defs = paramDefs();
    for (auto& d : defs)
    {
        if (d.group == "macros" || d.group == "matrix" || d.id == "master" || d.id == "polyphony" || d.id.endsWith ("_on")) continue;
        bool locked = false;
        for (auto& l : locks) locked |= paramInLock (d, l);
        if (locked) continue;
        auto& v = p.params[(size_t) d.index];
        if (d.kind == pkFloat)
        {
            const float n = d.range.convertTo0to1 (v);
            const float g = (rng.uni() + rng.uni() + rng.uni() - 1.5f) * 0.25f * amount; // ~gaussian
            v = d.range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n + g));
        }
        else if ((d.kind == pkChoice || d.kind == pkInt) && rng.uni() < amount * 0.04f && ! d.id.contains ("type") && ! d.id.contains ("table"))
        {
            v = juce::jlimit (d.range.start, d.range.end, v + (rng.uni() < 0.5f ? -1.0f : 1.0f));
        }
    }
    for (auto& a : p.patterns.arp) if (rng.uni() < amount * 0.2f) a.vel = juce::jlimit (0.2f, 1.0f, a.vel + rng.bi() * 0.2f);
    p.meta.uuid = juce::Uuid().toString();
    p.meta.name = src.meta.name.upToFirstOccurrenceOf (" ~", false, false) + " ~" + juce::String (1 + (int) (rng.next() % 99));
    p.meta.factory = false;
    p.meta.file = juce::File();
    return p;
}

Preset Generator::blend (const Preset& a, const Preset& b, float t, uint32_t seed)
{
    Preset p = a;
    Rng rng (seed ^ 0x5EEDu);
    for (auto& d : paramDefs())
    {
        auto& v = p.params[(size_t) d.index];
        const float vb = b.params[(size_t) d.index];
        if (d.kind == pkFloat && d.group != "matrix")
            v = d.range.convertFrom0to1 (d.range.convertTo0to1 (v) * (1 - t) + d.range.convertTo0to1 (vb) * t);
        else if (rng.uni() < t) v = vb;
    }
    // matrix: take whole slots from either parent
    for (int s = 0; s < kNumModSlots; ++s)
        if (rng.uni() < t)
            for (int f = 0; f < P::numModFields; ++f) p.params[(size_t) P::mod (s, f)] = b.params[(size_t) P::mod (s, f)];
    for (int i = 0; i < kMaxGateSteps; ++i) p.patterns.gate[(size_t) i] = a.patterns.gate[(size_t) i] * (1 - t) + b.patterns.gate[(size_t) i] * t;
    if (t > 0.5f) { p.patterns.arp = b.patterns.arp; p.patterns.fxOrder = b.patterns.fxOrder; }
    p.meta.uuid = juce::Uuid().toString();
    p.meta.name = a.meta.name.upToFirstOccurrenceOf (" x ", false, false) + " x " + b.meta.name.upToFirstOccurrenceOf (" x ", false, false);
    p.meta.category = t > 0.5f ? b.meta.category : a.meta.category;
    p.meta.subcategory = t > 0.5f ? b.meta.subcategory : a.meta.subcategory;
    p.meta.factory = false;
    p.meta.file = juce::File();
    return p;
}

// ======================================================================= tags & names
juce::StringArray Generator::deriveTags (const Preset& p, const GenRequest& q)
{
    auto v = [&] (const char* id) { return p.params[(size_t) paramIndex (id)]; };
    juce::StringArray t;
    if (q.brightness > 0.66f) t.add ("Bright");
    if (q.brightness < 0.34f) t.add ("Dark");
    if (q.movement > 0.62f || v ("osc1_type") == (float) otMutation || v ("osc2_type") == (float) otMutation) t.add ("Evolving");
    if (q.aggression > 0.62f || v ("dist_on") > 0.5f) t.add ("Aggressive");
    if (q.aggression < 0.25f && v ("dist_on") < 0.5f) t.add ("Soft");
    if (v ("uni_voices") >= 4 || v ("wide_on") > 0.5f) t.add ("Wide");
    if ((int) v ("play_mode") != pmPoly) t.add ("Mono");
    if (v ("chorus_on") > 0.5f && v ("osc1_type") == (float) otAnalog) t.add ("Vintage");
    if (v ("osc1_type") == (float) otWavetable || v ("osc1_type") == (float) otFM) t.add ("Modern");
    if (v ("osc1_type") == (float) otFM && v ("osc1_fm_r2") > 2.6f) t.add ("Metallic");
    if (q.space > 0.62f) t.add ("Airy");
    if (v ("env1_attack") < 0.005f && (p.meta.category == "Bass" || p.meta.category == "Drums/Perc" || p.meta.category == "Pluck")) t.add ("Punchy");
    if (v ("crush_on") > 0.5f) t.add ("Lo-Fi");
    if (v ("dist_on") > 0.5f || v ("crush_on") > 0.5f) t.add ("Dirty");
    else if (v ("reverb_on") < 0.5f && v ("delay_on") < 0.5f) t.add ("Clean");
    if ((p.meta.category == "Pad" || p.meta.category == "Ambient" || p.meta.category == "Strings") && q.space > 0.55f) t.add ("Cinematic");
    if (v ("arp_on") > 0.5f || v ("tg_on") > 0.5f) t.add ("Rhythmic");
    if (p.meta.subcategory == "Bell" || (v ("osc1_type") == (float) otWavetable && (v ("osc1_table") == (float) tGlass || v ("osc1_table") == (float) tBell))) t.add ("Glassy");
    if (! t.contains ("Bright") && ! t.contains ("Dark") && ! t.contains ("Aggressive")) t.add ("Warm");
    if (v ("osc1_type") == (float) otAnalog && ! t.contains ("Modern") && q.brightness < 0.5f && ! t.contains ("Warm")) t.add ("Warm");
    return t;
}

juce::String Generator::makeName (const juce::String& cat, const juce::String& sub, Rng& rng)
{
    static const std::map<juce::String, std::pair<juce::StringArray, juce::StringArray>> words {
        { "Bass", { { "Deep", "Rubber", "Gritty", "Velvet", "Dark", "Iron", "Fat", "Liquid", "Heavy", "Round", "Analog", "Tight", "Low", "Warm", "Dirty", "Smooth", "Massive", "Bouncy", "Wet", "Steel" },
                    { "Floor", "Rumble", "Pulse", "Growl", "Engine", "Thump", "Core", "Bottom", "Wire", "Tremor", "Basement", "Undertow", "Gravity", "Motor", "Woofer", "Bounce", "Quake", "Crawler", "Diesel", "Magma" } } },
        { "Lead", { { "Laser", "Silver", "Solar", "Neon", "Screaming", "Sharp", "Crystal", "Electric", "Hyper", "Cosmic", "Golden", "Burning", "Vocal", "Sweet", "Wild", "Razor", "Stellar", "Retro", "Bright", "Lucid" },
                    { "Lead", "Ray", "Blade", "Line", "Voice", "Comet", "Spark", "Flare", "Arrow", "Beam", "Hero", "Whistle", "Siren", "Torch", "Flame", "Pilot", "Quest", "Rider", "Signal", "Saber" } } },
        { "Pad", { { "Ocean", "Velvet", "Silent", "Frozen", "Endless", "Golden", "Misty", "Distant", "Glacial", "Soft", "Lunar", "Dreamy", "Hollow", "Floating", "Warm", "Ancient", "Celestial", "Amber", "Twilight", "Nebula" },
                   { "Pad", "Horizon", "Cloud", "Aurora", "Haze", "Dawn", "Field", "Veil", "Tide", "Mist", "Sky", "Bloom", "Drift", "Silk", "Glow", "Garden", "Shore", "Dream", "Space", "Halo" } } },
        { "Pluck", { { "Glass", "Tiny", "Wooden", "Crystal", "Pixel", "Rubber", "Snappy", "Little", "Sparkly", "Plastic", "Copper", "Ice", "Candy", "Hollow", "Bouncy", "Clean", "Short", "Silver", "Tropical", "Paper" },
                     { "Pluck", "Drop", "Pick", "Bell", "Bead", "Dew", "Chime", "Mallet", "Tick", "Pop", "Harp", "Spark", "Pebble", "Flick", "Ping", "Marble", "Click", "Rain", "Shard", "Thumb" } } },
        { "Keys", { { "Vintage", "Dusty", "Electric", "Soft", "Warm", "Velvet", "Glassy", "Night", "Smoky", "Old", "Bright", "Jazz", "Lounge", "Mellow", "Studio", "Crystal", "Mild", "Bar", "Silk", "Evening" },
                    { "Keys", "Piano", "Organ", "Tines", "Bells", "Clav", "Board", "Stage", "Chord", "Hammers", "Ivory", "Console", "Parlor", "Reed", "Bellows", "Ballad", "Room", "Club", "Session", "Suite" } } },
        { "Arp", { { "Running", "Pulsing", "Cyber", "Rapid", "Spinning", "Hypno", "Digital", "Neon", "Bouncing", "Racing", "Circuit", "Trance", "Electric", "Endless", "Clockwork", "Morning", "Star", "Chasing", "Liquid", "Sparkling" },
                   { "Arp", "Sequence", "Loop", "Steps", "Motion", "Runner", "Machine", "Pattern", "Engine", "Cycle", "Gears", "Chain", "Ripple", "Stream", "Train", "Wheel", "Pulse", "Clock", "Spiral", "Grid" } } },
        { "Strings", { { "Silk", "Cinematic", "Epic", "Warm", "Lush", "Velvet", "Grand", "Tender", "Analog", "Golden", "Slow", "Wide", "Noble", "Royal", "Bowed", "Soft", "Dark", "Vast", "Distant", "Summer" },
                       { "Strings", "Ensemble", "Section", "Orchestra", "Quartet", "Bows", "Legato", "Swell", "Sweep", "Hall", "Theme", "Score", "Chamber", "Elegy", "Anthem", "Overture", "Adagio", "Waves", "Lines", "Sonata" } } },
        { "Brass", { { "Bold", "Golden", "Brassy", "Punchy", "Royal", "Fat", "Big", "Bright", "Funky", "Analog", "Proud", "Heavy", "Warm", "Blazing", "Retro", "Vintage", "Thick", "Massive", "Sharp", "Soul" },
                     { "Brass", "Horns", "Stab", "Section", "Fanfare", "Blast", "Hit", "Trumpet", "Call", "Swell", "Herald", "March", "Parade", "Salute", "Glory", "Rise", "Brigade", "Signal", "Anthem", "Crown" } } },
        { "FX", { { "Alien", "Rising", "Falling", "Broken", "Cosmic", "Lost", "Static", "Digital", "Dark", "Electric", "Strange", "Haunted", "Toxic", "Frozen", "Glitched", "Deep", "Hyper", "Magnetic", "Orbital", "Phantom" },
                  { "Riser", "Sweep", "Impact", "Noise", "Signal", "Portal", "Transmission", "Drop", "Wind", "Machine", "Ghost", "Storm", "Vortex", "Burst", "Warp", "Echo", "Siren", "Beacon", "Void", "Spark" } } },
        { "Ambient", { { "Endless", "Quiet", "Floating", "Distant", "Infinite", "Silent", "Deep", "Frozen", "Hidden", "Ethereal", "Dark", "Gentle", "Sacred", "Slow", "Liquid", "Dusk", "Arctic", "Inner", "Lunar", "Weightless" },
                       { "Drone", "Space", "Breath", "Horizon", "Depths", "Atmosphere", "Void", "Sanctuary", "Glacier", "Meditation", "Orbit", "Echoes", "Temple", "Fog", "Expanse", "Current", "Dream", "Wilderness", "Sky", "Ether" } } },
    };
    static const std::map<juce::String, juce::StringArray> drumNouns {
        { "Kick", { "Kick", "Thud", "Boom", "Knock", "Punch", "Thump" } }, { "Snare", { "Snare", "Clap", "Crack", "Slap", "Snap", "Rimshot" } },
        { "Hat", { "Hat", "Shaker", "Tick", "Sizzle", "Hiss", "Chick" } }, { "Tom", { "Tom", "Floor", "Drop", "Thump", "Drum", "Roll" } },
        { "Perc", { "Perc", "Block", "Clank", "Zap", "Ping", "Tap" } } };
    static const juce::StringArray drumAdj { "Punchy", "Tight", "Deep", "Short", "Big", "Dusty", "Crisp", "Snappy", "Metal", "Analog", "Hard", "Soft", "Fat", "Dry", "Electro", "Round", "Clicky", "Lo", "Hi", "Raw" };
    if (cat == "Drums/Perc")
    {
        auto it = drumNouns.find (sub);
        const auto& nouns = it != drumNouns.end() ? it->second : drumNouns.at ("Perc");
        return drumAdj[(int) (rng.next() % (uint32_t) drumAdj.size())] + " " + nouns[(int) (rng.next() % (uint32_t) nouns.size())];
    }
    auto it = words.find (cat);
    const auto& wd = it != words.end() ? it->second : words.at ("Lead");
    return wd.first[(int) (rng.next() % (uint32_t) wd.first.size())] + " " + wd.second[(int) (rng.next() % (uint32_t) wd.second.size())];
}
}
