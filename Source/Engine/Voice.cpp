#include "Voice.h"

#include <cmath>

namespace scape
{

using namespace dsp;

namespace
{
    // Advance a one-pole smoother by n samples in one step (used for slow controls).
    inline float advance (OnePole& p, float target, int n) noexcept
    {
        p.z = target + (p.z - target) * std::pow (p.coeff, (float) n);
        return p.z;
    }

    inline float decayMultiplier (float seconds, float sr) noexcept
    {
        return std::exp (-6.9077553f / (std::max (seconds, 0.001f) * sr)); // -60 dB after `seconds`
    }

    inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline float wrap (float p) noexcept { return p >= 1.0f ? p - std::floor (p) : p; }

    // Morphing oscillator: 0 = sine, 1/3 = triangle, 2/3 = saw, 1 = square (band-limited edges).
    inline float morphOsc (float phase, float inc, float wave) noexcept
    {
        auto saw = [inc] (float p) { return 2.0f * p - 1.0f - BlepSaw::blep (p, inc); };
        const float w = std::clamp (wave, 0.0f, 1.0f) * 3.0f;

        if (w < 1.0f)
        {
            const float sine = std::sin (twoPi * phase);
            const float tri = 1.0f - 4.0f * std::abs (phase - 0.5f);
            return lerp (sine, -tri, w);
        }
        if (w < 2.0f)
        {
            const float tri = 1.0f - 4.0f * std::abs (phase - 0.5f);
            return lerp (-tri, saw (phase), w - 1.0f);
        }
        const float sq = (saw (phase) - saw (wrap (phase + 0.5f))) * 0.5f;
        return lerp (saw (phase), sq, w - 2.0f);
    }
}

//==============================================================================
void Voice::prepare (float sampleRate)
{
    sr = sampleRate;
    fadeInCoeff  = std::exp (-1.0f / (1.8f * sr));
    fadeOutCoeff = std::exp (-1.0f / (1.2f * sr));
    activityDecay = std::exp (-1.0f / (0.35f * sr));
    sounding = false;
    active = false;
    seed = 0; // forces begin() on the next sync, so a re-prepared engine restarts its voices
}

void Voice::begin (const ElementSnapshot& e)
{
    type = (ElementType) std::clamp (e.type, 0, kNumElementTypes - 1);
    seed = e.seed;
    rng.seed (seed);
    x = e.x; y = e.y; size = e.size;
    active = e.active;
    sounding = true;
    needsInitialPitch = true;
    activity = 0.0f;
    lastMood = nullptr;

    fade.reset (0.0f);
    voiceLpL.reset(); voiceLpR.reset();
    levelSmooth.setTime (0.08f, sr);
    levelSmooth.reset (size);
    panSmooth.setTime (0.08f, sr);
    panSmooth.reset (basePan());

    switch (type)
    {
        case ElementType::Drone:   typeGain = 0.11f; sendRev = 0.65f; sendDly = 0.06f; break;
        case ElementType::Bowl:    typeGain = 0.28f; sendRev = 0.70f; sendDly = 0.12f; break;
        case ElementType::Pluck:   typeGain = 0.30f; sendRev = 0.50f; sendDly = 0.35f; break;
        case ElementType::Pulse:   typeGain = 0.40f; sendRev = 0.45f; sendDly = 0.45f; break;
        case ElementType::Shimmer: typeGain = 0.07f; sendRev = 0.85f; sendDly = 0.20f; break;
        case ElementType::Chimes:  typeGain = 0.13f; sendRev = 0.65f; sendDly = 0.22f; break;
        case ElementType::Pad:     typeGain = 0.125f; sendRev = 0.60f; sendDly = 0.10f; break;
        case ElementType::NumTypes: typeGain = 0.0f; sendRev = 0.0f; sendDly = 0.0f; break;
    }

    // Drone
    for (auto& s : saws) s.phase = rng.next01();
    droneSub.phase = shimOct.phase = shimFifth.phase = 0.0f;
    droneFilterL.reset(); droneFilterR.reset();
    droneFreq.setTime (1.2f, sr);
    droneFreq.reset (0.0f);
    droneLfo = rng.next01() * twoPi;
    droneLfoInc = twoPi * rng.range (0.02f, 0.06f) / sr;
    droneBreath = rng.next01() * twoPi;
    droneBreathInc = twoPi * rng.range (0.04f, 0.09f) / sr;
    shimLfo = rng.next01() * twoPi;

    // Bowl / pluck / chimes
    for (auto& b : bowls) b.on = false;
    for (auto& p : plucks) { p.on = false; p.filter.reset(); }
    for (auto& c : chimes) c.on = false;
    motifLength = motifPos = nextPluck = nextChime = 0;

    // Pulse
    pulsePhase = 0.0f;
    pulseEnv = 0.0f;
    pulseAttackRemaining = 0;
    pulseStep = 0;
    pulseRotation = rng.nextInt (16);
    lastPulseHits = -1;
    pwmLfo = rng.next01() * twoPi;
    pulseFilter.reset();

    // Shimmer
    for (auto& g : grains) g.on = false;
    grainCountdown = rng.range (0.0f, 0.3f) * sr;

    // Chimes
    breeze.setTime (1.5f, sr);
    breeze.reset (0.3f);
    breezeTarget = 0.5f;
    chimeCountdown = rng.range (0.2f, 1.0f) * sr;
    clusterLeft = 0;

    // Pad
    for (auto& v : pad)
    {
        for (auto& s : v.saw) s.phase = rng.next01();
        v.triPhase = rng.next01();
        v.freq.setTime (1.4f, sr);
        v.freq.reset (0.0f);
    }
    padFilterL.reset(); padFilterR.reset();
    padSwell.setTime (2.5f, sr);
    padSwell.reset (0.0f);
    padSwellTarget = 0.0f;
    padLfo = rng.next01() * twoPi;
    padLfoInc = twoPi * rng.range (0.03f, 0.08f) / sr;
}

void Voice::update (const ElementSnapshot& e, const VoiceMods& m, const VoiceParamArray& params) noexcept
{
    active = e.active;
    x = e.x; y = e.y; size = e.size;
    mods = m;
    vp = params;

    // Universal synth controls: envelope times and the per-voice low-pass filter.
    fadeInCoeff  = std::exp (-1.0f / (attackSecondsFrom (vp[kAttack]) * sr));
    fadeOutCoeff = std::exp (-1.0f / (releaseSecondsFrom (vp[kRelease]) * sr));
    voiceFilterOn = vp[kCutoff] < 0.999f;
    if (voiceFilterOn)
    {
        const float cut = cutoffHzFrom (vp[kCutoff]), q = resonanceQFrom (vp[kResonance]);
        voiceLpL.set (cut, q, sr);
        voiceLpR.set (cut, q, sr);
    }

    if (type == ElementType::Pulse)
    {
        // Euclidean rhythm: hits spread as evenly as possible over 16 steps.
        const int hits = pulseHitsFrom (vp[kChar1]);
        if (hits != lastPulseHits)
        {
            for (int i = 0; i < 16; ++i)
                pulsePattern[(size_t) i] = ((i * hits) % 16) < hits;
            lastPulseHits = hits;
        }
    }
}

float Voice::pitchHz (const VoiceContext& ctx, int degree, int octave) const noexcept
{
    return midiToHz ((float) noteForDegree (*ctx.mood, degree, octave));
}

//==============================================================================
void Voice::onTick (const VoiceContext& ctx) noexcept
{
    if (! active)
        return;

    const bool newChord = ctx.chordChanged || needsInitialPitch || ctx.mood != lastMood;
    const float act = mods.activity;
    lastMood = ctx.mood;

    switch (type)
    {
        case ElementType::Drone:
        {
            // x picks the chord tone, y the register; re-evaluated each tick so dragging glides.
            static constexpr int degreeForZone[] = { 0, 4, 2, 4, 0 };
            const int zone = std::clamp ((int) (x * 4.999f), 0, 4);
            const int octave = (y < 0.33f ? 0 : (y < 0.66f ? -1 : -2)) + (zone == 4 ? 1 : 0) + octaveShift();
            droneTarget = pitchHz (ctx, ctx.chordRoot + degreeForZone[zone], octave);
            if (needsInitialPitch) droneFreq.reset (droneTarget);
            break;
        }

        case ElementType::Bowl:
        {
            if ((ctx.tick & 1) == 0 && rng.chance ((0.025f + 0.12f * act) * std::sqrt (size) * 2.0f * vp[kChar4]))
                strikeBowl (ctx);
            if (needsInitialPitch) strikeBowl (ctx);   // let a newly placed bowl sound straight away
            break;
        }

        case ElementType::Pluck:
        {
            if (motifPos < motifLength)
            {
                if (ctx.tick >= motif[(size_t) motifPos].tick)
                {
                    triggerPluck (ctx, motif[(size_t) motifPos].degree, rng.range (0.55f, 1.0f));
                    ++motifPos;
                }
            }
            else if ((ctx.tick & 1) == 0 && rng.chance ((0.02f + 0.14f * act) * 2.0f * vp[kChar3]))
            {
                motifLength = 3 + rng.nextInt (4);
                const int start = ctx.chordRoot + 2 * rng.nextInt (3);
                const int dir = rng.chance (0.5f) ? 1 : -1;
                const int stepSize = rng.chance (0.3f) ? 2 : 1;
                int spacing = vp[kChar4] < 0.33f ? 3 : (vp[kChar4] < 0.66f ? 2 : 1);
                if (rng.chance (0.2f)) spacing = std::clamp (spacing + (rng.chance (0.5f) ? 1 : -1), 1, 4);
                for (int i = 0; i < motifLength; ++i)
                {
                    const int turn = (i >= 3 && rng.chance (0.4f)) ? -2 * (i - 2) : 0;
                    motif[(size_t) i] = { start + dir * (i + turn) * stepSize, ctx.tick + i * spacing };
                }
                triggerPluck (ctx, motif[0].degree, rng.range (0.7f, 1.0f));
                motifPos = 1;
            }
            break;
        }

        case ElementType::Pulse:
        {
            const int step = (int) ((ctx.tick + pulseRotation) % 16);
            if (pulsePattern[(size_t) step] && rng.chance ((0.45f + 0.55f * act) * range (vp[kChar4], 0.1f, 1.9f)))
            {
                // A gentle arpeggio over the chord: root, fifth, third, fifth...
                static constexpr int seq[] = { 0, 4, 2, 4 };
                const int octave = (y < 0.4f ? 1 : (y > 0.75f ? -1 : 0)) + octaveShift();
                const float f = pitchHz (ctx, ctx.chordRoot + seq[pulseStep++ & 3], octave);
                pulseInc = f / sr;
                const float peak = rng.range (0.6f, 1.0f);
                pulseAttackRemaining = std::max (1, (int) (0.004f * sr));
                pulseAttackInc = (peak - pulseEnv) / (float) pulseAttackRemaining;
                pulseEnvMul = decayMultiplier (range (vp[kRelease], 0.12f, 1.6f) * (0.8f + 0.4f * size), sr);
                pulseFilter.set (f * (4.0f + 12.0f * mods.brightness), 0.8f, sr);
                activity = 1.0f;
            }
            break;
        }

        case ElementType::Chimes:
        {
            // The breeze picks up and dies down; it decides how often chimes are struck.
            if (needsInitialPitch || rng.chance (0.03f))
                breezeTarget = rng.range (0.05f, 1.0f) * (0.3f + 0.7f * vp[kChar1]);
            break;
        }

        case ElementType::Pad:
            if (newChord)
            {
                retunePad (ctx);
                padSwellTarget = 1.0f;
                activity = std::max (activity, 0.7f);
            }
            break;

        case ElementType::Shimmer:
        case ElementType::NumTypes:
            break;
    }

    needsInitialPitch = false;
}

void Voice::strikeBowl (const VoiceContext& ctx) noexcept
{
    BowlNote* note = &bowls[0];
    for (auto& b : bowls)
    {
        if (! b.on) { note = &b; break; }
        if (b.amp[0] * b.swell < note->amp[0] * note->swell) note = &b;
    }

    const int degree = ctx.chordRoot + 2 * rng.nextInt (3);
    const int octave = (y < 0.33f ? 1 : (y < 0.66f ? 0 : -1)) + octaveShift();
    const float f = pitchHz (ctx, degree, octave);

    // Crystal (pure, near-harmonic) <-> metal singing bowl (inharmonic, rich).
    static constexpr float crystalRatio[5] = { 1.0f, 2.0f, 3.01f, 4.0f, 5.02f };
    static constexpr float metalRatio[5]   = { 1.0f, 2.71f, 5.15f, 8.42f, 12.4f };
    static constexpr float crystalAmp[5]   = { 1.0f, 0.14f, 0.05f, 0.025f, 0.012f };
    static constexpr float metalAmp[5]     = { 1.0f, 0.55f, 0.35f, 0.2f, 0.12f };

    const float ring = vp[kChar1];
    const float beatHz = 0.15f + 2.2f * vp[kChar2] * vp[kChar2];
    const float swell = vp[kChar3];
    const float vel = rng.range (0.55f, 1.0f);
    const float t60 = range (vp[kRelease], 5.0f, 24.0f) * (1.0f + 0.5f * swell) * (0.8f + 0.2f * size);

    for (int k = 0; k < 5; ++k)
    {
        const float fk = f * lerp (crystalRatio[k], metalRatio[k], ring);
        const bool audible = fk < sr * 0.42f;
        const float beat = beatHz * (1.0f + 0.35f * (float) k) * (rng.chance (0.5f) ? 1.0f : -1.0f);
        note->incA[(size_t) k] = fk / sr;
        note->incB[(size_t) k] = (fk + beat) / sr;
        note->phA[(size_t) k] = rng.next01();
        note->phB[(size_t) k] = rng.next01();
        note->amp[(size_t) k] = audible ? vel * lerp (crystalAmp[k], metalAmp[k], ring) : 0.0f;
        note->ampMul[(size_t) k] = decayMultiplier (t60 / (1.0f + 0.9f * (float) k), sr);
    }

    // Swell: struck (a few ms) or sung, blooming in over a couple of seconds.
    note->swell = 0.0f;
    note->swellInc = 1.0f / ((0.004f + 2.4f * swell * swell) * sr);
    panGains (basePan() + rng.bipolar() * 0.25f, note->gl, note->gr);
    note->on = true;
    activity = 1.0f;
}

void Voice::triggerPluck (const VoiceContext& ctx, int degree, float velocity) noexcept
{
    auto& p = plucks[(size_t) (nextPluck++ % (int) plucks.size())];
    const int octave = (y < 0.33f ? 1 : (y < 0.66f ? 0 : -1)) + octaveShift();
    float f = pitchHz (ctx, degree, octave);
    while (f > sr * 0.2f) f *= 0.5f;

    p.inc = f / sr;
    p.phase = 0.0f;
    p.wave = vp[kChar1];
    p.snap = vp[kChar2];
    p.amp = velocity;
    p.ampMul = decayMultiplier (range (vp[kRelease], 0.35f, 4.5f) * (0.8f + 0.4f * size) * rng.range (0.85f, 1.15f), sr);
    p.env = 1.0f;
    p.envMul = decayMultiplier (0.08f + 0.5f * (1.0f - p.snap), sr);
    p.attack = 0.0f;
    p.attackInc = 1.0f / (0.002f * sr);
    p.cutoffBase = f * (1.2f + 4.0f * mods.brightness);
    p.filter.reset();
    p.counter = 0;
    panGains (basePan() + rng.bipolar() * 0.25f, p.gl, p.gr);
    p.on = true;
    activity = std::max (activity, velocity);
}

void Voice::strikeChime (const VoiceContext& ctx) noexcept
{
    auto& c = chimes[(size_t) (nextChime++ % (int) chimes.size())];

    // A chime set tuned to the current chord, with an occasional neighbouring scale tone.
    int degree = ctx.chordRoot + 2 * rng.nextInt (4);
    if (rng.chance (0.2f)) degree += 1;
    const int octave = (y < 0.33f ? 2 : (y < 0.66f ? 1 : 0)) + octaveShift();
    float f = pitchHz (ctx, degree, octave);
    while (f > sr * 0.12f) f *= 0.5f;

    static constexpr float softRatio[3]  = { 1.0f, 2.0f, 3.0f };
    static constexpr float metalRatio[3] = { 1.0f, 2.756f, 5.404f };
    static constexpr float softAmp[3]    = { 1.0f, 0.18f, 0.06f };
    static constexpr float metalAmp[3]   = { 1.0f, 0.6f, 0.35f };

    const float metal = vp[kChar3];
    const float vel = rng.range (0.35f, 1.0f);
    const float t60 = range (vp[kRelease], 1.0f, 7.0f) * (1.0f + 0.6f * metal);

    for (int k = 0; k < 3; ++k)
    {
        c.inc[(size_t) k] = f * lerp (softRatio[k], metalRatio[k], metal) / sr;
        c.ph[(size_t) k] = 0.0f;
        c.amp[(size_t) k] = vel * lerp (softAmp[k], metalAmp[k], metal);
        c.mul[(size_t) k] = decayMultiplier (t60 / (1.0f + 1.4f * (float) k), sr);
    }
    panGains (basePan() + rng.bipolar() * 0.9f * vp[kChar4], c.gl, c.gr);
    c.on = true;
    activity = std::max (activity, vel);
}

void Voice::spawnGrain (const VoiceContext& ctx) noexcept
{
    for (auto& g : grains)
    {
        if (g.on) continue;

        const int octave = 2 + (rng.chance (0.1f + 0.6f * vp[kChar4]) ? 1 : 0) - (y > 0.6f ? 1 : 0) + octaveShift();
        float f = pitchHz (ctx, ctx.chordRoot + 2 * rng.nextInt (4), octave) * (1.0f + rng.bipolar() * 0.002f);
        while (f > sr * 0.4f) f *= 0.5f;

        g.on = true;
        g.inc = f / sr;
        g.phase = 0.0f;
        g.pos = 0.0f;
        g.posInc = 1.0f / (rng.range (0.25f, 1.1f) * range (vp[kChar2], 0.25f, 2.2f) * sr);
        g.amp = rng.range (0.4f, 1.0f);
        panGains (basePan() + rng.bipolar() * vp[kChar3], g.gl, g.gr);
        activity = std::max (activity, 0.6f * g.amp);
        return;
    }
}

void Voice::retunePad (const VoiceContext& ctx) noexcept
{
    // Close triad .. wide open voicing with added 7th/9th colours.
    static constexpr int degrees[5] = { 0, 2, 4, 6, 8 };
    padVoices = 3 + (int) std::lround (vp[kChar3] * 2.0f);
    const bool open = vp[kChar3] > 0.5f;
    const int baseOct = (y < 0.33f ? 0 : (y < 0.66f ? -1 : -2)) + octaveShift();

    for (int k = 0; k < (int) pad.size(); ++k)
    {
        const int oct = baseOct + ((open && (k & 1)) ? 1 : 0);
        auto& v = pad[(size_t) k];
        v.target = pitchHz (ctx, ctx.chordRoot + degrees[k], oct);
        if (needsInitialPitch || v.freq.z < 1.0f) v.freq.reset (v.target);
        const float spread = ((float) k / 4.0f) * 2.0f - 1.0f;
        panGains (basePan() * 0.5f + spread * 0.7f, v.gl, v.gr);
    }
}

//==============================================================================
void Voice::render (const VoiceBuses& b, int start, int n, const VoiceContext& ctx) noexcept
{
    if (! sounding || ctx.mood == nullptr)
        return;

    fade.coeff = active ? fadeInCoeff : fadeOutCoeff;
    const float g = advance (fade, active ? 1.0f : 0.0f, n);
    const float level = advance (levelSmooth, size, n);
    const float pan = advance (panSmooth, basePan(), n);
    panGains (pan, curGl, curGr);

    curRev = sendRev;
    curDly = sendDly;

    const float gain = g * typeGain * std::pow (level, 1.2f) * levelGainFrom (vp[kLevel]);

    switch (type)
    {
        case ElementType::Drone:   renderDrone (b, start, n, gain); break;
        case ElementType::Bowl:    renderBowl (b, start, n, gain); break;
        case ElementType::Pluck:   renderPluck (b, start, n, gain); break;
        case ElementType::Pulse:   renderPulse (b, start, n, gain); break;
        case ElementType::Shimmer: renderShimmer (b, start, n, gain, ctx); break;
        case ElementType::Chimes:  renderChimes (b, start, n, gain, ctx); break;
        case ElementType::Pad:     renderPad (b, start, n, gain); break;
        case ElementType::NumTypes: break;
    }

    activity *= std::pow (activityDecay, (float) n);

    if (! active && fade.z < 1.0e-4f)
        sounding = false;
}

void Voice::renderDrone (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    static constexpr float baseDetune[4] = { 0.9948f, 0.9983f, 1.0023f, 1.0058f };
    float detune[4];
    for (int k = 0; k < 4; ++k)
        detune[k] = 1.0f + (baseDetune[k] - 1.0f) * 2.4f * vp[kChar1];

    const float subGain = 1.1f * vp[kChar2];
    const float shimmerGain = 0.35f * vp[kChar3];
    const float motion = vp[kChar4];
    const float breathDepth = 0.12f + 0.4f * motion;
    const float bright = mods.brightness;

    const float f0 = droneFreq.z > 1.0f ? droneFreq.z : droneTarget;
    const float lfo = std::sin (droneLfo);
    // Darker, softer filter than a plain saw drone: the airy colour comes from the shimmer partials.
    const float cutoff = (f0 * (1.0f + 4.5f * bright * bright) + 90.0f) * (1.0f + (0.15f + 0.6f * motion) * lfo);
    droneFilterL.set (cutoff, 0.7f, sr);
    droneFilterR.set (cutoff * 1.06f, 0.7f, sr);

    for (int i = start; i < start + n; ++i)
    {
        const float f = droneFreq.process (droneTarget);
        const float inc = f / sr;
        const float l = saws[0].process (inc * detune[0]) + saws[2].process (inc * detune[2]);
        const float r = saws[1].process (inc * detune[1]) + saws[3].process (inc * detune[3]);
        const float sub = droneSub.process (inc * 0.5f) * subGain;

        const float breath = 1.0f - breathDepth + breathDepth * std::sin (droneBreath);
        droneBreath += droneBreathInc;
        if (droneBreath > twoPi) droneBreath -= twoPi;

        // Airy partials an octave and a fifth above, each fading in and out on its own slow cycle.
        shimLfo += 0.07f * twoPi / sr;
        if (shimLfo > twoPi) shimLfo -= twoPi;
        const float sOct = shimOct.process (inc * 2.0f) * (0.5f + 0.5f * std::sin (shimLfo));
        const float sFifth = shimFifth.process (inc * 3.0f) * (0.5f + 0.5f * std::sin (shimLfo * 1.3f + 2.0f));
        const float shimmer = (sOct + 0.6f * sFifth) * shimmerGain;

        const float outL = (droneFilterL.lowpass (l * 0.45f + sub) + shimmer) * breath * gain;
        const float outR = (droneFilterR.lowpass (r * 0.45f + sub) + shimmer * 0.9f) * breath * gain;
        emit (b, i, outL * curGl * 1.41f, outR * curGr * 1.41f);
    }

    droneLfo += droneLfoInc * (float) n * (0.5f + motion);
    if (droneLfo > twoPi) droneLfo -= twoPi;
    activity = std::max (activity, 0.35f + 0.4f * (0.5f + 0.5f * std::sin (droneBreath)));
}

void Voice::renderBowl (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    for (auto& note : bowls)
    {
        if (! note.on) continue;

        for (int i = start; i < start + n; ++i)
        {
            note.swell = std::min (1.0f, note.swell + note.swellInc);
            float s = 0.0f;
            for (size_t k = 0; k < 5; ++k)
            {
                if (note.amp[k] < 1.0e-5f) continue;
                s += (std::sin (twoPi * note.phA[k]) + std::sin (twoPi * note.phB[k])) * 0.5f * note.amp[k];
                note.phA[k] = wrap (note.phA[k] + note.incA[k]);
                note.phB[k] = wrap (note.phB[k] + note.incB[k]);
                note.amp[k] *= note.ampMul[k];
            }
            s *= note.swell * gain;
            emit (b, i, s * note.gl, s * note.gr);
        }

        if (note.amp[0] < 1.0e-4f)
            note.on = false;
    }
}

void Voice::renderPluck (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    for (auto& p : plucks)
    {
        if (! p.on) continue;

        for (int i = start; i < start + n; ++i)
        {
            // Filter envelope: the "snap" opens the filter at the attack and closes it quickly.
            if ((p.counter++ & 15) == 0)
            {
                const float cutoff = p.cutoffBase + p.snap * 14000.0f * p.env * p.env + 200.0f * p.env;
                p.filter.set (cutoff, 0.8f + 2.5f * p.snap * p.env, sr);
            }

            p.attack = std::min (1.0f, p.attack + p.attackInc);
            const float osc = morphOsc (p.phase, p.inc, p.wave);
            p.phase = wrap (p.phase + p.inc);

            const float s = p.filter.lowpass (osc) * p.amp * p.attack * gain;
            p.amp *= p.ampMul;
            p.env *= p.envMul;
            emit (b, i, s * p.gl, s * p.gr);
        }

        if (p.amp < 1.0e-4f)
            p.on = false;
    }
}

void Voice::renderPulse (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    // Pulse wave from two band-limited saws; its width breathes with a slow PWM LFO.
    const float baseWidth = range (vp[kChar2], 0.5f, 0.08f);
    const float pwmDepth = 0.85f * vp[kChar3];
    const float pwmRate = 0.2f + 2.5f * vp[kChar3];

    for (int i = start; i < start + n; ++i)
    {
        if (pulseAttackRemaining > 0) { pulseEnv += pulseAttackInc; --pulseAttackRemaining; }
        else                          { pulseEnv *= pulseEnvMul; }

        pwmLfo += twoPi * pwmRate / sr;
        if (pwmLfo > twoPi) pwmLfo -= twoPi;
        const float width = std::clamp (baseWidth + pwmDepth * baseWidth * std::sin (pwmLfo), 0.03f, 0.97f);

        auto saw = [this] (float p) { return 2.0f * p - 1.0f - BlepSaw::blep (p, pulseInc); };
        const float pulse = 0.5f * (saw (pulsePhase) - saw (wrap (pulsePhase + width)));
        pulsePhase = wrap (pulsePhase + pulseInc);

        const float v = pulseFilter.lowpass (pulse) * pulseEnv * gain;
        emit (b, i, v * curGl * 1.41f, v * curGr * 1.41f);
    }
}

void Voice::renderShimmer (const VoiceBuses& b, int start, int n, float gain, const VoiceContext& ctx) noexcept
{
    const float rate = (1.0f + 7.0f * mods.activity) * range (vp[kChar1], 0.1f, 1.9f);

    for (int i = start; i < start + n; ++i)
    {
        if (active && --grainCountdown <= 0.0f)
        {
            spawnGrain (ctx);
            grainCountdown = sr / rate * -std::log (std::max (rng.next01(), 1.0e-4f));
        }

        float l = 0.0f, r = 0.0f;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            const float w = std::sin (pi * g.pos);
            const float s = std::sin (twoPi * g.phase) * w * w * g.amp;
            g.phase = wrap (g.phase + g.inc);
            g.pos += g.posInc;
            if (g.pos >= 1.0f) g.on = false;
            l += s * g.gl;
            r += s * g.gr;
        }

        emit (b, i, l * gain, r * gain);
    }
}

