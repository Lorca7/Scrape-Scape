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
}

void Voice::prepare (float sampleRate)
{
    sr = sampleRate;

    const int maxPluckDelay = (int) std::ceil (sr / 50.0f) + 8;
    for (auto& s : strings)
        s.line.allocate (maxPluckDelay);

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
    sounding = ! getElementInfo (type).isModifier;
    needsInitialPitch = true;
    activity = 0.0f;

    fade.reset (0.0f);
    levelSmooth.setTime (0.08f, sr);
    levelSmooth.reset (size);
    panSmooth.setTime (0.08f, sr);
    panSmooth.reset (basePan());

    switch (type)
    {
        case ElementType::Drone:   typeGain = 0.10f; sendRev = 0.55f; sendDly = 0.05f; break;
        case ElementType::Bell:    typeGain = 0.22f; sendRev = 0.60f; sendDly = 0.30f; break;
        case ElementType::Pluck:   typeGain = 0.60f; sendRev = 0.45f; sendDly = 0.35f; break;
        case ElementType::Bass:    typeGain = 0.13f; sendRev = 0.12f; sendDly = 0.00f; break;
        case ElementType::Shimmer: typeGain = 0.07f; sendRev = 0.85f; sendDly = 0.20f; break;
        case ElementType::Wind:    typeGain = 0.30f; sendRev = 0.50f; sendDly = 0.05f; break;
        case ElementType::Pulse:   typeGain = 0.36f; sendRev = 0.35f; sendDly = 0.50f; break;
        case ElementType::Shade:
        case ElementType::Spark:
        case ElementType::NumTypes: typeGain = 0.0f; sendRev = 0.0f; sendDly = 0.0f; break;
    }

    // Drone
    for (auto& s : saws) s.phase = rng.next01();
    droneSub.phase = 0.0f;
    droneFilterL.reset(); droneFilterR.reset();
    droneFreq.setTime (1.4f, sr);
    droneFreq.reset (0.0f);
    droneLfo = rng.next01() * twoPi;
    droneLfoInc = twoPi * rng.range (0.02f, 0.07f) / sr;
    droneBreath = rng.next01() * twoPi;
    droneBreathInc = twoPi * rng.range (0.04f, 0.11f) / sr;

    // Bell
    for (auto& b : bells) b.on = false;
    static constexpr float ratios[] = { 3.5f, 2.0f, 1.4142f, 2.76f, 4.0f };
    bellRatio = ratios[rng.nextInt (5)];

    // Pluck
    for (auto& s : strings) { s.on = false; s.line.clear(); s.level = 0.0f; s.last = 0.0f; s.excite = 0; }
    motifLength = motifPos = nextString = 0;

    // Bass
    bassOsc.phase = 0.0f;
    bassFreq.setTime (0.08f, sr);
    bassFreq.reset (0.0f);
    bassEnv.setTime (0.9f, sr);
    bassEnv.reset (0.0f);
    bassEnvTarget = 0.0f;
    bassHoldTicks = 0;
    bassReturnTick = -1;

    // Shimmer
    for (auto& g : grains) g.on = false;
    grainCountdown = rng.range (0.0f, 0.3f) * sr;

    // Wind
    windBandL.reset(); windBandR.reset(); windAir.reset();
    windCentreL.setTime (3.0f, sr); windCentreR.setTime (3.5f, sr);
    windCentreL.reset (0.0f); windCentreR.reset (0.0f);
    windGust.setTime (1.8f, sr); windGust.reset (0.0f);
    windGustTarget = 0.6f;

    // Pulse
    {
        const int hits = 3 + rng.nextInt (5);
        for (int i = 0; i < 16; ++i)
            pulsePattern[(size_t) i] = ((i * hits) % 16) < hits;
        pulseRotation = rng.nextInt (16);
        pulseOsc.phase = 0.0f;
        pulseFilter.reset();
        pulseEnv = 0.0f;
        pulseAttackRemaining = 0;
        pulseStep = 0;
    }
}

void Voice::update (const ElementSnapshot& e, const VoiceMods& m) noexcept
{
    active = e.active;
    x = e.x; y = e.y; size = e.size;
    mods = m;

    if (getElementInfo (type).isModifier)
        sounding = active;
}

