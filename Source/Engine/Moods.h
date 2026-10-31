#pragma once

#include <array>
#include <cstdint>

namespace scape
{

// A mood sets the key, scale, pace and colour of the whole scene.
struct Mood
{
    const char* name;
    int root;                       // MIDI note of the tonic around octave 4
    std::array<int, 7> scale;
    int scaleSize;
    float paceMultiplier;
    float brightnessBias;           // -1 .. 1
    std::uint32_t topColour, bottomColour;
};

constexpr int kNumMoods = 6;

inline const Mood& getMood (int index) noexcept
{
    static const Mood moods[kNumMoods] = {
        { "Dawn",    62, { 0, 2, 4, 6, 7, 9, 11 }, 7, 1.00f,  0.15f, 0xff3a2f5bu, 0xffd8987au },
        { "Tide",    57, { 0, 2, 3, 5, 7, 9, 10 }, 7, 0.90f,  0.00f, 0xff0b2d3fu, 0xff2f7f8fu },
        { "Ember",   60, { 0, 2, 3, 5, 7, 8, 10 }, 7, 0.80f, -0.20f, 0xff1a0b0bu, 0xff7a2e1eu },
        { "Glacier", 64, { 0, 2, 4, 7, 9, 0, 0 },  5, 0.70f,  0.30f, 0xff1c3a52u, 0xff9cc7deu },
        { "Dusk",    65, { 0, 2, 4, 5, 7, 9, 10 }, 7, 0.85f, -0.05f, 0xff1d1a3au, 0xff8a4f7du },
        { "Night",   58, { 0, 2, 3, 7, 8, 0, 0 },  5, 0.65f, -0.35f, 0xff05060fu, 0xff1b2340u },
    };

    return moods[index < 0 ? 0 : (index >= kNumMoods ? kNumMoods - 1 : index)];
}

// MIDI note for a scale degree (can be negative or exceed the scale size).
inline int noteForDegree (const Mood& mood, int degree, int octaveOffset = 0) noexcept
{
    const int n = mood.scaleSize;
    int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    int idx = degree - oct * n;
    return mood.root + 12 * (oct + octaveOffset) + mood.scale[(size_t) idx];
}

} // namespace scape
