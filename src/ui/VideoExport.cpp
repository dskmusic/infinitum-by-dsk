#include "VideoExport.h"
#include <cstdio>
#include <thread>

namespace gx::ui
{
namespace
{
constexpr int kFps = 30;
constexpr double kSr = 48000.0;

FILE* openPipe (const juce::String& cmd)
{
#if JUCE_WINDOWS
    return _wpopen (("\"" + cmd + "\"").toWideCharPointer(), L"wb"); // cmd.exe needs the whole line quoted once more
#else
    return popen (cmd.toRawUTF8(), "w");
#endif
}
int closePipe (FILE* f)
{
#if JUCE_WINDOWS
    return _pclose (f);
#else
    return pclose (f);
#endif
}
} // namespace

// ---------------------------------------------------------------- ffmpeg
juce::File findFfmpeg (Settings& settings)
{
    const juce::File custom (settings.file().getValue ("ffmpeg"));
    if (custom.existsAsFile()) return custom;
#if JUCE_WINDOWS
    const juce::String exe = "ffmpeg.exe";
    juce::StringArray dirs { juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}) + "\\Microsoft\\WinGet\\Links",
                             "C:\\ffmpeg\\bin", juce::SystemStats::getEnvironmentVariable ("ProgramFiles", {}) + "\\ffmpeg\\bin",
                             juce::SystemStats::getEnvironmentVariable ("ProgramFiles(x86)", {}) + "\\ffmpeg",
                             juce::SystemStats::getEnvironmentVariable ("ProgramFiles(x86)", {}) + "\\ffmpeg\\bin",
                             juce::SystemStats::getEnvironmentVariable ("ProgramData", {}) + "\\chocolatey\\bin" };
    // winget "portable" installs also live in a versioned folder under Packages
    for (auto& d : juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {}) + "\\Microsoft\\WinGet\\Packages")
                       .findChildFiles (juce::File::findDirectories, false, "Gyan.FFmpeg*"))
        for (auto& f : d.findChildFiles (juce::File::findFiles, true, exe)) dirs.add (f.getParentDirectory().getFullPathName());
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ";", "\"");
#else
    const juce::String exe = "ffmpeg";
    juce::StringArray dirs { "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/snap/bin" };
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), ":", "\"");
#endif
    for (auto& d : dirs)
        if (d.trim().isNotEmpty())
        {
            const juce::File f = juce::File (d.trim().unquoted()).getChildFile (exe);
            if (f.existsAsFile()) return f;
        }
    return {};
}

juce::String ffmpegInstallCommand()
{
#if JUCE_WINDOWS
    return "winget install -e --id Gyan.FFmpeg --accept-source-agreements --accept-package-agreements";
#elif JUCE_MAC
    for (auto b : { "/opt/homebrew/bin/brew", "/usr/local/bin/brew" })
        if (juce::File (b).existsAsFile()) return juce::String (b) + " install ffmpeg";
    return {};
#else
    return {};
#endif
}

juce::String ffmpegInstructions()
{
    return L ("Para exportar vídeo, Infinitum usa ffmpeg (gratuito).\n\n"
              "Windows: abre una terminal y escribe:\n   winget install Gyan.FFmpeg\n"
              "macOS (con Homebrew): brew install ffmpeg\n"
              "Linux: sudo apt install ffmpeg   (o el gestor de tu distribución)\n\n"
              "También puedes descargarlo de https://ffmpeg.org/download.html y elegir el ejecutable con el botón «Elegir ffmpeg…».",
              "To export video, Infinitum uses ffmpeg (free).\n\n"
              "Windows: open a terminal and type:\n   winget install Gyan.FFmpeg\n"
              "macOS (with Homebrew): brew install ffmpeg\n"
              "Linux: sudo apt install ffmpeg   (or your distribution's package manager)\n\n"
              "You can also download it from https://ffmpeg.org/download.html and pick the executable with \"Choose ffmpeg...\".");
}

