#include "BusTrack.h"

namespace djr
{

BusTrack::BusTrack(juce::String trackName)
    : Track(std::move(trackName), TrackKind::bus)
{
    // A bus is a summing point, so it starts at unity. Anything less and adding
    // a bus would quietly turn the mix down.
    setVolume(1.0f);
}

void BusTrack::renderAudio(juce::AudioBuffer<float>& buffer,
                           juce::MidiBuffer& midi,
                           const TrackPlaybackContext& context)
{
    juce::ignoreUnused(midi);

    // No input means nothing is routed here yet, or the mixer skipped the bus
    // this block. Either way silence is the right answer, and the buffer has
    // already been cleared by processAudio.
    if (context.busInput == nullptr)
        return;

    const auto& input = *context.busInput;
    const auto numSamples = juce::jmin(buffer.getNumSamples(), input.getNumSamples());

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        if (channel < input.getNumChannels())
            buffer.copyFrom(channel, 0, input, channel, 0, numSamples);
}

} // namespace djr
