#include <juce_audio_utils/juce_audio_utils.h>
#include "AmpEffects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double rate = 48000.0;
constexpr int blockSize = 256;
constexpr double pi = 3.14159265358979323846;

struct Scenario
{
    const char* name;
    float instrument, nylon, body, pedal, amp;
};

constexpr std::array<Scenario, 7> scenarios {{
    { "electric-dry", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "electric-overdrive", 0.0f, 0.0f, 0.0f, 0.5f, 0.0f },
    { "electric-distortion", 0.0f, 0.0f, 0.0f, 1.0f, 0.0f },
    { "electric-jcm800", 0.0f, 0.0f, 0.0f, 0.0f, 0.5f },
    { "acoustic-steel-no-body", 0.25f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "acoustic-steel-body", 0.25f, 0.0f, 1.0f, 0.0f, 0.0f },
    { "acoustic-nylon-body", 0.25f, 1.0f, 1.0f, 0.0f, 0.0f }
}};

juce::AudioProcessorParameter* parameter(juce::AudioPluginInstance& plugin, const juce::String& name)
{
    for (auto* p : plugin.getParameters())
        if (p->getName(256) == name)
            return p;
    return nullptr;
}

bool set(juce::AudioPluginInstance& plugin, const char* name, float value)
{
    if (auto* p = parameter(plugin, name))
    {
        p->setValueNotifyingHost(value);
        return true;
    }
    std::cerr << "Missing parameter: " << name << '\n';
    return false;
}

struct Measurement
{
    double peak = 0.0, rms = 0.0;
    std::array<double, 4> harmonics {};
};

double magnitudeAt(const juce::AudioBuffer<float>& audio, double frequency)
{
    const int first = static_cast<int>(rate * 0.1);
    const int last = static_cast<int>(rate * 0.6);
    double real = 0.0, imaginary = 0.0;
    for (int i = first; i < last; ++i)
    {
        const double phase = 2.0 * pi * frequency * i / rate;
        const double sample = audio.getSample(0, i);
        real += sample * std::cos(phase);
        imaginary += sample * std::sin(phase);
    }
    return 2.0 * std::hypot(real, imaginary) / (last - first);
}

Measurement analyse(const juce::AudioBuffer<float>& audio, double fundamental = 110.0)
{
    Measurement result;
    std::array<double, 4> real {}, imaginary {};
    const int first = static_cast<int>(rate * 0.1);
    const int last = static_cast<int>(rate * 0.6);
    for (int i = 0; i < audio.getNumSamples(); ++i)
    {
        const double value = audio.getSample(0, i);
        result.peak = std::max(result.peak, std::abs(value));
        if (i >= first && i < last)
        {
            result.rms += value * value;
            for (std::size_t n = 0; n < real.size(); ++n)
            {
                const double phase = 2.0 * pi * fundamental * static_cast<double>(n + 1) * i / rate;
                real[n] += value * std::cos(phase);
                imaginary[n] += value * std::sin(phase);
            }
        }
    }
    const auto count = static_cast<double>(last - first);
    result.rms = std::sqrt(result.rms / count);
    for (std::size_t n = 0; n < real.size(); ++n)
        result.harmonics[n] = 2.0 * std::hypot(real[n], imaginary[n]) / count;
    return result;
}

bool writeWave(const juce::File& file, const juce::AudioBuffer<float>& audio)
{
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk())
        return false;
    if (!stream->setPosition(0) || !stream->truncate())
        return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release();
    return writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

