#pragma once

#include "Dsp.h"

#include <array>
#include <vector>

namespace scape
{

struct GrainParams
{
    float mix = 0.0f;       // 0..1 dry/wet
    float size = 0.12f;     // seconds
    float density = 12.0f;  // grains per second
    float pitch = 0.0f;     // semitones
    float spray = 0.3f;     // 0..1 position jitter + stereo spread
};

// Granular resynthesis of the synth output: continuously records the last few
// seconds and plays overlapping, windowed, pitch-shifted grains from it.
class GranularFx
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        length = (int) (4.0f * sr);
        bufL.assign ((size_t) length, 0.0f);
        bufR.assign ((size_t) length, 0.0f);
        writePos = 0;
        countdown = 0.0f;
        for (auto& g : grains) g.on = false;
        rng.seed (0x6A41u);
        mixSmooth.setTime (0.05f, sr);
        mixSmooth.reset (0.0f);
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        for (auto& g : grains) g.on = false;
    }

    void process (float* L, float* R, int n, const GrainParams& p) noexcept
    {
        const float size = std::clamp (p.size, 0.01f, 1.0f);
        const float density = std::clamp (p.density, 0.5f, 100.0f);
        const float overlap = std::max (1.0f, density * size);
        const float norm = 1.0f / std::sqrt (overlap);
        const float baseRate = std::exp2 (std::clamp (p.pitch, -24.0f, 24.0f) / 12.0f);

        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) writePos] = L[i];
            bufR[(size_t) writePos] = R[i];

            const float mix = mixSmooth.process (std::clamp (p.mix, 0.0f, 1.0f));

            if (mix > 0.0005f && --countdown <= 0.0f)
            {
                spawn (size, baseRate, p.spray);
                countdown = sr / density * rng.range (0.6f, 1.4f);
            }

            float wl = 0.0f, wr = 0.0f;
            for (auto& g : grains)
            {
                if (! g.on) continue;

                const float w = std::sin (dsp::pi * (float) g.age / (float) g.len);
                const float env = w * w;
                const float sl = read (bufL, g.pos), sr_ = read (bufR, g.pos);
                wl += (sl * g.gll + sr_ * g.grl) * env;
                wr += (sl * g.glr + sr_ * g.grr) * env;

                g.pos += g.rate;
                if (g.pos >= (float) length) g.pos -= (float) length;
                if (++g.age >= g.len) g.on = false;
            }

            if (++writePos >= length) writePos = 0;

            L[i] = L[i] * (1.0f - mix) + wl * norm * mix * 1.4f;
            R[i] = R[i] * (1.0f - mix) + wr * norm * mix * 1.4f;
        }
    }

private:
    struct Grain
    {
        bool on = false;
        float pos = 0, rate = 1;
        int age = 0, len = 1;
        float gll = 1, grl = 0, glr = 0, grr = 1;   // stereo matrix (spread)
    };

    float read (const std::vector<float>& b, float pos) const noexcept
    {
        const int i0 = (int) pos;
        const float f = pos - (float) i0;
        const int i1 = i0 + 1 >= length ? 0 : i0 + 1;
        return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
    }

    void spawn (float size, float baseRate, float spray) noexcept
    {
        Grain* g = nullptr;
        for (auto& c : grains)
            if (! c.on) { g = &c; break; }
        if (g == nullptr) return;

        // A little random detune/octave sparkle when spray is high.
        float rate = baseRate * (1.0f + rng.bipolar() * 0.004f * (1.0f + 4.0f * spray));
        if (spray > 0.6f && rng.chance ((spray - 0.6f) * 0.5f))
            rate *= rng.chance (0.5f) ? 2.0f : 0.5f;

        g->len = std::max (16, (int) (size * sr));
        // Start far enough back that a fast grain never overtakes the write head.
        const float minDelay = (float) g->len * std::max (0.0f, rate - 1.0f) + 64.0f;
        const float delay = minDelay + spray * rng.next01() * 1.5f * sr + rng.next01() * 0.02f * sr;
        float start = (float) writePos - std::min (delay, (float) length - (float) g->len * rate - 64.0f);
        while (start < 0.0f) start += (float) length;

        g->pos = start;
        g->rate = rate;
        g->age = 0;

        const float pan = rng.bipolar() * spray;           // -1..1
        const float a = (pan + 1.0f) * 0.25f * dsp::pi;
        const float l = std::cos (a) * 1.414f, r = std::sin (a) * 1.414f;
        g->gll = 0.5f * l; g->grl = 0.5f * l;   // mono-sum then pan keeps grains coherent
        g->glr = 0.5f * r; g->grr = 0.5f * r;
        g->on = true;
    }

    float sr = 48000.0f;
    int length = 1, writePos = 0;
    std::vector<float> bufL, bufR;
    std::array<Grain, 64> grains;
    float countdown = 0.0f;
    dsp::Rng rng;
    dsp::OnePole mixSmooth;
};

} // namespace scape
