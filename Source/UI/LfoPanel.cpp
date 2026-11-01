#include "LfoPanel.h"
#include "Engine/Modulation.h"
#include "ScapeLookAndFeel.h"

using namespace juce;
using namespace scape;

static String lfoID (int i, const char* suffix) { return "lfo" + String (i + 1) + "_" + suffix; }

//==============================================================================
LfoPanel::Handle::Handle (LfoPanel& o, int i) : owner (o), index (i)
{
    setMouseCursor (MouseCursor::DraggingHandCursor);
    setTooltip ("Drag LFO " + String (i + 1) + " onto the XY pad or onto any knob with a dot target");
}

void LfoPanel::Handle::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);
    const auto col = getLfoColour (index);
    const float d = jmin (b.getWidth(), b.getHeight());
    auto circle = Rectangle<float> (d, d).withCentre (b.getCentre());

    g.setColour (col.withAlpha (0.18f + 0.12f * (0.5f + 0.5f * value)));
    g.fillEllipse (circle);
    g.setColour (col);
    g.drawEllipse (circle.reduced (0.5f), dragging ? 2.5f : 1.5f);

    // Waveform glyph
    Path wave;
    auto w = circle.reduced (d * 0.24f);
    for (int k = 0; k <= 24; ++k)
    {
        const float t = (float) k / 24.0f;
        float v = 0.0f;
        switch ((LfoShape) shape)
        {
            case LfoShape::Sine:       v = std::sin (t * MathConstants<float>::twoPi); break;
            case LfoShape::Triangle:   v = 1.0f - 4.0f * std::abs (t - 0.5f) ; v = -v; break;
            case LfoShape::Saw:        v = 2.0f * t - 1.0f; break;
            case LfoShape::Square:     v = t < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::SampleHold: { static const float sh[] = { 0.3f, -0.7f, 0.8f, -0.2f }; v = sh[jmin (3, (int) (t * 4.0f))]; break; }
            case LfoShape::Drift:      v = 0.6f * std::sin (t * 5.0f) + 0.3f * std::sin (t * 11.0f + 1.0f); break;
            case LfoShape::NumShapes:  break;
        }
        const float x = w.getX() + t * w.getWidth();
        const float y = w.getCentreY() - v * w.getHeight() * 0.35f;
        if (k == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
    }
    g.setColour (col.withAlpha (0.9f));
    g.strokePath (wave, PathStrokeType (1.4f));

    // Live value dot
    const float vy = circle.getCentreY() - value * circle.getHeight() * 0.36f;
    g.setColour (Colours::white);
    g.fillEllipse (circle.getRight() - d * 0.2f, vy - 2.5f, 5.0f, 5.0f);

    g.setColour (ScapeColours::text);
    g.setFont (FontOptions (10.0f, Font::bold));
    g.drawText (String (index + 1), circle.withTrimmedTop (d * 0.62f), Justification::centred);
}

void LfoPanel::Handle::mouseDrag (const MouseEvent&)
{
    if (auto* container = DragAndDropContainer::findParentDragContainerFor (this))
    {
        if (! container->isDragAndDropActive())
        {
            dragging = true;
            repaint();
            const auto img = createComponentSnapshot (getLocalBounds(), true, 1.0f);
            container->startDragging ("lfo:" + String (index), this, ScaledImage (img), true);
        }
    }
}