void Voice::renderChimes (const VoiceBuses& b, int start, int n, float gain, const VoiceContext& ctx) noexcept
{
    const float wind = advance (breeze, breezeTarget, n);
    const float strikesPerSecond = wind * (0.4f + 5.0f * vp[kChar1]) * (0.4f + mods.activity);

    for (int i = start; i < start + n; ++i)
    {
        // Gusts set chimes swinging; a cluster rings out with small, uneven gaps.
        if (active)
        {
            if (clusterLeft > 0)
            {
                if ((clusterGap -= 1.0f) <= 0.0f)
                {
                    strikeChime (ctx);
                    --clusterLeft;
                    clusterGap = rng.range (0.04f, 0.22f) * sr;
                }
            }
            else if ((chimeCountdown -= 1.0f) <= 0.0f)
            {
                clusterLeft = 1 + (int) std::lround (rng.next01() * 5.0f * vp[kChar2]);
                clusterGap = 0.0f;
                chimeCountdown = sr / std::max (0.05f, strikesPerSecond) * -std::log (std::max (rng.next01(), 1.0e-4f));
            }
        }

        float l = 0.0f, r = 0.0f;
        for (auto& c : chimes)
        {
            if (! c.on) continue;
            float s = 0.0f;
            for (size_t k = 0; k < 3; ++k)
            {
                s += std::sin (twoPi * c.ph[k]) * c.amp[k];
                c.ph[k] = wrap (c.ph[k] + c.inc[k]);
                c.amp[k] *= c.mul[k];
            }
            if (c.amp[0] < 1.0e-4f) c.on = false;
            l += s * c.gl;
            r += s * c.gr;
        }

        emit (b, i, l * gain, r * gain);
    }
}

