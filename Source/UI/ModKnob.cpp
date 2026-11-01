#include "ModKnob.h"
#include "Engine/Modulation.h"
#include "ScapeLookAndFeel.h"

using namespace juce;

Colour getLfoColour (int index)
{
    static const Colour colours[] = { Colour (0xffff7eb6), Colour (0xff7ee0ff), Colour (0xffffd166), Colour (0xffa6ff8f) };
    return colours[jlimit (0, 3, index)];
}

int lfoIndexFromDrag (const var& description)
{
    const auto s = description.toString();
    if (! s.startsWith ("lfo:")) return -1;
    const int i = s.fromFirstOccurrenceOf ("lfo:", false, false).getIntValue();
    return isPositiveAndBelow (i, scape::kNumLfos) ? i : -1;
}

ModKnob::ModKnob (AudioProcessorValueTreeState& state, const String& paramID, const String& name,
                  Colour colour, int modTarget, LabelPosition pos)
    : target (modTarget), labelPosition (pos)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 15);
    slider.setColour (Slider::rotarySliderFillColourId, colour);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

    label.setText (name, dontSendNotification);
    label.setFont (FontOptions (pos == LabelPosition::above ? 12.0f : 13.0f));
    label.setColour (Label::textColourId, ScapeColours::textDim);
    label.setJustificationType (pos == LabelPosition::above ? Justification::centred : Justification::centredRight);
    label.setInterceptsMouseClicks (false, false);

    if (target > 0)
        slider.setTooltip (name + ": drag an LFO here to modulate it");

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void ModKnob::resized()
{
    auto r = getLocalBounds();
    if (labelPosition == LabelPosition::above)
    {
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }
    else
    {
        label.setBounds (r.removeFromLeft (r.getWidth() / 2).withTrimmedRight (4));
        slider.setBounds (r.withSizeKeepingCentre (jmin (r.getWidth(), 76), r.getHeight()));
    }
}

void ModKnob::paintOverChildren (Graphics& g)
{
    if (target <= 0) return;

    const auto layout = slider.getLookAndFeel().getSliderLayout (slider);
    const auto knobArea = layout.sliderBounds.translated (slider.getX(), slider.getY()).toFloat().reduced (6.0f);
    const float radius = jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.5f;
    const auto centre = knobArea.getCentre();
    const auto rp = slider.getRotaryParameters();

    // Which LFOs are on this knob?
    int first = -1, pipIndex = 0;
    if (getLfoTarget)
        for (int i = 0; i < scape::kNumLfos; ++i)
            if (getLfoTarget (i) == target)
            {
                if (first < 0) first = i;
                const float px = centre.x + radius + 2.0f + (float) pipIndex * 0.0f;
                const float py = knobArea.getY() + (float) pipIndex * 9.0f;
                g.setColour (getLfoColour (i));
                g.fillEllipse (px - 3.5f, py, 7.0f, 7.0f);
                ++pipIndex;
            }

    if (first >= 0 && getModulatedValue)
    {
        const float m = getModulatedValue (target);
        if (m >= 0.0f)
        {
            const float angle = rp.startAngleRadians + m * (rp.endAngleRadians - rp.startAngleRadians);
            const float thickness = jmax (2.5f, radius * 0.14f);
            const auto p = centre.getPointOnCircumference (radius - thickness, angle);
            g.setColour (getLfoColour (first));
            g.fillEllipse (p.x - thickness, p.y - thickness, thickness * 2.0f, thickness * 2.0f);
            g.setColour (Colours::white.withAlpha (0.8f));
            g.drawEllipse (p.x - thickness, p.y - thickness, thickness * 2.0f, thickness * 2.0f, 1.0f);
        }
    }

    if (dragHover)
    {
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawEllipse (centre.x - radius - 3.0f, centre.y - radius - 3.0f, radius * 2.0f + 6.0f, radius * 2.0f + 6.0f, 2.0f);
    }
}

bool ModKnob::isInterestedInDragSource (const SourceDetails& d)
{
    return target > 0 && lfoIndexFromDrag (d.description) >= 0;
}

void ModKnob::itemDragEnter (const SourceDetails&) { dragHover = true; repaint(); }
void ModKnob::itemDragExit (const SourceDetails&)  { dragHover = false; repaint(); }

void ModKnob::itemDropped (const SourceDetails& d)
{
    dragHover = false;
    const int lfo = lfoIndexFromDrag (d.description);
    if (lfo >= 0 && onLfoDropped)
        onLfoDropped (lfo, target);
    repaint();
}
