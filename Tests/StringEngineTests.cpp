#include "StringEngine.h"

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

void pitchTests()
{
    using namespace stringlab;
    const StringModel ideal({ 1.0, 0.01, 100.0, 0.0 });
    require(std::abs(ideal.partialFrequency(0, 1) - 50.0) < 1.0e-8,
            "general string model derives frequency from tension and density");
    require(std::abs(ideal.partialFrequency(12, 1) - 100.0) < 1.0e-8,
            "general string model halves its speaking length at octave");
    for (int fret = 0; fret <= 24; ++fret)
    {
        const double open = midiFrequency(40, 440.0);
        const double expected = open * std::pow(2.0, fret / 12.0);
        const double actual = partialFrequency(open, fret, .648, .010 * .0254, 0.0, 1);
        require(std::abs(actual - expected) < 1.0e-8, "ideal frets follow equal temperament");
    }
    const double open = midiFrequency(64, 440.0);
    const double fundamental = partialFrequency(open, 0, .648, .010 * .0254, 1.0, 1);
    const double second = partialFrequency(open, 0, .648, .010 * .0254, 1.0, 2);
    require(std::abs(fundamental - open) < 1.0e-8, "open pitch is calibrated");
    require(second > 2.0 * fundamental, "stiffness stretches partials");
    for (const auto& instrument : instruments)
        for (int string = 0; string < instrument.stringCount; ++string)
            for (int fret = 0; fret <= 24; ++fret)
            {
                const auto i = static_cast<std::size_t>(string);
                const double base = midiFrequency(instrument.tuning[i], 440.0);
                const double actual = partialFrequency(base, fret, instrument.lengthMetres,
                    instrument.diameterInches[i] * .0254, 0.15, 1, instrument.coreRatio[i]);
                const double expected = base * std::pow(2.0, fret / 12.0);
                const double cents = std::abs(1200.0 * std::log2(actual / expected));
                require(cents < 5.0, "default instrument pitches stay within five cents through fret 24");
            }
}

void playbackTests(double rate)
{
    using namespace stringlab;
    Engine engine;
    engine.prepare(rate);
    Settings settings;
    engine.setSettings(settings);
    engine.noteOn(1, 64, 1.0f);
    require(engine.activeVoiceCount() == 1, "note starts a voice");
    double energy = 0.0;
    for (int i = 0; i < static_cast<int>(rate); ++i)
    {
        const double sample = engine.next();
        require(std::isfinite(sample), "output stays finite");
        energy += sample * sample;
    }
    require(energy > 0.01, "note produces sound");
    engine.noteOff(1, 64);
    for (int i = 0; i < static_cast<int>(2.0 * rate); ++i)
        engine.next();
    require(engine.activeVoiceCount() == 0, "released voice stops");

    engine.noteOn(1, 40, 1.0f);
    engine.noteOn(1, 41, 1.0f);
    require(engine.activeVoiceCount() == 1, "guitar mode replaces note on occupied string");
    settings.guitarAllocation = false;
    engine.setSettings(settings);
    engine.noteOn(1, 40, 1.0f);
    engine.noteOn(1, 41, 1.0f);
    require(engine.activeVoiceCount() == 2, "unrestricted mode permits overlapping notes");
    engine.sustainPedal(1, true);
    engine.noteOff(1, 40);
    for (int i = 0; i < static_cast<int>(0.5 * rate); ++i)
        engine.next();
    require(engine.activeVoiceCount() == 2, "sustain holds released notes");
    engine.sustainPedal(1, false);
    engine.allNotesOff(1, true);
    require(engine.activeVoiceCount() == 0, "all sound off stops voices");

    settings.instrument = 1;
    settings.tuningOffsets[0] = -2;
    engine.setSettings(settings);
    engine.noteOn(1, 38, 1.0f);
    require(engine.activeVoiceCount() == 1, "alternate tuning reaches D2");
    for (int i = 0; i < static_cast<int>(rate); ++i)
        require(std::isfinite(engine.next()), "acoustic body output stays finite");

    engine.reset();
    for (int i = 0; i < maxVoices + 5; ++i)
        engine.noteOn(1, 45, 1.0f);
    require(engine.activeVoiceCount() == maxVoices, "unrestricted polyphony is bounded");
}

