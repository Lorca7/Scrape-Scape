#include "PluginEditor.h"

using namespace juce;
using namespace scape;

//==============================================================================
ScrapeScapeEditor::ElementButton::ElementButton (int t, SkinBank& sk)
    : Button (getElementInfo (t).name), type (t), skins (sk)
{
    setClickingTogglesState (true);
    setRadioGroupId (1001);
    setTooltip (String (getElementInfo (t).name) + String::fromUTF8 (" \xe2\x80\x94 ") + getElementInfo (t).description
                + "\n\nDrop a photo here (or right-click) to use your own image for this element.");
}

void ScrapeScapeEditor::ElementButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onMenuRequested) onMenuRequested();
        return;
    }
    Button::mouseDown (e);
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
    const auto glyph = skins.getGlyph (type);
    drawElementGlyph (g, type, iconArea.getCentre(), r, getToggleState() ? 0.6f : 0.15f,
                      (float) Time::getMillisecondCounter() * 0.001f, false, &glyph);

    if (dropHighlight)
    {
        g.setColour (col.withAlpha (0.25f));
        g.fillRoundedRectangle (bounds, 9.0f);
        g.setColour (Colours::white);
        g.drawRoundedRectangle (bounds, 9.0f, 2.0f);
    }

    g.setColour (getToggleState() ? ScapeColours::text : ScapeColours::textDim);
    g.setFont (FontOptions (12.0f));
    g.drawText (getName(), bounds.removeFromBottom (20.0f), Justification::centred);
}

