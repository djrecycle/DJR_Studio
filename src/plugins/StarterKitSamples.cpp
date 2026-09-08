#include "StarterKitSamples.h"

#include <cmath>

namespace djr::StarterKitSamples
{

namespace
{
    /** Scales `buffer` so its peak sits at `targetPeak` - every generated
        pad ends up at roughly the same loudness without clipping, whatever
        shape its own envelope happens to produce.
    */
    void normalise(juce::AudioBuffer<float>& buffer, float targetPeak)
    {
        const auto peak = buffer.getMagnitude(0, buffer.getNumSamples());

        if (peak > 0.0001f)
            buffer.applyGain(targetPeak / peak);
    }

    /** Noise run through two cascaded one-pole highpasses, with an
        exponential decay - the shared shape behind both hi-hats, just at
        different decay rates and lengths.
    */
    juce::AudioBuffer<float> makeFilteredNoiseDecay(double sampleRate, double decayRate, double lengthSeconds)
    {
        const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
        juce::AudioBuffer<float> buffer(1, juce::jmax(1, numSamples));
        auto* data = buffer.getWritePointer(0);

        juce::Random random;
        float previous1 = 0.0f;
        float previous2 = 0.0f;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto t = static_cast<double>(i) / sampleRate;
            const auto raw = random.nextFloat() * 2.0f - 1.0f;
            const auto stage1 = raw - previous1 * 0.7f;
            previous1 = raw;
            const auto stage2 = stage1 - previous2 * 0.7f;
            previous2 = stage1;

            const auto envelope = static_cast<float>(std::exp(-t * decayRate));
            data[i] = stage2 * envelope;
        }

        normalise(buffer, 0.8f);
        return buffer;
    }

    /** A fundamental plus its second harmonic under a plucked envelope (fast
        attack, exponential decay) - the shared shape behind the bass and
        keys starter sounds, just at different pitches and lengths.
    */
    juce::AudioBuffer<float> makePluck(double sampleRate, double frequency, double lengthSeconds)
    {
        const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
        juce::AudioBuffer<float> buffer(1, juce::jmax(1, numSamples));
        auto* data = buffer.getWritePointer(0);

        double phase1 = 0.0;
        double phase2 = 0.0;

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto t = static_cast<double>(i) / sampleRate;
            phase1 += juce::MathConstants<double>::twoPi * frequency / sampleRate;
            phase2 += juce::MathConstants<double>::twoPi * (frequency * 2.0) / sampleRate;

            const auto attack = static_cast<float>(juce::jmin(1.0, t / 0.003));
            const auto decay = static_cast<float>(std::exp(-t * 3.0));
            const auto envelope = attack * decay;

            const auto value = std::sin(phase1) * 0.8 + std::sin(phase2) * 0.2;
            data[i] = static_cast<float>(value) * envelope;
        }

        normalise(buffer, 0.9f);
        return buffer;
    }
}

juce::AudioBuffer<float> makeKick(double sampleRate)
{
    const auto lengthSeconds = 0.35;
    const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* data = buffer.getWritePointer(0);

    juce::Random random;
    double phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        // The pitch starts high and drops fast - the "thump" of a kick is
        // mostly this sweep, not the tone it settles on.
        const auto pitchEnvelope = std::exp(-t * 35.0);
        const auto frequency = 50.0 + 110.0 * pitchEnvelope;
        phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;

        const auto amplitudeEnvelope = std::exp(-t * 9.0);
        auto sample = static_cast<float>(std::sin(phase) * amplitudeEnvelope);

        // A few milliseconds of noise at the very start, for the beater
        // click a pure sine sweep would otherwise leave out entirely.
        if (t < 0.004)
            sample += static_cast<float>((random.nextFloat() * 2.0f - 1.0f) * (1.0 - t / 0.004) * 0.4);

        data[i] = sample;
    }

    normalise(buffer, 0.9f);
    return buffer;
}

