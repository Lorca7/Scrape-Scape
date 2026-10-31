// Headless smoke test: drives the processor like a host would, checks that a
// recording and an offline render produce valid WAV files, that state survives a
// save/restore round trip, and writes a snapshot of the editor.

#include "PluginProcessor.h"

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
    check (renderer.start (renderFile, 30.0, sr, proc->getEngineParams(), proc->getElements()), "offline render starts");
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

    // ---- Editor snapshot -----------------------------------------------------
    {
        auto fresh = std::make_unique<ScrapeScapeProcessor>();
        fresh->prepareToPlay (sr, block);
        for (int i = 0; i < 200; ++i) fresh->processBlock (buffer, midi);  // let the elements glow
        fresh->startRecording();
        for (int i = 0; i < 400; ++i) fresh->processBlock (buffer, midi);

        std::unique_ptr<juce::AudioProcessorEditor> editor (fresh->createEditor());
        editor->setVisible (true);
        juce::Image img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto png = juce::File (outDir).getChildFile ("editor.png");
        png.deleteFile();
        juce::FileOutputStream os (png);
        juce::PNGImageFormat().writeImageToStream (img, os);
        check (png.getSize() > 0, "editor renders");
        editor.reset();
        fresh->stopRecording();
        fresh->getLastTake().deleteFile();
    }

    take.deleteFile();
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
