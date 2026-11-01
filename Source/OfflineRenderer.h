#pragma once

#include "Engine/SoundChain.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

// Renders a scene to a WAV file faster than realtime on a background thread,
// using its own engine instance and a snapshot of the current elements.
class OfflineRenderer : private juce::Thread
{
public:
    enum class Status { idle, rendering, finished, failed, cancelled };

    OfflineRenderer();
    ~OfflineRenderer() override;

    bool start (const juce::File& destination, double seconds, double sampleRate,
                const scape::ChainParams& params, const scape::ElementBank& elements);
    void cancel();

    Status getStatus() const noexcept   { return status.load(); }
    float getProgress() const noexcept  { return progress.load(); }
    juce::File getDestination() const   { return destination; }

    // Resets a finished/failed status back to idle once the UI has reported it.
    void acknowledge() noexcept;

private:
    void run() override;

    juce::File destination;
    double lengthSeconds = 60.0, rate = 48000.0;
    scape::ChainParams chainParams;
    scape::ElementBank bank;
    std::atomic<Status> status { Status::idle };
    std::atomic<float> progress { 0.0f };
};
