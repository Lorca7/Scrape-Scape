#include "OfflineRenderer.h"

OfflineRenderer::OfflineRenderer() : juce::Thread ("Scrape-Scape Offline Render") {}

OfflineRenderer::~OfflineRenderer()
{
    stopThread (4000);
}

bool OfflineRenderer::start (const juce::File& dest, double seconds, double sampleRate,
                             const scape::EngineParams& params, const scape::ElementBank& elements)
{
    if (isThreadRunning())
        return false;

    destination = dest;
    lengthSeconds = juce::jlimit (1.0, 4.0 * 3600.0, seconds);
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    engineParams = params;
    bank.copyFrom (elements);
    progress = 0.0f;
    status = Status::rendering;
    startThread();
    return true;
}

void OfflineRenderer::cancel()
{
    signalThreadShouldExit();
}

void OfflineRenderer::acknowledge() noexcept
{
    if (status.load() != Status::rendering)
        status = Status::idle;
}

void OfflineRenderer::run()
{
    auto engine = std::make_unique<scape::ScapeEngine>();
    engine->setParams (engineParams);
    engine->prepare (rate);

    auto tempFile = destination.getSiblingFile (destination.getFileNameWithoutExtension() + ".rendering.wav");
    tempFile.deleteFile();

    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (auto stream = std::unique_ptr<juce::FileOutputStream> (tempFile.createOutputStream()))
    {
        juce::WavAudioFormat wav;
        if (auto* w = wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0))
        {
            stream.release();
            writer.reset (w);
        }
    }

    if (writer == nullptr)
    {
        status = Status::failed;
        return;
    }

    constexpr int block = 1024;
    juce::AudioBuffer<float> buffer (2, block);
    const auto total = (juce::int64) std::llround (lengthSeconds * rate);
    const auto fadeLength = (juce::int64) std::min ((double) total * 0.2, 6.0 * rate);
    const auto fadeStart = total - fadeLength;

    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        if (threadShouldExit())
        {
            writer.reset();
            tempFile.deleteFile();
            status = Status::cancelled;
            return;
        }

        const int n = (int) std::min<juce::int64> (block, total - pos);
        engine->process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, bank);

        // Gentle fade-out over the last few seconds so the file ends cleanly.
        if (pos + n > fadeStart)
            for (int i = 0; i < n; ++i)
            {
                const auto s = pos + i;
                if (s >= fadeStart)
                {
                    const float t = (float) (s - fadeStart) / (float) std::max<juce::int64> (1, fadeLength);
                    const float g = 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * t));
                    buffer.setSample (0, i, buffer.getSample (0, i) * g);
                    buffer.setSample (1, i, buffer.getSample (1, i) * g);
                }
            }

        writer->writeFromAudioSampleBuffer (buffer, 0, n);
        progress = (float) ((double) (pos + n) / (double) total);
    }

    writer.reset(); // flush and close

    destination.deleteFile();
    if (tempFile.moveFileTo (destination))
        status = Status::finished;
    else
        status = Status::failed;
}