juce::AudioBuffer<float> makeSnare(double sampleRate)
{
    const auto lengthSeconds = 0.2;
    const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* data = buffer.getWritePointer(0);

    juce::Random random;
    double phase = 0.0;
    const double toneFrequency = 180.0;
    float previousNoise = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        phase += juce::MathConstants<double>::twoPi * toneFrequency / sampleRate;
        const auto toneEnvelope = std::exp(-t * 30.0);
        const auto tone = static_cast<float>(std::sin(phase) * toneEnvelope * 0.5);

        const auto rawNoise = random.nextFloat() * 2.0f - 1.0f;
        const auto filteredNoise = rawNoise - previousNoise * 0.5f;
        previousNoise = rawNoise;
        const auto noiseEnvelope = static_cast<float>(std::exp(-t * 14.0));

        data[i] = tone + filteredNoise * noiseEnvelope * 0.7f;
    }

    normalise(buffer, 0.9f);
    return buffer;
}

juce::AudioBuffer<float> makeClosedHihat(double sampleRate)
{
    return makeFilteredNoiseDecay(sampleRate, 40.0, 0.1);
}

juce::AudioBuffer<float> makeOpenHihat(double sampleRate)
{
    return makeFilteredNoiseDecay(sampleRate, 8.0, 0.4);
}

juce::AudioBuffer<float> makeClap(double sampleRate)
{
    const auto lengthSeconds = 0.25;
    const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    buffer.clear();
    auto* data = buffer.getWritePointer(0);

    juce::Random random;
    float previous = 0.0f;

    // Four short bursts close together (the "flam" of several hands at
    // once), then one longer tail burst - the usual shape of a clap.
    const double burstStarts[] = { 0.0, 0.01, 0.02, 0.03, 0.045 };
    const double burstLengths[] = { 0.02, 0.02, 0.02, 0.02, 0.15 };
    const double burstDecay[] = { 60.0, 60.0, 60.0, 60.0, 18.0 };

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        const auto rawNoise = random.nextFloat() * 2.0f - 1.0f;
        const auto filteredNoise = rawNoise - previous * 0.6f;
        previous = rawNoise;

        float sample = 0.0f;

        for (int b = 0; b < 5; ++b)
        {
            if (t >= burstStarts[b] && t < burstStarts[b] + burstLengths[b])
            {
                const auto localT = t - burstStarts[b];
                sample += filteredNoise * static_cast<float>(std::exp(-localT * burstDecay[b]));
            }
        }

        data[i] = sample;
    }

    normalise(buffer, 0.9f);
    return buffer;
}

juce::AudioBuffer<float> makeBassPluck(double sampleRate)
{
    // A1 - low enough to sit under everything else, high enough to still
    // read as a pitched note rather than a rumble.
    return makePluck(sampleRate, 55.0, 0.9);
}

juce::AudioBuffer<float> makePadSwell(double sampleRate)
{
    const auto lengthSeconds = 2.0;
    const auto numSamples = static_cast<int>(sampleRate * lengthSeconds);
    juce::AudioBuffer<float> buffer(1, numSamples);
    auto* data = buffer.getWritePointer(0);

    const double baseFrequency = 220.0; // A3 - sits above a bass pluck without masking it
    const double detunes[] = { -0.4, 0.0, 0.4 };
    double phases[3] = { 0.0, 0.0, 0.0 };

    const double attackSeconds = 0.5;
    const double releaseSeconds = 0.6;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto t = static_cast<double>(i) / sampleRate;
        double value = 0.0;

        for (int v = 0; v < 3; ++v)
        {
            phases[v] += juce::MathConstants<double>::twoPi * (baseFrequency + detunes[v]) / sampleRate;
            value += std::sin(phases[v]);
        }

        value /= 3.0;

        const auto attack = juce::jmin(1.0, t / attackSeconds);
        const auto timeToEnd = lengthSeconds - t;
        const auto release = juce::jmin(1.0, timeToEnd / releaseSeconds);
        const auto envelope = juce::jmin(attack, release);

        data[i] = static_cast<float>(value * envelope);
    }

    normalise(buffer, 0.85f);
    return buffer;
}

juce::AudioBuffer<float> makeKeysPluck(double sampleRate)
{
    // C4 - an octave and a half above the bass pluck, a familiar "middle of
    // the keyboard" register.
    return makePluck(sampleRate, 261.63, 0.7);
}

} // namespace djr::StarterKitSamples
