#include "SoundChain.h"

#include <algorithm>

namespace scape
{

SoundChain::SoundChain()
{
    for (auto& m : modulated) m.store (-1.0f);
    for (auto& v : lfoValues) v.store (0.0f);
}

void SoundChain::prepare (double sampleRate)
{
    sr = sampleRate;
    engine.prepare (sampleRate);
    granular.prepare ((float) sampleRate);
    pad.prepare ((float) sampleRate);
    limiter.prepare ((float) sampleRate);
    for (int i = 0; i < kNumLfos; ++i)
        lfos[(size_t) i].reset (0xA11CEu + (std::uint32_t) i * 7919u);
}

float SoundChain::toNorm (const ChainParams& p, int target) noexcept
{
    if (isVoiceTarget (target))
    {
        const int k = target - kFirstVoiceTarget;
        return p.engine.voice[(size_t) (k / kVoiceParams)][(size_t) (k % kVoiceParams)];
    }

    switch ((ModTarget) target)
    {
        case ModTarget::PadX:         return p.pad.x;
        case ModTarget::PadY:         return p.pad.y;
        case ModTarget::FxMix:        return p.pad.mix;
        case ModTarget::GrainMix:     return p.grain.mix;
        case ModTarget::GrainSize:    return ParamMaps::grainSizeToNorm (p.grain.size);
        case ModTarget::GrainDensity: return ParamMaps::grainDensityToNorm (p.grain.density);
        case ModTarget::GrainPitch:   return (p.grain.pitch + 12.0f) / 24.0f;
        case ModTarget::GrainSpray:   return p.grain.spray;
        case ModTarget::Reverb:       return p.engine.reverb;
        case ModTarget::Echo:         return p.engine.delay;
        case ModTarget::Brightness:   return p.engine.brightness;
        case ModTarget::Activity:     return p.engine.activity;
        case ModTarget::None:
        case ModTarget::FirstVoiceTarget: break;
    }
    return 0.0f;
}

void SoundChain::setFromNorm (ChainParams& p, int target, float n) noexcept
{
    n = std::clamp (n, 0.0f, 1.0f);
    if (isVoiceTarget (target))
    {
        const int k = target - kFirstVoiceTarget;
        p.engine.voice[(size_t) (k / kVoiceParams)][(size_t) (k % kVoiceParams)] = n;
        return;
    }

    switch ((ModTarget) target)
    {
        case ModTarget::PadX:         p.pad.x = n; break;
        case ModTarget::PadY:         p.pad.y = n; break;
        case ModTarget::FxMix:        p.pad.mix = n; break;
        case ModTarget::GrainMix:     p.grain.mix = n; break;
        case ModTarget::GrainSize:    p.grain.size = ParamMaps::grainSizeFromNorm (n); break;
        case ModTarget::GrainDensity: p.grain.density = ParamMaps::grainDensityFromNorm (n); break;
        case ModTarget::GrainPitch:   p.grain.pitch = n * 24.0f - 12.0f; break;
        case ModTarget::GrainSpray:   p.grain.spray = n; break;
        case ModTarget::Reverb:       p.engine.reverb = n; break;
        case ModTarget::Echo:         p.engine.delay = n; break;
        case ModTarget::Brightness:   p.engine.brightness = n; break;
        case ModTarget::Activity:     p.engine.activity = n; break;
        case ModTarget::None:
        case ModTarget::FirstVoiceTarget: break;
    }
}

void SoundChain::process (float* L, float* R, int numSamples, ElementBank& bank, const ChainParams& base) noexcept
{
    std::array<float, kNumModTargets> offsets {};
    std::array<bool, kNumModTargets> assigned {};
    ChainParams p = base;

    const float bpm = std::clamp (base.engine.tempo, 20.0f, 200.0f) * getMood (base.engine.mood).paceMultiplier;

    for (int pos = 0; pos < numSamples; pos += kModBlock)
    {
        const int n = std::min (kModBlock, numSamples - pos);

        // LFOs -> per-target offsets (in normalised units), summed if several share a target.
        offsets.fill (0.0f);
        assigned.fill (false);
        for (int i = 0; i < kNumLfos; ++i)
        {
            const auto& lp = base.lfo[(size_t) i];
            const float v = lfos[(size_t) i].advance (lp.shape, std::max (0.001f, lp.rate), sr, n);
            lfoValues[(size_t) i].store (v, std::memory_order_relaxed);
            if (lp.target > 0 && lp.target < kNumModTargets)
            {
                offsets[(size_t) lp.target] += v * lp.depth;
                assigned[(size_t) lp.target] = true;
            }
        }

        p = base;
        for (int t = 1; t < kNumModTargets; ++t)
            if (assigned[(size_t) t])
                setFromNorm (p, t, toNorm (base, t) + offsets[(size_t) t]);

        float* l = L + pos;
        float* r = R + pos;
        engine.setParams (p.engine);
        engine.process (l, r, n, bank);
        granular.process (l, r, n, p.grain);
        pad.process (l, r, n, p.pad, bpm);
        limiter.process (l, r, n);

        if (pos + kModBlock >= numSamples)
            for (int t = 0; t < kNumModTargets; ++t)
                modulated[(size_t) t].store (assigned[(size_t) t] ? toNorm (p, t) : -1.0f, std::memory_order_relaxed);
    }
}

} // namespace scape
