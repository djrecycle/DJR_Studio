#include "MasterBus.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr float gainEpsilon = 1.0e-7f;
}

void MasterBus::setGain(float newGain) noexcept
{
    gain.store(juce::jlimit(0.0f, 2.0f, newGain), std::memory_order_release);
}

float MasterBus::getGain() const noexcept
{
    return gain.load(std::memory_order_acquire);
}

float MasterBus::getPeakLevel() const noexcept
{
    return peakLevel.load(std::memory_order_acquire);
}

float MasterBus::getPeakLevel(int channel) const noexcept
{
    if (channel == 0)
        return peakLevelLeft.load(std::memory_order_acquire);

    if (channel == 1)
        return peakLevelRight.load(std::memory_order_acquire);

    return getPeakLevel();
}

void MasterBus::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const auto currentGain = getGain();

    // Ramped rather than a flat gain per buffer, or riding the master fader
    // during playback clicks at every block boundary.
    if (std::abs(currentGain - lastGain) < gainEpsilon)
        buffer.applyGain(currentGain);
    else
        buffer.applyGainRamp(0, buffer.getNumSamples(), lastGain, currentGain);

    lastGain = currentGain;

    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));

    peakLevel.store(peak, std::memory_order_release);

    const auto left = buffer.getNumChannels() > 0 ? buffer.getMagnitude(0, 0, buffer.getNumSamples()) : 0.0f;
    peakLevelLeft.store(left, std::memory_order_release);
    peakLevelRight.store(buffer.getNumChannels() > 1 ? buffer.getMagnitude(1, 0, buffer.getNumSamples()) : left,
                         std::memory_order_release);
}

} // namespace djr
