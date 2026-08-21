#include "AudioAnalysis.h"

#include <cmath>
#include <vector>

namespace djr
{

namespace
{
    /** How much audio the pitch search looks at. Long enough to hold several
        periods of a low note, short enough that the note has not changed.
    */
    constexpr int pitchWindowSamples = 8192;
    /** Hop of the energy envelope the tempo search is built on. Ten
        milliseconds is finer than any tempo needs and coarse enough to keep the
        autocorrelation small.
    */
    constexpr double envelopeHopSeconds = 0.01;

    /** Sums one channel into another buffer, or mixes both down to mono. */
    std::vector<float> toMono(const juce::AudioBuffer<float>& audio, int numSamples)
    {
        const auto length = juce::jlimit(0, audio.getNumSamples(), numSamples);
        const auto channels = juce::jmax(1, audio.getNumChannels());

        std::vector<float> mono(static_cast<size_t>(juce::jmax(0, length)), 0.0f);

        for (int channel = 0; channel < channels; ++channel)
        {
            const auto* source = audio.getReadPointer(juce::jmin(channel, audio.getNumChannels() - 1));

            for (int i = 0; i < length; ++i)
                mono[static_cast<size_t>(i)] += source[i] / static_cast<float>(channels);
        }

        return mono;
    }

    /** Start of the loudest window of `windowLength` samples. */
    int loudestWindowStart(const std::vector<float>& mono, int windowLength)
    {
        const auto total = static_cast<int>(mono.size());

        if (total <= windowLength)
            return 0;

        auto best = 0;
        auto bestEnergy = -1.0;

        // Stepped rather than slid: a window landing a few hundred samples off
        // the loudest point is still the loudest note.
        const auto step = juce::jmax(1, windowLength / 4);

        for (int start = 0; start + windowLength <= total; start += step)
        {
            auto energy = 0.0;

            for (int i = start; i < start + windowLength; ++i)
                energy += static_cast<double>(mono[static_cast<size_t>(i)]) * mono[static_cast<size_t>(i)];

            if (energy > bestEnergy)
            {
                bestEnergy = energy;
                best = start;
            }
        }

        return best;
    }
}

AudioAnalysis::Pitch AudioAnalysis::detectPitch(const juce::AudioBuffer<float>& audio,
                                                int numSamples,
                                                double sampleRate)
{
    Pitch result;

    if (sampleRate <= 0.0)
        return result;

    const auto mono = toMono(audio, numSamples);
    const auto window = juce::jmin(pitchWindowSamples, static_cast<int>(mono.size()));

    // Two periods of the lowest note we look for, or there is nothing to
    // correlate against.
    if (window < 1024)
        return result;

    const auto start = loudestWindowStart(mono, window);
    const auto* samples = mono.data() + start;

    auto energy = 0.0;

    for (int i = 0; i < window; ++i)
        energy += static_cast<double>(samples[i]) * samples[i];

    if (energy <= 1.0e-9)
        return result;

    const auto minLag = juce::jmax(2, static_cast<int>(sampleRate / 2000.0));
    const auto maxLag = juce::jmin(window / 2, static_cast<int>(sampleRate / 40.0));

    if (maxLag <= minLag)
        return result;

    auto bestLag = 0;
    auto bestScore = 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        auto correlation = 0.0;
        auto lagEnergy = 0.0;

        for (int i = 0; i + lag < window; ++i)
        {
            correlation += static_cast<double>(samples[i]) * samples[i + lag];
            lagEnergy += static_cast<double>(samples[i + lag]) * samples[i + lag];
        }

        // Normalised, so a lag that happens to sit in a louder stretch does not
        // win on volume alone.
        const auto score = lagEnergy > 1.0e-12 ? correlation / std::sqrt(energy * lagEnergy) : 0.0;

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestScore < 0.3)
        return result;

    result.frequencyHz = sampleRate / bestLag;
    result.confidence = juce::jlimit(0.0, 1.0, bestScore);

    const auto exact = 69.0 + 12.0 * std::log2(result.frequencyHz / 440.0);
    result.midiNote = juce::jlimit(0, 127, static_cast<int>(std::lround(exact)));
    result.centsOff = (exact - result.midiNote) * 100.0;

    return result;
}

AudioAnalysis::Tempo AudioAnalysis::detectTempo(const juce::AudioBuffer<float>& audio,
                                                int numSamples,
                                                double sampleRate)
{
    Tempo result;

    if (sampleRate <= 0.0)
        return result;

    const auto mono = toMono(audio, numSamples);
    const auto hop = juce::jmax(1, static_cast<int>(sampleRate * envelopeHopSeconds));
    const auto hops = static_cast<int>(mono.size()) / hop;

    // Fewer than four seconds of audio cannot show a tempo more than a few
    // times over, and a tempo seen twice is a coincidence.
    if (hops < static_cast<int>(4.0 / envelopeHopSeconds))
        return result;

    std::vector<double> onsets(static_cast<size_t>(hops), 0.0);
    auto previous = 0.0;

    for (int h = 0; h < hops; ++h)
    {
        auto energy = 0.0;

        for (int i = h * hop; i < (h + 1) * hop; ++i)
            energy += std::abs(static_cast<double>(mono[static_cast<size_t>(i)]));

        energy /= hop;

        // Only the rise. Energy falling away is a note ending, and note endings
        // say nothing about where the next beat is.
        onsets[static_cast<size_t>(h)] = juce::jmax(0.0, energy - previous);
        previous = energy;
    }

    auto mean = 0.0;

    for (const auto value : onsets)
        mean += value;

    mean /= juce::jmax(1, hops);

    if (mean <= 1.0e-9)
        return result;

    for (auto& value : onsets)
        value -= mean;

    const auto minLag = juce::jmax(1, static_cast<int>(60.0 / maximumBpm / envelopeHopSeconds));
    const auto maxLag = juce::jmin(hops / 2, static_cast<int>(60.0 / minimumBpm / envelopeHopSeconds));

    if (maxLag <= minLag)
        return result;

    auto bestLag = 0;
    auto bestScore = 0.0;
    auto totalScore = 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        auto correlation = 0.0;

        for (int h = 0; h + lag < hops; ++h)
            correlation += onsets[static_cast<size_t>(h)] * onsets[static_cast<size_t>(h + lag)];

        correlation /= (hops - lag);
        totalScore += juce::jmax(0.0, correlation);

        if (correlation > bestScore)
        {
            bestScore = correlation;
            bestLag = lag;
        }
    }

    if (bestLag <= 0 || bestScore <= 0.0)
        return result;

    result.bpm = 60.0 / (bestLag * envelopeHopSeconds);

    // How much the winner stands out from everything else that was tried. A
    // beat that is really there wins by a distance; noise scores evenly.
    const auto average = totalScore / juce::jmax(1, maxLag - minLag + 1);
    result.confidence = average > 1.0e-12
        ? juce::jlimit(0.0, 1.0, (bestScore / average - 1.0) / 4.0)
        : 0.0;

    return result;
}

} // namespace djr
