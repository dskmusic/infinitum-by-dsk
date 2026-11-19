// Sound-design analysis tools:
//   InfinitumTool bank-report <bank.infinitumbank> <out.md>   structural + audio report of a preset bank
//   InfinitumTool listen-pack <dir> [extra]                 fixed 20-request A/B listening pack (WAV + notes)
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "gen/Generator.h"
#include "preset/Render.h"
#include <map>
#include <set>

namespace
{
constexpr double kSr = 48000.0;
const char* kOscNames[] { "Wavetable", "Analog", "FM", "Noise/Sampler", "Spectral Mutation" };
const char* kFltNames[] { "LP12", "LP24", "Ladder", "HP12", "HP24", "BP", "Notch", "Comb+", "Comb-", "Formant" };

struct Ph { double beat; int note; double len; float vel; };

// Musical test phrase per category (120 bpm). Returns phrase length in seconds.
double phraseFor (const juce::String& cat, juce::MidiBuffer& m)
{
    std::vector<Ph> ph;
    auto chord = [&] (double at, std::initializer_list<int> ns, double len) { for (int n : ns) ph.push_back ({ at, n, len, 0.8f }); };
    if (cat == "Bass")
    {
        const int r[] { 33, 33, 45, 33, 36, 33, 31, 43, 29, 29, 41, 29, 31, 31, 43, 40 };
        for (int i = 0; i < 16; ++i) ph.push_back ({ i * 0.5, r[i], i % 2 ? 0.3 : 0.45, i % 4 == 0 ? 0.95f : 0.75f });
    }
    else if (cat == "Lead")
    {
        const Ph m2[] { { 0, 76, 0.9, 0.9f }, { 1, 74, 0.45, 0.8f }, { 1.5, 72, 0.45, 0.8f }, { 2, 74, 0.9, 0.85f }, { 3, 69, 0.9, 0.8f },
                        { 4, 72, 0.45, 0.85f }, { 4.5, 74, 0.45, 0.85f }, { 5, 76, 0.9, 0.9f }, { 6, 79, 0.45, 0.9f }, { 6.5, 76, 1.4, 0.85f } };
        ph.assign (std::begin (m2), std::end (m2));
    }
    else if (cat == "Pluck")
    {
        const int pat[] { 69, 72, 76, 81, 79, 76, 72, 76, 65, 69, 72, 77, 76, 72, 69, 72 };
        for (int i = 0; i < 16; ++i) ph.push_back ({ i * 0.5, pat[i], 0.4, i % 4 == 0 ? 0.95f : 0.75f });
    }
    else if (cat == "Arp") { chord (0, { 57, 60, 64 }, 7.7); chord (8, { 53, 57, 60 }, 7.7); }
    else if (cat == "Drums/Perc") { for (int i = 0; i < 16; ++i) ph.push_back ({ i * 0.5, 48, 0.2, i % 4 == 0 ? 1.0f : 0.7f }); }
    else if (cat == "FX") ph.push_back ({ 0, 60, 6.0, 0.85f });
    else if (cat == "Ambient") chord (0, { 45, 52, 57, 64 }, 11.5);
    else // Pad, Keys, Strings, Brass: Am - F - C - G
    {
        const double len = cat == "Keys" ? 1.9 : 3.8;
        chord (0, { 57, 60, 64 }, len); chord (4, { 53, 57, 60 }, len); chord (8, { 48, 55, 64 }, len); chord (12, { 55, 59, 62 }, len);
    }
    double end = 0;
    for (auto& p : ph)
    {
        const int t0 = (int) (p.beat * 0.5 * kSr), t1 = (int) ((p.beat + p.len) * 0.5 * kSr);
        m.addEvent (juce::MidiMessage::noteOn (1, p.note, p.vel), t0);
        m.addEvent (juce::MidiMessage::noteOff (1, p.note), t1);
        end = juce::jmax (end, (p.beat + p.len) * 0.5);
    }
    return end;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (kSr).withNumChannels (2).withBitsPerSample (24));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// Loudness-match for fair A/B: loudest 1 s window -> -18 dBFS RMS, peak <= -1 dBFS.
void normalise (juce::AudioBuffer<float>& b)
{
    const int n = b.getNumSamples(), w = (int) kSr;
    double best = 1e-12;
    for (int s = 0; s + w <= n; s += w / 4)
    {
        double e = 0;
        for (int c = 0; c < 2; ++c) { auto* d = b.getReadPointer (c); for (int i = s; i < s + w; ++i) e += 0.5 * d[i] * d[i]; }
        best = juce::jmax (best, e / w);
    }
    float g = juce::Decibels::decibelsToGain (-18.0f) / (float) std::sqrt (best);
    const float pk = b.getMagnitude (0, n) * g;
    if (pk > 0.891f) g *= 0.891f / pk;
    b.applyGain (g);
}

juce::AudioBuffer<float> renderPhrase (const gx::Preset& p)
{
    juce::MidiBuffer m;
    const double len = phraseFor (p.meta.category, m);
    juce::AudioBuffer<float> out (2, (int) ((len + 3.0) * kSr));
    gx::renderPreset (p, m, kSr, 256, out);
    return out;
}

// Sustained probe (held note / chord) for the interest metrics.
gx::Interest probe (const gx::Preset& p)
{
    const auto& cat = p.meta.category;
    const bool chord = cat == "Pad" || cat == "Keys" || cat == "Strings" || cat == "Arp" || cat == "Ambient" || cat == "Brass";
    const int n = gx::Generator::testNote (cat);
    const double hold = 4.0;
    juce::MidiBuffer m;
    for (int k : { 0, 4, 7 }) { if (! chord && k) continue; m.addEvent (juce::MidiMessage::noteOn (1, n + k, 0.8f), 0); m.addEvent (juce::MidiMessage::noteOff (1, n + k), (int) (hold * kSr)); }
    juce::AudioBuffer<float> out (2, (int) ((hold + 1.0) * kSr));
    gx::renderPreset (p, m, kSr, 256, out);
    return gx::Generator::measureInterest (out, kSr, hold);
}

float v (const gx::Preset& p, const char* id) { return p.params[(size_t) gx::paramIndex (id)]; }

// The 20 fixed requests of the A/B pack (same seeds and characters before/after).
struct Req { const char* cat; const char* sub; float br, mv, ag, sp, we; uint32_t seed; };
const Req kPack[] {
    { "Bass", "Reese", 0.45f, 0.6f, 0.5f, 0.2f, 0.2f, 101 },   { "Bass", "Synth", 0.6f, 0.4f, 0.4f, 0.1f, 0.1f, 102 },
    { "Lead", "Supersaw", 0.7f, 0.5f, 0.35f, 0.5f, 0.15f, 201 }, { "Lead", "Mono", 0.55f, 0.6f, 0.6f, 0.4f, 0.5f, 202 },
    { "Pad", "Evolving", 0.5f, 0.75f, 0.15f, 0.75f, 0.3f, 301 }, { "Pad", "Warm", 0.4f, 0.4f, 0.1f, 0.6f, 0.1f, 302 },
    { "Pad", "Bright", 0.75f, 0.55f, 0.2f, 0.8f, 0.2f, 303 },    { "Pluck", "Bell", 0.65f, 0.3f, 0.2f, 0.5f, 0.2f, 401 },
    { "Pluck", "Synth", 0.55f, 0.5f, 0.4f, 0.4f, 0.3f, 402 },    { "Keys", "Electric", 0.45f, 0.4f, 0.2f, 0.4f, 0.1f, 501 },
    { "Keys", "Digital", 0.6f, 0.5f, 0.3f, 0.5f, 0.4f, 502 },    { "Arp", "Trance", 0.6f, 0.5f, 0.4f, 0.5f, 0.2f, 601 },
    { "Arp", "Evolving", 0.55f, 0.75f, 0.3f, 0.6f, 0.5f, 602 },  { "Strings", "Ensemble", 0.5f, 0.45f, 0.2f, 0.7f, 0.1f, 701 },
    { "Strings", "Synth", 0.6f, 0.6f, 0.3f, 0.6f, 0.3f, 702 },   { "Brass", "Synth Brass", 0.6f, 0.4f, 0.55f, 0.4f, 0.1f, 801 },
    { "FX", "Riser", 0.6f, 0.8f, 0.5f, 0.7f, 0.6f, 901 },        { "Ambient", "Drone", 0.4f, 0.7f, 0.1f, 0.8f, 0.4f, 1001 },
    { "Ambient", "Soundscape", 0.55f, 0.6f, 0.1f, 0.85f, 0.3f, 1002 }, { "Drums/Perc", "Kick", 0.5f, 0.2f, 0.6f, 0.1f, 0.1f, 1101 },
};
// Extra requests rendered only if the subcategory exists (new archetypes).
const Req kExtra[] {
    { "Pad", "Cinematic", 0.5f, 0.7f, 0.25f, 0.8f, 0.3f, 1301 }, { "Lead", "Anthem", 0.7f, 0.55f, 0.4f, 0.6f, 0.2f, 1302 },
    { "Lead", "Vocal", 0.55f, 0.6f, 0.3f, 0.5f, 0.4f, 1303 },    { "Bass", "Growl", 0.5f, 0.7f, 0.7f, 0.15f, 0.4f, 1304 },
    { "Brass", "Trailer", 0.55f, 0.5f, 0.6f, 0.6f, 0.2f, 1305 }, { "Strings", "Hybrid", 0.55f, 0.65f, 0.35f, 0.7f, 0.3f, 1306 },
    { "Ambient", "Cathedral", 0.6f, 0.6f, 0.1f, 0.9f, 0.3f, 1307 }, { "FX", "Impact", 0.5f, 0.4f, 0.7f, 0.7f, 0.4f, 1308 },
};

gx::Preset make (const Req& r)
{
    gx::GenRequest q;
    q.category = r.cat; q.subcategory = r.sub; q.brightness = r.br; q.movement = r.mv; q.aggression = r.ag; q.space = r.sp; q.weirdness = r.we; q.seed = r.seed;
    return gx::Generator::generate (q);
}
}

