#pragma once

#include "Dsp.h"

#include <array>

namespace scape
{

// 8-line feedback delay network reverb with input diffusion and gentle modulation.
class FdnReverb
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        const float scale = sr / 48000.0f;

        static constexpr float lengthsMs[N] = { 55.7f, 65.5f, 76.9f, 89.9f, 101.0f, 110.9f, 124.7f, 146.5f };
        for (int i = 0; i < N; ++i)
        {
            baseDelay[i] = lengthsMs[i] * 0.001f * sr;
            lines[i].allocate ((int) baseDelay[i] + 64);
            damp[i].reset();
            lfoPhase[i] = (float) i * 0.785f;
            lfoInc[i] = twoPiF * (0.11f + 0.037f * (float) i) / sr;
        }

        static constexpr int apLengths[2][3] = { { 142, 379, 607 }, { 167, 433, 541 } };
        for (int c = 0; c < 2; ++c)
            for (int a = 0; a < 3; ++a)
            {
                allpass[c][a].len = (float) apLengths[c][a] * scale;
                allpass[c][a].line.allocate ((int) allpass[c][a].len + 4);
            }

        predelay.allocate ((int) (0.25f * sr));
        predelaySamples = 0.025f * sr;
        setDecay (decaySeconds);
        setDamping (6000.0f);
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& c : allpass) for (auto& a : c) a.line.clear();
        predelay.clear();
        for (auto& d : damp) d.reset();
    }

    void setDecay (float seconds)
    {
        decaySeconds = seconds;
        for (int i = 0; i < N; ++i)
            gains[i] = std::pow (10.0f, -3.0f * baseDelay[i] / (seconds * sr));
    }

    void setDamping (float hz)
    {
        for (auto& d : damp) d.setCutoff (hz, sr);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept
    {
        for (int s = 0; s < n; ++s)
        {
            predelay.push ((inL[s] + inR[s]) * 0.5f);
            const float pd = predelay.read (predelaySamples);
            float xl = pd + (inL[s] - inR[s]) * 0.3f;
            float xr = pd - (inL[s] - inR[s]) * 0.3f;

            for (auto& a : allpass[0]) xl = a.process (xl);
            for (auto& a : allpass[1]) xr = a.process (xr);

            float o[N];
            for (int i = 0; i < N; ++i)
            {
                const float mod = std::sin (lfoPhase[i]) * 6.0f;
                lfoPhase[i] += lfoInc[i];
                if (lfoPhase[i] > twoPiF) lfoPhase[i] -= twoPiF;
                o[i] = damp[i].process (lines[i].read (baseDelay[i] + mod)) * gains[i];
            }

            outL[s] = (o[0] - o[2] + o[4] - o[6]) * 0.5f;
            outR[s] = (o[1] - o[3] + o[5] - o[7]) * 0.5f;

            hadamard (o);
            for (int i = 0; i < N; ++i)
                lines[i].push (o[i] + ((i & 1) == 0 ? xl : xr) * 0.5f);
        }
    }

private:
    static constexpr int N = 8;
    static constexpr float twoPiF = dsp::twoPi;

    struct Allpass
    {
        dsp::DelayLine line;
        float len = 100.0f, g = 0.62f;

        float process (float x) noexcept
        {
            const float bo = line.read (len);
            const float v = x + g * bo;
            line.push (v);
            return bo - g * v;
        }
    };

    static void hadamard (float* v) noexcept
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
        const float norm = 0.35355339f; // 1 / sqrt(8)
        for (int i = 0; i < N; ++i) v[i] *= norm;
    }

    float sr = 48000.0f, decaySeconds = 9.0f, predelaySamples = 1200.0f;
    dsp::DelayLine lines[N];
    dsp::OnePole damp[N];
    float baseDelay[N] {}, gains[N] {}, lfoPhase[N] {}, lfoInc[N] {};
    Allpass allpass[2][3];
    dsp::DelayLine predelay;
};

// Tempo-synced ping-pong delay with a darkening feedback path.
class PingPongDelay
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        left.allocate ((int) (4.0f * sr));
        right.allocate ((int) (4.0f * sr));
        timeSmooth.setTime (0.5f, sr);
        timeSmooth.reset (0.5f * sr);
        lpL.setCutoff (3200.0f, sr);
        lpR.setCutoff (3200.0f, sr);
    }

    void reset() { left.clear(); right.clear(); lpL.reset(); lpR.reset(); }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                  float delaySeconds, float feedback) noexcept
    {
        const float target = std::clamp (delaySeconds * sr, 16.0f, 3.9f * sr);
        for (int s = 0; s < n; ++s)
        {
            const float d = timeSmooth.process (target);
            const float rl = left.read (d);
            const float rr = right.read (d);
            left.push ((inL[s] + inR[s]) * 0.5f + lpR.process (rr) * feedback);
            right.push (lpL.process (rl) * feedback);
            outL[s] = rl;
            outR[s] = rr;
        }
    }

private:
    float sr = 48000.0f;
    dsp::DelayLine left, right;
    dsp::OnePole timeSmooth, lpL, lpR;
};

// Simple peak limiter followed by a soft safety clip.
class SoftLimiter
{
public:
    void prepare (float sampleRate)
    {
        attack = std::exp (-1.0f / (0.002f * sampleRate));
        release = std::exp (-1.0f / (0.35f * sampleRate));
        gain = 1.0f;
    }

    void process (float* l, float* r, int n) noexcept
    {
        constexpr float ceiling = 0.89f;
        for (int s = 0; s < n; ++s)
        {
            const float peak = std::max (std::abs (l[s]), std::abs (r[s]));
            const float target = peak * gain > ceiling ? ceiling / std::max (peak, 1.0e-9f) : 1.0f;
            gain = target < gain ? target + (gain - target) * attack
                                 : target + (gain - target) * release;
            l[s] = clip (l[s] * gain);
            r[s] = clip (r[s] * gain);
        }
    }

private:
    static float clip (float x) noexcept
    {
        // Transparent below ~0.8, smoothly saturating to +/-1.
        const float a = std::abs (x);
        if (a < 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }

    float attack = 0.99f, release = 0.9999f, gain = 1.0f;
};

} // namespace scape