void Voice::renderPad (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    const float detuneCents = 4.0f + 22.0f * vp[kChar1];
    const float warmth = vp[kChar2];
    const float motion = vp[kChar4];
    const float bright = mods.brightness;

    // Swell after each chord change, then settle; Motion adds slow breathing.
    if (padSwell.z > 0.95f * padSwellTarget) padSwellTarget = 0.7f;
    const float swell = advance (padSwell, padSwellTarget, n);
    padLfo += padLfoInc * (float) n * (0.5f + motion);
    if (padLfo > twoPi) padLfo -= twoPi;
    const float lfo = std::sin (padLfo);

    const float root = pad[0].freq.z > 1.0f ? pad[0].freq.z : 220.0f;
    const float cutoff = root * (1.5f + 9.0f * bright * (1.0f - 0.6f * warmth)) * (1.0f + 0.5f * motion * lfo) + 150.0f;
    padFilterL.set (cutoff, 0.75f, sr);
    padFilterR.set (cutoff * 1.04f, 0.75f, sr);

    const float ratioUp = std::exp2 (detuneCents / 1200.0f), ratioDown = 1.0f / ratioUp;
    const float amp = swell * (1.0f - 0.25f * motion * (0.5f + 0.5f * std::sin (padLfo * 0.7f + 1.0f)));
    const float voiceNorm = 1.0f / std::sqrt ((float) padVoices);

    for (int i = start; i < start + n; ++i)
    {
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < padVoices; ++k)
        {
            auto& v = pad[(size_t) k];
            const float f = v.freq.process (v.target);
            const float inc = f / sr;
            const float saw = 0.5f * (v.saw[0].process (inc * ratioUp) + v.saw[1].process (inc * ratioDown));
            const float tri = 1.0f - 4.0f * std::abs (v.triPhase - 0.5f);
            v.triPhase = wrap (v.triPhase + inc);
            const float s = lerp (saw, tri, warmth * 0.85f);
            l += s * v.gl;
            r += s * v.gr;
        }
        const float outL = padFilterL.lowpass (l * voiceNorm) * amp * gain;
        const float outR = padFilterR.lowpass (r * voiceNorm) * amp * gain;
        emit (b, i, outL * 1.41f, outR * 1.41f);
    }

    activity = std::max (activity, 0.3f + 0.4f * swell);
}

} // namespace scape
