#include "Tests.h"
#include "PluginEditor.h"
#include "gen/Generator.h"
#include "ui/GeneratorPage.h"

int runVideo (int argc, char** argv); // Video.cpp
int runBankReport (int argc, char** argv); // Sound.cpp
int runListenPack (int argc, char** argv); // Sound.cpp

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String cmd = argc > 1 ? argv[1] : "test";
    if (cmd == "test") return gxtest::runAll();
    if (cmd == "video") return runVideo (argc, argv);
    if (cmd == "bank-report") return runBankReport (argc, argv);
    if (cmd == "listen-pack") return runListenPack (argc, argv);
    if (cmd == "bench")
    {
        // worst-ish case: 16 voices x 8 unison x 4 oscillators of mixed types, both filters, all FX
        InfinitumProcessor p;
        const char* types[] { "1", "0", "2", "4" };
        for (int o = 1; o <= 4; ++o)
        {
            gxtest::setParam (p, "osc" + juce::String (o) + "_on", 1);
            gxtest::setParam (p, "osc" + juce::String (o) + "_type", juce::String (types[o - 1]).getFloatValue());
        }
        gxtest::setParam (p, "uni_voices", 8);
        gxtest::setParam (p, "filter2_on", 1);
        for (auto id : { "chorus_on", "delay_on", "reverb_on", "eq_on" }) gxtest::setParam (p, id, 1);
        const double sr = 48000, secs = 10;
        juce::AudioBuffer<float> out (2, (int) (sr * secs));
        juce::MidiBuffer m;
        for (int i = 0; i < 16; ++i) m.addEvent (juce::MidiMessage::noteOn (1, 40 + i * 2, 0.8f), 0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p.renderOffline (out, m, sr, 256);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("rendered %.0f s in %.0f ms -> %.1f%% of one core\n", secs, ms, ms / (secs * 10.0));
        return 0;
    }
    if (cmd == "factory")
    {
        // InfinitumTool factory <out.infinitumbank> [perCategory]
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "resources/factory.infinitumbank");
        const int perCat = argc > 3 ? juce::String (argv[3]).getIntValue() : 20;
        juce::Array<juce::var> bank;
        juce::StringArray names;
        int total = 0, rejectedDup = 0;
        for (int ci = 0; ci < gx::categories().size(); ++ci)
        {
            const auto cat = gx::categories()[ci];
            auto subs = gx::subcategories (cat);
            if (cat == "Arp") subs.removeString ("Evolving"); // curated out of the factory bank (still available in the generator)
            std::vector<gx::Preset> accepted;
            for (int k = 0, guard = 0, misses = 0; (int) accepted.size() < perCat && guard < perCat * 12; ++guard)
            {
                gx::GenRequest r;
                r.category = cat;
                r.subcategory = subs[((int) accepted.size() + misses / 3) % subs.size()]; // round-robin; shift the rotation after every 3 misses
                r.seed = (uint32_t) (ci * 100003 + guard * 7919 + 17);
                gx::Rng rng (r.seed);
                // musically spread character settings, deterministic
                r.brightness = 0.15f + 0.7f * rng.uni();
                r.movement = 0.1f + 0.85f * rng.uni();
                r.aggression = cat == "Pad" || cat == "Ambient" ? 0.05f + 0.35f * rng.uni() : 0.1f + 0.75f * rng.uni();
                r.space = cat == "Bass" || cat == "Drums/Perc" ? 0.05f + 0.4f * rng.uni() : 0.2f + 0.75f * rng.uni();
                r.weirdness = rng.uni() < 0.7f ? 0.05f + 0.3f * rng.uni() : 0.45f + 0.5f * rng.uni();
                gx::Validation v;
                auto p = gx::Generator::generate (r, nullptr, &v);
                if (! v.ok) { ++misses; continue; }
                bool dup = false;
                for (auto& a : accepted) dup |= a.meta.subcategory == p.meta.subcategory && a.similarity (p) > 0.95f;
                if (dup) { ++rejectedDup; ++misses; continue; }
                for (int t = 0; names.contains (p.meta.name) && t < 30; ++t) p.meta.name = gx::Generator::makeName (cat, p.meta.subcategory, rng);
                if (names.contains (p.meta.name)) p.meta.name << " II";
                names.add (p.meta.name);
                p.meta.author = "DSK Music";
                p.meta.factory = true;
                p.meta.uuid = juce::Uuid().toString();
                p.meta.created = p.meta.modified = juce::Time (2026, 10, 1, 12, 0);
                juce::String tips = juce::String ("Macros: 1 Brightness, 2 Timbre, 3 Space, 4 Character. Mod wheel opens the filter.");
                if (p.params[(size_t) gx::P::arpOn] > 0.5f) tips << juce::String (" Hold a chord: the arpeggiator does the rest.");
                if (p.params[(size_t) gx::P::tgOn] > 0.5f) tips << " Trance gate synced to tempo.";
                for (int s = 0; s < gx::kNumModSlots; ++s)
                    if ((int) p.params[(size_t) gx::P::mod (s, gx::P::mSrc)] == gx::msStepSeq) { tips << " Step sequencer modulating the timbre."; break; }
                p.meta.notes = tips;
                accepted.push_back (p);
                ++k;
            }
            for (auto& p : accepted) bank.add (p.toVar());
            total += (int) accepted.size();
            std::printf ("%-11s %d presets\n", cat.toRawUTF8(), (int) accepted.size());
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "infinitumbank");
        root->setProperty ("version", gx::kPresetFormatVersion);
        root->setProperty ("name", "Infinitum Factory");
        root->setProperty ("presets", bank);
        out.getParentDirectory().createDirectory();
        out.replaceWithText (juce::JSON::toString (juce::var (root), true));
        std::printf ("factory bank: %d presets (%d near-duplicates rejected) -> %s\n", total, rejectedDup, out.getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "gen-debug")
    {
        gx::GenRequest r; r.category = juce::String::fromUTF8 (argv[2]); r.seed = (uint32_t) juce::String (argv[3]).getIntValue(); if (argc > 4) r.subcategory = argv[4];
        for (int k = 0; k < 5; ++k)
        {
            auto p = gx::Generator::candidate (r, r.seed + (uint32_t) k * 7919u);
            const float m0 = p.params[(size_t) gx::P::master];
            auto v = gx::Generator::validate (p);
            std::printf ("%s/%s '%s': ok=%d %s rms=%.1f peak=%.1f dc=%.4f sus=%.2f tail=%.1f cent=%.0f flat=%.2f master %.1f->%.1f\n",
                         p.meta.category.toRawUTF8(), p.meta.subcategory.toRawUTF8(), p.meta.name.toRawUTF8(), (int) v.ok, v.reason.toRawUTF8(),
                         v.rmsDb, v.peakDb, v.dc, v.sustainRatio, v.tailDb, v.centroidHz, v.flatness, m0, p.params[(size_t) gx::P::master]);
        }
        return 0;
    }
    if (cmd == "gen-report")
    {
        // InfinitumTool gen-report [count]: attempts and validation per category
        const int count = argc > 2 ? juce::String (argv[2]).getIntValue() : 10;
        for (auto& cat : gx::categories())
        {
            int att = 0, fails = 0; float rms = 0;
            std::map<juce::String, int> reasons;
            for (int i = 0; i < count; ++i)
            {
                gx::GenRequest r; r.category = cat; r.seed = (uint32_t) (1000 + i * 31);
                juce::Random rnd (i);
                r.brightness = rnd.nextFloat(); r.movement = rnd.nextFloat(); r.aggression = rnd.nextFloat(); r.space = rnd.nextFloat(); r.weirdness = rnd.nextFloat();
                for (int k = 0; k < 24; ++k)
                {
                    auto p = gx::Generator::candidate (r, r.seed + (uint32_t) k * 7919u);
                    auto v = gx::Generator::validate (p);
                    ++att;
                    if (v.ok) { rms += v.rmsDb; break; }
                    reasons[v.reason]++;
                    if (k == 23) ++fails;
                }
            }
            juce::String rs;
            for (auto& [k, n] : reasons) rs << k << "=" << n << " ";
            std::printf ("%-11s attempts/preset %.2f  fails %d  rejects: %s\n", cat.toRawUTF8(), (double) att / count, fails, rs.toRawUTF8());
        }
        return 0;
    }
    if (cmd == "preset-metrics")
    {
        // InfinitumTool preset-metrics "Name 1" "Name 2" ...  -> generator validation metrics of factory presets
        juce::var bank;
        juce::JSON::parse (juce::File::getCurrentWorkingDirectory().getChildFile ("resources/factory.infinitumbank").loadFileAsString(), bank);
        auto* arr = bank.isArray() ? bank.getArray() : bank.getProperty ("presets", {}).getArray();
        if (arr == nullptr) { std::printf ("bank not found (run from the repo root)\n"); return 1; }
        for (int a = 2; a < argc; ++a)
            for (auto& v : *arr)
            {
                auto p = gx::Preset::fromVar (v);
                if (p.meta.name != juce::String (argv[a])) continue;
                const auto r = gx::Generator::validate (p, false);
                std::printf ("%-18s rms %6.1f  peak %6.1f  centroid %6.0f Hz  flatness %.3f  sustain %.2f  %s\n", argv[a], r.rmsDb, r.peakDb,
                             r.centroidHz, r.flatness, r.sustainRatio, r.ok ? "ok" : r.reason.toRawUTF8());
            }
        return 0;
    }
    if (cmd == "icon")
    {
        // InfinitumTool icon <dir>  ->  infinitum-icon.png (1024 px, used by CMake ICON_BIG) + infinitum.ico (installer)
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "branding");
        dir.createDirectory();
        auto render = [] (int px)
        {
            juce::Image img (juce::Image::ARGB, px, px, true);
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale ((float) px / 64.0f)); // logo strokes are tuned for ~64 px
            const juce::Rectangle<float> r (2, 2, 60, 60);
            g.setColour (gx::ui::col::bgDeep);
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (gx::ui::col::accent.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
            gx::ui::Logo::drawMark (g, r.reduced (8.0f), 1.0f);
            return img;
        };
        auto png = [] (const juce::Image& img) { juce::MemoryOutputStream m; juce::PNGImageFormat().writeImageToStream (img, m); return m.getMemoryBlock(); };
        { auto f = dir.getChildFile ("infinitum-icon.png"); f.replaceWithData (png (render (1024)).getData(), png (render (1024)).getSize()); }
        // .ico with PNG-compressed entries (Windows Vista+)
        const int sizes[] { 16, 24, 32, 48, 64, 128, 256 };
        std::vector<juce::MemoryBlock> imgs;
        for (int s : sizes) imgs.push_back (png (render (s)));
        juce::MemoryOutputStream ico;
        ico.writeShort (0); ico.writeShort (1); ico.writeShort ((short) imgs.size());
        int offset = 6 + 16 * (int) imgs.size();
        for (size_t i = 0; i < imgs.size(); ++i)
        {
            ico.writeByte ((char) (sizes[i] >= 256 ? 0 : sizes[i])); ico.writeByte ((char) (sizes[i] >= 256 ? 0 : sizes[i]));
            ico.writeByte (0); ico.writeByte (0); ico.writeShort (1); ico.writeShort (32);
            ico.writeInt ((int) imgs[i].getSize()); ico.writeInt (offset);
            offset += (int) imgs[i].getSize();
        }
        for (auto& m : imgs) ico.write (m.getData(), m.getSize());
        dir.getChildFile ("infinitum.ico").replaceWithData (ico.getData(), ico.getDataSize());
        std::printf ("wrote %s\n", dir.getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "screenshot")
    {
        // InfinitumTool screenshot <outdir>
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "docs/screenshots");
        out.createDirectory();
        gx::forcedLanguage() = argc > 3 ? argv[3] : "en"; // InfinitumTool screenshot <outdir> [en|es]
        InfinitumProcessor p;
        p.prepareToPlay (48000, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* ge = dynamic_cast<InfinitumEditor*> (ed.get());
        ed->setSize (InfinitumEditor::kDesignW, InfinitumEditor::kDesignH);
        ed->addToDesktop (0);
        ed->setVisible (true);
        juce::AudioBuffer<float> buf (2, 512);
        bool first = true;
        auto pump = [&] (int frames)
        {
            for (int k = 0; k < frames; ++k)
            {
                juce::MidiBuffer m;
                if (first) { for (int n : { 48, 55, 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0); first = false; }
                for (int b = 0; b < 2; ++b) { p.processBlock (buf, m); m.clear(); }
                ge->advanceFrames (1.0 / 60.0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
            }
        };
        // generator: real generation (busy frame + finished frame)
        ge->showPage (0);
        pump (10);
        ge->generatorPage().generate();
        pump (30);
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = out.getChildFile ("generator-working.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        for (int k = 0; k < 400 && ge->generatorPage().isBusy(); ++k) pump (1);
        first = true;
        struct Shot { int page, sub; const char* name; };
        const Shot shots[] { { 0, -1, "generator" }, { 1, -1, "presets" }, { 2, 0, "editor-osc" }, { 2, 1, "editor-filters" },
                             { 2, 2, "editor-mod" }, { 2, 3, "editor-arp-gate-seq" }, { 2, 4, "editor-fx" }, { 3, -1, "export" }, { 4, -1, "settings" } };
        for (auto& s : shots)
        {
            ge->showPage (s.page);
            if (s.sub >= 0) ge->showEditorSub (s.sub);
            pump (40);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = out.getChildFile (juce::String (s.name) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        // language switch: whole UI rebuilt live, generator state kept
        const auto before = ge->generatorPage().uiState();
        gx::forcedLanguage() = gx::uiEnglish() ? "es" : "en";
        ge->rebuildUi();
        pump (10);
        const auto after = ge->generatorPage().uiState();
        const bool ok = gx::uiEnglish() == (gx::forcedLanguage() == "en") && after.category == before.category && after.sliders == before.sliders;
        std::printf ("language rebuild: %s\n", ok ? "OK" : "FAILED");
        ed->removeFromDesktop();
        ed.reset();
        return ok ? 0 : 1;
    }
    std::fprintf (stderr, "usage: InfinitumTool test\n");
    return 2;
}