void LfoPanel::Handle::mouseUp (const MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
LfoPanel::LfoPanel (AudioProcessorValueTreeState& s) : state (s)
{
    for (int i = 0; i < 4; ++i)
    {
        auto& r = rows[(size_t) i];
        r.handle = std::make_unique<Handle> (*this, i);
        addAndMakeVisible (*r.handle);

        for (int k = 0; k < kNumLfoShapes; ++k)
            r.shape.addItem (getLfoShapeName (k), k + 1);
        r.shapeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (state, lfoID (i, "shape"), r.shape);
        addAndMakeVisible (r.shape);

        r.rate = std::make_unique<ModKnob> (state, lfoID (i, "rate"), "Rate", getLfoColour (i), 0, ModKnob::LabelPosition::above);
        r.depth = std::make_unique<ModKnob> (state, lfoID (i, "depth"), "Depth", getLfoColour (i), 0, ModKnob::LabelPosition::above);
        addAndMakeVisible (*r.rate);
        addAndMakeVisible (*r.depth);

        r.targetButton.setTooltip ("Choose what LFO " + String (i + 1) + " modulates (or drag its handle)");
        r.targetButton.onClick = [this, i] { showTargetMenu (i); };
        addAndMakeVisible (r.targetButton);
    }
    refresh();
}

void LfoPanel::showTargetMenu (int lfo)
{
    PopupMenu menu;
    menu.addSectionHeader ("LFO " + String (lfo + 1) + " modulates...");
    const int current = getLfoTarget ? getLfoTarget (lfo) : 0;
    PopupMenu global, pad, grain;
    for (int t : { (int) ModTarget::Activity, (int) ModTarget::Brightness, (int) ModTarget::Reverb, (int) ModTarget::Echo })
        global.addItem (t, getModTargetName (t), true, t == current);
    for (int t : { (int) ModTarget::PadX, (int) ModTarget::PadY, (int) ModTarget::FxMix })
        pad.addItem (t, getModTargetName (t), true, t == current);
    for (int t = (int) ModTarget::GrainMix; t <= (int) ModTarget::GrainSpray; ++t)
        grain.addItem (t, getModTargetName (t), true, t == current);
    menu.addSubMenu ("Global", global);
    menu.addSubMenu ("XY effects pad", pad);
    menu.addSubMenu ("Granular", grain);
    menu.addSeparator();
    for (int type = 0; type < kNumSoundTypes; ++type)
    {
        PopupMenu sub;
        for (int k = 0; k < kVoiceParams; ++k)
        {
            const int t = voiceTarget (type, k);
            sub.addItem (t, getVoiceParamSpec (type, k).name, true, t == current);
        }
        menu.addSubMenu (getElementInfo (type).name, sub, true, Image(), current >= voiceTarget (type, 0) && current <= voiceTarget (type, kVoiceParams - 1));
    }
    menu.addSeparator();
    menu.addItem (100000, "Nothing (clear)", current != 0);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&rows[(size_t) lfo].targetButton),
                        [this, lfo, safe = Component::SafePointer<LfoPanel> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0 || ! setLfoTarget) return;
                            setLfoTarget (lfo, result == 100000 ? 0 : result);
                            refresh();
                        });
}

void LfoPanel::refresh()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& r = rows[(size_t) i];
        if (r.handle == nullptr) continue;
        r.handle->value = getLfoValue ? getLfoValue (i) : 0.0f;
        r.handle->shape = r.shape.getSelectedId() - 1;
        r.handle->repaint();

        const int t = getLfoTarget ? getLfoTarget (i) : 0;
        const auto text = t == 0 ? String ("Drag to assign") : String::fromUTF8 ("\xe2\x86\x92 ") + getModTargetName (t);
        if (r.targetButton.getButtonText() != text)
        {
            r.targetButton.setButtonText (text);
            r.targetButton.setColour (TextButton::textColourOffId, t == 0 ? ScapeColours::textDim : getLfoColour (i));
        }
    }
}

void LfoPanel::paint (Graphics& g)
{
    g.setColour (ScapeColours::textDim);
    g.setFont (FontOptions (12.0f, Font::bold));
    g.drawText ("LFOs", getLocalBounds().removeFromTop (18), Justification::centredLeft);

    g.setColour (ScapeColours::panelEdge.withAlpha (0.6f));
    auto area = getLocalBounds().withTrimmedTop (20);
    const int rowH = area.getHeight() / 4;
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (area.getY() + i * rowH, (float) area.getX(), (float) area.getRight());
}

void LfoPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop (20);
    const int rowH = area.getHeight() / 4;

    for (auto& r : rows)
    {
        auto row = area.removeFromTop (rowH).reduced (0, 4);
        r.handle->setBounds (row.removeFromLeft (jmin (52, row.getHeight())));
        row.removeFromLeft (8);

        auto knobs = row.removeFromRight (jmin (120, row.getWidth() / 2));
        r.rate->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        r.depth->setBounds (knobs);

        row.removeFromRight (4);
        auto controls = row.withSizeKeepingCentre (row.getWidth(), jmin (row.getHeight(), 64));
        r.shape.setBounds (controls.removeFromTop (28));
        controls.removeFromTop (6);
        r.targetButton.setBounds (controls.removeFromTop (28));
    }
}