//==============================================================================
ScrapeScapeEditor::ScrapeScapeEditor (ScrapeScapeProcessor& p)
    : AudioProcessorEditor (&p), processor (p), canvas (p.getElements(), p.getSkins()), lfoPanel (p.getParameters()), soundPage (p.getSkins())
{

    canvas.getSelectedType = [this] { return selectedType; };
    canvas.getMoodIndex = [this] { return processor.getEngineParams().mood; };
    canvas.getChordRoot = [this] { return processor.getChordRoot(); };
    canvas.isPlaying = [this] { return processor.isPlaying(); };
    addAndMakeVisible (canvas);

    for (int t = 0; t < kNumElementTypes; ++t)
    {
        auto* b = paletteButtons.add (new ElementButton (t, processor.getSkins()));
        b->onClick = [this, b]
        {
            if (! b->getToggleState()) return;
            selectedType = b->type;
            if (b->type < kNumSoundTypes) { soundPage.selectType (b->type); showSidebarPage (true); }
        };
        b->onMenuRequested = [this, t] { showPaletteMenu (t); };
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

    presetButton.setButtonText (String::fromUTF8 ("Presets \xe2\x96\xbe"));
    presetButton.setTooltip ("Save or load everything: scene, knobs, mood and your images");
    presetButton.onClick = [this] { showPresetMenu(); };

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

    for (auto* c : std::initializer_list<Component*> { &playButton, &presetButton, &scatterButton, &clearButton, &recordButton, &exportButton,
                                                       &renderButton, &takesButton, &timeLabel })
        addAndMakeVisible (c);

    using LP = ModKnob::LabelPosition;
    activity   = makeKnob ("activity",   "Activity",   Colour (0xff9cf2c0), ModTarget::Activity,   LP::left);
    brightness = makeKnob ("brightness", "Brightness", Colour (0xffffd27f), ModTarget::Brightness, LP::left);
    tempo      = makeKnob ("tempo",      "Tempo",      Colour (0xffff9a8c), ModTarget::None,       LP::left);
    reverb     = makeKnob ("reverb",     "Reverb",     Colour (0xff7fb8ff), ModTarget::Reverb,     LP::left);
    echo       = makeKnob ("delay",      "Echo",       Colour (0xff8fd6d6), ModTarget::Echo,       LP::left);
    volume     = makeKnob ("volume",     "Volume",     Colour (0xffe8ecf5), ModTarget::None,       LP::left);
    visuals    = makeKnob ("visuals",    "Visuals",    Colour (0xffc9a7ff), ModTarget::None,       LP::left);
    visuals->slider.setTooltip ("How strongly the canvas visualises the sound (particles, ripples, threads, trails)");

    // ---- XY effects pad bar ----
    padModeButton.setButtonText ("XY Pad");
    padModeButton.setClickingTogglesState (true);
    padModeButton.setColour (TextButton::buttonOnColourId, ScapeColours::accent.withAlpha (0.45f));
    padModeButton.setTooltip ("Turn the canvas into an XY effects pad. Turn off to place and move elements again.");
    padModeButton.onClick = [this] { canvas.repaint(); };
    addAndMakeVisible (padModeButton);

    for (int i = 0; i < kNumPadEffects; ++i)
    {
        auto* b = fxButtons.add (new TextButton (getPadEffectInfo (i).name));
        b->setRadioGroupId (3001);
        b->setClickingTogglesState (true);
        b->setColour (TextButton::buttonOnColourId, ScapeColours::accent.withAlpha (0.3f));
        b->setTooltip (String (getPadEffectInfo (i).name) + String::fromUTF8 (" \xe2\x80\x94 X: ") + String::fromUTF8 (getPadEffectInfo (i).xLabel)
                       + String::fromUTF8 (",  Y: ") + String::fromUTF8 (getPadEffectInfo (i).yLabel));
        b->onClick = [this, i, b]
        {
            if (! b->getToggleState()) return;
            setParam ("fx_type", (float) i);
            if (processor.getChainParams().pad.mix <= 0.0f)
                setParam ("fx_mix", 0.6f);   // make the first choice audible straight away
        };
        addAndMakeVisible (b);
    }
    fxMix = makeKnob ("fx_mix", "Dry/Wet", ScapeColours::accent, ModTarget::FxMix, LP::left);

    // ---- Granular ----
    grainMix     = makeKnob ("grain_mix",     "Dry/Wet", Colour (0xffe6f7ff), ModTarget::GrainMix,     LP::above);
    grainSize    = makeKnob ("grain_size",    "Size",    Colour (0xffe6f7ff), ModTarget::GrainSize,    LP::above);
    grainDensity = makeKnob ("grain_density", "Density", Colour (0xffe6f7ff), ModTarget::GrainDensity, LP::above);
    grainPitch   = makeKnob ("grain_pitch",   "Pitch",   Colour (0xffe6f7ff), ModTarget::GrainPitch,   LP::above);
    grainSpray   = makeKnob ("grain_spray",   "Spray",   Colour (0xffe6f7ff), ModTarget::GrainSpray,   LP::above);

    // ---- Per-engine sound page ----
    soundPage.build ([this] (const String& id, const String& name, Colour colour, int target, Component* parent)
                     { return makeKnob (id, name, colour, (ModTarget) target, ModKnob::LabelPosition::above, parent); },
                     [] (int type, int index) { return ScrapeScapeProcessor::voiceParamID (type, index); });
    addAndMakeVisible (soundPage);

    for (auto* tab : { &soundsTab, &grainTab })
    {
        tab->setRadioGroupId (5001);
        tab->setClickingTogglesState (true);
        tab->setColour (TextButton::buttonOnColourId, ScapeColours::panelEdge.brighter (0.2f));
        addAndMakeVisible (tab);
    }
    soundsTab.setTooltip ("Sound controls for each engine (drag LFOs onto any knob)");
    grainTab.setTooltip ("Granular effect on everything that's playing");
    soundsTab.onClick = [this] { showSidebarPage (true); };
    grainTab.onClick = [this] { showSidebarPage (false); };

    canvas.onElementSelected = [this] (int type)
    {
        if (type < kNumSoundTypes)
        {
            soundPage.selectType (type);
            showSidebarPage (true);
        }
    };
    canvas.getVisualContext = [this] { return buildVisualContext(); };

    // ---- LFOs ----
    lfoPanel.getLfoValue = [this] (int i) { return processor.getLfoValue (i); };
    lfoPanel.getLfoTarget = [this] (int i) { return processor.getLfoTarget (i); };
    lfoPanel.setLfoTarget = [this] (int lfo, int target) { assignLfo (lfo, target); };
    addAndMakeVisible (lfoPanel);

    // ---- Canvas as XY pad ----
    canvas.isPadMode = [this] { return padModeButton.getToggleState(); };
    canvas.getPadInfo = [this]
    {
        const auto c = processor.getChainParams();
        const auto& info = getPadEffectInfo (c.pad.type);
        SceneCanvas::PadInfo pi;
        pi.name = String::fromUTF8 (info.name);
        pi.xLabel = String::fromUTF8 (info.xLabel);
        pi.yLabel = String::fromUTF8 (info.yLabel);
        pi.x = c.pad.x; pi.y = c.pad.y;
        pi.modX = processor.getModulatedValue ((int) ModTarget::PadX);
        pi.modY = processor.getModulatedValue ((int) ModTarget::PadY);
        const float modMix = processor.getModulatedValue ((int) ModTarget::FxMix);
        pi.mix = modMix >= 0.0f ? modMix : c.pad.mix;
        return pi;
    };
    canvas.onPadGesture = [this] (bool starting)
    {
        for (auto* id : { "fx_x", "fx_y" })
            if (auto* param = processor.getParameters().getParameter (id))
                starting ? param->beginChangeGesture() : param->endChangeGesture();
    };
    canvas.onPadMoved = [this] (float x, float y)
    {
        for (auto [id, v] : { std::pair<const char*, float> { "fx_x", x }, { "fx_y", y } })
            if (auto* param = processor.getParameters().getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (v));
    };
    canvas.onLfoDropped = [this] (int lfo, int target) { assignLfo (lfo, target); };

    outliner.onApply = [this] (int type, ElementSkin skin)
    {
        processor.getSkins().set (type, std::move (skin));
        closeOutliner();
        canvas.flash (String (getElementInfo (type).name) + " now uses your image");
    };
    outliner.onCancel = [this] { closeOutliner(); };
    outliner.onChooseFile = [this] (int type) { chooseImageFile (type); };
    addChildComponent (outliner);

    showSidebarPage (true);

    // Set last so every child (including slider text boxes) picks up the theme.
    setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    setResizeLimits (1300, 800, 2600, 1700);
    setSize (1420, 880);

    updateTransport();
    lfoPanel.refresh();
    timerCallback();   // sync effect buttons etc. immediately
    lastTimerSeconds = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (30);
}

ScrapeScapeEditor::~ScrapeScapeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

std::unique_ptr<ModKnob> ScrapeScapeEditor::makeKnob (const String& paramID, const String& name, Colour colour,
                                                      ModTarget target, ModKnob::LabelPosition pos, Component* parent)
{
    auto k = std::make_unique<ModKnob> (processor.getParameters(), paramID, name, colour, (int) target, pos);
    k->getModulatedValue = [this] (int t) { return processor.getModulatedValue (t); };
    k->getLfoTarget = [this] (int lfo) { return processor.getLfoTarget (lfo); };
    k->onLfoDropped = [this] (int lfo, int t) { assignLfo (lfo, t); };
    (parent != nullptr ? parent : static_cast<Component*> (this))->addAndMakeVisible (*k);
    return k;
}

void ScrapeScapeEditor::showSidebarPage (bool sounds)
{
    soundsTab.setToggleState (sounds, dontSendNotification);
    grainTab.setToggleState (! sounds, dontSendNotification);
    soundPage.setVisible (sounds);
    for (auto* k : { grainMix.get(), grainSize.get(), grainDensity.get(), grainPitch.get(), grainSpray.get() })
        if (k != nullptr) k->setVisible (! sounds);
    repaint (sidebarArea);
}

VisualContext ScrapeScapeEditor::buildVisualContext() const
{
    // Start from the knob values, then apply whatever the LFOs are doing right now,
    // so the visuals move with the modulation too.
    auto c = processor.getChainParams();
    for (int t = 1; t < kNumModTargets; ++t)
    {
        const float m = processor.getModulatedValue (t);
        if (m >= 0.0f) SoundChain::setFromNorm (c, t, m);
    }

    VisualContext v;
    v.brightness = c.engine.brightness;
    v.activity = c.engine.activity;
    v.reverb = c.engine.reverb;
    v.echo = c.engine.delay;
    v.fxType = c.pad.type;
    v.fxMix = c.pad.mix;
    v.fxX = c.pad.x;
    v.fxY = c.pad.y;
    v.grainMix = c.grain.mix;
    v.chordRoot = processor.getChordRoot();
    v.voice = c.engine.voice;
    v.playing = processor.isPlaying();
    if (auto* vis = processor.getParameters().getRawParameterValue ("visuals"))
        v.amount = vis->load();
    return v;
}

void ScrapeScapeEditor::assignLfo (int lfo, int target)
{
    processor.setLfoTarget (lfo, target);
    lfoPanel.refresh();
    if (target > 0)
        canvas.flash ("LFO " + String (lfo + 1) + String::fromUTF8 (" \xe2\x86\x92 ") + getModTargetName (target), 1.8f);
    repaint();
}

void ScrapeScapeEditor::setParam (const String& id, float value)
{
    if (auto* p = processor.getParameters().getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
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

    for (auto r : { sidebarArea, fxBarArea })
    {
        g.setColour (ScapeColours::panel.withAlpha (0.6f));
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
        g.setColour (ScapeColours::panelEdge.withAlpha (0.6f));
        g.drawRoundedRectangle (r.toFloat(), 12.0f, 1.0f);
    }
    if (! sidebarArea.isEmpty() && grainTab.getToggleState())
    {
        g.setColour (ScapeColours::textDim);
        g.setFont (FontOptions (11.0f));
        g.drawText ("Grains cut from everything that's playing", grainMix->getBounds().getUnion (grainSpray->getBounds())
                        .translated (0, -26).withHeight (18), Justification::centred);
    }

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
    header.removeFromLeft (6);
    presetButton.setBounds (header.removeFromLeft (92));

    // Bottom panel: palette + main knobs
    auto bottom = area.removeFromBottom (190).reduced (24, 16);
    auto palette = bottom.removeFromTop (78);
    const int buttonW = palette.getWidth() / kNumElementTypes;
    for (auto* b : paletteButtons)
        b->setBounds (palette.removeFromLeft (buttonW).reduced (3, 0));

    bottom.removeFromTop (8);
    ModKnob* knobs[] = { activity.get(), brightness.get(), tempo.get(), reverb.get(), echo.get(), volume.get(), visuals.get() };
    const int knobW = bottom.getWidth() / 7;
    for (auto* k : knobs)
        k->setBounds (bottom.removeFromLeft (knobW));

    // Right sidebar: granular + LFOs
    sidebarArea = area.removeFromRight (310).withTrimmedRight (12).withTrimmedBottom (2);
    {
        auto side = sidebarArea.reduced (12, 10);
        auto tabs = side.removeFromTop (28);
        soundsTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (2, 0));
        grainTab.setBounds (tabs.reduced (2, 0));
        side.removeFromTop (8);

        auto page = side.removeFromTop (jmax (230, side.getHeight() * 2 / 5));
        soundPage.setBounds (page);
        auto grainRow = page.withSizeKeepingCentre (page.getWidth(), 90);
        ModKnob* grains[] = { grainMix.get(), grainSize.get(), grainDensity.get(), grainPitch.get(), grainSpray.get() };
        const int gw = grainRow.getWidth() / 5;
        for (auto* k : grains)
            k->setBounds (grainRow.removeFromLeft (gw));
        side.removeFromTop (10);
        lfoPanel.setBounds (side);
    }

    // FX bar under the canvas: pad toggle, effect buttons, dry/wet
    fxBarArea = area.removeFromBottom (84).withTrimmedLeft (12).withTrimmedRight (10).withTrimmedTop (8);
    {
        auto bar = fxBarArea.reduced (10, 8);
        padModeButton.setBounds (bar.removeFromLeft (84).withSizeKeepingCentre (84, 34));
        bar.removeFromLeft (12);
        fxMix->setBounds (bar.removeFromRight (170).expanded (0, 6));
        bar.removeFromRight (8);
        const int fw = bar.getWidth() / kNumPadEffects;
        for (auto* b : fxButtons)
            b->setBounds (bar.removeFromLeft (fw).reduced (2, 0).withSizeKeepingCentre (fw - 4, 34));
    }

    canvas.setBounds (area.withTrimmedLeft (12).withTrimmedRight (10));
    outliner.setBounds (getLocalBounds());
}

//==============================================================================
void ScrapeScapeEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    canvas.advanceAnimation ((float) jlimit (0.0, 0.2, now - lastTimerSeconds));
    lastTimerSeconds = now;
    canvas.repaint();

    const int skinVersion = processor.getSkins().getVersion();
    for (auto* b : paletteButtons)
        if (b->getToggleState() || skinVersion != lastSkinVersion) b->repaint();
    lastSkinVersion = skinVersion;

    updateTransport();

    // Keep the effect buttons in sync with the (automatable) FX type, animate LFO bits.
    const int fxType = processor.getChainParams().pad.type;
    for (int i = 0; i < fxButtons.size(); ++i)
        if (fxButtons[i]->getToggleState() != (i == fxType))
            fxButtons[i]->setToggleState (i == fxType, dontSendNotification);
    padModeButton.setButtonText (padModeButton.getToggleState() ? String::fromUTF8 ("XY Pad \xe2\x97\x8f") : String ("XY Pad"));

    lfoPanel.refresh();
    if (soundPage.isVisible()) soundPage.repaintKnobs();
    for (auto* k : { activity.get(), brightness.get(), reverb.get(), echo.get(), fxMix.get(),
                     grainMix.get(), grainSize.get(), grainDensity.get(), grainPitch.get(), grainSpray.get() })
        k->repaint();

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
                                                                 processor.getChainParams(), processor.getElements());
                                  updateTransport();
                              });
}

