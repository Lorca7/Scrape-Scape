#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scape::dsp
{

constexpr float twoPi = 6.283185307179586f;
constexpr float pi    = 3.141592653589793f;

inline float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline float dbToGain (float db) noexcept   { return std::pow (10.0f, db / 20.0f); }

// Equal-power pan, pan in [-1, 1].
inline void panGains (float pan, float& l, float& r) noexcept
{
    const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * pi;
    l = std::cos (a);
    r = std::sin (a);
}

// Small, fast, deterministic random generator (xorshift32).
struct Rng
{
    std::uint32_t state = 0x12345678u;

    void seed (std::uint32_t s) noexcept { state = s != 0 ? s : 0x12345678u; next(); next(); }

    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float next01() noexcept      { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bipolar() noexcept     { return next01() * 2.0f - 1.0f; }
    float range (float a, float b) noexcept { return a + (b - a) * next01(); }
    int   nextInt (int n) noexcept { return n <= 1 ? 0 : (int) (next() % (std::uint32_t) n); }
    bool  chance (float p) noexcept { return next01() < p; }
};

// One-pole smoother / lowpass.
struct OnePole
{
    float z = 0.0f, coeff = 0.0f;

    void setTime (float seconds, float sampleRate) noexcept
    {
        coeff = seconds <= 0.0f ? 0.0f : std::exp (-1.0f / (seconds * sampleRate));
    }

    void setCutoff (float hz, float sampleRate) noexcept
    {
        coeff = std::exp (-twoPi * std::clamp (hz, 1.0f, sampleRate * 0.49f) / sampleRate);
    }

    float process (float x) noexcept { z = x + (z - x) * coeff; return z; }
    void reset (float v = 0.0f) noexcept { z = v; }
};

// Topology-preserving-transform state variable filter (Cytomic / Simper).
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;

    void set (float cutoff, float q, float sampleRate) noexcept
    {
        const float g = std::tan (pi * std::clamp (cutoff, 10.0f, sampleRate * 0.45f) / sampleRate);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    // Returns lowpass; band-pass available through the out parameter.
    float process (float v0, float& band) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        band = v1;
        return v2;
    }

    // All three outputs at once.
    void processAll (float v0, float& low, float& band, float& high) noexcept
    {
        low = process (v0, band);
        high = v0 - k * band - low;
    }

    float lowpass (float v0) noexcept  { float b; return process (v0, b); }
    float bandpass (float v0) noexcept { float b; process (v0, b); return b * k; } // unity gain at centre

    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

// Band-limited sawtooth using PolyBLEP.
struct BlepSaw
{
    float phase = 0.0f;

    static float blep (float t, float dt) noexcept
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    float process (float inc) noexcept
    {
        float out = 2.0f * phase - 1.0f - blep (phase, inc);
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

struct SineOsc
{
    float phase = 0.0f;

    float process (float inc) noexcept
    {
        const float out = std::sin (twoPi * phase);
        phase += inc;
        if (phase >= 1.0f) phase -= std::floor (phase);
        return out;
    }
};

// Interpolating circular delay line.
struct DelayLine
{
    std::vector<float> buf;
    int writePos = 0;

    void allocate (int maxSamples) { buf.assign ((size_t) std::max (4, maxSamples + 4), 0.0f); writePos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    void push (float x) noexcept
    {
        buf[(size_t) writePos] = x;
        if (++writePos >= (int) buf.size()) writePos = 0;
    }

    // delay in samples (>= 1) measured from the most recently pushed sample.
    float read (float delay) const noexcept
    {
        const int size = (int) buf.size();
        delay = std::clamp (delay, 1.0f, (float) (size - 3));
        float rp = (float) writePos - delay;
        if (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const float frac = rp - (float) i0;
        const int i1 = (i0 + 1) % size;
        return buf[(size_t) i0] + frac * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
};

} // namespace scape::dsp
