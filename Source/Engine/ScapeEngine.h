#pragma once

#include "Effects.h"
#include "Elements.h"
#include "Moods.h"
#include "Voice.h"

#include <array>
#include <atomic>
#include <vector>

namespace scape
{

struct EngineParams
{
    int mood = 0;
    float activity = 0.5f;     // 0..1
    float brightness = 0.6f;   // 0..1
    float tempo = 64.0f;       // BPM before the mood's pace multiplier
    float reverb = 0.5f;       // 0..1
    float delay = 0.3f;        // 0..1
    float volumeDb = -3.0f;
};

// The whole generative instrument: a conductor (clock + chord progression), one voice
// per canvas slot, and the shared effects. Renders deterministic audio for a given
// element layout and seed, so it can run both live and offline.
class ScapeEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const EngineParams& p) noexcept { params = p; }
    void process (float* outL, float* outR, int numSamples, ElementBank& bank) noexcept;

    // For display: scale degree of the current chord root and current mood.
    int getChordRoot() const noexcept { return displayChordRoot.load(); }

private:
    static constexpr int kChunk = 256;

    void processChunk (float* outL, float* outR, int n, ElementBank& bank) noexcept;
    void syncVoices (ElementBank& bank) noexcept;
    void tickConductor() noexcept;
    int chooseNextChord() noexcept;

    float sr = 44100.0f;
    EngineParams params;
    std::array<Voice, kMaxElements> voices;
    std::array<ElementSnapshot, kMaxElements> snapshots {};

    std::vector<float> dryL, dryR, revL, revR, dlyL, dlyR, wetL, wetR, echoL, echoR;

    FdnReverb reverb;
    PingPongDelay delay;
    SoftLimiter limiter;
    dsp::OnePole reverbSmooth, delaySmooth, volumeSmooth;

    VoiceContext ctx;
    dsp::Rng rng;
    double samplesToNextTick = 0.0;
    int ticksUntilChordChange = 64;
    bool prepared = false, forceChordChange = false;
    std::atomic<int> displayChordRoot { 0 };
};

} // namespace scape