float Voice::pitchHz (const VoiceContext& ctx, int degree, int octave) const noexcept
{
    return midiToHz ((float) noteForDegree (*ctx.mood, degree, octave));
}

//==============================================================================
void Voice::onTick (const VoiceContext& ctx) noexcept
{
    if (! active || getElementInfo (type).isModifier)
        return;

    const bool newChord = ctx.chordChanged || needsInitialPitch || ctx.mood != lastMood;
    const float act = mods.activity;
    lastMood = ctx.mood;
    lastChordRoot = ctx.chordRoot;

    switch (type)
    {
        case ElementType::Drone:
        {
            // x picks which chord tone, y picks the register. Re-evaluated every tick so
            // dragging the element glides it to a new pitch.
            static constexpr int degreeForZone[] = { 0, 4, 2, 4, 0 };   // root, 5th, 3rd, 5th, root+8va
            const int zone = std::clamp ((int) (x * 4.999f), 0, 4);
            const int octave = (y < 0.33f ? 0 : (y < 0.66f ? -1 : -2)) + (zone == 4 ? 1 : 0);
            droneTarget = pitchHz (ctx, ctx.chordRoot + degreeForZone[zone], octave);
            if (needsInitialPitch) droneFreq.reset (droneTarget);
            break;
        }

        case ElementType::Bell:
        {
            if ((ctx.tick & 1) == 0 && rng.chance ((0.04f + 0.24f * act) * std::sqrt (size)))
                triggerBell (ctx);
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
            else if ((ctx.tick & 1) == 0 && rng.chance (0.02f + 0.14f * act))
            {
                motifLength = 3 + rng.nextInt (4);
                const int start = ctx.chordRoot + 2 * rng.nextInt (3);
                const int dir = rng.chance (0.5f) ? 1 : -1;
                const int stepSize = rng.chance (0.3f) ? 2 : 1;
                const int spacing = rng.chance (0.25f) ? 1 : (rng.chance (0.2f) ? 3 : 2);
                for (int i = 0; i < motifLength; ++i)
                {
                    const int turn = (i >= 3 && rng.chance (0.4f)) ? -2 * (i - 2) : 0;
                    motif[(size_t) i] = { start + dir * (i + turn) * stepSize, ctx.tick + i * spacing };
                }
                motifPos = 0;
                triggerPluck (ctx, motif[0].degree, rng.range (0.7f, 1.0f));
                motifPos = 1;
            }
            break;
        }

        case ElementType::Bass:
        {
            const int octave = y < 0.4f ? -1 : -2;
            ++bassHoldTicks;

            if (newChord)
            {
                bassTarget = pitchHz (ctx, ctx.chordRoot, octave);
                if (needsInitialPitch) bassFreq.reset (bassTarget);
                bassEnvTarget = 1.0f;
                bassEnv.setTime (0.9f, sr);
                bassHoldTicks = 0;
                activity = 1.0f;
            }
            else if (bassHoldTicks == 24)
            {
                bassEnvTarget = 0.6f;
                bassEnv.setTime (4.0f, sr);
            }

            if ((ctx.tick % 16) == 8 && rng.chance (0.05f + 0.2f * act))
            {
                bassTarget = pitchHz (ctx, ctx.chordRoot + 4, octave);
                bassReturnTick = ctx.tick + 4;
                activity = 0.8f;
            }
            else if (ctx.tick == bassReturnTick)
            {
                bassTarget = pitchHz (ctx, ctx.chordRoot, octave);
            }
            break;
        }

        case ElementType::Shimmer:
            break; // grains are spawned continuously in render()

        case ElementType::Wind:
        {
            if (newChord || rng.chance (0.03f))
            {
                static constexpr int harmonics[] = { 2, 3, 4, 5, 6 };
                const int oct = y < 0.5f ? 0 : -1;
                const float baseL = pitchHz (ctx, ctx.chordRoot + 2 * rng.nextInt (3), oct);
                const float baseR = pitchHz (ctx, ctx.chordRoot + 2 * rng.nextInt (3), oct);
                windTargetL = std::clamp (baseL * (float) harmonics[rng.nextInt (5)] * 0.5f, 150.0f, 6000.0f);
                windTargetR = std::clamp (baseR * (float) harmonics[rng.nextInt (5)] * 0.5f, 150.0f, 6000.0f);
                if (needsInitialPitch) { windCentreL.reset (windTargetL); windCentreR.reset (windTargetR); }
            }
            if (rng.chance (0.04f + 0.04f * act))
                windGustTarget = rng.range (0.2f, 1.0f);
            break;
        }

        case ElementType::Pulse:
        {
            const int step = (int) ((ctx.tick + pulseRotation) % 16);
            if (pulsePattern[(size_t) step] && rng.chance (0.45f + 0.55f * act))
            {
                static constexpr int seq[] = { 0, 2, 1, 2 };
                const int octave = y < 0.4f ? 1 : (y > 0.75f ? -1 : 0);
                const float f = pitchHz (ctx, ctx.chordRoot + 2 * seq[pulseStep++ & 3], octave);
                pulseInc = f / sr;
                const float peak = rng.range (0.6f, 1.0f);
                pulseAttackRemaining = std::max (1, (int) (0.004f * sr));
                pulseAttackInc = (peak - pulseEnv) / (float) pulseAttackRemaining;
                pulseEnvMul = decayMultiplier (0.25f + 0.45f * (1.0f - act) * size, sr);
                pulseFilter.set (f * (2.0f + 6.0f * mods.brightness), 0.7f, sr);
                activity = 1.0f;
            }
            break;
        }

        case ElementType::Shade:
        case ElementType::Spark:
        case ElementType::NumTypes:
            break;
    }

    needsInitialPitch = false;
}

