#include "ChannelEq.h"

#include <cmath>
#include <complex>

namespace djr
{

namespace
{
    constexpr float flatThresholdDb = 0.01f;
    /** Bell width: about an octave and a half - broad, musical, hard to misuse. */
    constexpr double midQ = 0.9;
    /** Shelf slope S = 1, the steepest without a bump. */
    constexpr double shelfSlope = 1.0;
}

//==============================================================================
bool ChannelEq::Settings::isNeutral() const noexcept
{
    return ! enabled
        || (std::abs(lowDb) < flatThresholdDb && std::abs(midDb) < flatThresholdDb && std::abs(highDb) < flatThresholdDb);
}

bool ChannelEq::Settings::operator== (const Settings& other) const noexcept
{
    return enabled == other.enabled && lowDb == other.lowDb && midDb == other.midDb
        && midHz == other.midHz && highDb == other.highDb;
}

ChannelEq::Settings ChannelEq::clamped(Settings settings) noexcept
{
    settings.lowDb = juce::jlimit(minGainDb, maxGainDb, settings.lowDb);
    settings.midDb = juce::jlimit(minGainDb, maxGainDb, settings.midDb);
    settings.highDb = juce::jlimit(minGainDb, maxGainDb, settings.highDb);
    settings.midHz = juce::jlimit(minMidHz, maxMidHz, settings.midHz);
    return settings;
}

//==============================================================================
void ChannelEq::setSettings(const Settings& newSettings) noexcept
{
    const auto settings = clamped(newSettings);

    enabled.store(settings.enabled, std::memory_order_relaxed);
    lowDb.store(settings.lowDb, std::memory_order_relaxed);
    midDb.store(settings.midDb, std::memory_order_relaxed);
    midHz.store(settings.midHz, std::memory_order_relaxed);
    highDb.store(settings.highDb, std::memory_order_relaxed);
    version.fetch_add(1, std::memory_order_release);
}

ChannelEq::Settings ChannelEq::getSettings() const noexcept
{
    Settings settings;
    settings.enabled = enabled.load(std::memory_order_relaxed);
    settings.lowDb = lowDb.load(std::memory_order_relaxed);
    settings.midDb = midDb.load(std::memory_order_relaxed);
    settings.midHz = midHz.load(std::memory_order_relaxed);
    settings.highDb = highDb.load(std::memory_order_relaxed);
    return settings;
}

void ChannelEq::prepare(double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    designedVersion = -1;
    reset();
}

void ChannelEq::reset() noexcept
{
    for (auto& channel : state)
        for (auto& band : channel)
            band = { 0.0f, 0.0f };
}

void ChannelEq::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto currentVersion = version.load(std::memory_order_acquire);
    const auto settings = getSettings();

    if (settings.isNeutral())
    {
        // Leaving the filters' memory behind would replay the tail of the
        // last note EQ'd as a click the next time a band is turned up.
        if (wasActive)
            reset();

        wasActive = false;
        return;
    }

    if (currentVersion != designedVersion)
    {
        for (int band = 0; band < numBands; ++band)
            coefficients[(size_t) band] = design(static_cast<Band>(band), settings, sampleRate);

        designedVersion = currentVersion;
    }

    wasActive = true;

    const auto numChannels = juce::jmin(buffer.getNumChannels(), maxChannels);
    const auto numSamples = buffer.getNumSamples();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = buffer.getWritePointer(channel);

        for (int band = 0; band < numBands; ++band)
        {
            const auto& c = coefficients[(size_t) band];
            auto& z = state[(size_t) channel][(size_t) band];
            auto z1 = z[0];
            auto z2 = z[1];

            for (int i = 0; i < numSamples; ++i)
            {
                const auto x = samples[i];
                const auto y = c.b0 * x + z1;
                z1 = c.b1 * x - c.a1 * y + z2;
                z2 = c.b2 * x - c.a2 * y;
                samples[i] = y;
            }

            z = { z1, z2 };
        }
    }
}