// ---------------------------------------------------------------- exporter
bool VideoExporter::start (const Preset& p, const juce::File& out, double seconds, int width, int height, const juce::File& ffmpeg, juce::String& error)
{
    preset = p; target = out; secs = seconds; w = width; h = height; ffmpegExe = ffmpeg;
    frame = 0; total = (int) (seconds * kFps); result = {};
    cancelFlag = false; audioDone = false; audioProgress = 0;
    wav = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("infinitum_video_audio_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)) + ".wav");
    if (! ffmpegExe.existsAsFile()) { error = "ffmpeg"; return false; }
    audioPending = true;
    audioThread = std::thread ([this]
    {
        audioError = exportAudio (preset, wav, efWav, secs, kSr, &audioProgress, &cancelFlag);
        audioDone = true;
    });
    return true;
}

void VideoExporter::cancel() { cancelFlag = true; }

void VideoExporter::finish (const juce::String& err)
{
    if (pipe != nullptr) { const int rc = closePipe (pipe); pipe = nullptr; if (err.isEmpty() && rc != 0) result = "ffmpeg error " + juce::String (rc); else result = err; }
    else result = err;
    if (audioThread.joinable()) audioThread.join();
    audioPending = false;
    wav.deleteFile();
    view.reset();
    eng.reset();
    if (result.isNotEmpty()) target.deleteFile();
}

bool VideoExporter::step (double budgetMs)
{
    if (audioPending)
    {
        if (! audioDone.load()) return true;           // still rendering the soundtrack
        audioThread.join();
        audioPending = false;
        if (audioError.isNotEmpty()) { finish (audioError); return false; }
        // soundtrack ready: start ffmpeg reading raw frames from stdin
        target.deleteFile();
        const juce::String cmd = "\"" + ffmpegExe.getFullPathName() + "\" -y -loglevel error -f rawvideo -pix_fmt bgra -s " + juce::String (w) + "x" + juce::String (h)
                                 + " -r " + juce::String (kFps) + " -i - -i \"" + wav.getFullPathName() + "\" -c:v libx264 -preset medium -crf 19 -pix_fmt yuv420p"
                                 + " -c:a aac -b:a 256k -shortest -movflags +faststart \"" + target.getFullPathName() + "\"";
        pipe = openPipe (cmd);
        if (pipe == nullptr) { finish ("cannot start ffmpeg"); return false; }
        eng = std::make_unique<Engine>();
        eng->prepare (kSr, 1600);
        eng->reset ((uint32_t) preset.get (P::seed));
        view = std::make_unique<Landscape>();
        view->showOverlay = false;
        view->fullBleed = true;
        view->setSize (w, h);
        buf.setSize (2, (int) (kSr / kFps));
        row.resize ((size_t) w * 4);
        fr = Frame();
    }
    if (cancelFlag.load()) { finish ("cancelled"); return false; }
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    const double stopAt = juce::jmax (0.0, secs - juce::jmin ((double) preset.get (P::fadeOut), secs * 0.25) - 3.0);
    juce::MidiBuffer none;
    while (frame < total && juce::Time::getMillisecondCounterHiRes() - t0 < budgetMs)
    {
        // run the engine for one video frame (same settings as the soundtrack) to drive the visuals
        HostInfo hi;
        hi.internalPlay = (double) frame / kFps < stopAt;
        const int n = buf.getNumSamples();
        eng->process (buf.getWritePointer (0), buf.getWritePointer (1), n, none, preset.params.data(), hi);
        float sq = 0;
        for (int i = 0; i < n; ++i) sq += buf.getSample (0, i) * buf.getSample (0, i) + buf.getSample (1, i) * buf.getSample (1, i);
        const float rms = std::sqrt (sq / (2.0f * (float) n)) * dbToGain (preset.get (P::master) * 0.0f);
        const float dt = 1.0f / kFps;
        auto& t = eng->tele;
        fr.time = (double) frame / kFps + 1.0;
        fr.energy += (juce::jmin (1.0f, rms * 6.0f) - fr.energy) * juce::jmin (1.0f, dt * 4.0f);
        for (int l = 0; l < kNumLayers; ++l)
        {
            fr.presence[(size_t) l] = t.presence[(size_t) l].load();
            fr.layerEnergy[(size_t) l] += (juce::jmin (1.0f, t.energy[(size_t) l].load() * 8.0f) - fr.layerEnergy[(size_t) l]) * juce::jmin (1.0f, dt * 3.0f);
        }
        for (int i = 0; i < kChordVoices; ++i) fr.chord[(size_t) i] = t.chordNotes[(size_t) i].load();
        fr.rootPc = t.rootPc.load(); fr.degree = t.chordDegree.load(); fr.bellCount = t.bellCount.load();
        fr.lastBellNote = t.lastBellNote.load(); fr.lastBellPan = t.lastBellPan.load();
        fr.runGain = t.runGain.load(); fr.evoTime = t.evoTime.load(); fr.musTime = t.musTime.load();
        fr.running = t.running.load(); fr.playButton = true;
        fr.light = preset.get (P::light); fr.space = preset.get (P::space); fr.harmony = preset.get (P::harmony);
        fr.scale = (int) preset.get (P::scale); fr.seed = (int) preset.get (P::seed); fr.frozen = preset.get (P::freeze) > 0.5f;
        fr.scene = (int) preset.get (P::scene); fr.evolution = preset.get (P::evolution); fr.density = preset.get (P::density);
        view->tick (fr);
        juce::Image img (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            view->paintEntireComponent (g, true);
            g.setColour (juce::Colours::white.withAlpha (0.35f)); // discreet signature
            g.setFont (font ((float) h * 0.018f, true));
            g.drawText ("Infinitum by DSK", juce::Rectangle<int> (0, 0, w - (int) (h * 0.025f), h - (int) (h * 0.02f)), juce::Justification::bottomRight);
        }
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* src = bd.getLinePointer (y);
            for (int x = 0; x < w; ++x) { const uint8_t* px = src + x * bd.pixelStride; std::memcpy (&row[(size_t) x * 4], px, 4); row[(size_t) x * 4 + 3] = 255; }
            if (std::fwrite (row.data(), 1, row.size(), pipe) != row.size()) { finish ("ffmpeg stopped (disk full?)"); return false; }
        }
        ++frame;
    }
    if (frame >= total) { finish ({}); return false; }
    return true;
}
}