//==============================================================================
// Custom images
//==============================================================================
int ScrapeScapeEditor::paletteTypeAt (Point<int> p) const
{
    for (auto* b : paletteButtons)
        if (b->getBounds().contains (p))
            return b->type;
    return -1;
}

void ScrapeScapeEditor::setDropHighlight (int type, bool active)
{
    for (auto* b : paletteButtons)
    {
        const bool on = active && b->type == type;
        if (b->dropHighlight != on) { b->dropHighlight = on; b->repaint(); }
    }
}

static bool isPresetFile (const File& f) { return f.hasFileExtension (ScrapeScapeProcessor::presetExtension); }

bool ScrapeScapeEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& path : files)
    {
        const File f (path);
        if (isPresetFile (f) || SkinTools::isSupportedImageFile (f))
            return true;
    }
    return false;
}

void ScrapeScapeEditor::fileDragEnter (const StringArray& files, int x, int y)
{
    dragActive = true;
    fileDragMove (files, x, y);
    repaint();
}

void ScrapeScapeEditor::fileDragMove (const StringArray&, int x, int y)
{
    setDropHighlight (paletteTypeAt ({ x, y }), true);
}

void ScrapeScapeEditor::fileDragExit (const StringArray&)
{
    dragActive = false;
    setDropHighlight (-1, false);
    repaint();
}

