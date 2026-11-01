#pragma once

#include "Dsp.h"
#include "Elements.h"
#include "Moods.h"
#include "VoiceParams.h"

#include <array>

namespace scape
{

// Shared musical state handed to every voice by the engine's conductor.
struct VoiceContext
{
    const Mood* mood = nullptr;
    float sampleRate = 44100.0f;
    float samplesPerTick = 11025.0f;   // one tick = a sixteenth note
    long long tick = 0;
    int chordRoot = 0;                 // scale degree of the current chord's root
    bool chordChanged = false;
};

// Per-voice influence of nearby Shade/Spark elements, combined with global controls.
struct VoiceMods
{
    float brightness = 0.6f;   // 0..1
    float activity = 0.5f;     // 0..1
    float reverbBoost = 0.0f;  // 0..1
};

struct VoiceBuses
{
    float* dryL; float* dryR;
    float* revL; float* revR;
    float* dlyL; float* dlyR;
};

// One generator per canvas slot. It owns the state for every element type so that
// nothing needs to be allocated on the audio thread when a type changes.
class Voice
{
public:
    void prepare (float sampleRate);
    void begin (const ElementSnapshot& e);
    void update (const ElementSnapshot& e, const VoiceMods& mods, const VoiceParamArray& params) noexcept;
    void onTick (const VoiceContext& ctx) noexcept;
    void render (const VoiceBuses& buses, int start, int numSamples, const VoiceContext& ctx) noexcept;

    bool isSounding() const noexcept    { return sounding; }
    float getActivity() const noexcept  { return activity; }
    std::uint32_t getSeed() const noexcept { return seed; }
    int getType() const noexcept        { return (int) type; }

private:
    // ---- per-type helpers -------------------------------------------------
    struct FmNote
    {
        bool on = false;
        float carInc = 0, modInc = 0, cPhase = 0, mPhase = 0;
        float amp = 0, ampMul = 0, index = 0, indexMul = 0;
        float gl = 0, gr = 0;
    };

    struct PluckString
    {
        dsp::DelayLine line;
        dsp::OnePole exciteLp;
        bool on = false;
        float delay = 100, loopGain = 0.99f, last = 0, level = 0, gl = 0, gr = 0, velocity = 1;
        int excite = 0;
    };

    struct Grain
    {
        bool on = false;
        float inc = 0, phase = 0, pos = 0, posInc = 0, amp = 0, gl = 0, gr = 0;
    };

    struct PluckEvent { int degree; long long tick; };

    float chordToneDegree (const VoiceContext& ctx, int k) const noexcept { return (float) (ctx.chordRoot + 2 * k); }
    float pitchHz (const VoiceContext& ctx, int degree, int octave) const noexcept;
    void triggerBell (const VoiceContext& ctx) noexcept;
    void triggerPluck (const VoiceContext& ctx, int degree, float velocity) noexcept;
    void spawnGrain (const VoiceContext& ctx) noexcept;

    void renderDrone (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderBell (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderPluck (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderBass (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderShimmer (const VoiceBuses&, int start, int n, float gain, const VoiceContext&) noexcept;
    void renderWind (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderPulse (const VoiceBuses&, int start, int n, float gain) noexcept;

    // ---- common state -----------------------------------------------------
    float sr = 44100.0f;
    ElementType type = ElementType::Drone;
    std::uint32_t seed = 0;
    bool active = false, sounding = false, needsInitialPitch = true;
    float x = 0.5f, y = 0.5f, size = 1.0f;
    VoiceMods mods;
    VoiceParamArray vp = defaultVoiceParams()[0];
    int lastPulseHits = -1;
    dsp::Rng rng;
    dsp::OnePole fade, levelSmooth, panSmooth;
    float fadeInCoeff = 0, fadeOutCoeff = 0;
    float activity = 0.0f, activityDecay = 0.999f;
    float sendRev = 0.4f, sendDly = 0.1f, typeGain = 0.1f;
    float curRev = 0, curDly = 0, curGl = 0.7f, curGr = 0.7f;

    inline void emit (const VoiceBuses& b, int i, float l, float r) const noexcept
    {
        b.dryL[i] += l;          b.dryR[i] += r;
        b.revL[i] += l * curRev; b.revR[i] += r * curRev;
        b.dlyL[i] += l * curDly; b.dlyR[i] += r * curDly;
    }

    float basePan() const noexcept { return (x * 2.0f - 1.0f) * 0.85f; }

    // ---- Drone ------------------------------------------------------------
    std::array<dsp::BlepSaw, 4> saws;
    dsp::SineOsc droneSub;
    dsp::Svf droneFilterL, droneFilterR;
    dsp::OnePole droneFreq;
    float droneTarget = 220.0f, droneLfo = 0, droneLfoInc = 0, droneBreath = 0, droneBreathInc = 0;

    // ---- Bell -------------------------------------------------------------
    std::array<FmNote, 6> bells;
    float bellRatio = 3.5f;

    // ---- Pluck ------------------------------------------------------------
    std::array<PluckString, 4> strings;
    std::array<PluckEvent, 6> motif {};
    int motifLength = 0, motifPos = 0, nextString = 0;

    // ---- Bass -------------------------------------------------------------
    dsp::SineOsc bassOsc;
    dsp::OnePole bassFreq, bassEnv;
    float bassTarget = 55.0f, bassEnvTarget = 0;
    int bassHoldTicks = 0;
    long long bassReturnTick = -1;

    // ---- Shimmer ----------------------------------------------------------
    std::array<Grain, 10> grains;
    float grainCountdown = 0;
    int lastChordRoot = 0;
    const Mood* lastMood = nullptr;

    // ---- Wind -------------------------------------------------------------
    dsp::Svf windBandL, windBandR, windAir;
    dsp::OnePole windCentreL, windCentreR, windGust;
    float windTargetL = 600, windTargetR = 900, windGustTarget = 0.6f;
    int windHarmonic = 3;

    // ---- Pulse ------------------------------------------------------------
    dsp::SineOsc pulseOsc;
    dsp::Svf pulseFilter;
    float pulseInc = 0, pulseEnv = 0, pulseEnvMul = 0.999f, pulseAttackInc = 0;
    int pulseAttackRemaining = 0, pulseRotation = 0;
    std::array<bool, 16> pulsePattern {};
    int pulseStep = 0;
};

} // namespace scape
