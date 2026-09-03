#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace djr

{

/** The delay that lines one track up with the rest.

    Sized once in prepare and never again: changing how much delay it applies
    only moves a read offset, so the audio thread never allocates. The amount is
    an atomic because the message thread works it out and the audio thread reads
    it, and a torn int here would be an audible jump.
*/
class AlignmentDelay
{
public:
    /** Longest compensation worth carrying. A linear-phase EQ asks for a few
        thousand samples; anything past half a second is a broken plugin, not a
        latency to chase.
    */
    static constexpr double maxDelaySeconds = 0.5;

    void prepare(int numChannels, double sampleRate);
    void setDelaySamples(int samples) noexcept;
    int getDelaySamples() const noexcept;

    /** Delays `buffer` in place. Does nothing at all when the delay is zero,
        which is the case for whichever track is the latest one anyway.
    */
    void process(juce::AudioBuffer<float>& buffer) noexcept;
    void reset() noexcept;

private:
    juce::AudioBuffer<float> history;
    int writePosition = 0;
    int capacity = 0;
    std::atomic<int> delaySamples { 0 };
};

} // namespace djr
