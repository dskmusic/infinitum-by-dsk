#include "Tests.h"
#include "gen/Generator.h"
#include <set>
#if __has_include(<BinaryData.h>)
 #include <BinaryData.h>
 #define GX_HAS_BINARYDATA 1
#endif

using namespace gxtest;

// Every factory preset is re-rendered through the validator: catches DSP regressions too.
static void testFactoryBank()
{
    std::puts ("[factory bank]");
#if GX_HAS_BINARYDATA
    int size = 0;
    const char* data = BinaryData::getNamedResource ("factory_infinitumbank", size);
    check (data != nullptr, "factory bank embedded");
    if (data == nullptr) return;
    juce::var v;
    check (juce::JSON::parse (juce::String::fromUTF8 (data, size), v).wasOk(), "factory bank parses");
    auto* arr = v.getProperty ("presets", {}).getArray();
    check (arr != nullptr && arr->size() >= 128, "at least 128 factory presets");
    if (arr == nullptr) return;
    std::set<juce::String> names;
    std::map<juce::String, int> perCat;
    int arp = 0, gate = 0, seq = 0, bad = 0;
    for (auto& pv : *arr)
    {
        auto p = gx::Preset::fromVar (pv);
        check (names.insert (p.meta.name).second, "unique name " + p.meta.name);
        check (p.meta.tags.size() > 0 && p.meta.subcategory.isNotEmpty(), "metadata " + p.meta.name);
        perCat[p.meta.category]++;
        arp += p.params[(size_t) gx::P::arpOn] > 0.5f;
        gate += p.params[(size_t) gx::P::tgOn] > 0.5f;
        for (int s = 0; s < gx::kNumModSlots; ++s)
            if ((int) p.params[(size_t) gx::P::mod (s, gx::P::mSrc)] == gx::msStepSeq) { ++seq; break; }
        auto vres = gx::Generator::validate (p, false);
        const bool ok = vres.ok && vres.peakDb <= -0.3f && vres.rmsDb > -36.0f && vres.rmsDb < -8.0f;
        if (! ok) { ++bad; std::printf ("    %s: %s rms %.1f peak %.1f\n", p.meta.name.toRawUTF8(), vres.reason.toRawUTF8(), vres.rmsDb, vres.peakDb); }
    }
    check (bad == 0, "all factory presets pass validation (" + juce::String (bad) + " failed)");
    for (auto& c : gx::categories()) check (perCat[c] >= 10, "category coverage " + c + " " + juce::String (perCat[c]));
    check (arp >= 10 && gate >= 3 && seq >= 5, "presets use arp/gate/seq: " + juce::String (arp) + "/" + juce::String (gate) + "/" + juce::String (seq));
    std::printf ("  factory: %d presets, arp %d, gate %d, seq %d\n", arr->size(), arp, gate, seq);
#else
    check (false, "BinaryData missing");
#endif
}

static const bool registered = [] { extraTests().push_back (testFactoryBank); return true; }();