void Voice::triggerBell (const VoiceContext& ctx) noexcept
{
    FmNote* note = &bells[0];
    for (auto& b : bells)
    {
        if (! b.on) { note = &b; break; }
        if (b.amp < note->amp) note = &b;
    }

    int degree = ctx.chordRoot + 2 * rng.nextInt (3);
    if (rng.chance (0.25f)) degree += 1;
    const int octave = y < 0.33f ? 2 : (y < 0.66f ? 1 : 0);
    const float f = pitchHz (ctx, degree, octave);
    const float vel = rng.range (0.45f, 1.0f);
    const float decay = rng.range (2.5f, 6.5f) * (0.7f + 0.3f * size);

    note->on = true;
    note->carInc = f / sr;
    note->modInc = f * bellRatio / sr;
    note->cPhase = note->mPhase = 0.0f;
    note->amp = vel;
    note->ampMul = decayMultiplier (decay, sr);
    note->index = (0.6f + 2.6f * mods.brightness) * vel;
    note->indexMul = decayMultiplier (decay * 0.3f, sr);
    panGains (basePan() + rng.bipolar() * 0.25f, note->gl, note->gr);
    activity = 1.0f;
}

void Voice::triggerPluck (const VoiceContext& ctx, int degree, float velocity) noexcept
{
    auto& s = strings[(size_t) (nextString++ & 3)];
    const int octave = y < 0.33f ? 1 : (y < 0.66f ? 0 : -1);
    const float f = std::clamp (pitchHz (ctx, degree, octave), 60.0f, 2000.0f);

    s.line.clear();
    s.delay = std::clamp (sr / f - 0.5f, 2.0f, (float) s.line.buf.size() - 4.0f);
    const float t60 = rng.range (1.8f, 4.5f) * (0.8f + 0.4f * size);
    s.loopGain = std::pow (10.0f, -3.0f * s.delay / (t60 * sr));
    s.excite = (int) s.delay;
    s.exciteLp.setCutoff (900.0f + 7000.0f * mods.brightness * velocity, sr);
    s.exciteLp.reset();
    s.velocity = velocity;
    s.last = 0.0f;
    s.level = velocity;
    s.on = true;
    panGains (basePan() + rng.bipolar() * 0.2f, s.gl, s.gr);
    activity = std::max (activity, velocity);
}

