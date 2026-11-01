#pragma once

#include <array>
#include <cmath>

namespace scape
{

// Per-engine sound controls. Every sound type has 8 controls, all stored as 0..1:
// four shared (Level, Tone, Space, Echo) and four specific to that engine.
constexpr int kNumSoundTypes = 7;      // Drone .. Pulse (modifiers have no sound)
constexpr int kVoiceParams = 8;

enum class VoiceParamDisplay { percent, octave, ratio, hits };

struct VoiceParamSpec
{
    const char* id;
    const char* name;
    float defaultValue;
    VoiceParamDisplay display;
    const char* help;
};

inline const VoiceParamSpec& getVoiceParamSpec (int type, int index) noexcept
{
    using D = VoiceParamDisplay;
    static const VoiceParamSpec common[4] = {
        { "level", "Level", 0.7f, D::percent, "Volume of this engine" },
        { "tone",  "Tone",  0.5f, D::percent, "Darker / brighter than the global Brightness" },
        { "space", "Space", 0.5f, D::percent, "How much goes into the reverb" },
        { "echo",  "Echo",  0.5f, D::percent, "How much goes into the echo" },
    };
    static const VoiceParamSpec specific[kNumSoundTypes][4] = {
        { { "detune", "Detune", 0.5f, D::percent, "Width of the detuned oscillators" },
          { "breath", "Breath", 0.5f, D::percent, "Depth and speed of the slow swell" },
          { "sub",    "Sub",    0.5f, D::percent, "Level of the sub-octave" },
          { "glide",  "Glide",  0.45f, D::percent, "How slowly it slides to new chord tones" } },
        { { "decay",  "Decay",  0.5f, D::percent, "How long each bell rings" },
          { "ratio",  "Metal",  0.5f, D::ratio,   "FM ratio: from pure tones to clangorous metal" },
          { "density","Density",0.5f, D::percent, "How often bells strike" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
        { { "decay",  "Decay",  0.5f, D::percent, "String ring time" },
          { "density","Density",0.5f, D::percent, "How often figures are played" },
          { "speed",  "Speed",  0.5f, D::percent, "How fast the notes of a figure follow each other" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
        { { "drive",  "Drive",  0.4f, D::percent, "Warm saturation" },
          { "attack", "Attack", 0.5f, D::percent, "How slowly notes swell in" },
          { "walk",   "Walk",   0.5f, D::percent, "How often it steps to the fifth" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
        { { "rate",   "Rate",   0.5f, D::percent, "Grains per second" },
          { "length", "Length", 0.5f, D::percent, "Grain length" },
          { "spread", "Spread", 0.5f, D::percent, "Stereo scatter" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
        { { "reso",   "Resonance", 0.5f, D::percent, "How sharply the wind sings its notes" },
          { "air",    "Air",    0.5f, D::percent, "Amount of breathy, unpitched air" },
          { "gusts",  "Gusts",  0.5f, D::percent, "How lively the gusts are" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
        { { "decay",  "Decay",  0.5f, D::percent, "Length of each pulse" },
          { "density","Density",0.5f, D::percent, "How many pattern steps actually play" },
          { "pattern","Pattern",0.5f, D::hits,    "Hits per bar in the rhythm" },
          { "octave", "Octave", 0.5f, D::octave,  "Register shift" } },
    };

    const int t = type < 0 ? 0 : (type >= kNumSoundTypes ? kNumSoundTypes - 1 : type);
    const int i = index < 0 ? 0 : (index >= kVoiceParams ? kVoiceParams - 1 : index);
    return i < 4 ? common[i] : specific[t][i - 4];
}

using VoiceParamArray = std::array<float, kVoiceParams>;
using AllVoiceParams = std::array<VoiceParamArray, kNumSoundTypes>;

inline AllVoiceParams defaultVoiceParams() noexcept
{
    AllVoiceParams a {};
    for (int t = 0; t < kNumSoundTypes; ++t)
        for (int i = 0; i < kVoiceParams; ++i)
            a[(size_t) t][(size_t) i] = getVoiceParamSpec (t, i).defaultValue;
    return a;
}

// Helpers for the typed meanings of the 0..1 values.
inline int octaveFrom (float v) noexcept        { return (int) std::lround (v * 4.0f - 2.0f); }     // -2..+2
inline float fmRatioFrom (float v) noexcept     { return 1.0f + v * 4.0f; }                          // 1..5
inline int pulseHitsFrom (float v) noexcept     { return 2 + (int) std::lround (v * 7.0f); }         // 2..9
inline float levelGainFrom (float v) noexcept   { const float k = v / 0.7f; return k * k; }          // 0..~2
inline float range (float v, float lo, float hi) noexcept { return lo + (hi - lo) * v; }

} // namespace scape
