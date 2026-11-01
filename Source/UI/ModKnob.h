#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

juce::Colour getLfoColour (int index);

// A rotary knob bound to a parameter. If it has a modulation target, LFOs can be
// dragged onto it; it then shows coloured LFO markers and the live modulated value.
class ModKnob : public juce::Component,
                public juce::DragAndDropTarget
{
public:
    enum class LabelPosition { left, above };

    ModKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& name,
             juce::Colour colour, int modTarget, LabelPosition labelPosition);

    std::function<float (int target)> getModulatedValue;   // normalised, or < 0 when unmodulated
    std::function<int (int lfo)> getLfoTarget;
    std::function<void (int lfo, int target)> onLfoDropped;

    int getModTarget() const noexcept { return target; }

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    int target;
    LabelPosition labelPosition;
    bool dragHover = false;
};

// Parses "lfo:N" drag descriptions; returns -1 if it isn't an LFO drag.
int lfoIndexFromDrag (const juce::var& description);
