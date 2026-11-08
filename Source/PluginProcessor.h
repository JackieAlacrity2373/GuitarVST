#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "AmpEffects.h"
#include "StringEngine.h"

class StringLabProcessor final : public juce::AudioProcessor
{
public:
    StringLabProcessor();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 120.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    stringlab::Settings readSettings() const noexcept;
    void handleMidi(const juce::MidiMessage& message) noexcept;
    void render(juce::AudioBuffer<float>& buffer, int start, int end) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>*, 22> values {};
    stringlab::Engine engine;
    stringlab::EffectsChain effects;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StringLabProcessor)
};
