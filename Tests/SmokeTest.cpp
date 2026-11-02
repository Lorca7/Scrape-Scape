// Headless smoke test: drives the processor like a host would, checks that a
// recording and an offline render produce valid WAV files, that state survives a
// save/restore round trip, and writes a snapshot of the editor.

#include "PluginProcessor.h"
#include "UI/ImageOutliner.h"
#include "UI/SceneCanvas.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdio>

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

static double fileRms (const juce::File& f, double& seconds, int& bits, double& rate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return -1.0;

    seconds = (double) r->lengthInSamples / r->sampleRate;
    bits = (int) r->bitsPerSample;
    rate = r->sampleRate;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    return b.getRMSLevel (0, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto outDir = juce::File (argc > 1 ? argv[1] : ".").getFullPathName();

    auto proc = std::make_unique<ScrapeScapeProcessor>();
    const double sr = 48000.0;
    const int block = 512;
    proc->setPlayConfigDetails (0, 2, sr, block);
    proc->prepareToPlay (sr, block);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    // ---- Live recording --------------------------------------------------
    check (proc->startRecording(), "recording starts");
    for (int i = 0; i < (int) (8.0 * sr / block); ++i)
        proc->processBlock (buffer, midi);
    check (proc->isRecording(), "recorder reports recording");
    const double recorded = proc->getSecondsRecorded();
    proc->stopRecording();

    const auto take = proc->getLastTake();
    double secs = 0, rate = 0; int bits = 0;
    const double rms = fileRms (take, secs, bits, rate);
    std::printf ("      take: %s  %.2fs  %d-bit  %.0f Hz  rms %.4f (counter %.2fs)\n",
                 take.getFullPathName().toRawUTF8(), secs, bits, rate, rms, recorded);
    check (take.existsAsFile(), "take file exists");
    check (std::abs (secs - 8.0) < 0.05 && bits == 24 && rate == sr, "take is 8 s of 24-bit 48 kHz audio");
    check (rms > 0.005, "take contains sound");

    // Export = copy
    auto exported = juce::File (outDir).getChildFile ("exported-take.wav");
    exported.deleteFile();
    check (take.copyFileTo (exported) && exported.getSize() == take.getSize(), "take exports to a chosen path");

    // ---- Offline render ----------------------------------------------------
    auto renderFile = juce::File (outDir).getChildFile ("offline-render.wav");
    renderFile.deleteFile();
    auto& renderer = proc->getRenderer();
    check (renderer.start (renderFile, 30.0, sr, proc->getChainParams(), proc->getElements()), "offline render starts");
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (renderer.getStatus() == OfflineRenderer::Status::rendering)
        juce::Thread::sleep (20);
    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    const double rrms = fileRms (renderFile, secs, bits, rate);
    std::printf ("      render: %.2fs of audio in %.2fs  rms %.4f\n", secs, elapsed, rrms);
    check (renderer.getStatus() == OfflineRenderer::Status::finished, "offline render finishes");
    check (std::abs (secs - 30.0) < 0.05 && bits == 24 && rrms > 0.005, "render is 30 s of 24-bit audio with sound");

    // ---- Re-preparing (hosts do this often) must keep the scene playing ----------
    proc->prepareToPlay (44100.0, 256);
    proc->prepareToPlay (sr, block);
    {
        double energy = 0.0;
        for (int i = 0; i < (int) (4.0 * sr / block); ++i)
        {
            proc->processBlock (buffer, midi);
            energy += buffer.getRMSLevel (0, 0, block);
        }
        check (energy / (4.0 * sr / block) > 0.005, "audio continues after prepareToPlay is called again");
    }

    // ---- Play / Stop transport ---------------------------------------------------
    {
        auto blockRms = [&] { proc->processBlock (buffer, midi); return buffer.getRMSLevel (0, 0, block); };
        proc->setPlaying (false);
        for (int i = 0; i < (int) (2.0 * sr / block); ++i) blockRms();   // fade out (1.5 s)
        float stoppedLevel = 0.0f;
        for (int i = 0; i < 20; ++i) stoppedLevel = juce::jmax (stoppedLevel, blockRms());
        check (stoppedLevel == 0.0f && proc->isSilent(), "Stop fades out to silence and pauses the engine");

        proc->setPlaying (true);
        double energy = 0.0;
        const int blocks = (int) (3.0 * sr / block);
        for (int i = 0; i < blocks; ++i) energy += blockRms();
        check (! proc->isSilent() && energy / blocks > 0.005, "Play resumes the music");
    }

    // ---- State round trip --------------------------------------------------
    proc->getElements().clear();
    proc->getElements().add (scape::ElementType::Pluck, 0.25f, 0.75f, 1.5f);
    proc->getElements().add (scape::ElementType::Spark, 0.4f, 0.6f, 0.8f);
    proc->getParameters().getParameter ("mood")->setValueNotifyingHost (1.0f);
    juce::MemoryBlock state;
    proc->getStateInformation (state);

    auto proc2 = std::make_unique<ScrapeScapeProcessor>();
    proc2->setStateInformation (state.getData(), (int) state.getSize());
    const auto e0 = proc2->getElements().get (0);
    check (proc2->getElements().countActive() == 2 && e0.type == (int) scape::ElementType::Pluck
               && std::abs (e0.x - 0.25f) < 1e-4f && std::abs (e0.size - 1.5f) < 1e-4f,
           "elements restore from saved state");
    check (proc2->getEngineParams().mood == scape::kNumMoods - 1, "parameters restore from saved state");

    // ---- Effects pad, granular, LFOs -------------------------------------------
    {
        auto setP = [&] (ScrapeScapeProcessor& pr, const char* id, float v)
        {
            auto* param = pr.getParameters().getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (v));
        };
        auto fx = std::make_unique<ScrapeScapeProcessor>();
        fx->prepareToPlay (sr, block);

        auto runRms = [&] (double seconds)
        {
            double e = 0.0; int nb = (int) (seconds * sr / block); bool finite = true;
            for (int i = 0; i < nb; ++i)
            {
                fx->processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < block; ++k)
                        finite = finite && std::isfinite (buffer.getSample (ch, k));
                e += buffer.getRMSLevel (0, 0, block);
            }
            return finite ? e / nb : -1.0;
        };

        const double dry = runRms (3.0);
        bool allOk = dry > 0.005;
        for (int t = 0; t < scape::kNumPadEffects; ++t)
        {
            setP (*fx, "fx_type", (float) t);
            setP (*fx, "fx_mix", 1.0f);
            setP (*fx, "fx_x", 0.8f);
            setP (*fx, "fx_y", 0.3f);
            const double wet = runRms (2.0);
            std::printf ("      %-9s rms %.4f\n", scape::getPadEffectInfo (t).name, wet);
            allOk = allOk && wet > 0.0005 && wet < 0.9;
        }
        check (allOk, "every XY pad effect produces finite, sensible audio");

        setP (*fx, "fx_mix", 0.0f);
        setP (*fx, "grain_mix", 1.0f);
        setP (*fx, "grain_pitch", 12.0f);
        const double grains = runRms (3.0);
        check (grains > 0.003 && grains < 0.9, "granular effect produces audio");

        fx->setLfoTarget (0, (int) scape::ModTarget::PadX);
        setP (*fx, "lfo1_rate", 5.0f);
        setP (*fx, "lfo1_depth", 1.0f);
        float lo = 2.0f, hi = -1.0f;
        for (int i = 0; i < 100; ++i)
        {
            fx->processBlock (buffer, midi);
            const float m = fx->getModulatedValue ((int) scape::ModTarget::PadX);
            lo = juce::jmin (lo, m); hi = juce::jmax (hi, m);
        }
        check (fx->getLfoTarget (0) == (int) scape::ModTarget::PadX && lo >= 0.0f && hi - lo > 0.5f,
               "LFO assigned to Pad X sweeps it");
        check (fx->getModulatedValue ((int) scape::ModTarget::Reverb) < 0.0f, "unassigned targets report no modulation");

        // Picture scale + effects settings survive presets.
        fx->getElements().setScale (0, 2.5f);
        auto pf = juce::File (outDir).getChildFile ("fx.scapepreset");
        fx->savePreset (pf);
        auto fx2 = std::make_unique<ScrapeScapeProcessor>();
        fx2->loadPreset (pf);
        const auto c2 = fx2->getChainParams();
        check (std::abs (fx2->getElements().get (0).scale - 2.5f) < 1e-4f && c2.pad.type == scape::kNumPadEffects - 1
                   && c2.grain.mix > 0.99f && c2.lfo[0].target == (int) scape::ModTarget::PadX && c2.lfo[0].rate > 4.9f,
               "presets keep picture size, XY pad, granular and LFO settings");
    }

    // ---- Per-engine sound controls ---------------------------------------------
    {
        auto measure = [&] (std::function<void (ScrapeScapeProcessor&)> setup)
        {
            auto pr = std::make_unique<ScrapeScapeProcessor>();
            pr->getElements().clear();
            pr->getElements().add (scape::ElementType::Bowl, 0.5f, 0.4f, 1.2f);
            pr->getElements().add (scape::ElementType::Drone, 0.3f, 0.6f, 1.0f);
            setup (*pr);
            pr->prepareToPlay (sr, block);
            double e = 0.0; const int nb = (int) (8.0 * sr / block);
            for (int i = 0; i < nb; ++i) { pr->processBlock (buffer, midi); e += buffer.getRMSLevel (0, 0, block); }
            return e / nb;
        };
        auto setP = [] (ScrapeScapeProcessor& pr, const juce::String& id, float v)
        { auto* param = pr.getParameters().getParameter (id); param->setValueNotifyingHost (param->convertTo0to1 (v)); };

        const double normal = measure ([] (ScrapeScapeProcessor&) {});
        const double muted = measure ([&] (ScrapeScapeProcessor& pr)
                                      { setP (pr, "bowl_level", 0.0f); setP (pr, "drone_level", 0.0f); });
        const double loud = measure ([&] (ScrapeScapeProcessor& pr)
                                     { setP (pr, "drone_level", 1.0f); setP (pr, "drone_sub", 1.0f); });
        std::printf ("      engine levels: normal %.4f  muted %.6f  louder %.4f\n", normal, muted, loud);
        check (muted < normal * 0.05 && loud > normal * 1.2, "per-engine Level controls change the sound");

        const double dark = measure ([&] (ScrapeScapeProcessor& pr)
                                     { setP (pr, "bowl_cutoff", 0.1f); setP (pr, "drone_cutoff", 0.1f); });
        const double resonant = measure ([&] (ScrapeScapeProcessor& pr)
                                         { setP (pr, "bowl_resonance", 1.0f); setP (pr, "drone_resonance", 1.0f); });
        std::printf ("      cutoff 0.1: %.4f  resonance 1.0: %.4f  (default %.4f)\n", dark, resonant, normal);
        check (dark < normal * 0.9 && resonant > 0.0 && std::isfinite (resonant), "Cutoff darkens and Resonance stays stable");

        auto pr = std::make_unique<ScrapeScapeProcessor>();
        pr->prepareToPlay (sr, block);
        const int target = scape::voiceTarget ((int) scape::ElementType::Bowl, 6);   // Bowl "Ring"
        pr->setLfoTarget (2, target);
        setP (*pr, "lfo3_rate", 4.0f);
        setP (*pr, "lfo3_depth", 0.8f);
        float lo = 2.0f, hi = -1.0f;
        for (int i = 0; i < 100; ++i)
        {
            pr->processBlock (buffer, midi);
            const float m = pr->getModulatedValue (target);
            lo = juce::jmin (lo, m); hi = juce::jmax (hi, m);
        }
        check (ScrapeScapeProcessor::getTargetParamID (target) == "bowl_ring" && lo >= 0.0f && hi - lo > 0.4f,
               "LFO on an engine control (Bowl Ring) modulates it");
        check (juce::String (scape::getModTargetName (target)) == "Bowl Ring", "engine targets have readable names");
    }

    // ---- Custom images --------------------------------------------------------
    // Make a test "photo": a sunset gradient with a bright disc in the middle.
    auto photoFile = juce::File (outDir).getChildFile ("test-photo.png");
    {
        juce::Image photo (juce::Image::RGB, 800, 600, true);
        juce::Graphics g (photo);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff203a6b), 0, 0, juce::Colour (0xfff2a65a), 0, 600, false));
        g.fillAll();
        g.setColour (juce::Colour (0xfffff1c1));
        g.fillEllipse (300, 200, 200, 200);
        g.setColour (juce::Colour (0xff1d6b4a));
        g.fillRect (0, 480, 800, 120);
        photoFile.deleteFile();
        juce::FileOutputStream os (photoFile);
        juce::PNGImageFormat().writeImageToStream (photo, os);
    }

    check (SkinTools::isSupportedImageFile (photoFile), "PNG is accepted for drag & drop");
    auto loaded = SkinTools::prepareSource (SkinTools::loadAnyImage (photoFile));
    check (loaded.isValid() && loaded.getWidth() == 800, "image file loads");

    ElementSkin skin;
    skin.source = loaded;
    skin.shape = ElementSkin::freehand;   // a lasso roughly around the disc
    for (int i = 0; i < 40; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 40.0f;
        skin.outline.push_back ({ 0.5f + 0.15f * std::cos (a), 0.5f + 0.2f * std::sin (a) });
    }
    auto glyph = SkinTools::renderGlyph (skin);
    check (glyph.isValid() && glyph.getPixelAt (0, 0).getAlpha() == 0
               && glyph.getPixelAt (glyph.getWidth() / 2, glyph.getHeight() / 2).getAlpha() == 255,
           "outline cuts out the selected part (transparent outside)");

    auto& skinBank = proc->getSkins();
    skinBank.set ((int) scape::ElementType::Drone, skin);
    ElementSkin ell; ell.source = loaded; ell.shape = ElementSkin::ellipse;
    ell.outline = { { 0.05f, 0.75f }, { 0.45f, 0.98f } };
    skinBank.set ((int) scape::ElementType::Bowl, ell);

    {
        juce::MemoryBlock st;
        proc->getStateInformation (st);
        auto p3 = std::make_unique<ScrapeScapeProcessor>();
        p3->setStateInformation (st.getData(), (int) st.getSize());
        const auto restored = p3->getSkins().get ((int) scape::ElementType::Drone);
        check (restored.isValid() && restored.shape == ElementSkin::freehand && restored.outline.size() == 40
                   && p3->getSkins().hasSkin ((int) scape::ElementType::Bowl)
                   && ! p3->getSkins().hasSkin ((int) scape::ElementType::Pluck),
               "custom images + outlines survive save/restore");
    }

    // ---- Lossless picture sizing ---------------------------------------------------
    {
        // A 4000 px photo with an 8 px checkerboard: any blur would show up as grey.
        auto bigFile = juce::File (outDir).getChildFile ("big-checker.png");
        {
            juce::Image big (juce::Image::RGB, 4000, 4000, true);
            juce::Image::BitmapData bd (big, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < 4000; ++y)
                for (int x = 0; x < 4000; ++x)
                    bd.setPixelColour (x, y, (((x / 8) + (y / 8)) & 1) ? juce::Colours::white : juce::Colours::black);
            bigFile.deleteFile();
            juce::FileOutputStream os (bigFile);
            juce::PNGImageFormat().writeImageToStream (big, os);
        }

        ElementSkin bigSkin;
        bigSkin.source = SkinTools::prepareSource (SkinTools::loadAnyImage (bigFile));
        bigSkin.shape = ElementSkin::wholeImage;
        bigSkin.encodedSource = SkinTools::encodeOriginalFile (bigFile);
        check (bigSkin.source.getWidth() == 4000, "full-resolution photo is kept (4000 px)");

        auto huge = SkinTools::renderGlyphAtSize (bigSkin, 4000);
        bool exact = huge.getWidth() == 4000;
        for (int k = 0; k < 200 && exact; ++k)
        {
            const int x = 3 + (k * 37) % 3990, y = 5 + (k * 53) % 3990;
            const bool white = (((x / 8) + (y / 8)) & 1) != 0;
            const auto px = huge.getPixelAt (x, y);
            exact = white ? px.getBrightness() > 0.95f : px.getBrightness() < 0.05f;
        }
        check (exact, "picture rendered at 4000 px is pixel-sharp (no blur)");

        // Direct drawing for giant on-screen sizes: a 600 px window into the picture drawn at
        // full photo resolution (4000 px), and at 3x beyond it (12000 px) for information.
        auto crispness = [&] (float drawnSize)
        {
            juce::Image view (juce::Image::ARGB, 600, 600, true);
            {
                juce::Graphics g (view);
                const float off = -(drawnSize - 600.0f) * 0.5f;
                SkinTools::drawDirect (g, bigSkin, juce::Rectangle<float> (off, off, drawnSize, drawnSize), 1.0f);
            }
            int crisp = 0, total = 0;
            for (int y = 0; y < 600; y += 7)
                for (int x = 0; x < 600; x += 7)
                {
                    const float b = view.getPixelAt (x, y).getBrightness();
                    crisp += (b < 0.15f || b > 0.85f) ? 1 : 0;
                    ++total;
                }
            return 100 * crisp / total;
        };
        const int atFull = crispness (4000.0f), beyond = crispness (12000.0f);
        std::printf ("      giant direct draw: %d%% crisp at photo resolution, %d%% at 3x beyond it (smoothed)\n", atFull, beyond);
        check (atFull >= 99, "giant pictures are drawn sharply straight from the photo");

        auto pr = std::make_unique<ScrapeScapeProcessor>();
        pr->getSkins().set ((int) scape::ElementType::Bass, bigSkin);
        pr->getElements().setScale (1, 60.0f);
        auto pf = juce::File (outDir).getChildFile ("big.scapepreset");
        pr->savePreset (pf);
        auto pr2 = std::make_unique<ScrapeScapeProcessor>();
        pr2->loadPreset (pf);
        check (pr2->getSkins().get ((int) scape::ElementType::Bass).source.getWidth() == 4000
                   && std::abs (pr2->getElements().get (1).scale - 60.0f) < 1e-3f,
               "presets keep the full-resolution photo and huge picture sizes");
        std::printf ("      preset with 4000 px photo: %.0f KB\n", (double) pf.getSize() / 1024.0);
    }

    // ---- Presets -----------------------------------------------------------------
    {
        auto presetFile = juce::File (outDir).getChildFile ("Test Scene.scapepreset");
        presetFile.deleteFile();
        check (proc->savePreset (presetFile) && presetFile.existsAsFile(), "preset saves to a file");
        std::printf ("      preset size: %.1f KB\n", (double) presetFile.getSize() / 1024.0);

        auto p4 = std::make_unique<ScrapeScapeProcessor>();
        check (p4->loadPreset (presetFile), "preset loads");
        check (p4->getElements().countActive() == proc->getElements().countActive()
                   && p4->getEngineParams().mood == proc->getEngineParams().mood
                   && p4->getSkins().hasSkin ((int) scape::ElementType::Drone)
                   && p4->getCurrentPresetName() == "Test Scene",
               "preset restores scene, knobs, mood and images");

        const int before = p4->getSkins().getVersion();
        p4->loadFactoryPreset (3);
        check (p4->getCurrentPresetName() == "Glass Garden" && p4->getElements().countActive() == 6
                   && p4->getSkins().hasSkin ((int) scape::ElementType::Drone) && p4->getSkins().getVersion() == before,
               "factory scene loads and keeps custom images");

        juce::File bogus = juce::File (outDir).getChildFile ("bogus.scapepreset");
        bogus.replaceWithText ("not a preset");
        check (! p4->loadPreset (bogus), "invalid preset file is rejected");
    }

    // ---- Editor snapshot -----------------------------------------------------
    {
        auto fresh = std::make_unique<ScrapeScapeProcessor>();
        fresh->prepareToPlay (sr, block);
        for (int i = 0; i < 200; ++i) fresh->processBlock (buffer, midi);  // let the elements glow
        fresh->startRecording();
        for (int i = 0; i < 400; ++i) fresh->processBlock (buffer, midi);

        auto snapshot = [&] (juce::Component& c, const char* name)
        {
            juce::Image img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            auto png = juce::File (outDir).getChildFile (name);
            png.deleteFile();
            juce::FileOutputStream os (png);
            juce::PNGImageFormat().writeImageToStream (img, os);
            return png.getSize() > 0;
        };

        std::unique_ptr<juce::AudioProcessorEditor> editor (fresh->createEditor());
        editor->setVisible (true);
        check (snapshot (*editor, "editor.png"), "editor renders");

        // With custom images on two elements, and the outliner open.
        fresh->getSkins().set ((int) scape::ElementType::Drone, skin);
        fresh->getSkins().set ((int) scape::ElementType::Bowl, ell);
        editor->resized();
        check (snapshot (*editor, "editor-images.png"), "editor renders custom images");

        for (auto* child : editor->getChildren())
            if (auto* o = dynamic_cast<ImageOutliner*> (child))
            {
                juce::ignoreUnused (o);
            }

        // Enlarged picture (crisp cache) + LFOs assigned + XY pad mode.
        fresh->getElements().setScale (0, 2.2f);   // the drone in the default scene
        fresh->setLfoTarget (0, (int) scape::ModTarget::PadX);
        fresh->setLfoTarget (1, (int) scape::ModTarget::GrainSize);
        fresh->setLfoTarget (2, (int) scape::ModTarget::Reverb);
        auto* fxType = fresh->getParameters().getParameter ("fx_type");
        fxType->setValueNotifyingHost (fxType->convertTo0to1 (2.0f));
        fresh->getParameters().getParameter ("fx_mix")->setValueNotifyingHost (0.7f);
        for (int i = 0; i < 40; ++i) fresh->processBlock (buffer, midi);
        for (auto* child : editor->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                if (tb->getButtonText().startsWith ("XY Pad")) { tb->setToggleState (true, juce::dontSendNotification); }
        check (snapshot (*editor, "editor-pad.png"), "editor renders XY pad mode");
        for (auto* child : editor->getChildren())
            if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                if (tb->getButtonText().startsWith ("XY Pad")) { tb->setToggleState (false, juce::dontSendNotification); }
        check (snapshot (*editor, "editor-lfo.png"), "editor renders enlarged picture + LFO markers");

        // Let the visuals evolve for ~12 seconds of sound with more elements interacting.
        fresh->getElements().add (scape::ElementType::Pluck, 0.55f, 0.45f, 1.2f);
        fresh->getElements().add (scape::ElementType::Chimes, 0.75f, 0.65f, 1.0f);
        fresh->getElements().add (scape::ElementType::Pad, 0.40f, 0.30f, 1.0f);
        fresh->getElements().add (scape::ElementType::Spark, 0.62f, 0.40f, 0.8f);
        SceneCanvas* canvas = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* c = dynamic_cast<SceneCanvas*> (child)) canvas = c;
        int maxParticles = 0;
        if (canvas != nullptr)
            for (int frame = 0; frame < 360; ++frame)
            {
                for (int i = 0; i < (int) (sr / 30.0 / block) + 1; ++i) fresh->processBlock (buffer, midi);
                canvas->advanceAnimation (1.0f / 30.0f);
                maxParticles = juce::jmax (maxParticles, canvas->getVisualParticleCount());
            }
        std::printf ("      visual particles: %d\n", maxParticles);
        check (maxParticles > 50, "visual field emits particles from playing elements");
        check (snapshot (*editor, "editor-visuals.png"), "editor renders the visual field");

        // Backdrop moods (+ a giant photo element drawn directly from the source).
        if (canvas != nullptr)
        {
            fresh->getElements().setScale (0, 9.0f);
            auto* moodParam = fresh->getParameters().getParameter ("mood");
            for (int m : { 3, 5, 1 })
            {
                moodParam->setValueNotifyingHost (moodParam->convertTo0to1 ((float) m));
                for (int frame = 0; frame < 120; ++frame)
                {
                    for (int i = 0; i < (int) (sr / 30.0 / block) + 1; ++i) fresh->processBlock (buffer, midi);
                    canvas->advanceAnimation (1.0f / 30.0f);
                }
                const juce::String name = "backdrop-" + juce::String (scape::getMood (m).name).toLowerCase() + ".png";
                check (snapshot (*editor, name.toRawUTF8()), ("backdrop renders for " + juce::String (scape::getMood (m).name)).toRawUTF8());
            }
        }

        for (auto* child : editor->getChildren())
            if (auto* o = dynamic_cast<ImageOutliner*> (child))
            {
                o->open ((int) scape::ElementType::Drone, skin);
                check (snapshot (*editor, "outliner.png"), "outliner renders");
                o->setVisible (false);
            }
        editor.reset();
        fresh->stopRecording();
        fresh->getLastTake().deleteFile();
    }

    take.deleteFile();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
