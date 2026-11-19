#include "Tests.h"
#include "gen/Generator.h"
#include "sfz/SfzExporter.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace gxtest;

static void testSfz()
{
    std::puts ("[sfz export]");
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("infinitum_sfz_test_" + juce::String (juce::Random::getSystemRandom().nextInt (100000)));
    for (auto cat : { "Pad", "Pluck" })
    {
        gx::GenRequest r; r.category = cat; r.seed = 4; if (juce::String (cat) == "Pad") r.subcategory = "Warm";
        auto p = gx::Generator::generate (r);
        gx::SfzOptions o;
        o.stepSemis = 12; o.lowNote = 36; o.highNote = 84; o.velLayers = 3; o.noteSeconds = 0.8; o.releaseTail = 0.4;
        o.bitDepth = juce::String (cat) == "Pad" ? 24 : 16;
        float lastProgress = 0;
        auto res = gx::SfzExporter::exportPreset (p, o, dir, nullptr, [&] (float pr) { lastProgress = pr; });
        check (res.ok, juce::String ("export ") + cat + " " + res.error);
        check (res.numSamples == 5 * 3, juce::String ("sample count ") + cat + " " + juce::String (res.numSamples));
        check (lastProgress > 0.99f, "progress reaches 100%");
        const auto err = gx::SfzExporter::verify (res.sfzFile);
        check (err.isEmpty(), juce::String ("sfz valid ") + cat + ": " + err);
        const auto txt = res.sfzFile.loadFileAsString();
        check (txt.contains ("pitch_keycenter=") && txt.contains ("ampeg_release=") && txt.contains ("fil_type="), "sfz opcodes");
        if (juce::String (cat) == "Pad") check (txt.contains ("loop_continuous"), "sustained sound is looped");
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        auto wav = res.sfzFile.getParentDirectory().getChildFile ("samples").findChildFiles (juce::File::findFiles, false, "*.wav");
        std::unique_ptr<juce::AudioFormatReader> rd (wav.isEmpty() ? nullptr : fm.createReaderFor (wav[0]));
        check (rd != nullptr && (int) rd->bitsPerSample == o.bitDepth && (int) rd->sampleRate == 44100, "wav format");
    }
    // cancel
    std::atomic<bool> cancel { true };
    gx::GenRequest r; r.category = "Lead"; r.seed = 3;
    auto res = gx::SfzExporter::exportPreset (gx::Generator::generate (r), {}, dir, &cancel);
    check (res.cancelled && ! res.ok, "export can be cancelled");
    dir.deleteRecursively();
}

static const bool registered = [] { extraTests().push_back (testSfz); return true; }();
