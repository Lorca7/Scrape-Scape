#pragma once

#include "Effects.h"
#include "FxPad.h"
#include "GranularFx.h"
#include "Modulation.h"
#include "ScapeEngine.h"

#include <array>
#include <atomic>

namespace scape
{

struct LfoParams
{
    int shape = 0;
    float rate = 0.2f;    // Hz
    float depth = 0.5f;   // 0..1 (of the target's full range, bipolar)
    int target = 0;       // ModTarget
};

struct ChainParams
{
    EngineParams engine;
    GrainParams grain;
    PadParams pad;
    std::array<LfoParams, kNumLfos> lfo;
};

// The full signal path used live and for offline rendering:
// generative engine -> granular -> XY effects pad -> safety limiter, with four LFOs
// modulating any of the targets.
class SoundChain
{
public:
    SoundChain();

    void prepare (double sampleRate);
    void process (float* L, float* R, int numSamples, ElementBank& bank, const ChainParams& params) noexcept;

    ScapeEngine& getEngine() noexcept { return engine; }
    const ScapeEngine& getEngine() const noexcept { return engine; }

    // For the UI: the modulated (normalised 0..1) value of a target, or -1 if no LFO is on it.
    float getModulatedValue (int target) const noexcept { return modulated[(size_t) target].load (std::memory_order_relaxed); }
    float getLfoValue (int index) const noexcept         { return lfoValues[(size_t) index].load (std::memory_order_relaxed); }

    static float toNorm (const ChainParams& p, int target) noexcept;
    static void setFromNorm (ChainParams& p, int target, float norm) noexcept;

private:
    static constexpr int kModBlock = 32;

    double sr = 48000.0;
    ScapeEngine engine;
    GranularFx granular;
    FxPad pad;
    SoftLimiter limiter;
    std::array<Lfo, kNumLfos> lfos;
    std::array<std::atomic<float>, kNumModTargets> modulated;
    std::array<std::atomic<float>, kNumLfos> lfoValues;
};

} // namespace scape
