#include "PluginEditor.h"

using namespace juce;
using namespace scape;

//==============================================================================
ScrapeScapeEditor::ElementButton::ElementButton (int t)
    : Button (getElementInfo (t).name), type (t)
{
    setClickingTogglesState (true);
    setRadioGroupId (1001);
    setTooltip (String (getElementInfo (t).name) + String::fromUTF8 (" \xe2\x80\x94 ") + getElementInfo (t).description);
}

void ScrapeScapeEditor::ElementButton::paintButton (Graphics& g, bool over, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);
    const Colour col (getElementInfo (type).colour);

    g.setColour (getToggleState() ? col.withAlpha (0.22f) : ScapeColours::panel.brighter (over ? 0.08f : 0.0f));
    g.fillRoundedRectangle (bounds, 9.0f);
    g.setColour (getToggleState() ? col.withAlpha (0.9f) : ScapeColours::panelEdge);
    g.drawRoundedRectangle (bounds, 9.0f, getToggleState() ? 1.6f : 1.0f);

    auto iconArea = bounds.withTrimmedBottom (18.0f);
    const float r = jmin (iconArea.getWidth(), iconArea.getHeight()) * 0.24f * (down ? 0.92f : 1.0f);
    drawElementGlyph (g, type, iconArea.getCentre(), r, getToggleState() ? 0.6f : 0.15f,
                      (float) Time::getMillisecondCounter() * 0.001f, false);

    g.setColour (getToggleState() ? ScapeColours::text : ScapeColours::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText (getName(), bounds.removeFromBottom (20.0f), Justification::centred);
}

//==============================================================================
ScrapeScapeEditor::ScrapeScapeEditor (ScrapeScapeProcessor& p)
    : AudioProcessorEditor (&p), processor (p), canvas (p.getElements())
{

    canvas.getSelectedType = [this] { return selectedType; };
    canvas.getMoodIndex = [this] { return processor.getEngineParams().mood; };
    canvas.getChordRoot = [this] { return processor.getChordRoot(); };
    canvas.isPlaying = [this] { return processor.isPlaying(); };
    addAndMakeVisible (canvas);

    for (int t = 0; t < kNumElementTypes; ++t)
    {
        auto* b = paletteButtons.add (new ElementButton (t));
        b->onClick = [this, b] { if (b->getToggleState()) selectedType = b->type; };
        addAndMakeVisible (b);
    }
    paletteButtons[0]->setToggleState (true, dontSendNotification);

    for (int i = 0; i < kNumMoods; ++i)
        moodBox.addItem (getMood (i).name, i + 1);
    moodBox.setTooltip ("Mood: the key, scale, pace and colour of the whole scene");
    moodAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (processor.getParameters(), "mood", moodBox);
    addAndMakeVisible (moodBox);

    scatterButton.setTooltip ("Replace the scene with a random arrangement");
    scatterButton.onClick = [this] { processor.scatterScene (random); };
    clearButton.setTooltip ("Remove every element");
    clearButton.onClick = [this] { processor.getElements().clear(); };

    playButton.setTooltip ("Start or stop the music (Stop fades out gently)");
    playButton.setColour (TextButton::buttonOnColourId, Colour (0xff2f6f5a));
    playButton.onClick = [this]
    {
        processor.setPlaying (! processor.isPlaying());
        updateTransport();
    };

    recordButton.setClickingTogglesState (false);
    recordButton.setTooltip ("Record the live output to a 24-bit WAV file");
    recordButton.onClick = [this] { toggleRecording(); };

    exportButton.setTooltip ("Save the last recorded take as a WAV file wherever you like");
    exportButton.onClick = [this] { exportLastTake(); };

    renderButton.setTooltip ("Render the current scene to a WAV file faster than realtime");
    renderButton.onClick = [this] { showRenderMenu(); };

    takesButton.setTooltip ("Show the folder where recorded takes are kept");
    takesButton.onClick = [] { Recorder::getTakesFolder().startAsProcess(); };

    timeLabel.setJustificationType (Justification::centred);
    timeLabel.setFont (FontOptions (15.0f));
    timeLabel.setColour (Label::textColourId, ScapeColours::textDim);
    timeLabel.setText ("00:00", dontSendNotification);

    for (auto* c : std::initializer_list<Component*> { &playButton, &scatterButton, &clearButton, &recordButton, &exportButton,
                                                       &renderButton, &takesButton, &timeLabel })
        addAndMakeVisible (c);

    addKnob (activity, "activity", "Activity", Colour (0xff9cf2c0));
    addKnob (brightness, "brightness", "Brightness", Colour (0xffffd27f));
    addKnob (tempo, "tempo", "Tempo", Colour (0xffff9a8c));
    addKnob (reverb, "reverb", "Reverb", Colour (0xff7fb8ff));
    addKnob (echo, "delay", "Echo", Colour (0xff8fd6d6));
    addKnob (volume, "volume", "Volume", Colour (0xffe8ecf5));

    // Set last so every child (including slider text boxes) picks up the theme.
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (1080, 640, 2400, 1600);
    setSize (1180, 760);

    updateTransport();
    lastTimerSeconds = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (30);
}

