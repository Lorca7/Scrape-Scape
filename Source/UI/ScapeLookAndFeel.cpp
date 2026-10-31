#include "ScapeLookAndFeel.h"

using namespace juce;

ScapeLookAndFeel::ScapeLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, ScapeColours::background);
    setColour (TextButton::buttonColourId, ScapeColours::panel);
    setColour (TextButton::buttonOnColourId, ScapeColours::record);
    setColour (TextButton::textColourOffId, ScapeColours::text);
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (ComboBox::backgroundColourId, ScapeColours::panel);
    setColour (ComboBox::textColourId, ScapeColours::text);
    setColour (ComboBox::outlineColourId, ScapeColours::panelEdge);
    setColour (ComboBox::arrowColourId, ScapeColours::textDim);
    setColour (PopupMenu::backgroundColourId, ScapeColours::panel);
    setColour (PopupMenu::textColourId, ScapeColours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, ScapeColours::panelEdge);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (Slider::textBoxTextColourId, ScapeColours::textDim);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::rotarySliderFillColourId, ScapeColours::accent);
    setColour (Label::textColourId, ScapeColours::text);
    setColour (TooltipWindow::backgroundColourId, ScapeColours::panel);
    setColour (TooltipWindow::textColourId, ScapeColours::text);
    setColour (TooltipWindow::outlineColourId, ScapeColours::panelEdge);
    setColour (AlertWindow::backgroundColourId, ScapeColours::panel);
    setColour (AlertWindow::textColourId, ScapeColours::text);
    setColour (AlertWindow::outlineColourId, ScapeColours::panelEdge);
}

void ScapeLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                         float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float thickness = jmax (2.5f, radius * 0.14f);
    const auto colour = slider.findColour (Slider::rotarySliderFillColourId);

    Path track;
    track.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, endAngle, true);
    g.setColour (ScapeColours::panelEdge);
    g.strokePath (track, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));

    Path value;
    value.addCentredArc (centre.x, centre.y, radius - thickness, radius - thickness, 0.0f, startAngle, angle, true);
    g.setColour (colour.withAlpha (slider.isEnabled() ? 0.95f : 0.4f));
    g.strokePath (value, PathStrokeType (thickness, PathStrokeType::curved, PathStrokeType::rounded));

    const float knobR = radius - thickness * 2.4f;
    g.setColour (ScapeColours::panel.brighter (0.15f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    const auto tip = centre.getPointOnCircumference (knobR * 0.8f, angle);
    g.setColour (ScapeColours::text);
    g.drawLine (Line<float> (centre.getPointOnCircumference (knobR * 0.25f, angle), tip), 2.0f);
}

void ScapeLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour& backgroundColour,
                                             bool over, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = button.getToggleState() ? button.findColour (TextButton::buttonOnColourId) : backgroundColour;

    if (! button.isEnabled()) base = base.withMultipliedAlpha (0.5f);
    if (down)      base = base.brighter (0.2f);
    else if (over) base = base.brighter (0.1f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (button.getToggleState() ? base.brighter (0.3f) : ScapeColours::panelEdge);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
}

Font ScapeLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Font (FontOptions (jmin (15.0f, (float) buttonHeight * 0.48f)));
}

void ScapeLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    auto bounds = Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    Path arrow;
    const float ax = (float) width - 18.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.strokePath (arrow, PathStrokeType (1.5f));
}

Label* ScapeLookAndFeel::createSliderTextBox (Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (Font (FontOptions (12.0f)));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::textColourId, ScapeColours::textDim);
    return l;
}
