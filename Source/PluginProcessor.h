#pragma once

#include "Engine/SoundChain.h"
#include "OfflineRenderer.h"
#include "Recorder.h"
#include "Skins.h"

#include <juce_audio_processors/juce_audio_processors.h>

class ScrapeScapeProcessor : public juce::AudioProcessor
{
public:
    ScrapeScapeProcessor();
    ~ScrapeScapeProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    scape::ElementBank& getElements() noexcept { return elements; }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    scape::EngineParams getEngineParams() const noexcept;
    scape::ChainParams getChainParams() const noexcept;
    int getChordRoot() const noexcept { return chain.getEngine().getChordRoot(); }

    // Modulation readouts for the UI
    float getModulatedValue (int target) const noexcept { return chain.getModulatedValue (target); }
    float getLfoValue (int index) const noexcept { return chain.getLfoValue (index); }
    int getLfoTarget (int index) const noexcept;
    void setLfoTarget (int index, int target);
    static juce::String getTargetParamID (int target);
    static juce::String voiceParamID (int type, int index)
    {
        return juce::String (scape::getElementInfo (type).name).toLowerCase() + "_" + scape::getVoiceParamSpec (type, index).id;
    }
    static juce::String lfoParamID (int index, const char* suffix) { return "lfo" + juce::String (index + 1) + "_" + suffix; }

    // Transport: Stop fades the music out and pauses the engine; Play fades it back in.
    void setPlaying (bool shouldPlay) noexcept { playing.store (shouldPlay); }
    bool isPlaying() const noexcept { return playing.load(); }
    bool isSilent() const noexcept { return engineIdle.load(); }

    // Recording (live output -> WAV)
    bool startRecording();
    void stopRecording();
    bool isRecording() const noexcept { return recorder.isRecording(); }
    double getSecondsRecorded() const noexcept { return recorder.getSecondsRecorded(); }
    juce::File getLastTake() const { return recorder.getLastTake(); }

    // Offline rendering (scene -> WAV, faster than realtime)
    OfflineRenderer& getRenderer() noexcept { return renderer; }
    double getCurrentSampleRate() const noexcept { return currentSampleRate > 0.0 ? currentSampleRate : 48000.0; }

    // Custom element images
    SkinBank& getSkins() noexcept { return skins; }

    // Full state (parameters + elements + images), used for host state and presets.
    juce::ValueTree createStateTree();
    void applyStateTree (const juce::ValueTree& state, bool includeSkins = true);

    // Presets
    static constexpr const char* presetExtension = ".scapepreset";
    static juce::File getPresetsFolder();
    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    static juce::StringArray getFactoryPresetNames();
    void loadFactoryPreset (int index);
    juce::String getCurrentPresetName() const;

    void loadDefaultScene();
    void scatterScene (juce::Random& random);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState parameters;
    scape::ElementBank elements;
    SkinBank skins;
    mutable juce::CriticalSection presetNameLock;
    juce::String currentPresetName { "Default" };
    scape::SoundChain chain;
    Recorder recorder;
    OfflineRenderer renderer;
    double currentSampleRate = 0.0;
    std::atomic<bool> playing { true }, engineIdle { false };
    float transportGain = 1.0f;   // audio thread only
    std::vector<float> monoScratch = std::vector<float> (2 * 1024, 0.0f);

    std::atomic<float>* moodParam = nullptr;
    std::atomic<float>* activityParam = nullptr;
    std::atomic<float>* brightnessParam = nullptr;
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<float>* reverbParam = nullptr;
    std::atomic<float>* delayParam = nullptr;
    std::atomic<float>* volumeParam = nullptr;
    std::atomic<float>* fxTypeParam = nullptr;
    std::atomic<float>* fxXParam = nullptr;
    std::atomic<float>* fxYParam = nullptr;
    std::atomic<float>* fxMixParam = nullptr;
    std::atomic<float>* grainMixParam = nullptr;
    std::atomic<float>* grainSizeParam = nullptr;
    std::atomic<float>* grainDensityParam = nullptr;
    std::atomic<float>* grainPitchParam = nullptr;
    std::atomic<float>* grainSprayParam = nullptr;
    struct LfoParamPtrs { std::atomic<float>* shape = nullptr; std::atomic<float>* rate = nullptr;
                          std::atomic<float>* depth = nullptr; std::atomic<float>* target = nullptr; };
    std::array<LfoParamPtrs, scape::kNumLfos> lfoParams;
    std::array<std::array<std::atomic<float>*, scape::kVoiceParams>, scape::kNumSoundTypes> voiceParams {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeScapeProcessor)
};