ScrapeScapeEditor::~ScrapeScapeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ScrapeScapeEditor::addKnob (Knob& knob, const String& paramID, const String& name, Colour colour)
{
    knob.slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (Slider::TextBoxBelow, false, 70, 16);
    knob.slider.setColour (Slider::rotarySliderFillColourId, colour);
    knob.slider.setPopupDisplayEnabled (false, false, nullptr);
    knob.attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (processor.getParameters(), paramID, knob.slider);

    knob.label.setText (name, dontSendNotification);
    knob.label.setJustificationType (Justification::centred);
    knob.label.setFont (FontOptions (13.0f));
    knob.label.setColour (Label::textColourId, ScapeColours::textDim);

    addAndMakeVisible (knob.slider);
    addAndMakeVisible (knob.label);
}

//==============================================================================
void ScrapeScapeEditor::paint (Graphics& g)
{
    g.fillAll (ScapeColours::background);

    auto header = getLocalBounds().removeFromTop (56).reduced (16, 0);
    g.setColour (ScapeColours::text);
    g.setFont (FontOptions (22.0f, Font::bold));
    g.drawText (String::fromUTF8 ("Scrape\xc2\xb7Scape"), header.removeFromLeft (170), Justification::centredLeft);
    if (showSubtitle())
    {
        g.setColour (ScapeColours::textDim);
        g.setFont (FontOptions (12.0f));
        g.drawText ("generative ambient", header.removeFromLeft (120), Justification::centredLeft);
    }

    // Bottom panel
    auto bottom = getLocalBounds().removeFromBottom (190).reduced (12, 8).toFloat();
    g.setColour (ScapeColours::panel.withAlpha (0.6f));
    g.fillRoundedRectangle (bottom, 12.0f);
    g.setColour (ScapeColours::panelEdge.withAlpha (0.6f));
    g.drawRoundedRectangle (bottom, 12.0f, 1.0f);

    if (processor.isRecording())
    {
        const float pulse = 0.5f + 0.5f * std::sin ((float) Time::getMillisecondCounter() * 0.006f);
        auto dot = recordButton.getBounds().toFloat().withWidth (10.0f).withHeight (10.0f)
                       .translated (-16.0f, (float) recordButton.getHeight() * 0.5f - 5.0f);
        g.setColour (ScapeColours::record.withAlpha (0.4f + 0.6f * pulse));
        g.fillEllipse (dot);
    }
}

void ScrapeScapeEditor::resized()
{
    auto area = getLocalBounds();

    // Header
    auto header = area.removeFromTop (56).reduced (16, 11);
    header.removeFromLeft (showSubtitle() ? 300 : 175);

    takesButton.setBounds (header.removeFromRight (62));
    header.removeFromRight (6);
    renderButton.setBounds (header.removeFromRight (118));
    header.removeFromRight (6);
    exportButton.setBounds (header.removeFromRight (108));
    header.removeFromRight (6);
    timeLabel.setBounds (header.removeFromRight (58));
    recordButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (26);
    playButton.setBounds (header.removeFromRight (84));
    header.removeFromRight (20);

    moodBox.setBounds (header.removeFromLeft (118));
    header.removeFromLeft (6);
    scatterButton.setBounds (header.removeFromLeft (76));
    header.removeFromLeft (6);
    clearButton.setBounds (header.removeFromLeft (62));

    // Bottom panel: palette + knobs
    auto bottom = area.removeFromBottom (190).reduced (24, 16);
    auto palette = bottom.removeFromTop (78);
    const int buttonW = palette.getWidth() / kNumElementTypes;
    for (auto* b : paletteButtons)
        b->setBounds (palette.removeFromLeft (buttonW).reduced (3, 0));

    bottom.removeFromTop (8);
    Knob* knobs[] = { &activity, &brightness, &tempo, &reverb, &echo, &volume };
    const int knobW = bottom.getWidth() / 6;
    for (auto* k : knobs)
    {
        auto cell = bottom.removeFromLeft (knobW);
        auto labelArea = cell.removeFromLeft (cell.getWidth() / 2);
        k->label.setBounds (labelArea.withTrimmedRight (4));
        k->label.setJustificationType (Justification::centredRight);
        k->slider.setBounds (cell.withSizeKeepingCentre (jmin (cell.getWidth(), 76), cell.getHeight()));
    }

    canvas.setBounds (area.reduced (12, 0));
}

//==============================================================================
void ScrapeScapeEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    canvas.advanceAnimation ((float) jlimit (0.0, 0.2, now - lastTimerSeconds));
    lastTimerSeconds = now;
    canvas.repaint();

    for (auto* b : paletteButtons)
        if (b->getToggleState()) b->repaint();

    updateTransport();

    auto& renderer = processor.getRenderer();
    const auto status = renderer.getStatus();

    if (status == OfflineRenderer::Status::finished || status == OfflineRenderer::Status::failed)
    {
        const auto file = renderer.getDestination();
        renderer.acknowledge();

        if (status == OfflineRenderer::Status::finished)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Render complete",
                                              "Saved " + file.getFileName() + "\n\n" + file.getParentDirectory().getFullPathName());
            file.revealToUser();
        }
        else
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Render failed",
                                              "Couldn't write " + file.getFullPathName());
        }
    }
    else if (status == OfflineRenderer::Status::cancelled)
    {
        renderer.acknowledge();
    }
}

