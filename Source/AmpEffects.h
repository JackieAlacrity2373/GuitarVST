#pragma once

#include <algorithm>
#include <cmath>

namespace stringlab
{
struct EffectsSettings
{
    int pedal = 0;
    int amp = 0;
    double pedalDrive = 0.5;
    double ampGain = 0.5;
    double tone = 0.5;
};

class EffectsChain
{
public:
    void prepare(double rate) noexcept
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        current = {};
        previous = {};
        transitioning = 0;
        initialized = false;
        smoothing = 1.0 - std::exp(-1.0 / (0.02 * sampleRate));
    }

    void setSettings(const EffectsSettings& settings) noexcept
    {
        target = settings;
        target.pedal = std::clamp(target.pedal, 0, 2);
        target.amp = std::clamp(target.amp, 0, 4);
        target.pedalDrive = std::clamp(target.pedalDrive, 0.0, 1.0);
        target.ampGain = std::clamp(target.ampGain, 0.0, 1.0);
        target.tone = std::clamp(target.tone, 0.0, 1.0);
        if (!initialized)
        {
            current.configure(target.pedal, target.amp, sampleRate);
            pedalDrive = target.pedalDrive;
            ampGain = target.ampGain;
            tone = target.tone;
            initialized = true;
        }
        else if (target.pedal != current.pedal || target.amp != current.amp)
        {
            previous = current;
            current = {};
            current.configure(target.pedal, target.amp, sampleRate);
            transitionLength = std::max(1, static_cast<int>(sampleRate * 0.01));
            transitioning = transitionLength;
        }
    }

    float process(float input) noexcept
    {
        pedalDrive += smoothing * (target.pedalDrive - pedalDrive);
        ampGain += smoothing * (target.ampGain - ampGain);
        tone += smoothing * (target.tone - tone);
        const double output = current.process(input, pedalDrive, ampGain, tone);
        if (transitioning > 0)
        {
            const double old = previous.process(input, pedalDrive, ampGain, tone);
            const double mix = 1.0 - static_cast<double>(transitioning) / transitionLength;
            --transitioning;
            return static_cast<float>(old + mix * (output - old));
        }
        return static_cast<float>(output);
    }

private:
    struct Rig
    {
        int pedal = 0, amp = 0;
        double pedalLow = 0.0, ampLow = 0.0, toneLow = 0.0, cabLow1 = 0.0, cabLow2 = 0.0;
        double previousClipInput = 0.0;
        double pedalCoeff = 0.0, ampCoeff = 0.0, toneCoeff = 0.0, cabCoeff = 0.0;

        static double coefficient(double frequency, double rate) noexcept
        {
            constexpr double twoPi = 6.28318530717958647692;
            return 1.0 - std::exp(-twoPi * frequency / rate);
        }

        void configure(int newPedal, int newAmp, double rate) noexcept
        {
            pedal = newPedal;
            amp = newAmp;
            pedalCoeff = coefficient(pedal == 1 ? 420.0 : 110.0, rate);
            static constexpr double bassCut[] { 70.0, 70.0, 100.0, 125.0, 95.0 };
            static constexpr double toneFreq[] { 800.0, 650.0, 850.0, 1050.0, 700.0 };
            static constexpr double cabFreq[] { 5000.0, 4800.0, 4300.0, 5200.0, 3900.0 };
            ampCoeff = coefficient(bassCut[amp], rate);
            toneCoeff = coefficient(toneFreq[amp], rate);
            cabCoeff = coefficient(cabFreq[amp], rate);
        }

        double process(double input, double drive, double gain, double tone) noexcept
        {
            double x = input;
            if (pedal != 0)
            {
                pedalLow += pedalCoeff * (x - pedalLow);
                const double highPassed = x - pedalLow;
                if (pedal == 1)
                    x = 0.72 * std::tanh((highPassed + 0.35 * pedalLow) * (2.0 + 8.0 * drive));
                else
                {
                    const double clipInput = highPassed * (4.0 + 24.0 * drive);
                    const double delta = clipInput - previousClipInput;
                    const auto primitive = [](double value)
                    {
                        const double magnitude = std::abs(value);
                        return magnitude <= 1.0 ? 0.5 * value * value : magnitude - 0.5;
                    };
                    // First-order antiderivative antialiasing integrates the clipper between samples.
                    const double clipped = std::abs(delta) > 1.0e-5
                        ? (primitive(clipInput) - primitive(previousClipInput)) / delta
                        : std::clamp((clipInput + previousClipInput) * 0.5, -1.0, 1.0);
                    previousClipInput = clipInput;
                    x = 0.65 * clipped;
                }
            }
            if (amp != 0)
            {
                ampLow += ampCoeff * (x - ampLow);
                x -= ampLow;
                static constexpr double baseGain[] { 1.0, 1.5, 3.0, 2.3, 5.0 };
                static constexpr double gainRange[] { 0.0, 3.0, 12.0, 7.0, 20.0 };
                x = std::tanh(x * (baseGain[amp] + gainRange[amp] * gain));
                toneLow += toneCoeff * (x - toneLow);
                const double low = toneLow, high = x - low;
                static constexpr double lowLevel[] { 1.0, 1.0, 0.95, 0.8, 1.1 };
                static constexpr double highLevel[] { 1.0, 1.25, 1.0, 1.4, 0.85 };
                x = low * lowLevel[amp] * (1.25 - 0.5 * tone)
                  + high * highLevel[amp] * (0.5 + tone);
                cabLow1 += cabCoeff * (x - cabLow1);
                cabLow2 += cabCoeff * (cabLow1 - cabLow2);
                x = 0.65 * cabLow2;
            }
            return x;
        }
    };

    double sampleRate = 44100.0, smoothing = 0.0;
    double pedalDrive = 0.5, ampGain = 0.5, tone = 0.5;
    EffectsSettings target {};
    Rig current {}, previous {};
    int transitioning = 0, transitionLength = 1;
    bool initialized = false;
};
}
