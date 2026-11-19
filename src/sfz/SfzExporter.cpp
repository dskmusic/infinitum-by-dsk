#include "SfzExporter.h"
#include "../preset/Render.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace gx
{
juce::String SfzExporter::noteName (int n)
{
    static const char* names[] { "C", "Cs", "D", "Ds", "E", "F", "Fs", "G", "Gs", "A", "As", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

juce::StringArray SfzExporter::unrepresentable (const Preset& p)
{
    juce::StringArray w;
    auto v = [&] (int i) { return p.params[(size_t) i]; };
    bool realtimeSrc = false, macroSrc = false, randomSrc = false;
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = (int) v (P::mod (s, P::mSrc));
        if (src <= 0 || (int) v (P::mod (s, P::mDst)) <= 0 || v (P::mod (s, P::mAmt)) == 0.0f) continue;
        if (src == msModWheel || src == msAftertouch || src == msPitchBend || src == msSlide) realtimeSrc = true;
        if (src >= msMacro1 && src <= msMacro8) macroSrc = true;
        if (src == msRandom) randomSrc = true;
    }
    if (realtimeSrc) w.add (L ("Modulaciones por mod wheel/aftertouch/pitch bend/slide no se reproducen (se graba su valor en reposo).", "Mod wheel/aftertouch/pitch bend/slide modulation is not reproduced (its resting value is recorded)."));
    if (macroSrc) w.add (L ("Las macros quedan fijadas en su valor actual.", "Macros are frozen at their current value."));
    if (randomSrc) w.add (L ("La fuente 'Random' por nota se graba con un único valor por muestra.", "The per-note 'Random' source is recorded with a single value per sample."));
    if (v (P::arpOn) > 0.5f) w.add (L ("El arpegiador se graba dentro de las muestras (no seguirá el tempo del host).", "The arpeggiator is baked into the samples (it does not follow host tempo)."));
    if (v (P::tgOn) > 0.5f) w.add (L ("El trance gate se graba dentro de las muestras (no seguirá el tempo del host).", "The trance gate is baked into the samples (it does not follow host tempo)."));
    if ((int) v (P::playMode) != pmPoly || v (P::glide) > 0.0005f) w.add (L ("Mono/legato y glide no tienen equivalente en este SFZ.", "Mono/legato and glide have no equivalent in this SFZ."));
    if (v (P::chordMode) > 0.5f) w.add (L ("El modo acorde se graba en cada muestra.", "Chord mode is baked into every sample."));
    for (int l = 0; l < kNumLfo; ++l)
        if (v (P::lfo (l, P::lRetrig)) < 0.5f) { w.add (L ("Los LFO libres (sin retrigger) quedan congelados en la fase de grabación.", "Free-running LFOs (no retrigger) are frozen at the recording phase.")); break; }
    if (v (P::mpeOn) > 0.5f) w.add (L ("MPE no está soportado en SFZ.", "MPE is not supported in SFZ."));
    return w;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr, int bits)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (! os) return;
    auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (bits));
    if (writer) writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

SfzResult SfzExporter::exportPreset (const Preset& p, const SfzOptions& o, const juce::File& parent,
                                     const std::atomic<bool>* cancel, std::function<void (float)> progress)
{
    SfzResult res;
    res.warnings = unrepresentable (p);
    const auto safeName = juce::File::createLegalFileName (p.meta.name.isEmpty() ? "Infinitum" : p.meta.name).replaceCharacter (' ', '_');
    auto dir = parent.getChildFile (safeName);
    auto samples = dir.getChildFile ("samples");
    if (! samples.createDirectory()) { res.error = "No se pudo crear " + samples.getFullPathName(); return res; }

    const int step = juce::jlimit (1, 24, o.stepSemis);
    const int lo = juce::jlimit (0, 127, juce::jmin (o.lowNote, o.highNote)), hi = juce::jlimit (0, 127, juce::jmax (o.lowNote, o.highNote));
    const int layers = juce::jlimit (1, 8, o.velLayers);
    std::vector<int> roots;
    for (int n = lo; n <= hi; n += step) roots.push_back (n);
    if (roots.back() != hi && hi - roots.back() > step / 2) roots.push_back (hi);

    const float sustain = p.params[(size_t) P::env (0, P::eSustain)];
    const bool rhythmic = p.params[(size_t) P::arpOn] > 0.5f || p.params[(size_t) P::tgOn] > 0.5f;
    const bool doLoop = o.loop && sustain > 0.05f && ! rhythmic;
    const double sr = o.sampleRate;
    const int holdN = (int) (o.noteSeconds * sr), totalN = (int) ((o.noteSeconds + o.releaseTail) * sr);
    const float release = p.params[(size_t) P::env (0, P::eRelease)];

    juce::String regions;
    const int total = (int) roots.size() * layers;
    int done = 0;
    for (size_t ri = 0; ri < roots.size(); ++ri)
    {
        const int root = roots[ri];
        const int lokey = ri == 0 ? 0 : (roots[ri - 1] + root) / 2 + 1;
        const int hikey = ri + 1 == roots.size() ? 127 : (root + roots[ri + 1]) / 2;
        for (int l = 0; l < layers; ++l)
        {
            if (cancel && cancel->load()) { res.cancelled = true; res.error = "Cancelado"; return res; }
            const int lovel = 1 + (127 * l) / layers, hivel = (127 * (l + 1)) / layers;
            const float vel = (float) (lovel + hivel) / 2.0f / 127.0f;
            juce::AudioBuffer<float> buf (2, totalN);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, root, vel), 0);
            m.addEvent (juce::MidiMessage::noteOff (1, root), doLoop ? totalN - 1 : holdN);
            if (! renderPreset (p, m, sr, 256, buf, cancel)) { res.cancelled = true; res.error = "Cancelado"; return res; }

            int loopStart = 0, loopEnd = 0;
            if (doLoop)
            {
                // loop inside the sustained part, length = whole number of periods, crossfade baked in
                const double period = sr / (440.0 * std::pow (2.0, (root - 69) / 12.0));
                const int xf = juce::jmax (16, (int) (o.crossfadeSeconds * sr));
                loopStart = juce::jmax (xf + 1, (int) (holdN * 0.45));
                const int wanted = (int) (holdN * 0.5);
                const int periods = juce::jmax (1, (int) std::round (wanted / period));
                loopEnd = juce::jmin (totalN - 2, loopStart + (int) std::round (periods * period));
                for (int c = 0; c < 2; ++c)
                {
                    float* d = buf.getWritePointer (c);
                    for (int i = 0; i < xf; ++i)
                    {
                        const float w = (float) i / (float) xf;
                        const int a = loopEnd - xf + i, b = loopStart - xf + i;
                        d[a] = d[a] * (1.0f - w) + d[b] * w;
                    }
                }
                buf.setSize (2, loopEnd + 1 + (int) (0.05 * sr), true, true); // keep a short tail after the loop
            }
            else
            {
                // trim trailing silence
                int last = buf.getNumSamples() - 1;
                while (last > holdN && buf.getMagnitude (last - 255 > 0 ? last - 255 : 0, 256) < 1.0e-4f) last -= 256;
                buf.setSize (2, juce::jmax (holdN, last + 1), true, true);
                const int fade = juce::jmin (buf.getNumSamples(), (int) (0.01 * sr));
                buf.applyGainRamp (buf.getNumSamples() - fade, fade, 1.0f, 0.0f);
            }
            const auto file = samples.getChildFile (safeName + "_" + noteName (root) + "_v" + juce::String (l + 1) + ".wav");
            writeWav (file, buf, sr, o.bitDepth == 16 ? 16 : 24);
            ++res.numSamples;
            regions << "<region> sample=" << file.getFileName()
                    << " lokey=" << lokey << " hikey=" << hikey << " pitch_keycenter=" << root
                    << " lovel=" << lovel << " hivel=" << hivel;
            if (doLoop) regions << " loop_mode=loop_continuous loop_start=" << loopStart << " loop_end=" << loopEnd;
            else regions << " loop_mode=no_loop";
            regions << "\n";
            if (progress) progress ((float) ++done / (float) total);
        }
    }

    juce::String sfz;
    sfz << "// " << p.meta.name << " - exported from Infinitum by DSK " << INFINITUM_VERSION << " (https://dskmusic.com)\n"
        << "// Category: " << p.meta.category << " / " << p.meta.subcategory << "\n";
    for (auto& w : res.warnings) sfz << "// NOTE: " << w << "\n";
    sfz << "\n<control>\ndefault_path=samples/\n\n<global>\n"
        << "ampeg_attack=0.001\n"
        << "ampeg_decay=0\nampeg_sustain=100\n"
        << "ampeg_release=" << juce::String (juce::jlimit (0.005f, 12.0f, release), 3) << "\n"
        << "amp_veltrack=" << (layers > 1 ? 30 : 80) << "\n"
        << "// Timbre (incl. the synth's filters) is baked into the samples: the SFZ filter is left open for tweaking.\n"
        << "fil_type=lpf_2p\ncutoff=20000\nresonance=0\n"
        << "\n<group>\n" << regions;
    res.sfzFile = dir.getChildFile (safeName + ".sfz");
    if (! res.sfzFile.replaceWithText (sfz)) { res.error = "No se pudo escribir el .sfz"; return res; }
    res.ok = true;
    return res;
}

