#pragma once

#include "Dsp.h"
#include "Elements.h"
#include "VoiceParams.h"

#include <string>

#include <cmath>

namespace scape
{

// Everything an LFO can be dragged onto.
enum class ModTarget : int
{
    None = 0,
    PadX, PadY, FxMix,
    GrainMix, GrainSize, GrainDensity, GrainPitch, GrainSpray,
    Reverb, Echo, Brightness, Activity,
    FirstVoiceTarget   // followed by kNumSoundTypes * kVoiceParams per-engine targets
};

constexpr int kFirstVoiceTarget = (int) ModTarget::FirstVoiceTarget;
constexpr int kNumModTargets = kFirstVoiceTarget + kNumSoundTypes * kVoiceParams;

inline int voiceTarget (int type, int index) noexcept { return kFirstVoiceTarget + type * kVoiceParams + index; }
inline bool isVoiceTarget (int t) noexcept            { return t >= kFirstVoiceTarget && t < kNumModTargets; }
constexpr int kNumLfos = 4;

inline const char* getModTargetName (int t) noexcept
{
    static const auto names = []
    {
        std::array<std::string, (size_t) kNumModTargets> n;
        const char* fixed[] = { "None", "Pad X", "Pad Y", "FX Dry/Wet",
                                "Grain Dry/Wet", "Grain Size", "Grain Density", "Grain Pitch", "Grain Spray",
                                "Reverb", "Echo", "Brightness", "Activity" };
        for (int i = 0; i < kFirstVoiceTarget; ++i) n[(size_t) i] = fixed[i];
        for (int type = 0; type < kNumSoundTypes; ++type)
            for (int k = 0; k < kVoiceParams; ++k)
                n[(size_t) voiceTarget (type, k)] = std::string (getElementInfo (type).name) + " " + getVoiceParamSpec (type, k).name;
        return n;
    }();
    return names[(size_t) (t < 0 || t >= kNumModTargets ? 0 : t)].c_str();
}

enum class LfoShape : int { Sine = 0, Triangle, Saw, Square, SampleHold, Drift, NumShapes };
constexpr int kNumLfoShapes = (int) LfoShape::NumShapes;

inline const char* getLfoShapeName (int s) noexcept
{
    static const char* names[kNumLfoShapes] = { "Sine", "Triangle", "Saw", "Square", "Random", "Drift" };
    return names[s < 0 || s >= kNumLfoShapes ? 0 : s];
}

// Shared value <-> normalised mappings (also used for the plugin's parameter ranges).
namespace ParamMaps
{
    inline float grainSizeFromNorm (float n) noexcept    { return 0.02f * std::pow (25.0f, n); }      // 20 ms .. 500 ms
    inline float grainSizeToNorm (float v) noexcept      { return std::log (std::max (v, 0.02f) / 0.02f) / std::log (25.0f); }
    inline float grainDensityFromNorm (float n) noexcept { return std::pow (60.0f, n); }               // 1 .. 60 grains/s
    inline float grainDensityToNorm (float v) noexcept   { return std::log (std::max (v, 1.0f)) / std::log (60.0f); }
    inline float lfoRateFromNorm (float n) noexcept      { return 0.01f * std::pow (1000.0f, n); }     // 0.01 .. 10 Hz
    inline float lfoRateToNorm (float v) noexcept        { return std::log (std::max (v, 0.01f) / 0.01f) / std::log (1000.0f); }
}

// Low-frequency oscillator producing -1..1.
class Lfo
{
public:
    void reset (std::uint32_t seed) noexcept
    {
        rng.seed (seed);
        phase = 0.0;
        held = rng.bipolar();
        from = rng.bipolar();
        to = rng.bipolar();
    }

    // Returns the current value, then advances by numSamples.
    float advance (int shape, float rateHz, double sampleRate, int numSamples) noexcept
    {
        const float p = (float) phase;
        float v = 0.0f;

        switch ((LfoShape) shape)
        {
            case LfoShape::Sine:       v = std::sin (dsp::twoPi * p); break;
            case LfoShape::Triangle:   v = 4.0f * std::abs (p - 0.5f) - 1.0f; break;
            case LfoShape::Saw:        v = 2.0f * p - 1.0f; break;
            case LfoShape::Square:     v = p < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::SampleHold: v = held; break;
            case LfoShape::Drift:
            {
                const float t = 0.5f - 0.5f * std::cos (dsp::pi * p);
                v = from + (to - from) * t;
                break;
            }
            case LfoShape::NumShapes: break;
        }

        phase += (double) rateHz * (double) numSamples / sampleRate;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = rng.bipolar();
            from = to;
            to = rng.bipolar();
        }
        return v;
    }

private:
    dsp::Rng rng;
    double phase = 0.0;
    float held = 0.0f, from = 0.0f, to = 0.0f;
};

} // namespace scape
