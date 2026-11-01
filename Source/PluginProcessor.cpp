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
    // ---- XY effects pad ----
    StringArray fxNames;
    for (int i = 0; i < kNumPadEffects; ++i)
        fxNames.add (getPadEffectInfo (i).name);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fx_type", 1 }, "FX Type", fxNames, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fx_x", 1 }, "FX Pad X", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fx_y", 1 }, "FX Pad Y", NormalisableRange<float> (0.0f, 1.0f), 0.5f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "fx_mix", 1 }, "FX Dry/Wet", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));

    // ---- Granular ----
    auto mapped = [] (std::function<float (float)> from, std::function<float (float)> to, float lo, float hi)
    {
        return NormalisableRange<float> (lo, hi,
                                         [from] (float, float, float n) { return from (n); },
                                         [to] (float, float, float v) { return to (v); });
    };
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain_mix", 1 }, "Grain Dry/Wet", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain_size", 1 }, "Grain Size",
        mapped (ParamMaps::grainSizeFromNorm, ParamMaps::grainSizeToNorm, 0.02f, 0.5f), 0.12f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 1000.0f)) + " ms"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain_density", 1 }, "Grain Density",
        mapped (ParamMaps::grainDensityFromNorm, ParamMaps::grainDensityToNorm, 1.0f, 60.0f), 12.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " /s"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain_pitch", 1 }, "Grain Pitch",
        NormalisableRange<float> (-12.0f, 12.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " st"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "grain_spray", 1 }, "Grain Spray", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));

    // ---- LFOs ----
    StringArray shapes, targets;
    for (int i = 0; i < kNumLfoShapes; ++i) shapes.add (getLfoShapeName (i));
    for (int i = 0; i < kNumModTargets; ++i) targets.add (getModTargetName (i));
    static constexpr float defaultRates[kNumLfos] = { 0.08f, 0.15f, 0.5f, 2.0f };
    static constexpr int defaultShapes[kNumLfos] = { 0, 1, 5, 4 };
    for (int i = 0; i < kNumLfos; ++i)
    {
        const String n (i + 1);
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { lfoParamID (i, "shape"), 1 }, "LFO " + n + " Shape", shapes, defaultShapes[i]));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { lfoParamID (i, "rate"), 1 }, "LFO " + n + " Rate",
            mapped (ParamMaps::lfoRateFromNorm, ParamMaps::lfoRateToNorm, 0.01f, 10.0f), defaultRates[i],
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                { return v < 1.0f ? String (v, 2) + " Hz" : String (v, 1) + " Hz"; })));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { lfoParamID (i, "depth"), 1 }, "LFO " + n + " Depth",
            NormalisableRange<float> (0.0f, 1.0f), 0.35f, percent));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { lfoParamID (i, "target"), 1 }, "LFO " + n + " Target", targets, 0));
    }

    return layout;
}

juce::String ScrapeScapeProcessor::getTargetParamID (int target)
{
    switch ((ModTarget) target)
    {
        case ModTarget::PadX:         return "fx_x";
        case ModTarget::PadY:         return "fx_y";
        case ModTarget::FxMix:        return "fx_mix";
        case ModTarget::GrainMix:     return "grain_mix";
        case ModTarget::GrainSize:    return "grain_size";
        case ModTarget::GrainDensity: return "grain_density";
        case ModTarget::GrainPitch:   return "grain_pitch";
        case ModTarget::GrainSpray:   return "grain_spray";
        case ModTarget::Reverb:       return ParamIDs::reverb;
        case ModTarget::Echo:         return ParamIDs::delay;
        case ModTarget::Brightness:   return ParamIDs::brightness;
        case ModTarget::Activity:     return ParamIDs::activity;
        case ModTarget::None:
        case ModTarget::NumTargets:   break;
    }
    return {};
}

int ScrapeScapeProcessor::getLfoTarget (int index) const noexcept
{
    return juce::jlimit (0, kNumModTargets - 1, (int) std::lround (lfoParams[(size_t) index].target->load()));
}

void ScrapeScapeProcessor::setLfoTarget (int index, int target)
{
    if (auto* param = parameters.getParameter (lfoParamID (index, "target")))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) target));
        param->endChangeGesture();
    }
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
    fxTypeParam = parameters.getRawParameterValue ("fx_type");
    fxXParam = parameters.getRawParameterValue ("fx_x");
    fxYParam = parameters.getRawParameterValue ("fx_y");
    fxMixParam = parameters.getRawParameterValue ("fx_mix");
    grainMixParam = parameters.getRawParameterValue ("grain_mix");
    grainSizeParam = parameters.getRawParameterValue ("grain_size");
    grainDensityParam = parameters.getRawParameterValue ("grain_density");
    grainPitchParam = parameters.getRawParameterValue ("grain_pitch");
    grainSprayParam = parameters.getRawParameterValue ("grain_spray");
    for (int i = 0; i < kNumLfos; ++i)
    {
        auto& l = lfoParams[(size_t) i];
        l.shape = parameters.getRawParameterValue (lfoParamID (i, "shape"));
        l.rate = parameters.getRawParameterValue (lfoParamID (i, "rate"));
        l.depth = parameters.getRawParameterValue (lfoParamID (i, "depth"));
        l.target = parameters.getRawParameterValue (lfoParamID (i, "target"));
    }

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

ChainParams ScrapeScapeProcessor::getChainParams() const noexcept
{
    ChainParams c;
    c.engine = getEngineParams();
    c.pad.type = juce::jlimit (0, kNumPadEffects - 1, (int) std::lround (fxTypeParam->load()));
    c.pad.x = fxXParam->load();
    c.pad.y = fxYParam->load();
    c.pad.mix = fxMixParam->load();
    c.grain.mix = grainMixParam->load();
    c.grain.size = grainSizeParam->load();
    c.grain.density = grainDensityParam->load();
    c.grain.pitch = grainPitchParam->load();
    c.grain.spray = grainSprayParam->load();
    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto& l = lfoParams[(size_t) i];
        auto& o = c.lfo[(size_t) i];
        o.shape = juce::jlimit (0, kNumLfoShapes - 1, (int) std::lround (l.shape->load()));
        o.rate = l.rate->load();
        o.depth = l.depth->load();
        o.target = juce::jlimit (0, kNumModTargets - 1, (int) std::lround (l.target->load()));
    }
    return c;
}

//==============================================================================
void ScrapeScapeProcessor::prepareToPlay (double sampleRate, int)
{
    if (isRecording() && ! juce::approximatelyEqual (sampleRate, currentSampleRate))
        recorder.stop(); // a take can't change sample rate mid-file

    currentSampleRate = sampleRate;
    monoScratch.assign (2 * 1024, 0.0f);
    chain.getEngine().setParams (getEngineParams());
    chain.prepare (sampleRate);
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

    const auto chainParams = getChainParams();

    if (buffer.getNumChannels() >= 2)
    {
        chain.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, elements, chainParams);
    }
    else
    {
        // Mono output: render stereo in pieces through a preallocated scratch buffer.
        auto* mono = buffer.getWritePointer (0);
        const int chunk = (int) monoScratch.size() / 2;
        for (int pos = 0; pos < n; pos += chunk)
        {
            const int len = juce::jmin (chunk, n - pos);
            chain.process (monoScratch.data(), monoScratch.data() + chunk, len, elements, chainParams);
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
        node.setProperty ("scale", e.scale, nullptr);
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
            e.scale = (float) node.getProperty ("scale", 1.0f);
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
