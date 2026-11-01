#include "SoundPage.h"
#include "SceneCanvas.h"
#include "ScapeLookAndFeel.h"
#include "Engine/Modulation.h"

using namespace juce;
using namespace scape;

SoundPage::TypeTab::TypeTab (int t, SkinBank& s) : Button (getElementInfo (t).name), type (t), skins (s)
{
    setClickingTogglesState (true);
    setRadioGroupId (4001);
    setTooltip (String ("Sound controls for ") + getElementInfo (t).name);
}

void SoundPage::TypeTab::paintButton (Graphics& g, bool over, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.5f);
    const Colour col (getElementInfo (type).colour);
    g.setColour (getToggleState() ? col.withAlpha (0.22f) : ScapeColours::background.withAlpha (over ? 0.8f : 0.5f));
    g.fillRoundedRectangle (b, 7.0f);
    if (getToggleState())
    {
        g.setColour (col);
        g.drawRoundedRectangle (b, 7.0f, 1.4f);
    }
    const auto glyph = SkinTools::scaledHighQuality (skins.getGlyph (type), 48);
    drawElementGlyph (g, type, b.getCentre(), jmin (b.getWidth(), b.getHeight()) * 0.26f,
                      getToggleState() ? 0.5f : 0.1f, 0.0f, false, glyph.isValid() ? &glyph : nullptr);
}

SoundPage::SoundPage (SkinBank& s) : skins (s)
{
    for (int t = 0; t < kNumSoundTypes; ++t)
    {
        auto* tab = tabs.add (new TypeTab (t, skins));
        tab->onClick = [this, t] { selectType (t); };
        addAndMakeVisible (tab);
    }
}

void SoundPage::build (const KnobFactory& factory, const std::function<String (int, int)>& paramID)
{
    for (int t = 0; t < kNumSoundTypes; ++t)
        for (int k = 0; k < kVoiceParams; ++k)
        {
            const auto& spec = getVoiceParamSpec (t, k);
            auto knob = factory (paramID (t, k), spec.name, Colour (getElementInfo (t).colour), voiceTarget (t, k), this);
            knob->slider.setTooltip (String (getElementInfo (t).name) + " " + spec.name + String::fromUTF8 (" \xe2\x80\x94 ")
                                     + spec.help + ". Drag an LFO here to modulate it.");
            knob->setVisible (false);
            knobs[(size_t) t][(size_t) k] = std::move (knob);
        }
    selectType (0);
}

void SoundPage::selectType (int type)
{
    selected = jlimit (0, kNumSoundTypes - 1, type);
    tabs[selected]->setToggleState (true, dontSendNotification);
    for (int t = 0; t < kNumSoundTypes; ++t)
        for (auto& k : knobs[(size_t) t])
            if (k != nullptr) k->setVisible (t == selected);
    resized();
    repaint();
}

void SoundPage::repaintKnobs()
{
    for (auto& k : knobs[(size_t) selected])
        if (k != nullptr) k->repaint();
    for (auto* t : tabs) t->repaint();
}

void SoundPage::paint (Graphics& g)
{
    g.setColour (Colour (getElementInfo (selected).colour).withAlpha (0.9f));
    g.setFont (FontOptions (12.0f, Font::bold));
    auto r = getLocalBounds().withTrimmedTop (44).removeFromTop (16);
    g.drawText (String (getElementInfo (selected).name).toUpperCase(), r, Justification::centredLeft);
    g.setColour (ScapeColours::textDim);
    g.setFont (FontOptions (11.0f));
    g.drawText ("applies to every " + String (getElementInfo (selected).name) + " on the canvas", r, Justification::centredRight);
}

void SoundPage::resized()
{
    auto area = getLocalBounds();
    auto tabRow = area.removeFromTop (40);
    const int tw = tabRow.getWidth() / kNumSoundTypes;
    for (auto* t : tabs)
        t->setBounds (tabRow.removeFromLeft (tw).reduced (1, 0));

    area.removeFromTop (22);
    const int rowH = area.getHeight() / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto line = area.removeFromTop (rowH);
        const int kw = line.getWidth() / 4;
        for (int c = 0; c < 4; ++c)
            if (auto& k = knobs[(size_t) selected][(size_t) (row * 4 + c)])
                k->setBounds (line.removeFromLeft (kw));
    }
}
