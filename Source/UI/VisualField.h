#pragma once

#include "Engine/Elements.h"
#include "Engine/VoiceParams.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

// Everything the visuals react to (already including live LFO modulation).
struct VisualContext
{
    float brightness = 0.6f, activity = 0.5f, reverb = 0.5f, echo = 0.3f;
    int fxType = 0;
    float fxMix = 0.0f, fxX = 0.5f, fxY = 0.5f;
    float grainMix = 0.0f;
    int chordRoot = 0;
    float amount = 0.8f;          // the Visuals knob
    bool playing = true;
    scape::AllVoiceParams voice = scape::defaultVoiceParams();
};

struct ElementVisual
{
    int index = 0, type = 0;
    juce::Point<float> pos;
    float radius = 10.0f, size = 1.0f, activity = 0.0f;
    std::uint32_t seed = 0;
};

// Sound-reactive, evolving visuals drawn behind the elements: particles that
// orbit and blend colours between elements, ripples (with echo repeats) whose
// crossings spark, threads between neighbours, drone spirographs, and trails
// that persist longer with more reverb.
class VisualField
{
public:
    void setBounds (int width, int height);
    void update (float dt, const std::vector<ElementVisual>& elements, const VisualContext& ctx);
    void paintBehind (juce::Graphics& g);      // trails + threads (under the elements)
    void paintFront (juce::Graphics& g);       // interference sparkles (over the elements)

    int getParticleCount() const noexcept { return (int) particles.size(); }

private:
    struct Particle
    {
        juce::Point<float> p, v;
        float life, maxLife, size;
        juce::Colour colour;
        int owner, style;
    };

    struct Ripple
    {
        juce::Point<float> c;
        float radius, speed, life, maxLife, width, delay;
        juce::Colour colour;
        int owner, style;
    };

    void emit (const ElementVisual& e, const VisualContext& ctx, float dt);
    void spawnRipples (const ElementVisual& e, const VisualContext& ctx, float strength);
    juce::Colour colourFor (int type, const VisualContext& ctx) const;
    void drawIntoTrail (const VisualContext& ctx);

    int width = 1, height = 1;
    float time = 0.0f;
    juce::Image trail;
    juce::Random rng { 4242 };
    std::vector<Particle> particles;
    std::vector<Ripple> ripples;
    std::vector<ElementVisual> current;
    VisualContext lastCtx;
    std::array<float, scape::kMaxElements> prevActivity {};
    std::array<std::uint32_t, scape::kMaxElements> prevSeed {};
    std::array<float, scape::kMaxElements> emitAccumulator {};
    std::array<float, scape::kMaxElements> lastRippleTime {};
    void drawRipples (juce::Graphics& g) const;

    struct Spark { juce::Point<float> p; juce::Colour c; float s; };
    std::vector<Spark> sparks;
};
