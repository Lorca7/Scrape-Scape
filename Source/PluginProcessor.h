#pragma once

#include "Engine/ScapeEngine.h"
#include "OfflineRenderer.h"
#include "Recorder.h"

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
    int getChordRoot() const noexcept { return engine.getChordRoot(); }

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

    void loadDefaultScene();
    void scatterScene (juce::Random& random);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState parameters;
    scape::ElementBank elements;
    scape::ScapeEngine engine;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScrapeScapeProcessor)
};
