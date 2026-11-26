#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace djr
{

/** Calls `render(inputs, numInputs, outputs, numOutputs, numSamples)` over a
    device block in pieces no longer than `maxBlock`, with every channel
    pointer advanced to each piece.

    The mixer and its plugins are prepared for one block size, but a JACK
    server's period can grow while the device runs (JUCE's JACK device does
    not restart on it) - PipeWire's quantum raised by another client, or by
    this app putting back the size it found. Rendering that whole would
    reallocate on the audio thread and hand plugins blocks past their
    maximum; split, nothing downstream notices.

    Channels past the 64 a piece can carry are cleared on output. Real-time
    safe: no allocation.
*/
template <typename RenderFn>
void renderInBlocksOf(int maxBlock,
                      const float* const* inputChannelData,
                      int numInputChannels,
                      float* const* outputChannelData,
                      int numOutputChannels,
                      int numSamples,
                      RenderFn&& render)
{
    maxBlock = juce::jmax(1, maxBlock);

    if (numSamples <= maxBlock)
    {
        render(inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
        return;
    }

    constexpr int maxChannels = 64;
    std::array<const float*, maxChannels> inputs {};
    std::array<float*, maxChannels> outputs {};
    const auto numIns = juce::jmin(numInputChannels, maxChannels);
    const auto numOuts = juce::jmin(numOutputChannels, maxChannels);

    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const auto count = juce::jmin(maxBlock, numSamples - offset);

        for (int channel = 0; channel < numIns; ++channel)
            inputs[(size_t) channel] = inputChannelData[channel] != nullptr ? inputChannelData[channel] + offset : nullptr;

        for (int channel = 0; channel < numOuts; ++channel)
            outputs[(size_t) channel] = outputChannelData[channel] != nullptr ? outputChannelData[channel] + offset : nullptr;

        render(inputs.data(), numIns, outputs.data(), numOuts, count);
    }

    for (int channel = numOuts; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
}

} // namespace djr
