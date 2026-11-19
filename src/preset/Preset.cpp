#include "Preset.h"
#include "../PluginProcessor.h"

namespace gx
{
static const char* kFxIds[kNumFx] { "dist", "chorus", "flanger", "phaser", "delay", "reverb", "comp", "eq", "crush", "widener", "shimmer", "granular" };

const juce::StringArray& categories()
{
    static const juce::StringArray c { "Bass", "Lead", "Pad", "Pluck", "Keys", "Arp", "Strings", "Brass", "FX", "Drums/Perc", "Ambient" };
    return c;
}
const juce::StringArray& subcategories (const juce::String& cat)
{
    static const std::map<juce::String, juce::StringArray> m {
        { "Bass", { "Sub", "Synth", "Reese", "Acid", "Wobble", "FM", "Pluck", "Growl" } },
        { "Lead", { "Mono", "Poly", "Sync", "Supersaw", "FM", "Soft", "Anthem", "Vocal" } },
        { "Pad", { "Warm", "Evolving", "Dark", "Bright", "Choir", "Gated", "Cinematic" } },
        { "Pluck", { "Short", "Bell", "Mallet", "Synth" } },
        { "Keys", { "Electric", "Organ", "Bell", "Clav", "Digital" } },
        { "Arp", { "Classic", "Trance", "Evolving", "Sequence" } },
        { "Strings", { "Ensemble", "Solo", "Pizzicato", "Synth", "Hybrid" } },
        { "Brass", { "Synth Brass", "Stab", "Horn", "Trailer" } },
        { "FX", { "Riser", "Downer", "Noise", "Texture", "Impact" } },
        { "Drums/Perc", { "Kick", "Snare", "Hat", "Tom", "Perc" } },
        { "Ambient", { "Drone", "Texture", "Soundscape", "Cathedral" } },
    };
    static const juce::StringArray none;
    auto it = m.find (cat);
    return it == m.end() ? none : it->second;
}
const juce::StringArray& characterTags()
{
    static const juce::StringArray t { "Dark", "Bright", "Warm", "Cold", "Evolving", "Aggressive", "Soft", "Vintage", "Modern",
                                       "Wide", "Mono", "Clean", "Dirty", "Metallic", "Airy", "Punchy", "Glassy", "Lo-Fi",
                                       "Cinematic", "Rhythmic" };
    return t;
}

// ---- helpers
static juce::String encodeFloats (const std::vector<float>& v)
{
    juce::MemoryOutputStream mo;
    for (float x : v) mo.writeShort ((short) juce::jlimit (-32767, 32767, (int) std::lround (x * 32767.0f)));
    return juce::Base64::toBase64 (mo.getData(), mo.getDataSize());
}
static std::vector<float> decodeFloats (const juce::String& s)
{
    juce::MemoryOutputStream mo;
    if (! juce::Base64::convertFromBase64 (mo, s)) return {};
    juce::MemoryInputStream mi (mo.getData(), mo.getDataSize(), false);
    std::vector<float> v;
    v.reserve (mo.getDataSize() / 2);
    while (! mi.isExhausted()) v.push_back ((float) mi.readShort() / 32767.0f);
    return v;
}
static bool isMatrixParam (int i) { return i >= P::modBase && i < P::fxBase; }

Preset::Preset()
{
    for (auto& d : paramDefs()) params.push_back (d.def);
    meta.uuid = juce::Uuid().toString();
    meta.created = meta.modified = juce::Time::getCurrentTime();
}

juce::var Preset::toVar (bool includeMeta) const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "infinitum");
    root->setProperty ("version", kPresetFormatVersion);
    root->setProperty ("app", INFINITUM_VERSION);
    if (includeMeta)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("uuid", meta.uuid);
        m->setProperty ("name", meta.name);
        m->setProperty ("category", meta.category);
        m->setProperty ("subcategory", meta.subcategory);
        m->setProperty ("author", meta.author);
        m->setProperty ("notes", meta.notes);
        juce::Array<juce::var> tags;
        for (auto& t : meta.tags) tags.add (t);
        m->setProperty ("tags", tags);
        m->setProperty ("favorite", meta.favorite);
        m->setProperty ("rating", meta.rating);
        m->setProperty ("created", meta.created.toISO8601 (true));
        m->setProperty ("modified", meta.modified.toISO8601 (true));
        root->setProperty ("meta", juce::var (m));
    }
    const auto& defs = paramDefs();
    auto* prm = new juce::DynamicObject();
    for (size_t i = 0; i < defs.size(); ++i)
        if (! isMatrixParam ((int) i))
            prm->setProperty (defs[i].id, defs[i].kind == pkFloat ? juce::var (params[i]) : juce::var ((int) std::lround (params[i])));
    root->setProperty ("params", juce::var (prm));

    juce::Array<juce::var> matrix;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = (int) params[(size_t) P::mod (s, P::mSrc)], dst = (int) params[(size_t) P::mod (s, P::mDst)];
        const float amt = params[(size_t) P::mod (s, P::mAmt)];
        if (src <= 0 && dst <= 0 && amt == 0.0f) continue;
        auto* slot = new juce::DynamicObject();
        slot->setProperty ("slot", s);
        slot->setProperty ("src", sourceIds()[juce::jlimit (0, msNumSources - 1, src)]);
        slot->setProperty ("dst", destDefs()[(size_t) juce::jlimit (0, (int) destDefs().size() - 1, dst)].id);
        slot->setProperty ("amt", amt);
        matrix.add (juce::var (slot));
    }
    root->setProperty ("matrix", matrix);

    auto* pt = new juce::DynamicObject();
    juce::Array<juce::var> arp, gate, seq, order, lfos;
    for (auto& a : patterns.arp)
    {
        juce::Array<juce::var> st { a.on, a.vel, a.gate, a.ratchet, a.prob, a.transpose };
        arp.add (st);
    }
    for (float g : patterns.gate) gate.add (g);
    for (float s : patterns.seq) seq.add (s);
    for (int o : patterns.fxOrder) order.add (kFxIds[juce::jlimit (0, kNumFx - 1, o)]);
    for (auto& t : patterns.lfoTable) lfos.add (encodeFloats (std::vector<float> (t.begin(), t.end())));
    pt->setProperty ("arp", arp);
    pt->setProperty ("gate", gate);
    pt->setProperty ("seq", seq);
    pt->setProperty ("fxOrder", order);
    pt->setProperty ("lfoTables", lfos);
    root->setProperty ("patterns", juce::var (pt));

    juce::Array<juce::var> wts;
    for (int o = 0; o < kNumOsc; ++o)
    {
        if (wavetables[(size_t) o].empty()) { wts.add (juce::var()); continue; }
        auto* w = new juce::DynamicObject();
        w->setProperty ("name", wavetableNames[(size_t) o]);
        w->setProperty ("frameSize", (int) wavetables[(size_t) o][0].size());
        std::vector<float> flat;
        for (auto& fr : wavetables[(size_t) o]) flat.insert (flat.end(), fr.begin(), fr.end());
        w->setProperty ("data", encodeFloats (flat));
        wts.add (juce::var (w));
    }
    root->setProperty ("wavetables", wts);
    if (! sample.empty())
    {
        auto* s = new juce::DynamicObject();
        s->setProperty ("name", sampleName);
        s->setProperty ("rate", sampleRate);
        s->setProperty ("root", sampleRoot);
        s->setProperty ("data", encodeFloats (sample));
        root->setProperty ("sample", juce::var (s));
    }
    return juce::var (root);
}

