#include "TimeStretch.h"

#include <cmath>
#include <vector>

namespace djr
{
namespace TimeStretch
{

namespace
{
    /** Half the window, so consecutive Hann windows sum back to one. */
    constexpr int synthesisHop = windowSize / 2;
    /** The correlation is read every fourth sample. Finding roughly the right
        phase is what matters; reading all of them costs four times as much for
        a shift that lands in the same place.
    */
    constexpr int correlationStride = 4;

    std::vector<float> makeHannWindow()
    {
        std::vector<float> window(static_cast<size_t>(windowSize));

        for (int i = 0; i < windowSize; ++i)
            window[static_cast<size_t>(i)] = 0.5f
                - 0.5f * std::cos(juce::MathConstants<float>::twoPi * static_cast<float>(i)
                                      / static_cast<float>(windowSize - 1));

        return window;
    }

    /** How well `source` starting at `at` continues what `reference` held.
        Plain correlation, not normalised: every candidate is read from the same
        signal, so the scale they share cancels out of the comparison.
    */
    float similarity(const float* source, int length, int at, const std::vector<float>& reference)
    {
        auto total = 0.0f;

        for (size_t i = 0; i < reference.size(); ++i)
        {
            const auto index = at + static_cast<int>(i) * correlationStride;

            if (index < 0 || index >= length)
                continue;

            total += source[index] * reference[i];
        }

        return total;
    }
}

int outputLengthFor(int sourceLength, double rate)
{
    if (sourceLength <= 0)
        return 0;

    if (rate <= 0.0)
        return sourceLength;

    return juce::jmax(1, static_cast<int>(std::ceil(sourceLength / rate)));
}

juce::AudioBuffer<float> process(const juce::AudioBuffer<float>& source, double rate)
{
    const auto numChannels = juce::jmax(1, source.getNumChannels());
    const auto sourceLength = source.getNumSamples();

    // Nothing to do, and nothing worth degrading the audio for.
    if (sourceLength <= 0 || rate <= 0.0 || std::abs(rate - 1.0) < 1.0e-9
        || sourceLength < windowSize * 2)
    {
        juce::AudioBuffer<float> copy(numChannels, juce::jmax(1, sourceLength));
        copy.clear();

        for (int channel = 0; channel < numChannels && channel < source.getNumChannels(); ++channel)
            copy.copyFrom(channel, 0, source, channel, 0, sourceLength);

        return copy;
    }

    const auto outputLength = outputLengthFor(sourceLength, rate);
    const auto analysisHop = juce::jmax(1, juce::roundToInt(synthesisHop * rate));

    static const auto window = makeHannWindow();

    juce::AudioBuffer<float> output(numChannels, outputLength + windowSize);
    output.clear();

    // The phase search runs on the first channel alone and the result is applied
    // to all of them: sliding the channels independently would tear the stereo
    // image apart at every window.
    const auto* guide = source.getReadPointer(0);

    std::vector<float> reference(static_cast<size_t>(synthesisHop / correlationStride), 0.0f);

    auto readPosition = 0;
    auto writePosition = 0;
    auto offset = 0;

    while (writePosition + windowSize < output.getNumSamples())
    {
        const auto take = juce::jlimit(0, juce::jmax(0, sourceLength - windowSize),
                                       readPosition + offset);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, source.getNumChannels() - 1);
            const auto* input = source.getReadPointer(sourceChannel);
            auto* out = output.getWritePointer(channel);

            for (int i = 0; i < windowSize; ++i)
                out[writePosition + i] += input[take + i] * window[static_cast<size_t>(i)];
        }

        // What would have followed this window if the source had simply carried
        // on. The next window is chosen to look as much like this as possible,
        // which is what keeps the waveform continuous across the join.
        for (size_t i = 0; i < reference.size(); ++i)
        {
            const auto index = take + synthesisHop + static_cast<int>(i) * correlationStride;
            reference[i] = juce::isPositiveAndBelow(index, sourceLength) ? guide[index] : 0.0f;
        }

        readPosition += analysisHop;
        writePosition += synthesisHop;

        if (readPosition + windowSize >= sourceLength)
            break;

        auto bestOffset = 0;
        auto bestScore = -std::numeric_limits<float>::max();

        for (int candidate = -searchRadius; candidate <= searchRadius; ++candidate)
        {
            const auto at = readPosition + candidate;

            if (at < 0 || at + windowSize >= sourceLength)
                continue;

            const auto score = similarity(guide, sourceLength, at, reference);

            if (score > bestScore)
            {
                bestScore = score;
                bestOffset = candidate;
            }
        }

        offset = bestOffset;
    }

    // The tail beyond the requested length is window overlap that never got its
    // partner; trimming keeps the clip exactly as long as the tempo asked for.
    juce::AudioBuffer<float> trimmed(numChannels, outputLength);
    trimmed.clear();

    for (int channel = 0; channel < numChannels; ++channel)
        trimmed.copyFrom(channel, 0, output, channel, 0,
                         juce::jmin(outputLength, output.getNumSamples()));

    return trimmed;
}

} // namespace TimeStretch
} // namespace djr
