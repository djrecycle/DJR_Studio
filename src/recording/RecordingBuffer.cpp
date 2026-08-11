#include "RecordingBuffer.h"

namespace djr
{

void RecordingBuffer::prepare(int channels, int capacitySamples)
{
    buffer.setSize(channels, juce::jmax(1, capacitySamples));
    fifo.setTotalSize(buffer.getNumSamples());
    reset();
}

void RecordingBuffer::push(const float* const* inputData, int channels, int numSamples)
{
    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);

    const auto copyBlock = [this, inputData, channels] (int start, int size)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* source = channel < channels ? inputData[channel] : nullptr;
            if (source != nullptr)
                buffer.copyFrom(channel, start, source, size);
            else
                buffer.clear(channel, start, size);
        }
    };

    copyBlock(start1, size1);
    copyBlock(start2, size2);
    fifo.finishedWrite(size1 + size2);
}

int RecordingBuffer::pop(juce::AudioBuffer<float>& destination, int maxSamples)
{
    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToRead(maxSamples, start1, size1, start2, size2);

    destination.setSize(buffer.getNumChannels(), size1 + size2, false, false, true);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        destination.copyFrom(channel, 0, buffer, channel, start1, size1);
        destination.copyFrom(channel, size1, buffer, channel, start2, size2);
    }

    fifo.finishedRead(size1 + size2);
    return size1 + size2;
}

void RecordingBuffer::reset()
{
    fifo.reset();
    buffer.clear();
}

} // namespace djr