int runBankReport (int argc, char** argv)
{
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File in = cwd.getChildFile (argc > 2 ? argv[2] : "resources/factory.infinitumbank");
    const juce::File out = cwd.getChildFile (argc > 3 ? argv[3] : "bank-report.md");
    juce::var bank;
    juce::JSON::parse (in.loadFileAsString(), bank);
    auto* arr = bank.isArray() ? bank.getArray() : bank.getProperty ("presets", {}).getArray();
    if (! arr) { std::printf ("cannot read %s\n", in.getFullPathName().toRawUTF8()); return 1; }

    struct Acc { int n = 0; double osc = 0, mods = 0, fx = 0, flux = 0, evo = 0, width = 0, rich = 0, slowMod = 0, layered = 0, sig = 0; };
    std::map<juce::String, Acc> per; Acc all;
    std::map<juce::String, int> oscUse, fltUse, fxUse, srcUse;
    std::vector<std::pair<float, juce::String>> flat;
    for (auto& item : *arr)
    {
        auto p = gx::Preset::fromVar (item);
        int nOsc = 0;
        std::set<juce::String> types;
        for (int o = 1; o <= 4; ++o)
        {
            const juce::String id = "osc" + juce::String (o);
            if (v (p, (id + "_on").toRawUTF8()) < 0.5f) continue;
            ++nOsc; types.insert (kOscNames[(int) v (p, (id + "_type").toRawUTF8())]);
        }
        for (auto& t : types) oscUse[t]++;
        for (int f = 1; f <= 2; ++f)
            if (v (p, ("filter" + juce::String (f) + "_on").toRawUTF8()) > 0.5f) fltUse[kFltNames[(int) v (p, ("filter" + juce::String (f) + "_type").toRawUTF8())]]++;
        int nFx = 0;
        for (int k = 0; k < gx::kNumFx; ++k)
        {
            const auto name = gx::fxNames()[k];
            static const char* ids[] { "dist_on", "chorus_on", "flanger_on", "phaser_on", "delay_on", "reverb_on", "comp_on", "eq_on", "crush_on", "wide_on", "shimmer_on", "granular_on" };
            if (v (p, ids[k]) > 0.5f) { ++nFx; fxUse[name]++; }
        }
        int nMods = 0; bool slow = false;
        for (int s = 0; s < gx::kNumModSlots; ++s)
        {
            const int src = (int) p.params[(size_t) gx::P::mod (s, gx::P::mSrc)];
            if (src <= 0) continue;
            ++nMods; srcUse[gx::sourceNames()[src]]++;
            if (src >= 5 && src <= 8 && v (p, ("lfo" + juce::String (src - 4) + "_rate").toRawUTF8()) < 0.25f) slow = true;
        }
        const bool sig = (v (p, "shimmer_on") > 0.5f || v (p, "granular_on") > 0.5f
                          || v (p, "osc1_type") == gx::otMutation || v (p, "osc2_type") == gx::otMutation || v (p, "osc3_type") == gx::otMutation
                          || v (p, "filter1_type") == gx::ftFormant || v (p, "filter2_type") == gx::ftFormant
                          || v (p, "filter1_type") == gx::ftCombPos || v (p, "filter1_type") == gx::ftCombNeg);
        const auto in = probe (p);
        for (Acc* a : { &per[p.meta.category], &all })
        {
            a->n++; a->osc += nOsc; a->mods += nMods; a->fx += nFx; a->flux += in.flux; a->evo += in.evolution; a->width += in.width; a->rich += in.richness;
            a->slowMod += slow; a->layered += nOsc >= 3; a->sig += sig;
        }
        flat.push_back ({ in.flux + in.evolution, p.meta.category + " / " + p.meta.subcategory + " - " + p.meta.name });
    }

    juce::String md;
    auto pct = [] (double a, int n) { return juce::String (juce::roundToInt (n ? 100.0 * a / n : 0.0)) + " %"; };
    md << "# Infinitum factory bank report\n\nBank: `" << in.getFileName() << "` - " << all.n << " presets\n\n";
    md << "Audio metrics measured on a 4 s held note/chord (48 kHz):\n"
          "- **Movement**: short-term spectral change (dB/frame). ~0.3 = static, 1-3 = alive, >6 = busy/chaotic.\n"
          "- **Evolution**: how much the spectral shape differs between the start and the end of the hold (dB).\n"
          "- **Width**: side/mid ratio (0 = mono, ~0.5 = wide, >0.9 = very wide).\n"
          "- **Richness**: share of 100 Hz-12 kHz bands within 36 dB of the loudest (0-1).\n\n";
    md << "## Per category\n\n| Category | Presets | Osc/preset | 3+ layers | Mod routes | FX/preset | Slow LFO motion | Signature features | Movement | Evolution | Width | Richness |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    auto row = [&] (const juce::String& name, const Acc& a)
    {
        const double n = juce::jmax (1, a.n);
        md << "| " << name << " | " << a.n << " | " << juce::String (a.osc / n, 1) << " | " << pct (a.layered, a.n) << " | " << juce::String (a.mods / n, 1)
           << " | " << juce::String (a.fx / n, 1) << " | " << pct (a.slowMod, a.n) << " | " << pct (a.sig, a.n) << " | " << juce::String (a.flux / n, 2)
           << " | " << juce::String (a.evo / n, 2) << " | " << juce::String (a.width / n, 2) << " | " << juce::String (a.rich / n, 2) << " |\n";
    };
    for (auto& c : gx::categories()) if (per.count (c)) row (c, per[c]);
    row ("**All**", all);
    auto usage = [&] (const char* title, std::map<juce::String, int>& m)
    {
        md << "\n## " << title << "\n\n| | Presets | Share |\n|---|---|---|\n";
        std::vector<std::pair<int, juce::String>> s; for (auto& [k, n] : m) s.push_back ({ n, k });
        std::sort (s.rbegin(), s.rend());
        for (auto& [n, k] : s) md << "| " << k << " | " << n << " | " << pct (n, all.n) << " |\n";
    };
    usage ("Oscillator types (presets using each type)", oscUse);
    usage ("Filter types", fltUse);
    usage ("Effects", fxUse);
    usage ("Modulation sources", srcUse);
    std::sort (flat.begin(), flat.end());
    md << "\n## Flattest 15 presets (movement + evolution)\n\n";
    for (size_t i = 0; i < juce::jmin ((size_t) 15, flat.size()); ++i) md << "- " << flat[i].second << " (" << juce::String (flat[i].first, 2) << ")\n";
    out.replaceWithText (md);
    std::printf ("%s", md.toRawUTF8());
    return 0;
}

int runListenPack (int argc, char** argv)
{
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "listen");
    dir.createDirectory();
    juce::String notes;
    int idx = 0;
    auto emit = [&] (const Req& r, bool extra)
    {
        auto p = make (r);
        auto audio = renderPhrase (p);
        normalise (audio);
        const auto in = probe (p);
        const auto file = juce::String (++idx).paddedLeft ('0', 2) + " - " + juce::String (r.cat).replace ("/", "-") + " - " + juce::String (r.sub) + ".wav";
        writeWav (dir.getChildFile (file), audio);
        notes << file << "  ->  " << p.meta.name << (extra ? "  [new archetype]" : "")
              << juce::String::formatted ("   movement %.2f  evolution %.2f  width %.2f  richness %.2f\n", in.flux, in.evolution, in.width, in.richness);
        std::printf ("%s\n", file.toRawUTF8());
    };
    for (auto& r : kPack) emit (r, false);
    for (auto& r : kExtra) if (gx::subcategories (r.cat).contains (r.sub)) emit (r, true);
    dir.getChildFile ("notes.txt").replaceWithText (notes);
    return 0;
}
