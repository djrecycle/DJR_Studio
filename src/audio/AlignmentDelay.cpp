#include "AlignmentDelay.h"

namespace djr
{

void AlignmentDelay::prepare(int numChannels, double sampleRate)
{
    capacity = juce::jmax(1, static_cast<int>(sampleRate * maxDelaySeconds) + 1);
    history.setSize(juce::jmax(1, numChannels), capacity, false, true, false);
    reset();
}

void AlignmentDelay::setDelaySamples(int samples) noexcept
{
    delaySamples.store(juce::jlimit(0, juce::jmax(0, capacity - 1), samples),
                       std::memory_order_release);
}

int AlignmentDelay::getDelaySamples() const noexcept
{
    return delaySamples.load(std::memory_order_acquire);
}

void AlignmentDelay::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto delay = getDelaySamples();

    if (delay <= 0 || capacity <= 1)
        return;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin(buffer.getNumChannels(), history.getNumChannels());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Read before write, so a delay shorter than one block still works: the
        // sample being written this instant must not be the one read back.
        const auto readPosition = (writePosition - delay + capacity) % capacity;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* line = history.getWritePointer(channel);
            const auto incoming = buffer.getSample(channel, sample);

            buffer.setSample(channel, sample, line[readPosition]);
            line[writePosition] = incoming;
        }

        writePosition = (writePosition + 1) % capacity;
    }
}

void AlignmentDelay::reset() noexcept
{
    history.clear();
    writePosition = 0;
}

} // namespace djr
