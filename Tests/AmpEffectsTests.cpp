#include "AmpEffects.h"

#include <array>
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

constexpr double pi = 3.14159265358979323846;

struct Measurement
{
    double rms = 0.0, difference = 0.0, third = 0.0, flatFraction = 0.0;
};

Measurement measure(double rate, int pedal, int amp, double drive, double gain, double tone)
{
    stringlab::EffectsChain effect;
    effect.prepare(rate);
    effect.setSettings({ pedal, amp, drive, gain, tone });
    Measurement result;
    double thirdSine = 0.0, thirdCosine = 0.0;
    const int count = static_cast<int>(rate);
    for (int i = 0; i < count; ++i)
    {
        const double phase = 2.0 * pi * 200.0 * i / rate;
        const float input = static_cast<float>(0.12 * std::sin(phase));
        const double output = effect.process(input);
        require(std::isfinite(output) && std::abs(output) < 3.0, "effect output stays finite and bounded");
        if (pedal == 0 && amp == 0)
            require(output == input, "bypassed effects preserve input exactly");
        if (i >= count / 2)
        {
            result.rms += output * output;
            result.difference += (output - input) * (output - input);
            thirdSine += output * std::sin(3.0 * phase);
            thirdCosine += output * std::cos(3.0 * phase);
            if (std::abs(std::abs(output) - 0.65) < 0.00001)
                result.flatFraction += 1.0;
        }
    }
    result.rms = std::sqrt(result.rms / (count / 2));
    result.difference = std::sqrt(result.difference / (count / 2));
    result.third = std::hypot(thirdSine, thirdCosine) / (count / 2);
    result.flatFraction /= count / 2;
    return result;
}

void testRate(double rate)
{
    for (int pedal = 0; pedal <= 2; ++pedal)
        for (int amp = 0; amp <= 4; ++amp)
        {
            const auto result = measure(rate, pedal, amp, 0.7, 0.7, 0.5);
            require(result.rms > 0.005, "every signal chain produces audible output");
            if (pedal != 0 || amp != 0)
                require(result.difference > 0.005, "enabled processing changes the signal");
        }

    const auto soft = measure(rate, 1, 0, 0.7, 0.5, 0.5);
    const auto hard = measure(rate, 2, 0, 0.7, 0.5, 0.5);
    require(hard.third > soft.third * 1.2, "distortion produces a different, stronger third harmonic");
    require(hard.flatFraction > 0.1 && soft.flatFraction < 0.01,
            "distortion has hard-clipped flat peaks unlike overdrive");
    const auto clean = measure(rate, 0, 1, 0.5, 0.5, 0.5);
    const auto marshall = measure(rate, 0, 2, 0.5, 0.5, 0.5);
    require(marshall.third > clean.third * 1.5, "JCM 800-style preset has more harmonic drive than clean");
    const auto low = measure(rate, 0, 2, 0.5, 0.0, 0.5);
    const auto high = measure(rate, 0, 2, 0.5, 1.0, 0.5);
    require(high.third > low.third * 2.0, "amp gain changes distortion");
    const auto dark = measure(rate, 0, 2, 0.5, 0.5, 0.0);
    const auto bright = measure(rate, 0, 2, 0.5, 0.5, 1.0);
    require(std::abs(dark.rms - bright.rms) > 0.001, "amp tone affects output");

    stringlab::EffectsChain effect;
    effect.prepare(rate);
    effect.setSettings({});
    for (int i = 0; i < 200; ++i)
        effect.process(0.1f);
    effect.setSettings({ 2, 2, 0.5, 0.5, 0.5 });
    require(std::abs(effect.process(0.1f) - 0.1f) < 0.00001f, "switch begins with old signal");
    for (int i = 0; i < static_cast<int>(rate * 0.02); ++i)
        require(std::isfinite(effect.process(0.1f)), "switch transition remains finite");
    effect.reset();
    effect.setSettings({ 2, 2, 1.0, 1.0, 1.0 });
    for (int i = 0; i < static_cast<int>(rate * 0.1); ++i)
        require(effect.process(0.0f) == 0.0f, "reset chain produces silence with no input");
}

void aliasTest()
{
    constexpr double rate = 48000.0;
    stringlab::EffectsChain effect;
    effect.prepare(rate);
    effect.setSettings({ 2, 0, 0.7, 0.5, 0.5 });
    double fundamentalReal = 0.0, fundamentalImaginary = 0.0;
    double aliasReal = 0.0, aliasImaginary = 0.0;
    for (int i = 0; i < 28800; ++i)
    {
        const double input = 0.12 * std::sin(2.0 * pi * 9000.0 * i / rate);
        const double sample = effect.process(static_cast<float>(input));
        if (i < 4800)
            continue;
        const double fundamentalPhase = 2.0 * pi * 9000.0 * i / rate;
        const double aliasPhase = 2.0 * pi * 21000.0 * i / rate;
        fundamentalReal += sample * std::cos(fundamentalPhase);
        fundamentalImaginary += sample * std::sin(fundamentalPhase);
        aliasReal += sample * std::cos(aliasPhase);
        aliasImaginary += sample * std::sin(aliasPhase);
    }
    const double fundamental = std::hypot(fundamentalReal, fundamentalImaginary);
    const double alias = std::hypot(aliasReal, aliasImaginary);
    require(fundamental > 1.0, "high-frequency distortion produces a fundamental");
    require(alias / fundamental < 0.15, "folded third harmonic stays below the measured alias budget");
}
}

int main()
{
    testRate(44100.0);
    testRate(48000.0);
    testRate(96000.0);
    aliasTest();
    std::cout << "Amp and drive tests passed\n";
}
