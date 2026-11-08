#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

juce::AudioProcessorParameter* findParameter(juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* parameter : plugin.getParameters())
        if (parameter->getName(256) == name)
            return parameter;
    return nullptr;
}
}

int main(int argc, char** argv)
{
    require(argc == 2, "expected VST3 bundle path");
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
    require(descriptions.size() == 1, "VST3 scan discovers one instrument");
    require(descriptions[0]->isInstrument, "VST3 reports itself as an instrument");

    juce::String error;
    auto plugin = format.createInstanceFromDescription(*descriptions[0], 48000.0, 256, error);
    if (plugin == nullptr)
        std::cerr << error << '\n';
    require(plugin != nullptr, "VST3 instance loads");
    plugin->prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 128);
    plugin->processBlock(audio, midi);
    double before = 0.0, after = 0.0;
    for (int i = 0; i < 256; ++i)
    {
        const auto value = audio.getSample(0, i);
        require(std::isfinite(value), "VST3 audio is finite");
        (i < 128 ? before : after) += std::abs(value);
        require(audio.getSample(1, i) == value, "VST3 stereo channels agree");
    }
    require(before == 0.0, "MIDI note is rendered at its sample offset");
    require(after > 0.1, "VST3 renders audible output after MIDI note");

    auto* tuning = findParameter(*plugin, "String 1 tuning offset (semitones)");
    require(tuning != nullptr, "string tuning parameter is exposed to the host");
    auto* pedal = findParameter(*plugin, "Drive effect");
    auto* amp = findParameter(*plugin, "Amp style");
    auto* drive = findParameter(*plugin, "Drive amount");
    auto* gain = findParameter(*plugin, "Amp gain");
    auto* tone = findParameter(*plugin, "Amp tone");
    auto* acousticStrings = findParameter(*plugin, "Acoustic strings");
    auto* instrument = findParameter(*plugin, "Instrument");
    require(pedal && amp && drive && gain && tone && acousticStrings && instrument, "parameters are exposed to the host");
    require(pedal->getValue() == 0.0f && amp->getValue() == 0.0f, "effects default to off");
    tuning->setValueNotifyingHost(22.0f / 48.0f);
    pedal->setValueNotifyingHost(0.5f);
    amp->setValueNotifyingHost(0.5f);
    drive->setValueNotifyingHost(0.8f);
    gain->setValueNotifyingHost(0.7f);
    tone->setValueNotifyingHost(0.3f);
    acousticStrings->setValueNotifyingHost(1.0f);
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    require(state.getSize() > 0, "VST3 saves state");
    tuning->setValueNotifyingHost(0.5f);
    pedal->setValueNotifyingHost(0.0f);
    amp->setValueNotifyingHost(0.0f);
    drive->setValueNotifyingHost(0.1f);
    gain->setValueNotifyingHost(0.1f);
    tone->setValueNotifyingHost(0.1f);
    acousticStrings->setValueNotifyingHost(0.0f);
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(std::abs(tuning->getValue() - 22.0f / 48.0f) < 0.001f, "VST3 restores tuning");
    require(std::abs(pedal->getValue() - 0.5f) < 0.001f, "VST3 restores overdrive selection");
    require(std::abs(amp->getValue() - 0.5f) < 0.001f, "VST3 restores JCM 800-style amp");
    require(std::abs(drive->getValue() - 0.8f) < 0.001f, "VST3 restores drive");
    require(std::abs(gain->getValue() - 0.7f) < 0.001f, "VST3 restores gain");
    require(std::abs(tone->getValue() - 0.3f) < 0.001f, "VST3 restores tone");
    require(acousticStrings->getValue() == 1.0f, "VST3 restores nylon selection");

    auto hostState = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    require(hostState != nullptr, "host state can be decoded");
    auto* componentState = hostState->getChildByName("IComponent");
    require(componentState != nullptr, "host state has component data");
    juce::MemoryBlock componentData;
    require(componentData.fromBase64Encoding(componentState->getAllSubText()), "component data is base64");
    auto processorState = juce::AudioProcessor::getXmlFromBinary(componentData.getData(),
                                                                static_cast<int>(componentData.getSize()));
    require(processorState != nullptr, "processor state can be decoded");
    for (const auto* id : { "pedal", "amp", "pedalDrive", "ampGain", "ampTone", "acousticStrings" })
    {
        auto* child = processorState->getChildByAttribute("id", id);
        require(child != nullptr, "new effect parameter is present in saved state");
        processorState->removeChildElement(child, true);
    }
    juce::AudioProcessor::copyXmlToBinary(*processorState, componentData);
    componentState->deleteAllChildElements();
    componentState->addTextElement(componentData.toBase64Encoding());
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*hostState, legacyState);
    plugin->setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    require(std::abs(tuning->getValue() - 22.0f / 48.0f) < 0.001f, "legacy tuning remains intact");
    require(pedal->getValue() == 0.0f && amp->getValue() == 0.0f,
            "legacy state disables effects even when they were previously enabled");
    require(acousticStrings->getValue() == 0.0f, "legacy state selects steel strings");
    require(std::abs(drive->getValue() - 0.5f) < 0.001f
            && std::abs(gain->getValue() - 0.5f) < 0.001f
            && std::abs(tone->getValue() - 0.5f) < 0.001f,
            "legacy state restores effect control defaults");
    plugin->releaseResources();
    plugin->prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> dry(2, 256);
    juce::MidiBuffer note;
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    dry.clear();
    plugin->processBlock(dry, note);
    plugin->releaseResources();
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(amp->getText(amp->getValue(), 256).containsIgnoreCase("JCM 800"),
            "amp selection identifies the JCM 800-style preset");
    plugin->prepareToPlay(48000.0, 256);
    audio.clear();
    note.clear();
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    plugin->processBlock(audio, note);
    double difference = 0.0;
    for (int i = 0; i < audio.getNumSamples(); ++i)
        difference += std::abs(audio.getSample(0, i) - dry.getSample(0, i));
    require(difference > 1.0, "VST3 routes MIDI instrument audio through selected effects");
    plugin->releaseResources();
    pedal->setValueNotifyingHost(0.0f);
    amp->setValueNotifyingHost(0.0f);
    instrument->setValueNotifyingHost(0.25f);
    acousticStrings->setValueNotifyingHost(0.0f);
    plugin->prepareToPlay(48000.0, 256);
    note.clear();
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    dry.clear();
    plugin->processBlock(dry, note);
    plugin->releaseResources();
    acousticStrings->setValueNotifyingHost(1.0f);
    plugin->prepareToPlay(48000.0, 256);
    note.clear();
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    audio.clear();
    plugin->processBlock(audio, note);
    difference = 0.0;
    double acousticEnergy = 0.0;
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        difference += std::abs(audio.getSample(0, i) - dry.getSample(0, i));
        acousticEnergy += std::abs(audio.getSample(0, i));
    }
    require(acousticEnergy > 1.0, "VST3 acoustic nylon produces sound");
    require(difference > 0.1, "VST3 acoustic nylon changes the rendered waveform");
    instrument->setValueNotifyingHost(0.0f);
    note.clear();
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    plugin->processBlock(audio, note);
    instrument->setValueNotifyingHost(0.25f);
    acousticStrings->setValueNotifyingHost(0.0f);
    note.clear();
    note.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
    audio.clear();
    plugin->processBlock(audio, note);
    acousticEnergy = 0.0;
    for (int i = 0; i < audio.getNumSamples(); ++i)
        acousticEnergy += std::abs(audio.getSample(0, i));
    require(acousticEnergy > 1.0, "VST3 plays acoustic notes after live instrument switch");
    midi.clear();
    for (int block = 0; block < 10; ++block)
    {
        audio.clear();
        plugin->processBlock(audio, midi);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            require(std::isfinite(audio.getSample(0, i)), "VST3 effects audio stays finite");
    }
    plugin->releaseResources();

    for (double sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        instrument->setValueNotifyingHost(0.25f);
        acousticStrings->setValueNotifyingHost(0.0f);
        plugin->prepareToPlay(sampleRate, 256);
        for (int channel : { 1, 10, 16 })
            for (int noteNumber : { 40, 45, 60, 64, 76, 88 })
            {
                juce::MidiBuffer fileEvents;
                fileEvents.addEvent(juce::MidiMessage::programChange(channel, 24), 0);
                fileEvents.addEvent(juce::MidiMessage::noteOn(channel, noteNumber, static_cast<juce::uint8>(90)), 32);
                audio.clear();
                plugin->processBlock(audio, fileEvents);
                double signal = 0.0;
                for (int i = 32; i < audio.getNumSamples(); ++i)
                    signal += std::abs(audio.getSample(0, i));
                require(signal > 0.01, "acoustic MIDI-file-style events produce sound on each channel and sample rate");
                juce::MidiBuffer silence;
                silence.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
                plugin->processBlock(audio, silence);
            }
        plugin->releaseResources();
    }
    std::cout << "VST3 host tests passed\n";
}