void Voice::spawnGrain (const VoiceContext& ctx) noexcept
{
    for (auto& g : grains)
    {
        if (g.on) continue;

        int octave = 2 + (rng.chance (0.2f + 0.4f * mods.brightness) ? 1 : 0) - (y > 0.6f ? 1 : 0);
        float f = pitchHz (ctx, ctx.chordRoot + 2 * rng.nextInt (4), octave) * (1.0f + rng.bipolar() * 0.002f);
        while (f > sr * 0.4f) f *= 0.5f;

        g.on = true;
        g.inc = f / sr;
        g.phase = 0.0f;
        g.pos = 0.0f;
        g.posInc = 1.0f / (rng.range (0.25f, 1.1f) * sr);
        g.amp = rng.range (0.4f, 1.0f);
        panGains (basePan() + rng.bipolar() * 0.5f, g.gl, g.gr);
        activity = std::max (activity, 0.6f * g.amp);
        return;
    }
}

//==============================================================================
void Voice::render (const VoiceBuses& b, int start, int n, const VoiceContext& ctx) noexcept
{
    if (! sounding || getElementInfo (type).isModifier || ctx.mood == nullptr)
        return;

    fade.coeff = active ? fadeInCoeff : fadeOutCoeff;
    const float g = advance (fade, active ? 1.0f : 0.0f, n);
    const float level = advance (levelSmooth, size, n);
    const float pan = advance (panSmooth, basePan(), n);
    panGains (pan, curGl, curGr);

    curRev = sendRev + 0.45f * mods.reverbBoost;
    curDly = sendDly;

    const float gain = g * typeGain * std::pow (level, 1.2f);

    switch (type)
    {
        case ElementType::Drone:   renderDrone (b, start, n, gain); break;
        case ElementType::Bell:    renderBell (b, start, n, gain); break;
        case ElementType::Pluck:   renderPluck (b, start, n, gain); break;
        case ElementType::Bass:    renderBass (b, start, n, gain); break;
        case ElementType::Shimmer: renderShimmer (b, start, n, gain, ctx); break;
        case ElementType::Wind:    renderWind (b, start, n, gain); break;
        case ElementType::Pulse:   renderPulse (b, start, n, gain); break;
        case ElementType::Shade:
        case ElementType::Spark:
        case ElementType::NumTypes: break;
    }

    activity *= std::pow (activityDecay, (float) n);

    if (! active && fade.z < 1.0e-4f)
        sounding = false;
}

void Voice::renderDrone (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    static constexpr float detune[4] = { 0.9948f, 0.9983f, 1.0023f, 1.0058f };
    const float bright = mods.brightness;

    const float f0 = droneFreq.z > 1.0f ? droneFreq.z : droneTarget;
    const float lfo = std::sin (droneLfo);
    const float cutoff = (f0 * (1.2f + 7.0f * bright * bright) + 120.0f) * (1.0f + 0.35f * lfo);
    droneFilterL.set (cutoff, 0.8f, sr);
    droneFilterR.set (cutoff * 1.07f, 0.8f, sr);

    for (int i = start; i < start + n; ++i)
    {
        const float f = droneFreq.process (droneTarget);
        const float inc = f / sr;
        const float l = saws[0].process (inc * detune[0]) + saws[2].process (inc * detune[2]);
        const float r = saws[1].process (inc * detune[1]) + saws[3].process (inc * detune[3]);
        const float sub = droneSub.process (inc * 0.5f) * 0.6f;
        const float breath = 0.7f + 0.3f * std::sin (droneBreath);
        droneBreath += droneBreathInc;
        if (droneBreath > twoPi) droneBreath -= twoPi;

        const float outL = droneFilterL.lowpass (l * 0.5f + sub) * breath * gain;
        const float outR = droneFilterR.lowpass (r * 0.5f + sub) * breath * gain;
        emit (b, i, outL * curGl * 1.41f, outR * curGr * 1.41f);
    }

    droneLfo += droneLfoInc * (float) n;
    if (droneLfo > twoPi) droneLfo -= twoPi;
    activity = std::max (activity, 0.35f + 0.4f * (0.5f + 0.5f * std::sin (droneBreath)));
}

