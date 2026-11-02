#pragma once

#include <array>
#include <cmath>

namespace scape
{

// Per-engine synth controls. Every sound type has the same six universal controls
// (Level, Cutoff, Resonance, Attack, Release, Octave) plus four that shape its character.
// All stored as 0..1.
constexpr int kNumSoundTypes = 7;      // Drone .. Pad (modifiers have no sound)
constexpr int kVoiceParams = 10;

enum VoiceParamIndex : int
{
    kLevel = 0, kCutoff, kResonance, kAttack, kRelease, kOctave, kChar1, kChar2, kChar3, kChar4
};

enum class VoiceParamDisplay { percent, octave, ratio, hits, hertz, seconds };

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
    static const VoiceParamSpec common[6] = {
        { "level",     "Level",     0.7f, D::percent, "Volume of this engine" },
        { "cutoff",    "Cutoff",    1.0f, D::hertz,   "Low-pass filter: lower it to darken the sound" },
        { "resonance", "Resonance", 0.0f, D::percent, "Emphasis at the cutoff: from smooth to whistling. Works with Cutoff" },
        { "attack",    "Attack",    0.5f, D::seconds, "How slowly the sound swells in" },
        { "release",   "Release",   0.5f, D::seconds, "How long the sound rings out after it's removed (and note decay)" },
        { "octave",    "Octave",    0.5f, D::octave,  "Transpose this engine up or down" },
    };
    static const VoiceParamSpec specific[kNumSoundTypes][4] = {
        { { "detune",  "Detune",  0.5f, D::percent, "Width of the detuned oscillator layers" },
          { "sub",     "Sub",     0.4f, D::percent, "Level of the sub-octave" },
          { "shimmer", "Shimmer", 0.5f, D::percent, "Airy octave and fifth partials floating above" },
          { "motion",  "Motion",  0.5f, D::percent, "How much the tone slowly drifts and breathes" } },
        { { "ring",    "Ring",    0.3f, D::percent, "From pure crystal (left) to rich metal bowl overtones (right)" },
          { "beat",    "Beat",    0.5f, D::percent, "Speed of the slow wah-wah beating of the bowl" },
          { "swell",   "Swell",   0.5f, D::percent, "Struck (left) or softly sung, swelling in (right)" },
          { "density", "Density", 0.5f, D::percent, "How often bowls sound" } },
        { { "wave",    "Wave",    0.35f, D::percent, "Oscillator: sine, triangle, saw, square" },
          { "snap",    "Snap",    0.5f, D::percent, "How bright and percussive the pluck's attack is" },
          { "density", "Density", 0.5f, D::percent, "How often figures are played" },
          { "speed",   "Speed",   0.5f, D::percent, "How fast the notes of a figure follow each other" } },
        { { "pattern", "Pattern", 0.3f,  D::hits,    "Hits per bar in the repeating rhythm" },
          { "width",   "Width",   0.35f, D::percent, "Pulse width: hollow square (left) to thin and reedy (right)" },
          { "pwm",     "PWM",     0.4f,  D::percent, "Slow pulse-width modulation for a moving, chorused shimmer" },
          { "density", "Density", 0.5f,  D::percent, "How many pattern steps actually play" } },
        { { "rate",    "Rate",    0.5f, D::percent, "Grains per second" },
          { "length",  "Length",  0.5f, D::percent, "Grain length: glitter (left) to smooth haze (right)" },
          { "spread",  "Spread",  0.5f, D::percent, "Stereo scatter" },
          { "sparkle", "Sparkle", 0.4f, D::percent, "Chance of octave-up sparkles" } },
        { { "breeze",  "Breeze",  0.5f, D::percent, "How strong and frequent the gusts are" },
          { "cluster", "Cluster", 0.5f, D::percent, "How many chimes ring together in a gust" },
          { "metal",   "Metal",   0.5f, D::percent, "From soft bamboo-like tones to bright metal tubes" },
          { "spread",  "Spread",  0.6f, D::percent, "Stereo width of the chime set" } },
        { { "detune",  "Detune",  0.5f, D::percent, "Spread of the detuned voices (thin to lush)" },
          { "warmth",  "Warmth",  0.5f, D::percent, "Oscillator: bright saw (left) to warm, soft (right)" },
          { "voicing", "Voicing", 0.5f, D::percent, "Chord size: close triad to wide, open voicing" },
          { "motion",  "Motion",  0.5f, D::percent, "Slow filter movement and swells" } },
    };

    const int t = type < 0 ? 0 : (type >= kNumSoundTypes ? kNumSoundTypes - 1 : type);
    const int i = index < 0 ? 0 : (index >= kVoiceParams ? kVoiceParams - 1 : index);
    return i < 6 ? common[i] : specific[t][i - 6];
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

// Typed meanings of the 0..1 values.
inline int octaveFrom (float v) noexcept          { return (int) std::lround (v * 4.0f - 2.0f); }   // -2..+2
inline float fmRatioFrom (float v) noexcept       { return 1.0f + v * 4.0f; }                        // 1..5
inline int pulseHitsFrom (float v) noexcept       { return 2 + (int) std::lround (v * 7.0f); }       // 2..9
inline float levelGainFrom (float v) noexcept     { const float k = v / 0.7f; return k * k; }        // 0..~2
inline float cutoffHzFrom (float v) noexcept      { return 60.0f * std::pow (20000.0f / 60.0f, v); } // 60 Hz..20 kHz
inline float resonanceQFrom (float v) noexcept    { return 0.7f + 11.0f * v; }
inline float attackSecondsFrom (float v) noexcept { return 0.2f + 6.4f * v * v; }                    // 0.2..6.6 s
inline float releaseSecondsFrom (float v) noexcept{ return 0.2f + 4.0f * v * v; }                    // 0.2..4.2 s
inline float range (float v, float lo, float hi) noexcept { return lo + (hi - lo) * v; }

} // namespace scape
