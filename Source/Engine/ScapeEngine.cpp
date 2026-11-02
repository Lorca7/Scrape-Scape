#include "ScapeEngine.h"

#include <algorithm>
#include <cmath>

namespace scape
{

void ScapeEngine::prepare (double sampleRate)
{
    sr = (float) sampleRate;

    for (auto* v : { &dryL, &dryR, &revL, &revR, &dlyL, &dlyR, &wetL, &wetR, &echoL, &echoR })
        v->assign ((size_t) kChunk, 0.0f);

    for (auto& v : voices)
        v.prepare (sr);

    reverb.prepare (sr);
    delay.prepare (sr);
    limiter.prepare (sr);

    reverbSmooth.setTime (0.2f, sr);
    delaySmooth.setTime (0.2f, sr);
    volumeSmooth.setTime (0.05f, sr);

    prepared = true;
    reset();
}

void ScapeEngine::reset()
{
    reverb.reset();
    delay.reset();
    for (auto& s : snapshots) s = {};
    for (auto& v : voices) v.prepare (sr);

    rng.seed (0xC0FFEEu);
    ctx = {};
    ctx.tick = -1;
    samplesToNextTick = 0.0;
    ticksUntilChordChange = 64;

    reverbSmooth.reset (params.reverb);
    delaySmooth.reset (params.delay);
    volumeSmooth.reset (dsp::dbToGain (params.volumeDb));
}

void ScapeEngine::process (float* outL, float* outR, int numSamples, ElementBank& bank) noexcept
{
    if (! prepared)
    {
        std::fill (outL, outL + numSamples, 0.0f);
        std::fill (outR, outR + numSamples, 0.0f);
        return;
    }

    for (int pos = 0; pos < numSamples; pos += kChunk)
    {
        const int n = std::min (kChunk, numSamples - pos);
        processChunk (outL + pos, outR + pos, n, bank);
    }
}

int ScapeEngine::chooseNextChord() noexcept
{
    const Mood& mood = *ctx.mood;
    static constexpr int degrees7[] = { 0, 1, 3, 4, 5 };
    static constexpr float weights7[] = { 3.0f, 1.0f, 2.0f, 2.5f, 2.5f };
    static constexpr int degrees5[] = { 0, 1, 2, 3, 4 };
    static constexpr float weights5[] = { 3.0f, 1.5f, 1.5f, 2.0f, 1.5f };

    const bool seven = mood.scaleSize == 7;
    const int* degrees = seven ? degrees7 : degrees5;
    const float* weights = seven ? weights7 : weights5;

    float w[5];
    float total = 0.0f;
    for (int i = 0; i < 5; ++i)
    {
        w[i] = weights[i];
        if (degrees[i] == ctx.chordRoot)
            w[i] = 0.0f;
        // Only use chords with a perfect fifth so the harmony stays consonant.
        if (seven && noteForDegree (mood, degrees[i] + 4) - noteForDegree (mood, degrees[i]) != 7)
            w[i] = 0.0f;
        total += w[i];
    }

    if (total <= 0.0f)
        return 0;

    float r = rng.next01() * total;
    for (int i = 0; i < 5; ++i)
    {
        if (r < w[i]) return degrees[i];
        r -= w[i];
    }
    return 0;
}

void ScapeEngine::tickConductor() noexcept
{
    ++ctx.tick;
    ctx.chordChanged = false;

    if (ctx.tick == 0)
    {
        ctx.chordRoot = 0;
        ctx.chordChanged = true;
        ticksUntilChordChange = 64;
    }
    else if (forceChordChange)
    {
        ctx.chordChanged = true;
        forceChordChange = false;
        ticksUntilChordChange = 64;
    }
    else if (--ticksUntilChordChange <= 0)
    {
        ctx.chordRoot = chooseNextChord();
        ctx.chordChanged = true;
        ticksUntilChordChange = rng.chance (0.3f) ? 32 : 64;
    }

    displayChordRoot.store (ctx.chordRoot);
}

void ScapeEngine::syncVoices (ElementBank& bank) noexcept
{
    for (int i = 0; i < kMaxElements; ++i)
    {
        snapshots[(size_t) i] = bank.get (i);
        auto& s = snapshots[(size_t) i];
        if (s.active && s.seed != voices[(size_t) i].getSeed())
            voices[(size_t) i].begin (s);
    }

    const Mood& mood = *ctx.mood;
    const float globalBright = std::clamp (params.brightness + mood.brightnessBias * 0.3f, 0.0f, 1.0f);

    for (int i = 0; i < kMaxElements; ++i)
    {
        const auto& s = snapshots[(size_t) i];

        VoiceMods mods;
        mods.brightness = globalBright;
        mods.activity = std::clamp (params.activity, 0.0f, 1.0f);

        // Inactive slots keep their old seed so the voice can fade its tail out.
        ElementSnapshot e = s;
        if (! e.active) e.seed = voices[(size_t) i].getSeed();
        const int vt = std::clamp (voices[(size_t) i].getType(), 0, kNumSoundTypes - 1);
        voices[(size_t) i].update (e, mods, params.voice[(size_t) vt]);
    }
}

void ScapeEngine::processChunk (float* outL, float* outR, int n, ElementBank& bank) noexcept
{
    const Mood& mood = getMood (params.mood);
    const bool moodChanged = ctx.mood != nullptr && ctx.mood != &mood;
    ctx.mood = &mood;
    ctx.sampleRate = sr;

    const float bpm = std::clamp (params.tempo, 20.0f, 200.0f) * mood.paceMultiplier;
    ctx.samplesPerTick = sr * 60.0f / (bpm * 4.0f);

    if (moodChanged)
    {
        // New key: restart the progression on the tonic and let voices re-pitch.
        ctx.chordRoot = 0;
        forceChordChange = true;
    }

    syncVoices (bank);

    for (auto* v : { &dryL, &dryR, &revL, &revR, &dlyL, &dlyR })
        std::fill (v->begin(), v->begin() + n, 0.0f);

    const VoiceBuses buses { dryL.data(), dryR.data(), revL.data(), revR.data(), dlyL.data(), dlyR.data() };

    int pos = 0;
    while (pos < n)
    {
        if (samplesToNextTick <= 0.0)
        {
            tickConductor();
            for (auto& v : voices)
                v.onTick (ctx);
            samplesToNextTick += ctx.samplesPerTick;
        }

        const int seg = std::clamp ((int) std::ceil (samplesToNextTick), 1, n - pos);
        for (auto& v : voices)
            v.render (buses, pos, seg, ctx);

        samplesToNextTick -= seg;
        pos += seg;
    }

    // Effects
    const float revAmt = reverbSmooth.z = params.reverb + (reverbSmooth.z - params.reverb) * std::pow (reverbSmooth.coeff, (float) n);
    const float dlyAmt = delaySmooth.z = params.delay + (delaySmooth.z - params.delay) * std::pow (delaySmooth.coeff, (float) n);
    const float vol = dsp::dbToGain (params.volumeDb);
    const float volStart = volumeSmooth.z;
    const float volEnd = volumeSmooth.z = vol + (volumeSmooth.z - vol) * std::pow (volumeSmooth.coeff, (float) n);

    reverb.setDamping (2500.0f + 7000.0f * params.brightness);
    const float decay = 5.0f + 9.0f * revAmt;
    reverb.setDecay (decay);

    const float delaySeconds = 0.75f * 60.0f / bpm;
    delay.process (dlyL.data(), dlyR.data(), echoL.data(), echoR.data(), n, delaySeconds, 0.3f + 0.4f * dlyAmt);

    for (int i = 0; i < n; ++i)
    {
        revL[(size_t) i] += echoL[(size_t) i] * 0.3f * dlyAmt;
        revR[(size_t) i] += echoR[(size_t) i] * 0.3f * dlyAmt;
    }

    reverb.process (revL.data(), revR.data(), wetL.data(), wetR.data(), n);

    const float wetGain = 1.5f * revAmt;
    const float echoGain = 0.9f * dlyAmt;
    for (int i = 0; i < n; ++i)
    {
        const float g = volStart + (volEnd - volStart) * ((float) i / (float) n);
        outL[i] = (dryL[(size_t) i] + wetL[(size_t) i] * wetGain + echoL[(size_t) i] * echoGain) * g;
        outR[i] = (dryR[(size_t) i] + wetR[(size_t) i] * wetGain + echoR[(size_t) i] * echoGain) * g;
    }

    limiter.process (outL, outR, n);

    for (int i = 0; i < kMaxElements; ++i)
    {
        auto& slot = bank.slot (i);
        slot.activity.store (voices[(size_t) i].getActivity(), std::memory_order_relaxed);
        slot.sounding.store (voices[(size_t) i].isSounding(), std::memory_order_relaxed);
    }
}

} // namespace scape