void ScrapeScapeEditor::updateTransport()
{
    const bool playing = processor.isPlaying();
    playButton.setToggleState (playing, dontSendNotification);
    playButton.setButtonText (String::fromUTF8 (playing ? "\xe2\x96\xa0  Stop" : "\xe2\x96\xb6  Play"));

    const bool recording = processor.isRecording();
    recordButton.setToggleState (recording, dontSendNotification);
    recordButton.setButtonText (String::fromUTF8 (recording ? "\xe2\x96\xa0  Stop Rec" : "\xe2\x97\x8f  Record"));

    const double secs = recording ? processor.getSecondsRecorded() : 0.0;
    if (recording)
    {
        const int s = (int) secs;
        timeLabel.setText (String::formatted ("%02d:%02d", s / 60, s % 60), dontSendNotification);
        timeLabel.setColour (Label::textColourId, ScapeColours::record);
        repaint (recordButton.getBounds().withX (recordButton.getX() - 20).withWidth (20));
    }
    else
    {
        timeLabel.setColour (Label::textColourId, ScapeColours::textDim);
    }

    exportButton.setEnabled (! recording && processor.getLastTake().existsAsFile());

    auto& renderer = processor.getRenderer();
    if (renderer.getStatus() == OfflineRenderer::Status::rendering)
        renderButton.setButtonText ("Cancel " + String (roundToInt (renderer.getProgress() * 100.0f)) + "%");
    else
        renderButton.setButtonText ("Render WAV...");
}

void ScrapeScapeEditor::toggleRecording()
{
    if (processor.isRecording())
    {
        processor.stopRecording();
        const auto take = processor.getLastTake();
        const int s = (int) processor.getSecondsRecorded();
        timeLabel.setText (String::formatted ("%02d:%02d", s / 60, s % 60), dontSendNotification);
        canvas.repaint();
        ignoreUnused (take);
    }
    else if (! processor.startRecording())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't start recording",
                                          "Scrape-Scape couldn't create a file in\n" + Recorder::getTakesFolder().getFullPathName());
    }

    updateTransport();
    repaint();
}

void ScrapeScapeEditor::exportLastTake()
{
    const auto take = processor.getLastTake();
    if (! take.existsAsFile())
        return;

    auto start = File::getSpecialLocation (File::userDesktopDirectory).getChildFile (take.getFileName());
    fileChooser = std::make_unique<FileChooser> ("Export recording as WAV", start, "*.wav");

    fileChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                                  | FileBrowserComponent::warnAboutOverwriting,
                              [take] (const FileChooser& fc)
                              {
                                  auto dest = fc.getResult();
                                  if (dest == File()) return;
                                  if (! dest.hasFileExtension ("wav")) dest = dest.withFileExtension ("wav");

                                  if (dest != take && ! take.copyFileTo (dest))
                                      AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Export failed",
                                                                        "Couldn't write " + dest.getFullPathName());
                                  else
                                      dest.revealToUser();
                              });
}

void ScrapeScapeEditor::showRenderMenu()
{
    auto& renderer = processor.getRenderer();
    if (renderer.getStatus() == OfflineRenderer::Status::rendering)
    {
        renderer.cancel();
        return;
    }

    PopupMenu menu;
    menu.addSectionHeader ("Render the current scene to WAV");
    const int minutes[] = { 1, 3, 5, 10, 20, 30, 60 };
    for (int m : minutes)
        menu.addItem (m, String (m) + (m == 1 ? " minute" : " minutes"));

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&renderButton),
                        [safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (int result)
                        {
                            if (safeThis != nullptr && result > 0)
                                safeThis->chooseRenderDestination (result * 60.0);
                        });
}

void ScrapeScapeEditor::chooseRenderDestination (double seconds)
{
    const auto stamp = Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M");
    const auto name = "Scrape-Scape " + String (getMood (processor.getEngineParams().mood).name) + " "
                      + String (roundToInt (seconds / 60.0)) + "min " + stamp + ".wav";
    auto start = File::getSpecialLocation (File::userDesktopDirectory).getChildFile (name);
    fileChooser = std::make_unique<FileChooser> ("Render scene to WAV", start, "*.wav");

    fileChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                                  | FileBrowserComponent::warnAboutOverwriting,
                              [this, seconds, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (const FileChooser& fc)
                              {
                                  if (safeThis == nullptr) return;
                                  auto dest = fc.getResult();
                                  if (dest == File()) return;
                                  if (! dest.hasFileExtension ("wav")) dest = dest.withFileExtension ("wav");

                                  processor.getRenderer().start (dest, seconds, processor.getCurrentSampleRate(),
                                                                 processor.getEngineParams(), processor.getElements());
                                  updateTransport();
                              });
}
