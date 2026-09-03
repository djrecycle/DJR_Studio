#pragma once

#include "Track.h"

namespace djr
{

/** A track whose signal comes from other tracks rather than from a clip.

    It plays nothing of its own: the mixer sums everything routed to it into one
    buffer, hands that over through `TrackPlaybackContext::busInput`, and from
    there a bus behaves exactly like any other track - inserts, fader, pan, its
    own output destination and its own sends. That is what makes a reverb bus and
    a submix the same object.

    Nothing here works without the mixer's process order putting every feeder
    ahead of the bus; see Mixer::rebuildProcessOrder.
*/
class BusTrack final : public Track
{
public:
    explicit BusTrack(juce::String trackName);

protected:
    void renderAudio(juce::AudioBuffer<float>& buffer,
                     juce::MidiBuffer& midi,
                     const TrackPlaybackContext& context) override;
};

} // namespace djr
