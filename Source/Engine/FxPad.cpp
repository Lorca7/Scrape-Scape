#include "FxPad.h"

#include <cmath>

namespace scape
{

using namespace dsp;

void FxPad::prepare (float sampleRate)
{
    sr = sampleRate;
    xs.setTime (0.03f, sr);  ys.setTime (0.03f, sr);  mixs.setTime (0.03f, sr);
    xs.reset (0.5f); ys.reset (0.5f); mixs.reset (0.0f);

    wash.prepare (sr);
    washHpL.setCutoff (120.0f, sr); washHpR.setCutoff (120.0f, sr);

    dubL.allocate ((int) (2.5f * sr)); dubR.allocate ((int) (2.5f * sr));
    dubTime.setTime (0.25f, sr);
    dubLpL.setCutoff (2800.0f, sr); dubLpR.setCutoff (2800.0f, sr);
    dubHpL.setCutoff (180.0f, sr);  dubHpR.setCutoff (180.0f, sr);

    warL.allocate ((int) (0.05f * sr)); warR.allocate ((int) (0.05f * sr));

    stL.allocate ((int) (4.5f * sr)); stR.allocate ((int) (4.5f * sr));

    ringFreq.setTime (0.03f, sr);
    reset();
}

void FxPad::reset()
{
    for (int t = 0; t < kNumPadEffects; ++t)
        resetEffect (t);
}

void FxPad::resetEffect (int type)
{
    switch ((PadEffect) type)
    {
        case PadEffect::Filter:  fl.reset(); fr.reset(); break;
        case PadEffect::Wash:    wash.reset(); washDecay = -1.0f; washHpL.reset(); washHpR.reset(); break;
        case PadEffect::DubEcho: dubL.clear(); dubR.clear(); dubTime.reset (0.4f * sr);
                                 dubLpL.reset(); dubLpR.reset(); dubHpL.reset(); dubHpR.reset(); break;
        case PadEffect::Warble:  warL.clear(); warR.clear(); warLpL.reset(); warLpR.reset(); break;
        case PadEffect::Crush:   holdL = holdR = 0.0f; holdCount = 0.0f; break;
        case PadEffect::Stutter: stL.clear(); stR.clear(); stWritten = 0; stHaveSlice = false;
                                 stPlayPos = 0; stRepeat = 0; break;
        case PadEffect::Phaser:  apL.fill (0.0f); apR.fill (0.0f); phFbL = phFbR = 0.0f; break;
        case PadEffect::RingMod: ringPhaseL = ringPhaseR = 0.0f; ringFreq.reset (200.0f); break;
        case PadEffect::NumEffects: break;
    }
}

void FxPad::process (float* L, float* R, int n, const PadParams& p, float bpm) noexcept
{
    bpmNow = std::clamp (bpm, 20.0f, 300.0f);
    const int requested = std::clamp (p.type, 0, kNumPadEffects - 1);
    if (requested != current) pending = requested;

    const float targetMix = std::clamp (p.mix, 0.0f, 1.0f);

    // Nothing to do while fully dry: keep CPU at zero but stay ready.
    if (targetMix <= 0.0f && mixs.z < 1.0e-4f && pending == current)
    {
        mixs.z = 0.0f;
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        // Short fade when the effect type changes, so switching never clicks.
        if (pending != current)
        {
            switchGain -= 1.0f / (0.015f * sr);
            if (switchGain <= 0.0f)
            {
                switchGain = 0.0f;
                current = pending;
                resetEffect (current);
            }
        }
        else if (switchGain < 1.0f)
        {
            switchGain = std::min (1.0f, switchGain + 1.0f / (0.015f * sr));
        }

        const float x = xs.process (std::clamp (p.x, 0.0f, 1.0f));
        const float y = 1.0f - ys.process (std::clamp (p.y, 0.0f, 1.0f));   // up = more
        const float mix = mixs.process (targetMix) * switchGain;

        float l = L[i], r = R[i];
        processSample (current, x, y, l, r);
        L[i] = L[i] + (l - L[i]) * mix;
        R[i] = R[i] + (r - R[i]) * mix;
        ++counter;
    }
}

void FxPad::processSample (int type, float x, float y, float& l, float& r) noexcept
{
    switch ((PadEffect) type)
    {
        case PadEffect::Filter:
        {
            if ((counter & 15) == 0)
            {
                const float q = 0.7f + y * y * 14.0f;
                const float cutoff = x < 0.5f ? 20000.0f * std::pow (80.0f / 20000.0f, (0.5f - x) * 2.0f)
                                              : 20.0f * std::pow (8000.0f / 20.0f, (x - 0.5f) * 2.0f);
                fl.set (cutoff, q, sr);
                fr.set (cutoff, q, sr);
            }
            float lo, ba, hi;
            fl.processAll (l, lo, ba, hi); const float ol = x < 0.5f ? lo : hi;
            fr.processAll (r, lo, ba, hi); const float or_ = x < 0.5f ? lo : hi;
            const float comp = 1.0f / (1.0f + y * y * 2.0f);
            l = std::tanh (ol * comp);
            r = std::tanh (or_ * comp);
            break;
        }

        case PadEffect::Wash:
        {
            if ((counter & 63) == 0)
            {
                const float decay = x > 0.97f ? 600.0f : 1.5f + x * x * 40.0f;   // far right freezes
                if (std::abs (decay - washDecay) > 0.05f) { wash.setDecay (decay); washDecay = decay; }
                wash.setDamping (1200.0f + y * y * 13000.0f);
            }
            const float inGain = x > 0.97f ? 0.0f : 1.0f;   // freeze: stop feeding new sound
            float il = washHpL.process (l), ir = washHpR.process (r);
            il = (l - il) * inGain; ir = (r - ir) * inGain;   // high-passed input
            float ol, or_;
            wash.process (&il, &ir, &ol, &or_, 1);
            l = ol * 0.9f;
            r = or_ * 0.9f;
            break;
        }

        case PadEffect::DubEcho:
        {
            const float t = dubTime.process ((0.06f + x * x * 1.14f) * sr);
            const float fb = y * 1.05f;
            const float rl = dubL.read (t), rr = dubR.read (t * 0.75f);
            auto loop = [] (OnePole& hp, OnePole& lp, float s) { return lp.process (s - hp.process (s)); };
            const float fl_ = loop (dubHpL, dubLpL, rr), fr_ = loop (dubHpR, dubLpR, rl);
            dubL.push ((l + r) * 0.5f + std::tanh (fb * fl_ * 1.3f) / 1.3f);
            dubR.push (std::tanh (fb * fr_ * 1.3f) / 1.3f);
            l = rl; r = rr;
            break;
        }

        case PadEffect::Warble:
        {
            const float rate = 0.1f * std::pow (80.0f, y);
            warPhase += rate / sr;            if (warPhase >= 1.0f) warPhase -= 1.0f;
            flutterPhase += 13.0f / sr;       if (flutterPhase >= 1.0f) flutterPhase -= 1.0f;
            const float depth = x * 0.006f * sr;   // up to 6 ms
            const float modL = std::sin (twoPi * warPhase) * 0.8f + std::sin (twoPi * flutterPhase) * 0.2f;
            const float modR = std::sin (twoPi * warPhase + 0.9f) * 0.8f + std::sin (twoPi * flutterPhase + 1.7f) * 0.2f;
            warL.push (l); warR.push (r);
            if ((counter & 31) == 0)
            {
                warLpL.setCutoff (12000.0f - 8000.0f * x, sr);
                warLpR.coeff = warLpL.coeff;
            }
            l = warLpL.process (warL.read (0.008f * sr + depth * (0.5f + 0.5f * modL)));
            r = warLpR.process (warR.read (0.008f * sr + depth * (0.5f + 0.5f * modR)));
            break;
        }

        case PadEffect::Crush:
        {
            const float factor = 1.0f + x * x * 90.0f;
            holdCount += 1.0f;
            if (holdCount >= factor) { holdCount -= factor; holdL = l; holdR = r; }
            const float bits = 16.0f - y * 14.0f;
            const float steps = std::exp2 (bits - 1.0f);
            l = std::round (holdL * steps) / steps;
            r = std::round (holdR * steps) / steps;
            break;
        }

        case PadEffect::Stutter:
        {
            stL.push (l); stR.push (r);
            ++stWritten;

            static constexpr float divisions[] = { 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f };   // in beats
            const int div = std::clamp ((int) (x * 5.999f), 0, 5);
            const float beat = 60.0f / bpmNow * sr;
            const int bufferSize = (int) stL.buf.size() - 8;

            if (! stHaveSlice || stPlayPos >= stSliceLen)
            {
                stPlayPos = 0;
                if (! stHaveSlice || ++stRepeat >= stRepeats)
                {
                    // Capture the most recent slice and loop it for a number of repeats.
                    stRepeat = 0;
                    stSliceLen = std::clamp ((int) (divisions[div] * beat), 64, bufferSize / 2);
                    stRepeats = std::clamp (1 + (int) std::round (y * 15.0f), 1, bufferSize / stSliceLen - 1);
                    stCaptureAt = stWritten;
                    stHaveSlice = stWritten > stSliceLen;
                }
            }

            if (stHaveSlice)
            {
                const long long absolute = stCaptureAt - stSliceLen + stPlayPos;
                const float delay = (float) (stWritten - absolute);
                const int fade = std::min (96, stSliceLen / 4);
                float g = 1.0f;
                if (stPlayPos < fade) g = (float) stPlayPos / (float) fade;
                else if (stPlayPos > stSliceLen - fade) g = (float) (stSliceLen - stPlayPos) / (float) fade;
                l = stL.read (delay) * g;
                r = stR.read (delay) * g;
                ++stPlayPos;
            }
            break;
        }

        case PadEffect::Phaser:
        {
            const float rate = 0.05f * std::pow (100.0f, x);
            phaseLfo += rate / sr; if (phaseLfo >= 1.0f) phaseLfo -= 1.0f;
            if ((counter & 15) == 0)
            {
                const float sweep = 0.5f + 0.5f * std::sin (twoPi * phaseLfo);
                const float f = 200.0f * std::pow (10.0f, sweep);    // 200 Hz .. 2 kHz
                const float t = std::tan (pi * f / sr);
                phaserCoeff = (t - 1.0f) / (t + 1.0f);
            }
            const float fb = y * 0.85f;
            auto run = [this] (std::array<float, 6>& st, float in) {
                float s = in;
                for (auto& z : st)
                {
                    const float out = phaserCoeff * s + z;
                    z = s - phaserCoeff * out;
                    s = out;
                }
                return s;
            };
            const float pl = run (apL, l + phFbL * fb);
            const float pr = run (apR, r + phFbR * fb);
            phFbL = pl; phFbR = pr;
            l = 0.5f * (l + pl);
            r = 0.5f * (r + pr);
            break;
        }

        case PadEffect::RingMod:
        {
            const float f = ringFreq.process (30.0f * std::pow (66.0f, x));
            ringPhaseL += f / sr;          if (ringPhaseL >= 1.0f) ringPhaseL -= 1.0f;
            ringPhaseR += f * 1.007f / sr; if (ringPhaseR >= 1.0f) ringPhaseR -= 1.0f;
            const float sl = std::sin (twoPi * ringPhaseL), srr = std::sin (twoPi * ringPhaseR);
            // y = 1: pure ring modulation, y = 0: gentle tremolo/AM.
            const float ml = y * sl + (1.0f - y) * (0.5f + 0.5f * sl);
            const float mr = y * srr + (1.0f - y) * (0.5f + 0.5f * srr);
            l *= ml * 1.3f;
            r *= mr * 1.3f;
            break;
        }

        case PadEffect::NumEffects:
            break;
    }
}

} // namespace scape
