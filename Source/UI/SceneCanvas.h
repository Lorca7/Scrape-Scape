#pragma once

#include "Engine/Elements.h"
#include "Engine/Moods.h"
#include "Skins.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Draws an element glyph. Shared by the canvas and the palette buttons.
void drawElementGlyph (juce::Graphics& g, int type, juce::Point<float> centre, float radius,
                       float activity, float time, bool highlighted,
                       const juce::Image* customImage = nullptr);

// The interactive scene: click to place the selected element, drag to move,
// scroll to resize, double-click / right-click to remove.
class SceneCanvas : public juce::Component,
                    public juce::DragAndDropTarget
{
public:
    SceneCanvas (scape::ElementBank& bank, SkinBank& skins);

    void flash (const juce::String& message, float seconds = 2.5f);

    std::function<int()> getSelectedType;     // element type to place on click
    std::function<int()> getMoodIndex;
    std::function<int()> getChordRoot;
    std::function<bool()> isPlaying;

    // XY effects pad (the canvas doubles as the pad when pad mode is on)
    struct PadInfo { juce::String name, xLabel, yLabel; float x = 0.5f, y = 0.5f, modX = -1.0f, modY = -1.0f, mix = 0.0f; };
    std::function<bool()> isPadMode;
    std::function<PadInfo()> getPadInfo;
    std::function<void (float x, float y)> onPadMoved;
    std::function<void (bool starting)> onPadGesture;
    std::function<void (int lfo, int target)> onLfoDropped;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

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
    void paintPad (juce::Graphics&);
    juce::Point<float> handlePosition (const scape::ElementSnapshot& e) const noexcept;

    scape::ElementBank& bank;
    SkinBank& skins;
    int dragIndex = -1, hoverIndex = -1, selectedIndex = -1, resizeIndex = -1;
    float resizeStartScale = 1.0f, resizeStartDistance = 1.0f;
    bool padDragging = false, lfoDragHover = false;
    GlyphCache glyphCache;
    juce::Point<float> dragOffset;
    float time = 0.0f;
    juce::Colour topColour, bottomColour;
    juce::String flashMessage;
    float flashTime = 0.0f;
    float stoppedDim = 0.0f;   // 0 = playing, 1 = fully stopped (visual)

    struct Mote { float x, y, speed, phase, size; };
    std::vector<Mote> motes;
};
