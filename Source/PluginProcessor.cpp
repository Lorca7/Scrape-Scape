#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace scape;

namespace ParamIDs
{
    static constexpr const char* mood = "mood";
    static constexpr const char* activity = "activity";
    static constexpr const char* brightness = "brightness";
    static constexpr const char* tempo = "tempo";
    static constexpr const char* reverb = "reverb";
    static constexpr const char* delay = "delay";
    static constexpr const char* volume = "volume";
}

juce::AudioProcessorValueTreeState::ParameterLayout ScrapeScapeProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray moodNames;
    for (int i = 0; i < kNumMoods; ++i)
        moodNames.add (getMood (i).name);

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mood, 1 }, "Mood", moodNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::activity, 1 }, "Activity",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::brightness, 1 }, "Brightness",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.6f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tempo, 1 }, "Tempo",
                                                       NormalisableRange<float> (36.0f, 110.0f, 1.0f), 64.0f,
                                                       AudioParameterFloatAttributes().withLabel ("BPM").withStringFromValueFunction (
                                                           [] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::reverb, 1 }, "Reverb",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.55f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::delay, 1 }, "Echo",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::volume, 1 }, "Volume",
                                                       NormalisableRange<float> (-40.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
                                                           [] (float v, int) { return String (v, 1) + " dB"; })));
    return layout;
}

ScrapeScapeProcessor::ScrapeScapeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "ScrapeScape", createParameterLayout())
{
    moodParam = parameters.getRawParameterValue (ParamIDs::mood);
    activityParam = parameters.getRawParameterValue (ParamIDs::activity);
    brightnessParam = parameters.getRawParameterValue (ParamIDs::brightness);
    tempoParam = parameters.getRawParameterValue (ParamIDs::tempo);
    reverbParam = parameters.getRawParameterValue (ParamIDs::reverb);
    delayParam = parameters.getRawParameterValue (ParamIDs::delay);
    volumeParam = parameters.getRawParameterValue (ParamIDs::volume);

    loadDefaultScene();
}

ScrapeScapeProcessor::~ScrapeScapeProcessor()
{
    recorder.stop();
    renderer.cancel();
}

void ScrapeScapeProcessor::loadDefaultScene()
{
    elements.clear();
    elements.add (ElementType::Drone,   0.28f, 0.58f, 1.2f);
    elements.add (ElementType::Bass,    0.52f, 0.82f, 1.0f);
    elements.add (ElementType::Bell,    0.70f, 0.30f, 1.0f);
    elements.add (ElementType::Shimmer, 0.84f, 0.18f, 0.9f);
    elements.add (ElementType::Shade,   0.18f, 0.40f, 0.9f);
}

void ScrapeScapeProcessor::scatterScene (juce::Random& random)
{
    elements.clear();

    // Always a harmonic foundation, then a handful of random colours.
    elements.add (ElementType::Drone, random.nextFloat() * 0.8f + 0.1f, 0.45f + random.nextFloat() * 0.35f,
                  0.8f + random.nextFloat() * 0.6f);
    if (random.nextBool())
        elements.add (ElementType::Bass, random.nextFloat() * 0.8f + 0.1f, 0.75f + random.nextFloat() * 0.2f, 1.0f);

    const int extras = 2 + random.nextInt (4);
    for (int i = 0; i < extras; ++i)
    {
        const auto t = (ElementType) random.nextInt (kNumElementTypes);
        elements.add (t, random.nextFloat() * 0.84f + 0.08f, random.nextFloat() * 0.84f + 0.08f,
                      0.6f + random.nextFloat() * 0.8f);
    }
}

EngineParams ScrapeScapeProcessor::getEngineParams() const noexcept
{
    EngineParams p;
    p.mood = juce::jlimit (0, kNumMoods - 1, (int) std::lround (moodParam->load()));
    p.activity = activityParam->load();
    p.brightness = brightnessParam->load();
    p.tempo = tempoParam->load();
    p.reverb = reverbParam->load();
    p.delay = delayParam->load();
    p.volumeDb = volumeParam->load();
    return p;
}

//==============================================================================
void ScrapeScapeProcessor::prepareToPlay (double sampleRate, int)
{
    if (isRecording() && ! juce::approximatelyEqual (sampleRate, currentSampleRate))
        recorder.stop(); // a take can't change sample rate mid-file

    currentSampleRate = sampleRate;
    monoScratch.assign (2 * 1024, 0.0f);
    engine.setParams (getEngineParams());
    engine.prepare (sampleRate);
}

void ScrapeScapeProcessor::releaseResources() {}

bool ScrapeScapeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ScrapeScapeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();

    const int n = buffer.getNumSamples();
    buffer.clear();

    if (buffer.getNumChannels() == 0 || n == 0)
        return;

    engine.setParams (getEngineParams());

    if (buffer.getNumChannels() >= 2)
    {
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, elements);
    }
    else
    {
        // Mono output: render stereo in pieces through a preallocated scratch buffer.
        auto* mono = buffer.getWritePointer (0);
        const int chunk = (int) monoScratch.size() / 2;
        for (int pos = 0; pos < n; pos += chunk)
        {
            const int len = juce::jmin (chunk, n - pos);
            engine.process (monoScratch.data(), monoScratch.data() + chunk, len, elements);
            for (int i = 0; i < len; ++i)
                mono[pos + i] = 0.5f * (monoScratch[(size_t) i] + monoScratch[(size_t) (chunk + i)]);
        }
    }

    recorder.write (buffer);
}

//==============================================================================
bool ScrapeScapeProcessor::startRecording()
{
    return recorder.start (Recorder::createNewTakeFile(), getCurrentSampleRate(),
                           juce::jmax (1, getTotalNumOutputChannels()));
}

void ScrapeScapeProcessor::stopRecording()
{
    recorder.stop();
}

//==============================================================================
void ScrapeScapeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

    if (auto old = state.getChildWithName ("Elements"); old.isValid())
        state.removeChild (old, nullptr);

    juce::ValueTree list ("Elements");
    for (int i = 0; i < kMaxElements; ++i)
    {
        const auto e = elements.get (i);
        if (! e.active) continue;

        juce::ValueTree node ("Element");
        node.setProperty ("type", e.type, nullptr);
        node.setProperty ("x", e.x, nullptr);
        node.setProperty ("y", e.y, nullptr);
        node.setProperty ("size", e.size, nullptr);
        node.setProperty ("seed", (juce::int64) e.seed, nullptr);
        list.appendChild (node, nullptr);
    }
    state.appendChild (list, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ScrapeScapeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    auto list = state.getChildWithName ("Elements");

    if (list.isValid())
    {
        elements.clear();
        int slot = 0;
        for (auto node : list)
        {
            if (slot >= kMaxElements) break;
            ElementSnapshot e;
            e.active = true;
            e.type = (int) node.getProperty ("type", 0);
            e.x = (float) node.getProperty ("x", 0.5f);
            e.y = (float) node.getProperty ("y", 0.5f);
            e.size = (float) node.getProperty ("size", 1.0f);
            e.seed = (std::uint32_t) (juce::int64) node.getProperty ("seed", 0);
            elements.set (slot++, e);
        }
        state.removeChild (list, nullptr);
    }

    parameters.replaceState (state);
}

//==============================================================================
juce::AudioProcessorEditor* ScrapeScapeProcessor::createEditor()
{
    return new ScrapeScapeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScrapeScapeProcessor();
}
