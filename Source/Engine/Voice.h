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

// Global controls as seen by a voice.
struct VoiceMods
{
    float brightness = 0.6f;   // 0..1
    float activity = 0.5f;     // 0..1
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
    // ---- engine state ---------------------------------------------------------
    struct BowlNote
    {
        bool on = false;
        std::array<float, 5> phA {}, phB {}, incA {}, incB {}, amp {}, ampMul {};
        float swell = 0.0f, swellInc = 1.0f, gl = 0.7f, gr = 0.7f;
    };

    struct PluckNote
    {
        bool on = false;
        float phase = 0.0f, inc = 0.0f, amp = 0.0f, ampMul = 0.999f, env = 0.0f, envMul = 0.999f;
        float attack = 0.0f, attackInc = 1.0f, cutoffBase = 1000.0f, snap = 0.5f, wave = 0.3f, gl = 0.7f, gr = 0.7f;
        dsp::Svf filter;
        int counter = 0;
    };

    struct Grain
    {
        bool on = false;
        float inc = 0, phase = 0, pos = 0, posInc = 0, amp = 0, gl = 0, gr = 0;
    };

    struct Chime
    {
        bool on = false;
        std::array<float, 3> ph {}, inc {}, amp {}, mul {};
        float gl = 0.7f, gr = 0.7f;
    };

    struct PadVoice
    {
        std::array<dsp::BlepSaw, 2> saw;
        float triPhase = 0.0f, target = 220.0f, gl = 0.7f, gr = 0.7f;
        dsp::OnePole freq;
    };

    struct PluckEvent { int degree; long long tick; };

    // ---- helpers ------------------------------------------------------------
    float pitchHz (const VoiceContext& ctx, int degree, int octave) const noexcept;
    int octaveShift() const noexcept { return octaveFrom (vp[kOctave]); }
    float basePan() const noexcept   { return (x * 2.0f - 1.0f) * 0.85f; }

    void strikeBowl (const VoiceContext& ctx) noexcept;
    void triggerPluck (const VoiceContext& ctx, int degree, float velocity) noexcept;
    void strikeChime (const VoiceContext& ctx) noexcept;
    void spawnGrain (const VoiceContext& ctx) noexcept;
    void retunePad (const VoiceContext& ctx) noexcept;

    void renderDrone (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderBowl (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderPluck (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderPulse (const VoiceBuses&, int start, int n, float gain) noexcept;
    void renderShimmer (const VoiceBuses&, int start, int n, float gain, const VoiceContext&) noexcept;
    void renderChimes (const VoiceBuses&, int start, int n, float gain, const VoiceContext&) noexcept;
    void renderPad (const VoiceBuses&, int start, int n, float gain) noexcept;

    inline void emit (const VoiceBuses& b, int i, float l, float r) noexcept
    {
        if (voiceFilterOn) { l = voiceLpL.lowpass (l); r = voiceLpR.lowpass (r); }
        b.dryL[i] += l;          b.dryR[i] += r;
        b.revL[i] += l * curRev; b.revR[i] += r * curRev;
        b.dlyL[i] += l * curDly; b.dlyR[i] += r * curDly;
    }

    // ---- common state -----------------------------------------------------
    float sr = 44100.0f;
    ElementType type = ElementType::Drone;
    std::uint32_t seed = 0;
    bool active = false, sounding = false, needsInitialPitch = true;
    float x = 0.5f, y = 0.5f, size = 1.0f;
    VoiceMods mods;
    VoiceParamArray vp = defaultVoiceParams()[0];
    dsp::Rng rng;
    dsp::OnePole fade, levelSmooth, panSmooth;
    float fadeInCoeff = 0, fadeOutCoeff = 0;
    float activity = 0.0f, activityDecay = 0.999f;
    float sendRev = 0.4f, sendDly = 0.1f, typeGain = 0.1f;
    float curRev = 0, curDly = 0, curGl = 0.7f, curGr = 0.7f;
    dsp::Svf voiceLpL, voiceLpR;
    bool voiceFilterOn = false;
    const Mood* lastMood = nullptr;

    // ---- Drone ------------------------------------------------------------
    std::array<dsp::BlepSaw, 4> saws;
    dsp::SineOsc droneSub, shimOct, shimFifth;
    dsp::Svf droneFilterL, droneFilterR;
    dsp::OnePole droneFreq;
    float droneTarget = 220.0f, droneLfo = 0, droneLfoInc = 0, droneBreath = 0, droneBreathInc = 0, shimLfo = 0;

    // ---- Bowl -------------------------------------------------------------
    std::array<BowlNote, 4> bowls;

    // ---- Pluck ------------------------------------------------------------
    std::array<PluckNote, 6> plucks;
    std::array<PluckEvent, 6> motif {};
    int motifLength = 0, motifPos = 0, nextPluck = 0;

    // ---- Pulse ------------------------------------------------------------
    float pulsePhase = 0.0f, pulseInc = 0.0f, pulseEnv = 0.0f, pulseEnvMul = 0.999f, pulseAttackInc = 0.0f, pwmLfo = 0.0f;
    int pulseAttackRemaining = 0, pulseRotation = 0, pulseStep = 0, lastPulseHits = -1;
    std::array<bool, 16> pulsePattern {};
    dsp::Svf pulseFilter;

    // ---- Shimmer ----------------------------------------------------------
    std::array<Grain, 10> grains;
    float grainCountdown = 0;

    // ---- Chimes -----------------------------------------------------------
    std::array<Chime, 12> chimes;
    dsp::OnePole breeze;
    float breezeTarget = 0.5f, chimeCountdown = 0.0f, clusterGap = 0.0f;
    int clusterLeft = 0, nextChime = 0;

    // ---- Pad --------------------------------------------------------------
    std::array<PadVoice, 5> pad;
    dsp::Svf padFilterL, padFilterR;
    dsp::OnePole padSwell;
    float padSwellTarget = 0.0f, padLfo = 0.0f, padLfoInc = 0.0f;
    int padVoices = 3;
};

} // namespace scape
