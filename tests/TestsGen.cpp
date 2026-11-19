#include "Tests.h"
#include "gen/Generator.h"

using namespace gxtest;

// Every category must produce a validated preset within the attempt budget.
static void testGenerator()
{
    std::puts ("[generator]");
    int total = 0, worst = 0;
    for (auto& cat : gx::categories())
        for (uint32_t seed : { 11u, 222u })
        {
            gx::GenRequest r;
            r.category = cat; r.seed = seed;
            r.brightness = 0.3f + 0.4f * (float) (seed % 3) / 2.0f;
            int attempts = 0;
            gx::Validation v;
            auto p = gx::Generator::generate (r, &attempts, &v);
            check (v.ok, "generate " + cat + " seed " + juce::String (seed) + " (" + v.reason + ")");
            check (v.peakDb <= -0.9f, "peak headroom " + cat + " " + juce::String (v.peakDb, 1));
            check (p.meta.name.isNotEmpty() && p.meta.category == cat, "meta " + cat);
            total += attempts; worst = juce::jmax (worst, attempts);
        }
    std::printf ("  generator attempts: total %d, worst %d\n", total, worst);

    gx::GenRequest r; r.category = "Pad"; r.seed = 5;
    auto a = gx::Generator::generate (r);
    auto m = gx::Generator::mutate (a, 0.5f, 9);
    check (m.similarity (a) < 0.999f && m.similarity (a) > 0.3f, "mutate is a gentle variation");
    r.category = "Bass"; r.seed = 77;
    auto b2 = gx::Generator::generate (r);
    auto bl = gx::Generator::blend (a, b2, 0.5f, 3);
    check (gx::Generator::validate (bl).rmsDb > -70.0f, "blend renders");
    // locks: osc section copied from base
    gx::GenRequest lr; lr.category = "Lead"; lr.seed = 99; lr.base = &a; lr.locks = { "osc" };
    auto locked = gx::Generator::candidate (lr, 99);
    check (locked.params[(size_t) gx::paramIndex ("osc1_type")] == a.params[(size_t) gx::paramIndex ("osc1_type")]
           && locked.params[(size_t) gx::paramIndex ("osc1_morph")] == a.params[(size_t) gx::paramIndex ("osc1_morph")], "lock keeps oscillator section");
}

static const bool registered = [] { extraTests().push_back (testGenerator); return true; }();
