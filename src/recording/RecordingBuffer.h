#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace djr
{

class RecordingBuffer
{
public:
    void prepare(int channels, int capacitySamples);
    void push(const float* const* inputData, int channels, int numSamples);
    int pop(juce::AudioBuffer<float>& destination, int maxSamples);
    void reset();

private:
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> buffer;
};

} // namespace djr
