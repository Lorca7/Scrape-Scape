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

    // Transport: once fully faded out, the engine is paused (no CPU, no clock advance).
    const bool play = playing.load();
    if (! play && transportGain <= 0.0f)
    {
        engineIdle = true;
        recorder.write (buffer); // keep a running take continuous (silence)
        return;
    }
    engineIdle = false;

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

    // Fade in over ~1 s on Play, out over ~1.5 s on Stop.
    const float sr = (float) getCurrentSampleRate();
    const float step = play ? 1.0f / sr : -1.0f / (1.5f * sr);
    if (play ? transportGain < 1.0f : true)
    {
        for (int i = 0; i < n; ++i)
        {
            transportGain = juce::jlimit (0.0f, 1.0f, transportGain + step);
            const float g = transportGain * transportGain; // smoother perceived fade
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer (ch)[i] *= g;
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
juce::ValueTree ScrapeScapeProcessor::createStateTree()
{
    auto state = parameters.copyState();

    for (auto name : { "Elements", "Skins" })
        if (auto old = state.getChildWithName (name); old.isValid())
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
    state.appendChild (skins.toValueTree(), nullptr);
    state.setProperty ("presetName", getCurrentPresetName(), nullptr);
    return state;
}

void ScrapeScapeProcessor::applyStateTree (const juce::ValueTree& source, bool includeSkins)
{
    if (! source.hasType (parameters.state.getType()))
        return;

    auto state = source.createCopy();

    if (auto list = state.getChildWithName ("Elements"); list.isValid())
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

    if (auto skinTree = state.getChildWithName ("Skins"); skinTree.isValid())
    {
        if (includeSkins)
            skins.fromValueTree (skinTree);
        state.removeChild (skinTree, nullptr);
    }
    else if (includeSkins)
    {
        skins.resetAll();
    }

    if (state.hasProperty ("presetName"))
    {
        const juce::ScopedLock sl (presetNameLock);
        currentPresetName = state.getProperty ("presetName").toString();
    }

    parameters.replaceState (state);
}

void ScrapeScapeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void ScrapeScapeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateTree (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::File ScrapeScapeProcessor::getPresetsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                      .getChildFile ("Scrape-Scape").getChildFile ("Presets");
    folder.createDirectory();
    return folder;
}

juce::String ScrapeScapeProcessor::getCurrentPresetName() const
{
    const juce::ScopedLock sl (presetNameLock);
    return currentPresetName;
}

bool ScrapeScapeProcessor::savePreset (const juce::File& file)
{
    {
        const juce::ScopedLock sl (presetNameLock);
        currentPresetName = file.getFileNameWithoutExtension();
    }

    auto state = createStateTree();
    state.setProperty ("formatVersion", 1, nullptr);

    if (auto xml = state.createXml())
        return xml->writeTo (file);
    return false;
}

bool ScrapeScapeProcessor::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return false;

    auto state = juce::ValueTree::fromXml (*xml);
    state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    applyStateTree (state);
    return true;
}

namespace
{
    struct FactoryElement { ElementType type; float x, y, size; };
    struct FactoryPreset
    {
        const char* name;
        int mood;
        float activity, brightness, tempo, reverb, delay;
        std::vector<FactoryElement> elements;
    };

    const std::vector<FactoryPreset>& getFactoryPresets()
    {
        using T = ElementType;
        static const std::vector<FactoryPreset> presets = {
            { "First Light", 0, 0.45f, 0.65f, 62.0f, 0.55f, 0.30f,
              { { T::Drone, 0.28f, 0.58f, 1.2f }, { T::Bass, 0.52f, 0.82f, 1.0f }, { T::Bell, 0.70f, 0.30f, 1.0f },
                { T::Shimmer, 0.84f, 0.18f, 0.9f }, { T::Spark, 0.76f, 0.24f, 0.8f } } },
            { "Undertow", 1, 0.35f, 0.45f, 54.0f, 0.65f, 0.40f,
              { { T::Bass, 0.40f, 0.86f, 1.3f }, { T::Drone, 0.22f, 0.72f, 1.3f }, { T::Wind, 0.68f, 0.55f, 1.1f },
                { T::Pulse, 0.80f, 0.40f, 0.8f }, { T::Shade, 0.30f, 0.70f, 1.2f } } },
            { "Embers", 2, 0.55f, 0.40f, 60.0f, 0.50f, 0.35f,
              { { T::Drone, 0.50f, 0.64f, 1.1f }, { T::Pluck, 0.25f, 0.45f, 1.0f }, { T::Pluck, 0.78f, 0.35f, 0.8f },
                { T::Bass, 0.55f, 0.88f, 1.0f }, { T::Shade, 0.70f, 0.60f, 0.9f } } },
            { "Glass Garden", 3, 0.60f, 0.80f, 70.0f, 0.60f, 0.45f,
              { { T::Bell, 0.25f, 0.30f, 1.0f }, { T::Bell, 0.70f, 0.22f, 0.8f }, { T::Shimmer, 0.50f, 0.15f, 1.0f },
                { T::Pluck, 0.40f, 0.50f, 0.9f }, { T::Drone, 0.60f, 0.65f, 0.9f }, { T::Spark, 0.30f, 0.35f, 1.0f } } },
            { "Night Window", 5, 0.30f, 0.35f, 48.0f, 0.75f, 0.25f,
              { { T::Drone, 0.35f, 0.70f, 1.4f }, { T::Wind, 0.72f, 0.45f, 1.0f }, { T::Bell, 0.60f, 0.25f, 0.8f },
                { T::Shade, 0.30f, 0.55f, 1.1f }, { T::Shade, 0.75f, 0.35f, 0.7f } } },
        };
        return presets;
    }
}

juce::StringArray ScrapeScapeProcessor::getFactoryPresetNames()
{
    juce::StringArray names;
    for (auto& p : getFactoryPresets())
        names.add (p.name);
    return names;
}

void ScrapeScapeProcessor::loadFactoryPreset (int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    const auto& p = presets[(size_t) index];

    auto setParam = [this] (const char* id, float value)
    {
        if (auto* param = parameters.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };
    setParam (ParamIDs::mood, (float) p.mood);
    setParam (ParamIDs::activity, p.activity);
    setParam (ParamIDs::brightness, p.brightness);
    setParam (ParamIDs::tempo, p.tempo);
    setParam (ParamIDs::reverb, p.reverb);
    setParam (ParamIDs::delay, p.delay);

    elements.clear();
    for (auto& e : p.elements)
        elements.add (e.type, e.x, e.y, e.size);

    const juce::ScopedLock sl (presetNameLock);
    currentPresetName = p.name;   // factory scenes keep your custom images
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
