#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ScapeColours
{
    const juce::Colour background { 0xff0d0f17 };
    const juce::Colour panel      { 0xff161a26 };
    const juce::Colour panelEdge  { 0xff2a3044 };
    const juce::Colour text       { 0xffe8ecf5 };
    const juce::Colour textDim    { 0xff8a93a8 };
    const juce::Colour accent     { 0xff9fd3ff };
    const juce::Colour record     { 0xffff4d5e };
}

class ScapeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ScapeLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
};
