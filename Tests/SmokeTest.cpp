// Headless smoke test: drives the processor like a host would, checks that a
// recording and an offline render produce valid WAV files, that state survives a
// save/restore round trip, and writes a snapshot of the editor.

#include "PluginProcessor.h"
#include "UI/ImageOutliner.h"

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
    skinBank.set ((int) scape::ElementType::Bell, ell);

    {
        juce::MemoryBlock st;
        proc->getStateInformation (st);
        auto p3 = std::make_unique<ScrapeScapeProcessor>();
        p3->setStateInformation (st.getData(), (int) st.getSize());
        const auto restored = p3->getSkins().get ((int) scape::ElementType::Drone);
        check (restored.isValid() && restored.shape == ElementSkin::freehand && restored.outline.size() == 40
                   && p3->getSkins().hasSkin ((int) scape::ElementType::Bell)
                   && ! p3->getSkins().hasSkin ((int) scape::ElementType::Pluck),
               "custom images + outlines survive save/restore");
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
        fresh->getSkins().set ((int) scape::ElementType::Bell, ell);
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
