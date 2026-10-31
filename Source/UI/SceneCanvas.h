#pragma once

#include "Engine/Elements.h"
#include "Engine/Moods.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Draws an element glyph. Shared by the canvas and the palette buttons.
void drawElementGlyph (juce::Graphics& g, int type, juce::Point<float> centre, float radius,
                       float activity, float time, bool highlighted);

// The interactive scene: click to place the selected element, drag to move,
// scroll to resize, double-click / right-click to remove.
class SceneCanvas : public juce::Component
{
public:
    explicit SceneCanvas (scape::ElementBank& bank);

    std::function<int()> getSelectedType;     // element type to place on click
    std::function<int()> getMoodIndex;
    std::function<int()> getChordRoot;

    void advanceAnimation (float seconds);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Point<float> toScreen (float x, float y) const noexcept;
    juce::Point<float> toNormalised (juce::Point<float> p) const noexcept;
    float radiusFor (int type, float size) const noexcept;
    int hitTestElement (juce::Point<float> p) const noexcept;
    void paintBackground (juce::Graphics&);

    scape::ElementBank& bank;
    int dragIndex = -1, hoverIndex = -1;
    juce::Point<float> dragOffset;
    float time = 0.0f;
    juce::Colour topColour, bottomColour;
    juce::String flashMessage;
    float flashTime = 0.0f;

    struct Mote { float x, y, speed, phase, size; };
    std::vector<Mote> motes;
};
