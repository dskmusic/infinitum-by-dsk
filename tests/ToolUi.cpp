// InfinitumTool commands that need the UI / generator: factory, gen-report, icon, screenshot.
#include "Tests.h"
#include "PluginEditor.h"
#include "gen/Generator.h"
#include "ui/GeneratorPage.h"
#include <BinaryData.h>

static juce::String factoryNotes (const gx::Preset& p)
{
    juce::String n ("Press Play and let it evolve.");
    if ((int) p.get (gx::P::tuning) == 1) n << " Tuned to A4 = 432 Hz.";
    if (p.meta.category == "Meditation") n << " Try the session timer for a meditation with a gentle fade-out.";
    else if (p.get (gx::P::evolution) > 0.6f) n << " Fast evolution: try Freeze when you hear a moment you love.";
    else n << " Turn Evolution and Light to move the landscape.";
    return n;
}

int runVideo (int argc, char** argv); // Video.cpp

int runToolExtras (const juce::String& cmd, int argc, char** argv)
{
    if (cmd == "video") return runVideo (argc, argv);
    if (cmd == "demos")
    {
        // InfinitumTool demos <dir> [minutes]: the first factory preset of every mood, rendered to OGG for listening
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "demos");
        const double mins = argc > 3 ? juce::String (argv[3]).getDoubleValue() : 3.0;
        dir.createDirectory();
        int size = 0;
        const char* data = BinaryData::getNamedResource ("factory_infinitumbank", size);
        const juce::var bank = data ? juce::JSON::parse (juce::String::fromUTF8 (data, size)) : juce::var();
        auto* arr = bank.getProperty ("presets", {}).getArray();
        if (arr == nullptr) { std::printf ("no factory bank\n"); return 1; }
        juce::StringArray picks (gx::categories());
        picks.addArray ({ "Neo-Noir", "Derelict" });
        for (auto& mood : picks)
            for (auto& v : *arr)
            {
                auto p = gx::Preset::fromVar (v);
                if (p.meta.category != mood && p.meta.subcategory != mood) continue;
                const auto f = dir.getChildFile (juce::File::createLegalFileName ("Infinitum demo - " + gx::categoryLabel (mood) + " - " + p.meta.name + ".ogg"));
                const auto err = gx::exportAudio (p, f, gx::efOgg, mins * 60.0, 48000.0);
                std::printf ("%s %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8());
                break;
            }
        return 0;
    }
    if (cmd == "factory")
    {
        // InfinitumTool factory <out.infinitumbank> [perSubMood]
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "resources/factory.infinitumbank");
        const int perSub = argc > 3 ? juce::String (argv[3]).getIntValue() : 5;
        juce::Array<juce::var> bank;
        juce::StringArray names;
        int total = 0, dups = 0;
        for (int mi = 0; mi < gx::categories().size(); ++mi)
        {
            const auto mood = gx::categories()[mi];
            int count = 0;
            for (int si = 0; si < gx::subcategories (mood).size(); ++si)
            {
                std::vector<gx::Preset> accepted;
                const auto subName = gx::subcategories (mood)[si];
                const int want = perSub + (subName == "Neo-Noir" || subName == "Derelict" ? 3 : 0);
                for (int guard = 0; (int) accepted.size() < want && guard < want * 8; ++guard)
                {
                    gx::GenRequest r;
                    r.category = mood;
                    r.subcategory = gx::subcategories (mood)[si];
                    r.seed = (uint32_t) (mi * 100003 + si * 10007 + guard * 7919 + 17);
                    gx::Rng rng (r.seed);
                    r.light = 0.2f + 0.6f * rng.uni();
                    r.movement = 0.2f + 0.6f * rng.uni();
                    r.density = 0.3f + 0.5f * rng.uni();
                    r.space = 0.4f + 0.5f * rng.uni();
                    r.mystery = rng.uni() < 0.7f ? 0.1f + 0.3f * rng.uni() : 0.5f + 0.4f * rng.uni();
                    gx::Validation v;
                    auto p = gx::Generator::generate (r, nullptr, &v);
                    if (! v.ok) continue;
                    bool dup = false;
                    for (auto& a : accepted) dup |= a.similarity (p) > 0.93f;
                    if (dup) { ++dups; continue; }
                    for (int t = 0; names.contains (p.meta.name) && t < 40; ++t) p.meta.name = gx::Generator::makeName (mood, rng, p.meta.subcategory);
                    if (names.contains (p.meta.name)) p.meta.name << " II";
                    names.add (p.meta.name);
                    p.meta.author = "DSK Music";
                    p.meta.factory = true;
                    p.meta.uuid = juce::Uuid().toString();
                    p.meta.created = p.meta.modified = juce::Time (2026, 10, 20, 12, 0);
                    p.meta.notes = factoryNotes (p);
                    accepted.push_back (p);
                }
                for (auto& p : accepted) bank.add (p.toVar());
                count += (int) accepted.size();
            }
            total += count;
            std::printf ("%-11s %d presets\n", mood.toRawUTF8(), count);
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", "infinitumbank");
        root->setProperty ("version", gx::kPresetFormatVersion);
        root->setProperty ("name", "Infinitum Factory");
        root->setProperty ("presets", bank);
        out.getParentDirectory().createDirectory();
        out.replaceWithText (juce::JSON::toString (juce::var (root), true));
        std::printf ("factory bank: %d presets (%d near-duplicates rejected) -> %s\n", total, dups, out.getFullPathName().toRawUTF8());
        return 0;
    }
    if (cmd == "gen-report")
    {
        // InfinitumTool gen-report [count]: attempts and levels per mood
        const int count = argc > 2 ? juce::String (argv[2]).getIntValue() : 6;
        for (auto& mood : gx::categories())
        {
            int att = 0, fails = 0;
            float rms = 0;
            for (int i = 0; i < count; ++i)
            {
                gx::GenRequest r; r.category = mood; r.seed = (uint32_t) (1000 + i * 31);
                juce::Random rnd (i);
                r.light = rnd.nextFloat(); r.movement = rnd.nextFloat(); r.density = rnd.nextFloat(); r.space = rnd.nextFloat(); r.mystery = rnd.nextFloat();
                int a = 0;
                gx::Validation v;
                gx::Generator::generate (r, &a, &v);
                att += a; fails += v.ok ? 0 : 1; rms += v.rmsDb;
            }
            std::printf ("%-11s attempts/preset %.2f  fails %d  mean rms %.1f dB\n", mood.toRawUTF8(), (double) att / count, fails, rms / (float) count);
        }
        return 0;
    }
    if (cmd == "icon")
    {
        // InfinitumTool icon <dir>  ->  infinitum-icon.png (1024 px, used by CMake ICON_BIG) + infinitum.ico (installer)
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "branding");
        dir.createDirectory();
        auto render = [] (int px, bool small = false)
        {
            juce::Image img (juce::Image::ARGB, px, px, true);
            {
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale ((float) px / 64.0f));
                const juce::Rectangle<float> r (2, 2, 60, 60);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1544), 0, 2, juce::Colour (0xff04050d), 0, 62, false));
                g.fillRoundedRectangle (r, 13.0f);
                // horizon glow
                if (! small) {
                g.setGradientFill (juce::ColourGradient (gx::ui::col::accent2.withAlpha (0.5f), 32, 50, gx::ui::col::accent2.withAlpha (0.0f), 32 + 24, 50, true));
                g.fillEllipse (8, 43, 48, 14);
                g.setColour (gx::ui::col::accent2.withAlpha (0.7f));
                g.fillRect (12.0f, 49.6f, 40.0f, 0.8f);
                }
                g.setColour (gx::ui::col::accent.withAlpha (0.35f));
                g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
                if (small) gx::ui::Logo::drawMark (g, juce::Rectangle<float> (3, 15, 58, 34), 0.0f, 0.15f); // bigger, bolder for taskbar sizes
                else gx::ui::Logo::drawMark (g, juce::Rectangle<float> (8, 16, 48, 26), 1.0f, 0.15f);
            }
            return img;
        };
        auto png = [] (const juce::Image& img) { juce::MemoryOutputStream m; juce::PNGImageFormat().writeImageToStream (img, m); return m.getMemoryBlock(); };
        { auto f = dir.getChildFile ("infinitum-icon.png"); const auto data = png (render (1024)); f.replaceWithData (data.getData(), data.getSize()); }
        { auto f = dir.getChildFile ("infinitum-icon-small.png"); const auto data = png (render (256, true)); f.replaceWithData (data.getData(), data.getSize()); }
        const int sizes[] { 16, 24, 32, 48, 64, 128, 256 };
        std::vector<juce::MemoryBlock> imgs;
        for (int s : sizes) imgs.push_back (png (render (s, s <= 48)));
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
        // InfinitumTool screenshot <outdir> [en|es]
        const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "docs/screenshots");
        out.createDirectory();
        gx::forcedLanguage() = argc > 3 ? argv[3] : "en";
        InfinitumProcessor p;
        p.prepareToPlay (48000, 512);
        // a factory-like sound, playing for a while so the landscape is alive
        {
            gx::GenRequest r; r.category = "Cinematic"; r.subcategory = "Epic"; r.seed = 4242; r.light = 0.55f;
            auto pr = gx::Generator::generate (r);
            pr.params[(size_t) gx::P::seed] = 2026;
            pr.applyTo (p);
        }
        p.setPlaying (true);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* ge = dynamic_cast<InfinitumEditor*> (ed.get());
        ed->setSize (InfinitumEditor::kDesignW, InfinitumEditor::kDesignH);
        ed->addToDesktop (0);
        ed->setVisible (true);
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer none;
        for (int k = 0; k < 1500; ++k) p.processBlock (buf, none); // ~16 s of sound
        auto pump = [&] (int frames)
        {
            for (int k = 0; k < frames; ++k)
            {
                for (int b = 0; b < 3; ++b) p.processBlock (buf, none);
                ge->advanceFrames (1.0 / 30.0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
            }
        };
        auto shot = [&] (const char* name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = out.getChildFile (juce::String (name) + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        };
        const char* names[] { "landscape", "sound", "generator", "presets", "export", "settings" };
        for (int pg = 0; pg < 6; ++pg)
        {
            ge->showPage (pg);
            if (pg == 0)
            {
                const char* sceneNames[] { "landscape-valley", "landscape-starfield", "landscape-nebula", "landscape-neon", "landscape-ocean", "landscape-aurora" };
                for (int sc = gx::numScenes; --sc >= 0;)
                {
                    p.setParamPlain (gx::P::scene, (float) sc);
                    pump (60);
                    shot (sceneNames[sc]);
                }
            }
            pump (pg == 0 ? 60 : 40);
            if (pg == 2)
            {
                ge->generatorPage().generate();
                pump (8);
                shot ("generator-working");
                for (int k = 0; k < 600 && ge->generatorPage().isBusy(); ++k) pump (1);
                pump (30);
            }
            shot (names[pg]);
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
    return -1;
}
