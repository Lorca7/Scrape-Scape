#pragma once

#include "PluginProcessor.h"
#include "UI/ImageOutliner.h"
#include "UI/SceneCanvas.h"
#include "UI/ScapeLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ScrapeScapeEditor : public juce::AudioProcessorEditor,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit ScrapeScapeEditor (ScrapeScapeProcessor&);
    ~ScrapeScapeEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Drag & drop: images (onto a palette slot or anywhere) and .scapepreset files.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    // A palette button showing one element type.
    class ElementButton : public juce::Button
    {
    public:
        ElementButton (int t, SkinBank& skins);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void mouseDown (const juce::MouseEvent&) override;
        const int type;
        SkinBank& skins;
        bool dropHighlight = false;
        std::function<void()> onMenuRequested;
    };

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    bool showSubtitle() const noexcept { return getWidth() >= 1360; }
    void timerCallback() override;
    void toggleRecording();
    void exportLastTake();
    void showRenderMenu();
    void chooseRenderDestination (double seconds);
    void updateTransport();
    void openImageFile (int type, const juce::File& file);
    void chooseImageFile (int type);
    void openOutliner (int type, const ElementSkin& skin);
    void closeOutliner();
    void showPaletteMenu (int type);
    void showPresetMenu();
    void savePresetAs();
    void loadPresetFromChooser();
    void loadPresetFile (const juce::File& file);
    int paletteTypeAt (juce::Point<int> p) const;
    void setDropHighlight (int type, bool active);
    void addKnob (Knob& knob, const juce::String& paramID, const juce::String& name, juce::Colour colour);

    ScrapeScapeProcessor& processor;
    ScapeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    SceneCanvas canvas;
    juce::OwnedArray<ElementButton> paletteButtons;
    int selectedType = 0;

    juce::ComboBox moodBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> moodAttachment;

    juce::TextButton scatterButton { "Scatter" }, clearButton { "Clear" }, presetButton;
    juce::TextButton playButton;
    juce::TextButton recordButton { "Record" }, exportButton { "Export WAV..." };
    juce::TextButton renderButton { "Render WAV..." }, takesButton { "Takes" };
    juce::Label timeLabel;

    Knob activity, brightness, tempo, reverb, echo, volume;

    ImageOutliner outliner;
    bool dragActive = false;
    int lastSkinVersion = -1;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Random random;
    double lastTimerSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeScapeEditor)
};
