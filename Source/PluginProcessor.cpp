#include "PluginProcessor.h"

namespace
{
const std::array<const char*, 22> parameterIds {
    "instrument", "allocation", "reference", "pluck", "pickup", "diameter", "stiffness", "decay",
    "body", "output", "tune1", "tune2", "tune3", "tune4", "tune5", "tune6",
    "pedal", "amp", "pedalDrive", "ampGain", "ampTone", "acousticStrings"
};
}

StringLabProcessor::StringLabProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "StringLabState", createParameters())
{
    for (std::size_t i = 0; i < values.size(); ++i)
        values[i] = parameters.getRawParameterValue(parameterIds[i]);
}

juce::AudioProcessorValueTreeState::ParameterLayout StringLabProcessor::createParameters()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "instrument", 1 }, "Instrument",
        StringArray { "Electric guitar", "Acoustic guitar", "Electric bass", "Bass VI", "Upright bass" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "allocation", 1 }, "Playing mode",
        StringArray { "Guitar-like", "Unrestricted" }, 0));
    const auto addFloat = [&layout](const char* id, const char* name, float minimum, float maximum, float initial)
    {
        layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name,
            NormalisableRange<float>(minimum, maximum), initial));
    };
    addFloat("reference", "Reference A4 (Hz)", 400.0f, 480.0f, 440.0f);
    addFloat("pluck", "Pluck position", 0.03f, 0.5f, 0.20f);
    addFloat("pickup", "Pickup position", 0.03f, 0.5f, 0.16f);
    addFloat("diameter", "String thickness scale", 0.5f, 1.5f, 1.0f);
    addFloat("stiffness", "String stiffness scale", 0.0f, 2.0f, 0.15f);
    addFloat("decay", "Decay (seconds)", 0.2f, 10.0f, 3.0f);
    addFloat("body", "Acoustic body amount", 0.0f, 1.0f, 0.35f);
    addFloat("output", "Output level", 0.0f, 1.0f, 0.5f);
    for (int i = 1; i <= 6; ++i)
    {
        const String id = "tune" + String(i);
        const String name = "String " + String(i) + " tuning offset (semitones)";
        layout.add(std::make_unique<AudioParameterInt>(ParameterID { id, 1 }, name, -24, 24, 0));
    }
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "pedal", 1 }, "Drive effect",
        StringArray { "Off", "Overdrive", "Distortion" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "amp", 1 }, "Amp style",
        StringArray { "Off", "Fender-style clean", "Marshall JCM 800-style", "Vox AC30-style", "Mesa-style high gain" }, 0));
    addFloat("pedalDrive", "Drive amount", 0.0f, 1.0f, 0.5f);
    addFloat("ampGain", "Amp gain", 0.0f, 1.0f, 0.5f);
    addFloat("ampTone", "Amp tone", 0.0f, 1.0f, 0.5f);
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { "acousticStrings", 1 }, "Acoustic strings",
        StringArray { "Steel", "Nylon" }, 0));
    return layout;
}

stringlab::Settings StringLabProcessor::readSettings() const noexcept
{
    stringlab::Settings result;
    result.instrument = static_cast<int>(values[0]->load());
    result.guitarAllocation = values[1]->load() < 0.5f;
    result.referenceA = values[2]->load();
    result.pluckPosition = values[3]->load();
    result.pickupPosition = values[4]->load();
    result.diameterScale = values[5]->load();
    result.stiffnessScale = values[6]->load();
    result.decaySeconds = values[7]->load();
    result.bodyAmount = values[8]->load();
    result.output = 1.0;
    result.nylon = values[21]->load() >= 0.5f;
    for (int i = 0; i < 6; ++i)
        result.tuningOffsets[static_cast<std::size_t>(i)] = static_cast<int>(values[static_cast<std::size_t>(10 + i)]->load());
    return result;
}

void StringLabProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
    effects.prepare(sampleRate);
    engine.setSettings(readSettings());
    effects.setSettings({ static_cast<int>(values[16]->load()), static_cast<int>(values[17]->load()),
                          values[18]->load(), values[19]->load(), values[20]->load() });
}

void StringLabProcessor::releaseResources() { engine.reset(); effects.reset(); }

bool StringLabProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void StringLabProcessor::handleMidi(const juce::MidiMessage& message) noexcept
{
    const int channel = message.getChannel();
    if (message.isNoteOn())
        engine.noteOn(channel, message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff())
        engine.noteOff(channel, message.getNoteNumber());
    else if (message.isSustainPedalOn())
        engine.sustainPedal(channel, true);
    else if (message.isSustainPedalOff())
        engine.sustainPedal(channel, false);
    else if (message.isAllSoundOff())
        engine.allNotesOff(channel, true);
    else if (message.isAllNotesOff())
        engine.allNotesOff(channel, false);
}

void StringLabProcessor::render(juce::AudioBuffer<float>& buffer, int start, int end) noexcept
{
    for (int sample = start; sample < end; ++sample)
    {
        const float value = effects.process(engine.next()) * values[9]->load();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, value);
    }
}

void StringLabProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    engine.setSettings(readSettings());
    effects.setSettings({ static_cast<int>(values[16]->load()), static_cast<int>(values[17]->load()),
                          values[18]->load(), values[19]->load(), values[20]->load() });
    int position = 0;
    for (const auto metadata : midi)
    {
        const int eventPosition = juce::jlimit(0, buffer.getNumSamples(), metadata.samplePosition);
        render(buffer, position, eventPosition);
        if (metadata.numBytes <= 3)
            handleMidi(metadata.getMessage());
        position = eventPosition;
    }
    render(buffer, position, buffer.getNumSamples());
}

juce::AudioProcessorEditor* StringLabProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void StringLabProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void StringLabProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto restored = juce::ValueTree::fromXml(*xml);
            for (std::size_t i = 16; i < parameterIds.size(); ++i)
            {
                if (!restored.getChildWithProperty("id", parameterIds[i]).isValid())
                {
                    const auto* parameter = parameters.getParameter(parameterIds[i]);
                    juce::ValueTree entry("PARAM");
                    entry.setProperty("id", parameterIds[i], nullptr);
                    entry.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
                    restored.appendChild(entry, nullptr);
                }
            }
            parameters.replaceState(restored);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StringLabProcessor(); }
