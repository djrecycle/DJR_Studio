#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace djr
{

class MasterBus
{
public:
    void setGain(float newGain) noexcept;
    float getGain() const noexcept;
    float getPeakLevel() const noexcept;
    /** Per-channel peak, 0 = left, 1 = right. Falls back to the summed peak. */
    float getPeakLevel(int channel) const noexcept;

    void process(juce::AudioBuffer<float>& buffer) noexcept;

private:
    std::atomic<float> gain { 0.85f };
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<float> peakLevelLeft { 0.0f };
    std::atomic<float> peakLevelRight { 0.0f };

    /** Where the last block's gain actually ended up. Only process() (audio
        thread only) touches this, so it needs no atomic.
    */
    float lastGain = 0.85f;
};

} // namespace djr