void ScrapeScapeEditor::filesDropped (const StringArray& files, int x, int y)
{
    dragActive = false;
    setDropHighlight (-1, false);
    repaint();

    for (auto& path : files)
    {
        const File f (path);
        if (isPresetFile (f)) { loadPresetFile (f); return; }
    }

    for (auto& path : files)
    {
        const File f (path);
        if (SkinTools::isSupportedImageFile (f))
        {
            const int slot = paletteTypeAt ({ x, y });
            openImageFile (slot >= 0 ? slot : selectedType, f);
            return;
        }
    }
}

void ScrapeScapeEditor::paintOverChildren (Graphics& g)
{
    if (! dragActive) return;

    auto r = canvas.getBounds().toFloat().reduced (8.0f);
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 12.0f);
    Path border;
    border.addRoundedRectangle (r, 12.0f);
    Path dashed;
    const float dashes[] = { 10.0f, 8.0f };
    PathStrokeType (2.0f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (Colours::white.withAlpha (0.8f));
    g.fillPath (dashed);
    g.setFont (FontOptions (18.0f));
    g.drawFittedText ("Drop an image on a palette slot below to set that element's look\n"
                      "(or anywhere here to use it for " + String (getElementInfo (selectedType).name) + ").\n"
                      "Drop a .scapepreset file to load it.",
                      r.toNearestInt().reduced (30), Justification::centred, 3);
}

