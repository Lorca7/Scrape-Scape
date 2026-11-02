#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

// Subtle, slowly evolving background patterns, one style per mood, plus a drifting
// aurora on every mood. Breathes gently with how busy the music is and blooms softly
// on chord changes; crossfades when the mood changes.
class Backdrop
{
public:
    Backdrop();

    void update (float dt, int mood, int chordRoot, float energy);
    void paint (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour top, juce::Colour bottom, float amount) const;

private:
    void paintMood (juce::Graphics& g, juce::Rectangle<float> b, int mood, float alpha, juce::Colour tint) const;
    void paintAurora (juce::Graphics& g, juce::Rectangle<float> b, float alpha, juce::Colour tint) const;

    float time = 0.0f, energy = 0.0f, bloom = 0.0f, moodBlend = 1.0f;
    int mood = 0, previousMood = 0, lastChord = -1;

    struct Star { float x, y, size, phase, speed; };
    std::vector<Star> stars;
};
