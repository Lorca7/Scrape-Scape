#pragma once

#include "ModKnob.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>

// Four LFOs. Drag an LFO's round handle onto the XY pad or any knob that has a
// target to assign it; click the target button to choose or clear a target.
class LfoPanel : public juce::Component
{
public:
    LfoPanel (juce::AudioProcessorValueTreeState& state);

    std::function<float (int lfo)> getLfoValue;
    std::function<int (int lfo)> getLfoTarget;
    std::function<void (int lfo, int target)> setLfoTarget;

    void refresh();   // call from a timer: animates handles, updates target labels
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Handle : public juce::Component,
                   public juce::SettableTooltipClient
    {
    public:
        Handle (LfoPanel& owner, int index);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        LfoPanel& owner;
        const int index;
        float value = 0.0f;
        int shape = 0;
        bool dragging = false;
    };

    struct Row
    {
        std::unique_ptr<Handle> handle;
        juce::ComboBox shape;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment;
        std::unique_ptr<ModKnob> rate, depth;
        juce::TextButton targetButton;
    };

    void showTargetMenu (int lfo);

    juce::AudioProcessorValueTreeState& state;
    std::array<Row, 4> rows;
};
