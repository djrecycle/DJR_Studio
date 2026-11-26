#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

namespace djr
{

/** The three-band EQ every mixer channel carries: a low shelf, a sweepable
    mid bell and a high shelf, after the inserts and before the fader - where
    a console puts its channel EQ, so a pre-fader send hears it too.

    Settings are written from the message thread and read by the audio thread
    through atomics; the filters are redesigned on the audio thread, once per
    block, only when something changed. Flat or switched off, process() does
    nothing at all, so a channel nobody EQs costs nothing.

    Biquads from Robert Bristow-Johnson's Audio EQ Cookbook, transposed
    direct form II, written out here rather than taken from juce_dsp, which
    this app does not otherwise link.
*/
class ChannelEq
{
public:
    static constexpr float minGainDb = -15.0f;
    static constexpr float maxGainDb = 15.0f;
    static constexpr float lowShelfHz = 100.0f;
    static constexpr float highShelfHz = 8000.0f;
    static constexpr float minMidHz = 150.0f;
    static constexpr float maxMidHz = 8000.0f;
    static constexpr float defaultMidHz = 1000.0f;
    /** Channels with their own filter state; more than a stereo strip ever has. */
    static constexpr int maxChannels = 8;

    struct Settings
    {
        bool enabled = true;
        float lowDb = 0.0f;
        float midDb = 0.0f;
        float midHz = defaultMidHz;
        float highDb = 0.0f;

        /** Nothing to do: switched off, or every band at 0 dB. */
        bool isNeutral() const noexcept;
        bool operator== (const Settings& other) const noexcept;
        bool operator!= (const Settings& other) const noexcept { return ! (*this == other); }
    };

    ChannelEq() = default;

    /** Message thread. Values are clamped to the ranges above. */
    void setSettings(const Settings& newSettings) noexcept;
    Settings getSettings() const noexcept;

    void prepare(double sampleRate) noexcept;
    /** Audio thread. Clears filter state, e.g. after a gap in processing. */
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer) noexcept;

    /** The response of `settings` at `frequency`, in dB - for the curve the
        strip and the editor draw, and for tests.
    */
    static float magnitudeDb(const Settings& settings, double sampleRate, double frequency) noexcept;

    juce::var toVar() const;
    /** Anything missing or not an object reads as the defaults: flat. */
    void fromVar(const juce::var& state);

private:
    struct Coefficients
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    };

    enum Band { lowBand, midBand, highBand, numBands };

    static Coefficients design(Band band, const Settings& settings, double sampleRate) noexcept;
    static Settings clamped(Settings settings) noexcept;

    std::atomic<bool> enabled { true };
    std::atomic<float> lowDb { 0.0f };
    std::atomic<float> midDb { 0.0f };
    std::atomic<float> midHz { defaultMidHz };
    std::atomic<float> highDb { 0.0f };
    std::atomic<int> version { 0 };

    // Audio thread only from here on.
    double sampleRate = 44100.0;
    int designedVersion = -1;
    bool wasActive = false;
    std::array<Coefficients, numBands> coefficients {};
    /** z1, z2 per band per channel. */
    std::array<std::array<std::array<float, 2>, numBands>, maxChannels> state {};
};

} // namespace djr