void Voice::renderBell (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    for (auto& note : bells)
    {
        if (! note.on) continue;

        for (int i = start; i < start + n; ++i)
        {
            const float m = std::sin (twoPi * note.mPhase) * note.index;
            const float s = std::sin (twoPi * note.cPhase + m) * note.amp * gain;
            note.cPhase += note.carInc; if (note.cPhase >= 1.0f) note.cPhase -= 1.0f;
            note.mPhase += note.modInc; if (note.mPhase >= 1.0f) note.mPhase -= 1.0f;
            note.amp *= note.ampMul;
            note.index *= note.indexMul;
            emit (b, i, s * note.gl, s * note.gr);
        }

        if (note.amp < 1.0e-4f)
            note.on = false;
    }
}

void Voice::renderPluck (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    for (auto& s : strings)
    {
        if (! s.on) continue;

        for (int i = start; i < start + n; ++i)
        {
            const float out = s.line.read (s.delay);
            float in = 0.0f;
            if (s.excite > 0)
            {
                in = s.exciteLp.process (rng.bipolar()) * s.velocity;
                --s.excite;
            }
            const float avg = 0.5f * (out + s.last);
            s.last = out;
            s.line.push (in + avg * s.loopGain);
            s.level = std::max (std::abs (out), s.level * 0.9997f);

            const float v = out * gain;
            emit (b, i, v * s.gl, v * s.gr);
        }

        if (s.excite == 0 && s.level < 1.0e-4f)
            s.on = false;
    }
}

void Voice::renderBass (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    float gl, gr;
    panGains ((x * 2.0f - 1.0f) * 0.25f, gl, gr);

    for (int i = start; i < start + n; ++i)
    {
        const float f = bassFreq.process (bassTarget);
        const float env = bassEnv.process (bassEnvTarget);
        const float p = twoPi * bassOsc.phase;
        bassOsc.phase += f / sr;
        if (bassOsc.phase >= 1.0f) bassOsc.phase -= 1.0f;

        const float s = std::tanh (1.3f * (std::sin (p) + 0.25f * std::sin (2.0f * p))) * env * gain;
        emit (b, i, s * gl, s * gr);
    }

    activity = std::max (activity, bassEnv.z * 0.6f);
}

void Voice::renderShimmer (const VoiceBuses& b, int start, int n, float gain, const VoiceContext& ctx) noexcept
{
    const float rate = 1.0f + 7.0f * mods.activity;

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
            g.phase += g.inc; if (g.phase >= 1.0f) g.phase -= 1.0f;
            g.pos += g.posInc;
            if (g.pos >= 1.0f) g.on = false;
            l += s * g.gl;
            r += s * g.gr;
        }

        emit (b, i, l * gain, r * gain);
    }
}

void Voice::renderWind (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    const float q = 9.0f + 10.0f * (1.0f - mods.brightness);
    const float cl = advance (windCentreL, windTargetL, n);
    const float cr = advance (windCentreR, windTargetR, n);
    windBandL.set (cl, q, sr);
    windBandR.set (cr, q, sr);
    windAir.set (350.0f + 2500.0f * mods.brightness, 0.6f, sr);

    const float normL = 0.22f * std::sqrt ((sr * 0.5f) / (cl / q));
    const float normR = 0.22f * std::sqrt ((sr * 0.5f) / (cr / q));

    for (int i = start; i < start + n; ++i)
    {
        const float gust = windGust.process (windGustTarget);
        const float nl = rng.bipolar(), nr = rng.bipolar();
        const float air = windAir.lowpass ((nl + nr) * 0.5f) * 0.25f;
        const float l = (windBandL.bandpass (nl) * normL + air) * gust * gain;
        const float r = (windBandR.bandpass (nr) * normR + air) * gust * gain;
        emit (b, i, l * curGl * 1.41f, r * curGr * 1.41f);
    }

    activity = std::max (activity, windGust.z * 0.7f);
}

void Voice::renderPulse (const VoiceBuses& b, int start, int n, float gain) noexcept
{
    for (int i = start; i < start + n; ++i)
    {
        if (pulseAttackRemaining > 0) { pulseEnv += pulseAttackInc; --pulseAttackRemaining; }
        else                          { pulseEnv *= pulseEnvMul; }

        const float s = std::tanh (2.5f * pulseOsc.process (pulseInc)) * 0.6f;
        const float v = pulseFilter.lowpass (s) * pulseEnv * gain;
        emit (b, i, v * curGl * 1.41f, v * curGr * 1.41f);
    }
}

} // namespace scape