bool writeScope(const juce::File& file, const juce::AudioBuffer<float>& audio, const Scenario& scenario)
{
    auto stream = file.createOutputStream();
    if (stream == nullptr || !stream->openedOk() || !stream->setPosition(0) || !stream->truncate())
        return false;
    juce::String svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1000\" height=\"320\" viewBox=\"0 0 1000 320\">"
        << "<rect width=\"1000\" height=\"320\" fill=\"#101820\"/>"
        << "<text x=\"20\" y=\"25\" fill=\"white\" font-family=\"sans-serif\" font-size=\"16\">"
        << scenario.name << " - first 30 ms, fixed vertical range +/- 1.0</text>"
        << "<path d=\"M 40 160 H 980 M 40 40 H 980 M 40 280 H 980\" stroke=\"#485660\" fill=\"none\"/>"
        << "<text x=\"2\" y=\"44\" fill=\"white\" font-size=\"12\">1.0</text>"
        << "<text x=\"12\" y=\"164\" fill=\"white\" font-size=\"12\">0</text>"
        << "<text x=\"2\" y=\"284\" fill=\"white\" font-size=\"12\">-1.0</text>"
        << "<polyline fill=\"none\" stroke=\"#48e0c3\" stroke-width=\"1\" points=\"";
    constexpr int points = 940;
    for (int i = 0; i < points; ++i)
    {
        const int index = static_cast<int>((rate * 0.03) * i / (points - 1));
        const double y = 160.0 - 120.0 * audio.getSample(0, index);
        svg << (40 + i) << "," << juce::String(y, 2) << " ";
    }
    svg << "\"/><text x=\"40\" y=\"307\" fill=\"white\" font-size=\"12\">0 ms</text>"
        << "<text x=\"940\" y=\"307\" fill=\"white\" font-size=\"12\">30 ms</text></svg>";
    return stream->writeText(svg, false, false, nullptr);
}
}

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4)
    {
        std::cerr << "Usage: StringLabAudioProbe <VST3 bundle> <output directory> [--legacy]\n";
        return 1;
    }
    const bool legacy = argc == 4 && juce::String::fromUTF8(argv[3]) == "--legacy";
    if (argc == 4 && !legacy)
    {
        std::cerr << "Unknown option: " << argv[3] << '\n';
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI initializer;
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, juce::String::fromUTF8(argv[1]));
    if (descriptions.size() != 1)
    {
        std::cerr << "Could not scan one VST3 instrument\n";
        return 1;
    }
    juce::String error;
    auto plugin = format.createInstanceFromDescription(*descriptions[0], rate, blockSize, error);
    if (plugin == nullptr)
    {
        std::cerr << "Could not load VST3: " << error << '\n';
        return 1;
    }
    const auto output = juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[2]));
    const auto result = output.createDirectory();
    if (result.failed())
    {
        std::cerr << "Could not create output directory: " << result.getErrorMessage() << '\n';
        return 1;
    }
    juce::String report("case,peak,rms_100_to_600ms,fundamental_110Hz,harmonic_220Hz,harmonic_330Hz,harmonic_440Hz\n");
    for (const auto& scenario : scenarios)
    {
        if (legacy && scenario.nylon > 0.0f)
            continue;
        if (!set(*plugin, "Instrument", scenario.instrument)
            || (!legacy && !set(*plugin, "Acoustic strings", scenario.nylon))
            || !set(*plugin, "Acoustic body amount", scenario.body)
            || !set(*plugin, "Drive effect", scenario.pedal)
            || !set(*plugin, "Amp style", scenario.amp)
            || !set(*plugin, "Output level", 0.25f))
            return 1;
        plugin->prepareToPlay(rate, blockSize);
        juce::AudioBuffer<float> recording(1, static_cast<int>(rate));
        juce::AudioBuffer<float> block(2, blockSize);
        for (int offset = 0; offset < recording.getNumSamples(); offset += blockSize)
        {
            block.clear();
            juce::MidiBuffer midi;
            if (offset == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, 45, static_cast<juce::uint8>(100)), 0);
            plugin->processBlock(block, midi);
            recording.copyFrom(0, offset, block, 0, 0,
                               std::min(blockSize, recording.getNumSamples() - offset));
        }
        plugin->releaseResources();
        for (int i = 0; i < recording.getNumSamples(); ++i)
            if (!std::isfinite(recording.getSample(0, i)))
            {
                std::cerr << scenario.name << ": output contains a non-finite sample\n";
                return 1;
            }
        const auto m = analyse(recording);
        if (!std::isfinite(m.peak) || m.peak >= 1.0 || m.rms < 0.0001)
        {
            std::cerr << scenario.name << ": output is silent, non-finite, or exceeds WAV range\n";
            return 1;
        }
        const auto wav = output.getChildFile(juce::String(scenario.name) + ".wav");
        const auto svg = output.getChildFile(juce::String(scenario.name) + ".svg");
        if (!writeWave(wav, recording) || !writeScope(svg, recording, scenario))
        {
            std::cerr << "Could not write files for " << scenario.name << '\n';
            return 1;
        }
        report << scenario.name << "," << juce::String(m.peak, 6) << "," << juce::String(m.rms, 6);
        for (const auto harmonic : m.harmonics)
            report << "," << juce::String(harmonic, 6);
        report << "\n";
    }
    const auto reportFile = output.getChildFile("measurements.csv");
    if (!reportFile.replaceWithText(report))
    {
        std::cerr << "Could not write measurements.csv\n";
        return 1;
    }
    if (legacy)
    {
        std::cout << report << "Files written to " << output.getFullPathName() << '\n';
        return 0;
    }
    juce::String sineReport("case,peak,rms_100_to_600ms,fundamental_200Hz,harmonic_400Hz,harmonic_600Hz,harmonic_800Hz\n");
    for (int pedal = 0; pedal <= 2; ++pedal)
    {
        const char* names[] { "sine-dry", "sine-overdrive", "sine-distortion" };
        const Scenario scenario { names[pedal], 0, 0, 0, 0, 0 };
        stringlab::EffectsChain effect;
        effect.prepare(rate);
        effect.setSettings({ pedal, 0, 0.7, 0.5, 0.5 });
        juce::AudioBuffer<float> audio(1, static_cast<int>(rate));
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(0, i, effect.process(static_cast<float>(0.12 * std::sin(2.0 * pi * 200.0 * i / rate))));
        const auto m = analyse(audio, 200.0);
        if (!std::isfinite(m.peak) || m.peak >= 1.0 || m.rms < 0.0001)
        {
            std::cerr << scenario.name << ": invalid sine probe output\n";
            return 1;
        }
        if (!writeWave(output.getChildFile(juce::String(scenario.name) + ".wav"), audio)
            || !writeScope(output.getChildFile(juce::String(scenario.name) + ".svg"), audio, scenario))
        {
            std::cerr << "Could not write sine probe files\n";
            return 1;
        }
        sineReport << scenario.name << "," << juce::String(m.peak, 6) << "," << juce::String(m.rms, 6);
        for (const auto harmonic : m.harmonics)
            sineReport << "," << juce::String(harmonic, 6);
        sineReport << "\n";
    }
    if (!output.getChildFile("sine-measurements.csv").replaceWithText(sineReport))
    {
        std::cerr << "Could not write sine-measurements.csv\n";
        return 1;
    }
    juce::String aliasReport("case,fundamental_9000Hz,folded_third_21000Hz\n");
    for (int pedal : { 0, 2 })
    {
        stringlab::EffectsChain effect;
        effect.prepare(rate);
        effect.setSettings({ pedal, 0, 0.7, 0.5, 0.5 });
        juce::AudioBuffer<float> audio(1, static_cast<int>(rate));
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(0, i, effect.process(static_cast<float>(0.12 * std::sin(2.0 * pi * 9000.0 * i / rate))));
        aliasReport << (pedal == 0 ? "dry" : "distortion") << ","
                    << juce::String(magnitudeAt(audio, 9000.0), 6) << ","
                    << juce::String(magnitudeAt(audio, 21000.0), 6) << "\n";
    }
    if (!output.getChildFile("alias-measurements.csv").replaceWithText(aliasReport))
    {
        std::cerr << "Could not write alias-measurements.csv\n";
        return 1;
    }
    std::cout << report << sineReport << aliasReport << "Files written to " << output.getFullPathName() << '\n';
}
