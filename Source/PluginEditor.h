#pragma once

#include "PluginProcessor.h"
#include "UI/SceneCanvas.h"
#include "UI/ScapeLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ScrapeScapeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ScrapeScapeEditor (ScrapeScapeProcessor&);
    ~ScrapeScapeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // A palette button showing one element type.
    class ElementButton : public juce::Button
    {
    public:
        explicit ElementButton (int t);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        const int type;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    bool showSubtitle() const noexcept { return getWidth() >= 1100; }
    void timerCallback() override;
    void toggleRecording();
    void exportLastTake();
    void showRenderMenu();
    void chooseRenderDestination (double seconds);
    void updateTransport();
    void addKnob (Knob& knob, const juce::String& paramID, const juce::String& name, juce::Colour colour);

    ScrapeScapeProcessor& processor;
    ScapeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    SceneCanvas canvas;
    juce::OwnedArray<ElementButton> paletteButtons;
    int selectedType = 0;

    juce::ComboBox moodBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> moodAttachment;

    juce::TextButton scatterButton { "Scatter" }, clearButton { "Clear" };
    juce::TextButton recordButton { "Record" }, exportButton { "Export WAV..." };
    juce::TextButton renderButton { "Render WAV..." }, takesButton { "Takes" };
    juce::Label timeLabel;

    Knob activity, brightness, tempo, reverb, echo, volume;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Random random;
    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeScapeEditor)
};