// Upgrade older documents in place. v1 is the first public format.
static void migrate (juce::var& v)
{
    const int ver = (int) v.getProperty ("version", 1);
    juce::ignoreUnused (ver); // future: if (ver < 2) { rename ids... }
}

Preset Preset::fromVar (const juce::var& in)
{
    juce::var v = in;
    migrate (v);
    Preset p;
    if (auto* m = v.getProperty ("meta", {}).getDynamicObject())
    {
        p.meta.uuid = m->getProperty ("uuid").toString();
        if (p.meta.uuid.isEmpty()) p.meta.uuid = juce::Uuid().toString();
        p.meta.name = m->getProperty ("name").toString();
        p.meta.category = m->getProperty ("category").toString();
        p.meta.subcategory = m->getProperty ("subcategory").toString();
        p.meta.author = m->getProperty ("author").toString();
        p.meta.notes = m->getProperty ("notes").toString();
        if (auto* t = m->getProperty ("tags").getArray()) for (auto& x : *t) p.meta.tags.add (x.toString());
        p.meta.favorite = (bool) m->getProperty ("favorite");
        p.meta.rating = (int) m->getProperty ("rating");
        p.meta.created = juce::Time::fromISO8601 (m->getProperty ("created").toString());
        p.meta.modified = juce::Time::fromISO8601 (m->getProperty ("modified").toString());
    }
    const auto& defs = paramDefs();
    if (auto* prm = v.getProperty ("params", {}).getDynamicObject())
        for (auto& nv : prm->getProperties())
        {
            const int i = paramIndex (nv.name.toString());
            if (i < 0 || isMatrixParam (i)) continue;
            p.params[(size_t) i] = defs[(size_t) i].range.snapToLegalValue ((float) (double) nv.value);
        }
    if (auto* mx = v.getProperty ("matrix", {}).getArray())
        for (auto& sv : *mx)
        {
            const int s = (int) sv.getProperty ("slot", -1);
            if (s < 0 || s >= kNumModSlots) continue;
            p.params[(size_t) P::mod (s, P::mSrc)] = (float) juce::jmax (0, sourceIds().indexOf (sv.getProperty ("src", "none").toString()));
            p.params[(size_t) P::mod (s, P::mDst)] = (float) destIndex (sv.getProperty ("dst", "none").toString());
            p.params[(size_t) P::mod (s, P::mAmt)] = juce::jlimit (-1.0f, 1.0f, (float) (double) sv.getProperty ("amt", 0.0));
        }
    if (auto* pt = v.getProperty ("patterns", {}).getDynamicObject())
    {
        if (auto* a = pt->getProperty ("arp").getArray())
            for (int i = 0; i < juce::jmin (kMaxArpSteps, a->size()); ++i)
                if (auto* st = (*a)[i].getArray(); st != nullptr && st->size() >= 6)
                    p.patterns.arp[(size_t) i] = { (float) (*st)[0], (float) (*st)[1], (float) (*st)[2], (float) (*st)[3], (float) (*st)[4], (float) (*st)[5] };
        if (auto* g = pt->getProperty ("gate").getArray())
            for (int i = 0; i < juce::jmin (kMaxGateSteps, g->size()); ++i) p.patterns.gate[(size_t) i] = (float) (*g)[i];
        if (auto* s = pt->getProperty ("seq").getArray())
            for (int i = 0; i < juce::jmin (kMaxSeqSteps, s->size()); ++i) p.patterns.seq[(size_t) i] = (float) (*s)[i];
        if (auto* o = pt->getProperty ("fxOrder").getArray(); o != nullptr && o->size() == kNumFx)
        {
            std::array<int, kNumFx> ord {};
            std::array<bool, kNumFx> seen {};
            bool ok = true;
            for (int i = 0; i < kNumFx && ok; ++i)
            {
                int idx = -1;
                for (int k = 0; k < kNumFx; ++k) if ((*o)[i].toString() == kFxIds[k]) idx = k;
                ok = idx >= 0 && ! seen[(size_t) idx];
                if (ok) { seen[(size_t) idx] = true; ord[(size_t) i] = idx; }
            }
            if (ok) p.patterns.fxOrder = ord;
        }
        if (auto* l = pt->getProperty ("lfoTables").getArray())
            for (int i = 0; i < juce::jmin (kNumLfo, l->size()); ++i)
            {
                auto t = decodeFloats ((*l)[i].toString());
                if ((int) t.size() == kLfoTableSize) std::copy (t.begin(), t.end(), p.patterns.lfoTable[(size_t) i].begin());
            }
    }
    if (auto* w = v.getProperty ("wavetables", {}).getArray())
        for (int o = 0; o < juce::jmin (kNumOsc, w->size()); ++o)
        {
            const auto& wv = (*w)[o];
            if (! wv.isObject()) continue;
            const int fs = juce::jmax (16, (int) wv.getProperty ("frameSize", 2048));
            auto flat = decodeFloats (wv.getProperty ("data", "").toString());
            for (size_t s = 0; s + (size_t) fs <= flat.size() && p.wavetables[(size_t) o].size() < 256; s += (size_t) fs)
                p.wavetables[(size_t) o].emplace_back (flat.begin() + (long) s, flat.begin() + (long) s + fs);
            p.wavetableNames[(size_t) o] = wv.getProperty ("name", "User").toString();
        }
    if (auto s = v.getProperty ("sample", {}); s.isObject())
    {
        p.sample = decodeFloats (s.getProperty ("data", "").toString());
        p.sampleRate = (double) s.getProperty ("rate", 44100.0);
        p.sampleRoot = (int) s.getProperty ("root", 60);
        p.sampleName = s.getProperty ("name", "").toString();
    }
    return p;
}

