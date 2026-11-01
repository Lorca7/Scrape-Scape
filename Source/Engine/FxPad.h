#pragma once

#include "Dsp.h"
#include "Effects.h"

#include <array>

namespace scape
{

enum class PadEffect : int { Filter = 0, Wash, DubEcho, Warble, Crush, Stutter, Phaser, RingMod, NumEffects };
constexpr int kNumPadEffects = (int) PadEffect::NumEffects;

struct PadEffectInfo { const char* name; const char* xLabel; const char* yLabel; };

inline const PadEffectInfo& getPadEffectInfo (int e) noexcept
{
    static const PadEffectInfo infos[kNumPadEffects] = {
        { "Filter",   "Low-pass  \xe2\x86\x90  \xc2\xb7  \xe2\x86\x92  High-pass", "Resonance" },
        { "Wash",     "Size  (far right = freeze)",                                 "Brightness" },
        { "Dub Echo", "Delay time",                                                 "Feedback" },
        { "Warble",   "Depth",                                                      "Speed" },
        { "Crush",    "Sample-rate reduction",                                      "Bit reduction" },
        { "Stutter",  "Slice length",                                               "Repeats" },
        { "Phaser",   "Sweep speed",                                                "Feedback" },
        { "Ring Mod", "Frequency",                                                  "Ring \xe2\x86\x94 tremolo" },
    };
    return infos[e < 0 || e >= kNumPadEffects ? 0 : e];
}

struct PadParams
{
    int type = 0;
    float x = 0.5f, y = 0.5f;   // 0..1, y = 0 at the TOP of the pad
    float mix = 0.0f;           // dry/wet
};

// Kaoss-style effects matrix: one effect at a time, two continuous axes + dry/wet.
class FxPad
{
public:
    void prepare (float sampleRate);
    void reset();
    void process (float* L, float* R, int n, const PadParams& p, float bpm) noexcept;

private:
    void resetEffect (int type);
    void processSample (int type, float x, float y, float& l, float& r) noexcept;

    float sr = 48000.0f;
    int current = 0, pending = 0;
    float switchGain = 1.0f;
    dsp::OnePole xs, ys, mixs;
    int counter = 0;
    float bpmNow = 60.0f;

    // Filter
    dsp::Svf fl, fr;
    // Wash
    FdnReverb wash;
    float washDecay = -1.0f;
    dsp::OnePole washHpL, washHpR;
    // Dub echo
    dsp::DelayLine dubL, dubR;
    dsp::OnePole dubTime, dubLpL, dubLpR, dubHpL, dubHpR;
    // Warble
    dsp::DelayLine warL, warR;
    float warPhase = 0.0f, flutterPhase = 0.0f;
    dsp::OnePole warLpL, warLpR;
    // Crush
    float holdL = 0, holdR = 0, holdCount = 0;
    // Stutter
    dsp::DelayLine stL, stR;
    long long stWritten = 0;
    int stSliceLen = 1, stPlayPos = 0, stRepeat = 0, stRepeats = 1;
    long long stCaptureAt = 0;
    bool stHaveSlice = false;
    // Phaser
    std::array<float, 6> apL {}, apR {};
    float phaseLfo = 0.0f, phaserCoeff = 0.0f, phFbL = 0, phFbR = 0;
    // Ring mod
    float ringPhaseL = 0, ringPhaseR = 0;
    dsp::OnePole ringFreq;
};

} // namespace scape