double spectralMagnitude(double pluck, int partial)
{
    using namespace stringlab;
    constexpr double rate = 48000.0;
    Engine engine;
    engine.prepare(rate);
    Settings settings;
    settings.instrument = 1;
    settings.stiffnessScale = 0.0;
    settings.pluckPosition = pluck;
    settings.bodyAmount = 0.0;
    engine.setSettings(settings);
    engine.noteOn(1, 64, 1.0f);
    const double frequency = midiFrequency(64, 440.0) * partial;
    double real = 0.0, imaginary = 0.0;
    for (int i = 0; i < static_cast<int>(rate); ++i)
    {
        const double sample = engine.next();
        const double phase = 2.0 * pi * frequency * i / rate;
        real += sample * std::cos(phase);
        imaginary += sample * std::sin(phase);
    }
    return std::hypot(real, imaginary);
}

void timbreTests()
{
    const double centreSecond = spectralMagnitude(0.5, 2);
    const double offsetSecond = spectralMagnitude(0.2, 2);
    require(offsetSecond > 1.0, "offset pluck excites second harmonic");
    require(centreSecond < offsetSecond * 0.05, "centre pluck suppresses second harmonic");
}

struct AudioMeasurement
{
    double fundamental = 0.0, fourth = 0.0, peak = 0.0;
};

AudioMeasurement measureAcoustic(double rate, bool nylon, double body)
{
    using namespace stringlab;
    Engine engine;
    engine.prepare(rate);
    Settings settings;
    settings.instrument = 1;
    settings.nylon = nylon;
    settings.bodyAmount = body;
    settings.stiffnessScale = 0.0;
    settings.output = 1.0;
    engine.setSettings(settings);
    engine.noteOn(1, 45, 1.0f);
    double real1 = 0.0, imag1 = 0.0, real4 = 0.0, imag4 = 0.0, peak = 0.0;
    const int count = static_cast<int>(rate * 0.8);
    for (int i = 0; i < count; ++i)
    {
        const double sample = engine.next();
        require(std::isfinite(sample), "acoustic samples are finite");
        peak = std::max(peak, std::abs(sample));
        const double phase = 2.0 * pi * 110.0 * i / rate;
        real1 += sample * std::cos(phase);
        imag1 += sample * std::sin(phase);
        real4 += sample * std::cos(4.0 * phase);
        imag4 += sample * std::sin(4.0 * phase);
    }
    return { 2.0 * std::hypot(real1, imag1) / count, 2.0 * std::hypot(real4, imag4) / count, peak };
}

void acousticTests()
{
    using namespace stringlab;
    const auto nylonModel = StringModel::tunedMaterial(midiFrequency(64, 440.0), 0.648,
                                                       .028 * .0254, 0.15, 1.0, 1150.0, 3.0e9);
    for (int fret = 0; fret <= 24; ++fret)
    {
        const double expected = midiFrequency(64 + fret, 440.0);
        const double cents = std::abs(1200.0 * std::log2(nylonModel.partialFrequency(fret, 1) / expected));
        require(cents < 5.0, "nylon pitch remains within five cents through fret 24");
    }
    const auto steel = measureAcoustic(48000.0, false, 0.0);
    const auto nylon = measureAcoustic(48000.0, true, 0.0);
    require(steel.fundamental > 0.1 && nylon.fundamental > 0.1, "both acoustic modes produce the correct note");
    require(nylon.fourth < steel.fourth * 0.65, "nylon has a softer upper harmonic response");
    const auto body44 = measureAcoustic(44100.0, false, 1.0);
    const auto body48 = measureAcoustic(48000.0, false, 1.0);
    const auto body96 = measureAcoustic(96000.0, false, 1.0);
    require(body48.fundamental > steel.fundamental * 1.10, "body resonance audibly boosts its matched frequency");
    require(body48.fundamental < steel.fundamental * 1.6, "body resonance remains bounded");
    require(std::abs(body44.fundamental / body48.fundamental - 1.0) < 0.02
            && std::abs(body96.fundamental / body48.fundamental - 1.0) < 0.02,
            "body resonance is consistent across sample rates");
    require(body96.peak < 1.0, "body response stays bounded at 96 kHz");
}
}

int main()
{
    pitchTests();
    playbackTests(44100.0);
    playbackTests(48000.0);
    playbackTests(96000.0);
    timbreTests();
    acousticTests();
    std::cout << "String engine tests passed\n";
}