juce::String Preset::toJson() const { return juce::JSON::toString (toVar(), false); }

bool Preset::fromJson (const juce::String& json, Preset& out)
{
    juce::var v;
    if (juce::JSON::parse (json, v).failed() || v.getProperty ("format", "").toString() != "infinitum") return false;
    out = fromVar (v);
    return true;
}

bool Preset::saveTo (const juce::File& f) const
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (toJson())) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

bool Preset::loadFrom (const juce::File& f, Preset& out)
{
    if (! f.existsAsFile() || f.getSize() > 64 * 1024 * 1024) return false;
    if (! fromJson (f.loadFileAsString(), out)) return false;
    out.meta.file = f;
    return true;
}

Preset Preset::capture (InfinitumProcessor& proc)
{
    Preset p;
    for (int i = 0; i < P::numParams; ++i) p.params[(size_t) i] = proc.getParam (i);
    p.patterns = proc.getPatterns();
    for (int o = 0; o < kNumOsc; ++o)
        if (auto* w = proc.getUserWavetable (o))
        {
            p.wavetables[(size_t) o] = w->exportFrames();
            p.wavetableNames[(size_t) o] = w->getName();
        }
    if (auto* s = proc.getSample())
    {
        p.sample = s->data; p.sampleRate = s->sampleRate; p.sampleRoot = s->rootNote; p.sampleName = s->name;
    }
    p.meta = proc.currentMeta;
    return p;
}

