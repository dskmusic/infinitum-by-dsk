#include "Tests.h"
#include <juce_audio_formats/juce_audio_formats.h>

int runToolExtras (const juce::String& cmd, int argc, char** argv); // ToolUi.cpp (UI build only)

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String cmd = argc > 1 ? argv[1] : "test";
    if (cmd == "test") return gxtest::runAll();
    if (cmd == "render")
    {
        // InfinitumTool render <out.wav> [seconds] [preset.infinitum | seed]
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "render.wav");
        const double secs = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 60.0;
        gx::Preset p;
        if (argc > 4)
        {
            const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (argv[4]);
            if (! (f.existsAsFile() && gx::Preset::loadFrom (f, p))) p.params[(size_t) gx::P::seed] = (float) juce::String (argv[4]).getIntValue();
        }
        const auto err = gx::exportAudio (p, out, gx::efWav, secs, 48000.0);
        std::printf ("%s\n", err.isEmpty() ? out.getFullPathName().toRawUTF8() : err.toRawUTF8());
        return err.isEmpty() ? 0 : 1;
    }
#if ! INFINITUM_NO_UI
    if (const int r = runToolExtras (cmd, argc, argv); r != -1) return r;
#endif
    std::fprintf (stderr, "usage: InfinitumTool test | render <out.wav> [seconds] [preset|seed]\n");
    return 2;
}
