#pragma once

#include "Skins.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Overlay where you outline the part of an image that becomes an element's glyph.
// Modes: freehand lasso, ellipse, rectangle, or the whole image.
class ImageOutliner : public juce::Component
{
public:
    ImageOutliner();

    void open (int elementType, const ElementSkin& skin);

    std::function<void (int type, ElementSkin skin)> onApply;
    std::function<void()> onCancel;
    std::function<void (int type)> onChooseFile;   // "Choose another image..."

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void setShape (int shape);
    void updatePreview();
    juce::Point<float> toNormalised (juce::Point<float> p) const noexcept;

    ElementSkin skin;
    int type = 0;
    bool drawing = false;

    juce::Rectangle<int> panel, imageBox, previewBox;
    juce::Rectangle<float> imageArea;
    juce::Image preview;

    juce::ComboBox typeBox;
    juce::TextButton freehandButton { "Freehand" }, ellipseButton { "Ellipse" },
                     rectangleButton { "Rectangle" }, wholeButton { "Whole image" };
    juce::TextButton chooseButton { "Choose another image..." }, cancelButton { "Cancel" }, applyButton { "Use this" };
};