void Preset::applyTo (InfinitumProcessor& proc) const
{
    for (int i = 0; i < P::numParams; ++i) proc.setParamPlain (i, params[(size_t) i]);
    proc.setPatterns (patterns);
    for (int o = 0; o < kNumOsc; ++o)
        proc.setUserWavetable (o, wavetables[(size_t) o].empty() ? nullptr : Wavetable::fromFrames (wavetables[(size_t) o], wavetableNames[(size_t) o]));
    if (sample.empty()) proc.setSample (nullptr);
    else
    {
        auto s = std::make_unique<SampleData>();
        s->data = sample; s->sampleRate = sampleRate; s->rootNote = sampleRoot; s->name = sampleName;
        proc.setSample (std::move (s));
    }
    proc.currentMeta = meta;
}

float Preset::similarity (const Preset& o) const
{
    const auto& defs = paramDefs();
    double diff = 0; int n = 0;
    for (size_t i = 0; i < defs.size(); ++i)
    {
        if (defs[i].group == "macros") continue;
        const float a = defs[i].range.convertTo0to1 (params[i]), b = defs[i].range.convertTo0to1 (o.params[i]);
        diff += std::abs (a - b) * (defs[i].kind == pkChoice || defs[i].kind == pkBool ? 2.0 : 1.0);
        ++n;
    }
    return (float) juce::jlimit (0.0, 1.0, 1.0 - 12.0 * diff / juce::jmax (1, n));
}
}
