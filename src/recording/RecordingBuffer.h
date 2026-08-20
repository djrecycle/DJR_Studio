#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>

namespace djr
{

class RecordingBuffer
{
public:
    void prepare(int channels, int capacitySamples);
    void push(const float* const* inputData, int channels, int numSamples);
    int pop(juce::AudioBuffer<float>& destination, int maxSamples);
    void reset();

    /** How many samples did not fit since this was last asked, and clears the
        count.

        A ring the reader stopped draining silently loses the newest audio, and
        audio with a hole in it that nothing admits to is the worst of the
        possible outcomes - worse than refusing to record at all. The audio
        thread cannot report it, so it counts and someone else asks.
    */
    int getAndClearDroppedSamples() noexcept;

private:
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> buffer;
    std::atomic<int> droppedSamples { 0 };
};

} // namespace djr