//==============================================================================
ChannelEq::Coefficients ChannelEq::design(Band band, const Settings& settings, double rate) noexcept
{
    const auto gainDb = band == lowBand ? settings.lowDb : band == midBand ? settings.midDb : settings.highDb;

    // A flat band passes straight through, exactly.
    if (std::abs(gainDb) < flatThresholdDb)
        return {};

    const auto frequency = band == lowBand ? (double) lowShelfHz : band == midBand ? (double) settings.midHz : (double) highShelfHz;
    const auto nyquistSafe = juce::jmin(frequency, rate * 0.45);

    const auto a = std::pow(10.0, gainDb / 40.0);
    const auto w0 = juce::MathConstants<double>::twoPi * nyquistSafe / rate;
    const auto cosW = std::cos(w0);
    const auto sinW = std::sin(w0);

    double b0, b1, b2, a0, a1, a2;

    if (band == midBand)
    {
        const auto alpha = sinW / (2.0 * midQ);
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha / a;
    }
    else
    {
        const auto alpha = sinW / 2.0 * std::sqrt((a + 1.0 / a) * (1.0 / shelfSlope - 1.0) + 2.0);
        const auto twoRootAAlpha = 2.0 * std::sqrt(a) * alpha;

        if (band == lowBand)
        {
            b0 = a * ((a + 1.0) - (a - 1.0) * cosW + twoRootAAlpha);
            b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW);
            b2 = a * ((a + 1.0) - (a - 1.0) * cosW - twoRootAAlpha);
            a0 = (a + 1.0) + (a - 1.0) * cosW + twoRootAAlpha;
            a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW);
            a2 = (a + 1.0) + (a - 1.0) * cosW - twoRootAAlpha;
        }
        else
        {
            b0 = a * ((a + 1.0) + (a - 1.0) * cosW + twoRootAAlpha);
            b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW);
            b2 = a * ((a + 1.0) + (a - 1.0) * cosW - twoRootAAlpha);
            a0 = (a + 1.0) - (a - 1.0) * cosW + twoRootAAlpha;
            a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW);
            a2 = (a + 1.0) - (a - 1.0) * cosW - twoRootAAlpha;
        }
    }

    Coefficients c;
    c.b0 = static_cast<float>(b0 / a0);
    c.b1 = static_cast<float>(b1 / a0);
    c.b2 = static_cast<float>(b2 / a0);
    c.a1 = static_cast<float>(a1 / a0);
    c.a2 = static_cast<float>(a2 / a0);
    return c;
}

float ChannelEq::magnitudeDb(const Settings& settings, double rate, double frequency) noexcept
{
    if (settings.isNeutral() || rate <= 0.0)
        return 0.0f;

    const auto safeSettings = clamped(settings);
    const auto w = juce::MathConstants<double>::twoPi * juce::jlimit(1.0, rate * 0.5, frequency) / rate;
    const auto z1 = std::polar(1.0, -w);
    const auto z2 = z1 * z1;
    auto total = 0.0;

    for (int band = 0; band < numBands; ++band)
    {
        const auto c = design(static_cast<Band>(band), safeSettings, rate);
        const auto response = ((double) c.b0 + (double) c.b1 * z1 + (double) c.b2 * z2)
                             / (1.0 + (double) c.a1 * z1 + (double) c.a2 * z2);
        total += 20.0 * std::log10(juce::jmax(1.0e-12, std::abs(response)));
    }

    return static_cast<float>(total);
}

//==============================================================================
juce::var ChannelEq::toVar() const
{
    const auto settings = getSettings();
    auto* object = new juce::DynamicObject();
    object->setProperty("enabled", settings.enabled);
    object->setProperty("lowDb", settings.lowDb);
    object->setProperty("midDb", settings.midDb);
    object->setProperty("midHz", settings.midHz);
    object->setProperty("highDb", settings.highDb);
    return juce::var(object);
}

void ChannelEq::fromVar(const juce::var& stateVar)
{
    Settings settings;

    if (auto* object = stateVar.getDynamicObject())
    {
        const auto read = [object] (const char* key, double fallback)
        {
            return object->hasProperty(key) ? static_cast<double>(object->getProperty(key)) : fallback;
        };

        settings.enabled = object->hasProperty("enabled") ? static_cast<bool>(object->getProperty("enabled")) : true;
        settings.lowDb = static_cast<float>(read("lowDb", 0.0));
        settings.midDb = static_cast<float>(read("midDb", 0.0));
        settings.midHz = static_cast<float>(read("midHz", defaultMidHz));
        settings.highDb = static_cast<float>(read("highDb", 0.0));
    }

    setSettings(settings);
}

} // namespace djr