void ScrapeScapeEditor::openImageFile (int type, const File& file)
{
    auto img = SkinTools::prepareSource (SkinTools::loadAnyImage (file));
    if (! img.isValid())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't open image",
                                          file.getFileName() + " couldn't be read as an image.");
        return;
    }

    ElementSkin skin;
    skin.source = img;
    skin.shape = ElementSkin::freehand;
    openOutliner (type, skin);
}

void ScrapeScapeEditor::chooseImageFile (int type)
{
    fileChooser = std::make_unique<FileChooser> ("Choose an image for " + String (getElementInfo (type).name),
                                                 File::getSpecialLocation (File::userPicturesDirectory),
                                                 SkinTools::getImageWildcard());
    fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this, type, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (const FileChooser& fc)
                              {
                                  if (safeThis == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) openImageFile (type, f);
                              });
}

void ScrapeScapeEditor::openOutliner (int type, const ElementSkin& skin)
{
    outliner.open (type, skin);
}

void ScrapeScapeEditor::closeOutliner()
{
    outliner.setVisible (false);
    for (auto* b : paletteButtons) b->repaint();
}

void ScrapeScapeEditor::showPaletteMenu (int type)
{
    auto& skins = processor.getSkins();
    const bool has = skins.hasSkin (type);
    const auto name = String (getElementInfo (type).name);

    PopupMenu menu;
    menu.addSectionHeader (name);
    menu.addItem (1, "Use an image for " + name + "...");
    menu.addItem (2, "Re-outline image...", has);
    menu.addItem (3, "Back to built-in " + name + " icon", has);
    menu.addSeparator();
    menu.addItem (4, "Reset all elements to built-in icons");

    auto* button = paletteButtons[type];
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (button),
                        [this, type, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (int result)
                        {
                            if (safeThis == nullptr) return;
                            auto& sk = processor.getSkins();
                            switch (result)
                            {
                                case 1: chooseImageFile (type); break;
                                case 2: openOutliner (type, sk.get (type)); break;
                                case 3: sk.reset (type); break;
                                case 4: sk.resetAll(); break;
                                default: break;
                            }
                        });
}

