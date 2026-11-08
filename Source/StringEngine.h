#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace stringlab
{
constexpr double pi = 3.14159265358979323846;
constexpr int maxStrings = 6;
constexpr int maxVoices = 32;
constexpr int partialCount = 16;

struct Instrument
{
    const char* name;
    int stringCount;
    std::array<int, maxStrings> tuning;
    std::array<double, maxStrings> diameterInches;
    std::array<double, maxStrings> coreRatio;
    double lengthMetres;
    bool acoustic;
    bool upright;
};

inline constexpr std::array<Instrument, 5> instruments {{
    { "Electric guitar", 6, { 40, 45, 50, 55, 59, 64 }, { .046, .036, .026, .017, .013, .010 }, { .4, .4, .45, 1, 1, 1 }, .648, false, false },
    { "Acoustic guitar", 6, { 40, 45, 50, 55, 59, 64 }, { .053, .042, .032, .024, .016, .012 }, { .4, .4, .4, .45, 1, 1 }, .648, true, false },
    { "Electric bass", 4, { 28, 33, 38, 43, 0, 0 }, { .105, .085, .065, .045, 0, 0 }, { .4, .4, .4, .4, 1, 1 }, .864, false, false },
    { "Bass VI", 6, { 28, 33, 38, 43, 47, 52 }, { .095, .075, .055, .040, .030, .024 }, { .4, .4, .4, .4, .4, .45 }, .762, false, false },
    { "Upright bass", 4, { 28, 33, 38, 43, 0, 0 }, { .105, .085, .065, .045, 0, 0 }, { .4, .4, .4, .4, 1, 1 }, 1.055, true, true }
}};

struct Settings
{
    int instrument = 0;
    bool guitarAllocation = true;
    std::array<int, maxStrings> tuningOffsets {};
    double referenceA = 440.0;
    double pluckPosition = 0.20;
    double pickupPosition = 0.16;
    double diameterScale = 1.0;
    double stiffnessScale = 0.15;
    double decaySeconds = 3.0;
    double bodyAmount = 0.35;
    double output = 0.5;
    bool nylon = false;
};

inline double midiFrequency(double note, double referenceA)
{
    return referenceA * std::pow(2.0, (note - 69.0) / 12.0);
}

// A fretted string retains its open-string tension but has a shorter speaking length.
struct StringProperties
{
    double lengthMetres;
    double massPerMetre;
    double tensionNewtons;
    double bendingRigidity;
};

class StringModel
{
public:
    explicit StringModel(StringProperties properties) noexcept : properties_(properties) {}

    double partialFrequency(int fret, int partial) const noexcept
    {
        const double speakingLength = properties_.lengthMetres * std::pow(2.0, -fret / 12.0);
        const double n = static_cast<double>(partial);
        return n / (2.0 * speakingLength)
            * std::sqrt((properties_.tensionNewtons
                + properties_.bendingRigidity * pi * pi * n * n / (speakingLength * speakingLength))
                / properties_.massPerMetre);
    }

    static StringModel tunedSteel(double openHz, double length, double diameter,
                                  double stiffnessScale, double coreRatio = 1.0) noexcept
    {
        return tunedMaterial(openHz, length, diameter, stiffnessScale, coreRatio, 7850.0, 2.0e11);
    }

    static StringModel tunedMaterial(double openHz, double length, double diameter,
                                     double stiffnessScale, double coreRatio,
                                     double density, double youngsModulus) noexcept
    {
        const double massPerLength = density * pi * diameter * diameter / 4.0;
        const double requestedRigidity = youngsModulus * pi * std::pow(diameter * coreRatio, 4.0) / 64.0 * stiffnessScale;
        const double idealTension = massPerLength * std::pow(2.0 * length * openHz, 2.0);
        const double maximumRigidity = idealTension * length * length / (pi * pi);
        const double rigidity = requestedRigidity / (1.0 + requestedRigidity / maximumRigidity);
        const double tension = idealTension - rigidity * pi * pi / (length * length);
        return StringModel({ length, massPerLength, tension, rigidity });
    }

private:
    StringProperties properties_;
};

inline double partialFrequency(double openHz, int fret, double length,
                               double diameter, double stiffnessScale, int partial, double coreRatio = 1.0)
{
    return StringModel::tunedSteel(openHz, length, diameter, stiffnessScale, coreRatio)
        .partialFrequency(fret, partial);
}

struct Partial
{
    double x = 0.0, y = 0.0, cosine = 0.0, sine = 0.0, decay = 0.0;
    float next() noexcept
    {
        const double value = x;
        const double nextX = decay * (x * cosine - y * sine);
        y = decay * (y * cosine + x * sine);
        x = nextX;
        return static_cast<float>(value);
    }
};

struct Voice
{
    std::array<Partial, partialCount> partials {};
    bool active = false;
    bool released = false;
    bool keyDown = false;
    bool acoustic = false;
    int note = -1, channel = 0, stringIndex = -1;
    std::uint64_t age = 0;
    double envelope = 0.0;

    float next(double releaseFactor) noexcept
    {
        if (!active)
            return 0.0f;
        double sample = 0.0;
        double energy = 0.0;
        for (auto& partial : partials)
        {
            sample += partial.next();
            energy += std::abs(partial.x) + std::abs(partial.y);
        }
        if (released)
            envelope *= releaseFactor;
        if (envelope < 0.00001 || energy < 0.000001)
            active = false;
        return static_cast<float>(sample * envelope);
    }
};

class Engine
{
public:
    void prepare(double rate)
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        releaseFactor = std::exp(-1.0 / (0.12 * sampleRate));
        updateBodyCoefficients();
        reset();
    }

    void reset() noexcept
    {
        voices = {};
        sustain = {};
        body = {};
        bodyHistory = {};
        ageCounter = 0;
    }

    void setSettings(const Settings& newSettings) noexcept
    {
        const bool changedInstrument = settings.instrument != newSettings.instrument;
        const bool changedAllocation = settings.guitarAllocation != newSettings.guitarAllocation;
        settings = newSettings;
        if (changedInstrument || changedAllocation)
            reset();
        if (changedInstrument)
            updateBodyCoefficients();
    }

    void noteOn(int channel, int note, float velocity) noexcept
    {
        if (channel < 1 || channel > 16 || note < 0 || note > 127)
            return;
        if (velocity <= 0.0f)
        {
            noteOff(channel, note);
            return;
        }
        const auto& instrument = instruments[static_cast<std::size_t>(std::clamp(settings.instrument, 0, 4))];
        int selected = -1;
        int bestScore = 100000;
        for (int i = 0; i < instrument.stringCount; ++i)
        {
            const int open = instrument.tuning[static_cast<std::size_t>(i)] + settings.tuningOffsets[static_cast<std::size_t>(i)];
            const int fret = note - open;
            if (fret < 0 || fret > 24)
                continue;
            bool occupied = false;
            if (settings.guitarAllocation)
                for (const auto& voice : voices)
                    occupied = occupied || (voice.active && voice.stringIndex == i);
            const int score = fret + (occupied ? 100 : 0);
            if (score < bestScore)
            {
                bestScore = score;
                selected = i;
            }
        }
        if (selected < 0)
            return;
        if (settings.guitarAllocation)
            for (auto& voice : voices)
                if (voice.active && voice.stringIndex == selected)
                    voice.active = false;

        Voice* chosen = nullptr;
        for (auto& voice : voices)
            if (!voice.active)
            {
                chosen = &voice;
                break;
            }
        if (chosen == nullptr)
            chosen = &*std::min_element(voices.begin(), voices.end(),
                                        [](const Voice& a, const Voice& b) { return a.age < b.age; });
        *chosen = {};
        chosen->active = true;
        chosen->keyDown = true;
        chosen->acoustic = instrument.acoustic;
        chosen->channel = channel;
        chosen->note = note;
        chosen->stringIndex = selected;
        chosen->age = ++ageCounter;
        chosen->envelope = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        const auto index = static_cast<std::size_t>(selected);
        const int open = instrument.tuning[index] + settings.tuningOffsets[index];
        const int fret = note - open;
        const double openHz = midiFrequency(open, settings.referenceA);
        const bool nylon = settings.nylon && settings.instrument == 1;
        constexpr std::array<double, maxStrings> nylonDiameters { .044, .035, .030, .040, .032, .028 };
        const double diameter = (nylon ? nylonDiameters[index] : instrument.diameterInches[index])
                              * .0254 * settings.diameterScale;
        const auto string = nylon
            ? StringModel::tunedMaterial(openHz, instrument.lengthMetres, diameter, settings.stiffnessScale,
                                        index < 3 ? 0.6 : 1.0, index < 3 ? 4000.0 : 1150.0, 3.0e9)
            : StringModel::tunedSteel(openHz, instrument.lengthMetres, diameter,
                                      settings.stiffnessScale, instrument.coreRatio[index]);
        for (int i = 0; i < partialCount; ++i)
        {
            const int n = i + 1;
            const double frequency = string.partialFrequency(fret, n);
            auto& partial = chosen->partials[static_cast<std::size_t>(i)];
            if (frequency >= sampleRate * 0.45)
                continue;
            const double angle = 2.0 * pi * frequency / sampleRate;
            partial.cosine = std::cos(angle);
            partial.sine = std::sin(angle);
            const double harmonic = static_cast<double>(n);
            const double pickup = instrument.acoustic ? 1.0 : std::sin(pi * harmonic * settings.pickupPosition);
            const double spectralRollOff = nylon ? std::pow(harmonic, 1.4) : harmonic;
            partial.x = 0.30 * std::sin(pi * harmonic * settings.pluckPosition) * pickup / spectralRollOff;
            const double lifetime = settings.decaySeconds / (1.0 + (nylon ? 0.4 : 0.16) * (harmonic - 1.0));
            partial.decay = std::exp(-1.0 / (std::max(0.05, lifetime) * sampleRate));
        }
    }

    void noteOff(int channel, int note) noexcept
    {
        if (channel < 1 || channel > 16)
            return;
        for (auto& voice : voices)
            if (voice.active && voice.channel == channel && voice.note == note && voice.keyDown)
            {
                voice.keyDown = false;
                if (!sustain[static_cast<std::size_t>(channel - 1)])
                    voice.released = true;
            }
    }

    void sustainPedal(int channel, bool down) noexcept
    {
        if (channel < 1 || channel > 16)
            return;
        sustain[static_cast<std::size_t>(channel - 1)] = down;
        if (!down)
            for (auto& voice : voices)
                if (voice.active && voice.channel == channel && !voice.keyDown)
                    voice.released = true;
    }

    void allNotesOff(int channel, bool immediate) noexcept
    {
        if (channel < 1 || channel > 16)
            return;
        sustain[static_cast<std::size_t>(channel - 1)] = false;
        for (auto& voice : voices)
            if (voice.active && voice.channel == channel)
            {
                voice.keyDown = false;
                voice.released = true;
                if (immediate)
                    voice.active = false;
            }
    }

    float next() noexcept
    {
        double direct = 0.0, acoustic = 0.0;
        for (auto& voice : voices)
        {
            const float value = voice.next(releaseFactor);
            if (voice.acoustic)
                acoustic += value;
            else
                direct += value;
        }
        double resonated = 0.0;
        for (std::size_t i = 0; i < body.size(); ++i)
        {
            const double out = bodyInput[i] * (acoustic - bodyHistory[1])
                             + bodyCoefficient[i] * body[i][0] - bodyRadiusSquared[i] * body[i][1];
            body[i][1] = body[i][0];
            body[i][0] = out;
            resonated += out * 0.25;
        }
        bodyHistory[1] = bodyHistory[0];
        bodyHistory[0] = acoustic;
        return static_cast<float>((direct + acoustic + settings.bodyAmount * resonated) * settings.output);
    }

    int activeVoiceCount() const noexcept
    {
        return static_cast<int>(std::count_if(voices.begin(), voices.end(), [](const Voice& v) { return v.active; }));
    }

private:
    void updateBodyCoefficients() noexcept
    {
        const auto& instrument = instruments[static_cast<std::size_t>(std::clamp(settings.instrument, 0, 4))];
        const double scale = instrument.upright ? 0.5 : 1.0;
        const std::array<double, 4> freqs { 110.0 * scale, 220.0 * scale, 440.0 * scale, 720.0 * scale };
        for (std::size_t i = 0; i < body.size(); ++i)
        {
            const double r = std::exp(-1.0 / (sampleRate * (0.07 + 0.02 * i)));
            bodyInput[i] = (1.0 - r * r) * 0.5;
            bodyCoefficient[i] = 2.0 * r * std::cos(2.0 * pi * freqs[i] / sampleRate);
            bodyRadiusSquared[i] = r * r;
        }
    }

    double sampleRate = 44100.0, releaseFactor = 0.0;
    Settings settings {};
    std::array<Voice, maxVoices> voices {};
    std::array<bool, 16> sustain {};
    std::array<std::array<double, 2>, 4> body {};
    std::array<double, 2> bodyHistory {};
    std::array<double, 4> bodyInput {}, bodyCoefficient {}, bodyRadiusSquared {};
    std::uint64_t ageCounter = 0;
};
}