juce::String SfzExporter::verify (const juce::File& sfz)
{
    if (! sfz.existsAsFile()) return "sfz missing";
    juce::StringArray lines;
    lines.addLines (sfz.loadFileAsString());
    struct R { int lk, hk, lv, hv, ls = -1, le = -1; juce::String file; };
    std::vector<R> rs;
    for (auto& ln : lines)
    {
        if (! ln.startsWith ("<region>")) continue;
        R r {};
        auto toks = juce::StringArray::fromTokens (ln.fromFirstOccurrenceOf ("<region>", false, false), " ", "");
        for (auto& t : toks)
        {
            const auto k = t.upToFirstOccurrenceOf ("=", false, false), v = t.fromFirstOccurrenceOf ("=", false, false);
            if (k == "sample") r.file = v;
            else if (k == "lokey") r.lk = v.getIntValue();
            else if (k == "hikey") r.hk = v.getIntValue();
            else if (k == "lovel") r.lv = v.getIntValue();
            else if (k == "hivel") r.hv = v.getIntValue();
            else if (k == "loop_start") r.ls = v.getIntValue();
            else if (k == "loop_end") r.le = v.getIntValue();
        }
        rs.push_back (r);
    }
    if (rs.empty()) return "no regions";
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    // every (key, vel) pair covered exactly once
    for (int key = 0; key < 128; ++key)
        for (int vel = 1; vel < 128; ++vel)
        {
            int c = 0;
            for (auto& r : rs) c += key >= r.lk && key <= r.hk && vel >= r.lv && vel <= r.hv;
            if (c != 1) return "coverage key " + juce::String (key) + " vel " + juce::String (vel) + " = " + juce::String (c);
        }
    for (auto& r : rs)
    {
        auto f = sfz.getParentDirectory().getChildFile ("samples").getChildFile (r.file);
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (! rd) return "unreadable " + r.file;
        if (r.ls >= 0 && (r.ls >= r.le || r.le >= (int) rd->lengthInSamples)) return "bad loop " + r.file;
    }
    return {};
}
}
