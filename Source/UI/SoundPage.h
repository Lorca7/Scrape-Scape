#pragma once

#include "ModKnob.h"
#include "Skins.h"
#include "Engine/VoiceParams.h"

#include <array>
#include <functional>

// Per-engine sound controls: pick an engine from the tabs, tweak its 8 controls.
// Every knob accepts LFO drops.
class SoundPage : public juce::Component
{
public:
    using KnobFactory = std::function<std::unique_ptr<ModKnob> (const juce::String& paramID, const juce::String& name,
                                                                juce::Colour colour, int target, juce::Component* parent)>;

    explicit SoundPage (SkinBank& skins);
    void build (const KnobFactory& factory, const std::function<juce::String (int type, int index)>& paramID);

    void selectType (int type);
    int getSelectedType() const noexcept { return selected; }
    void repaintKnobs();

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class TypeTab : public juce::Button
    {
    public:
        TypeTab (int type, SkinBank& skins);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        const int type;
        SkinBank& skins;
    };

    SkinBank& skins;
    juce::OwnedArray<TypeTab> tabs;
    std::array<std::array<std::unique_ptr<ModKnob>, scape::kVoiceParams>, scape::kNumSoundTypes> knobs;
    int selected = 0;
};