//==============================================================================
// Presets
//==============================================================================
void ScrapeScapeEditor::showPresetMenu()
{
    PopupMenu menu;
    menu.addSectionHeader ("Current: " + processor.getCurrentPresetName());
    menu.addItem (1, "Save preset...");
    menu.addItem (2, "Load preset from file...");
    menu.addItem (3, "Show presets folder");

    PopupMenu factory;
    const auto factoryNames = ScrapeScapeProcessor::getFactoryPresetNames();
    for (int i = 0; i < factoryNames.size(); ++i)
        factory.addItem (100 + i, factoryNames[i]);
    menu.addSeparator();
    menu.addSubMenu ("Factory scenes (keep your images)", factory);

    const auto userPresets = ScrapeScapeProcessor::getPresetsFolder()
                                 .findChildFiles (File::findFiles, false, String ("*") + ScrapeScapeProcessor::presetExtension);
    auto sorted = userPresets;
    std::sort (sorted.begin(), sorted.end(), [] (const File& a, const File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });

    if (! sorted.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("My presets");
        for (int i = 0; i < sorted.size(); ++i)
            menu.addItem (1000 + i, sorted[i].getFileNameWithoutExtension());
    }

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetButton),
                        [this, sorted, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (int result)
                        {
                            if (safeThis == nullptr || result == 0) return;
                            if (result == 1) savePresetAs();
                            else if (result == 2) loadPresetFromChooser();
                            else if (result == 3) ScrapeScapeProcessor::getPresetsFolder().startAsProcess();
                            else if (result >= 1000) loadPresetFile (sorted[result - 1000]);
                            else if (result >= 100)
                            {
                                processor.loadFactoryPreset (result - 100);
                                canvas.flash ("Loaded " + processor.getCurrentPresetName());
                            }
                        });
}

void ScrapeScapeEditor::savePresetAs()
{
    auto name = processor.getCurrentPresetName();
    if (name.isEmpty() || name == "Default") name = "My Scene";
    const auto start = ScrapeScapeProcessor::getPresetsFolder().getChildFile (File::createLegalFileName (name)
                                                                               + ScrapeScapeProcessor::presetExtension);
    fileChooser = std::make_unique<FileChooser> ("Save preset", start, String ("*") + ScrapeScapeProcessor::presetExtension);
    fileChooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles
                                  | FileBrowserComponent::warnAboutOverwriting,
                              [this, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (const FileChooser& fc)
                              {
                                  if (safeThis == nullptr) return;
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  f = f.withFileExtension (ScrapeScapeProcessor::presetExtension);
                                  if (processor.savePreset (f))
                                      canvas.flash ("Saved preset " + f.getFileNameWithoutExtension());
                                  else
                                      AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't save preset",
                                                                        "Couldn't write " + f.getFullPathName());
                              });
}

void ScrapeScapeEditor::loadPresetFromChooser()
{
    fileChooser = std::make_unique<FileChooser> ("Load preset", ScrapeScapeProcessor::getPresetsFolder(),
                                                 String ("*") + ScrapeScapeProcessor::presetExtension);
    fileChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this, safeThis = Component::SafePointer<ScrapeScapeEditor> (this)] (const FileChooser& fc)
                              {
                                  if (safeThis == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) loadPresetFile (f);
                              });
}

void ScrapeScapeEditor::loadPresetFile (const File& file)
{
    if (processor.loadPreset (file))
        canvas.flash ("Loaded preset " + file.getFileNameWithoutExtension());
    else
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Couldn't load preset",
                                          file.getFileName() + " isn't a Scrape-Scape preset.");
}
