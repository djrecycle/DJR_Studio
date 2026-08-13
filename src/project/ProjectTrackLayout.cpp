#include "ProjectTrackLayout.h"

#include "audio/AudioTrack.h"
#include "audio/BusTrack.h"
#include "audio/MidiTrack.h"
#include "audio/Mixer.h"

namespace djr
{

namespace
{
    TrackKind kindFor(const ProjectTrackState& state, const Track* existing)
    {
        const auto type = state.type.trim().toLowerCase();

        if (type == "audio")
            return TrackKind::audio;

        if (type == "midi")
            return TrackKind::midi;

        if (type == "bus")
            return TrackKind::bus;

        // Projects written before the type was saved leave it empty. Whatever is
        // already in this slot is a better guess than a fixed default, and it
        // keeps such a file opening the way it always has.
        return existing != nullptr ? existing->getKind() : TrackKind::midi;
    }

    std::unique_ptr<Track> makeTrack(TrackKind kind, const juce::String& name)
    {
        if (kind == TrackKind::audio)
            return std::make_unique<AudioTrack>(name);

        if (kind == TrackKind::bus)
            return std::make_unique<BusTrack>(name);

        return std::make_unique<MidiTrack>(name);
    }
} // namespace

bool applyProjectTrackLayout(Mixer& mixer, const juce::Array<ProjectTrackState>& states)
{
    // A project that names no tracks says nothing about the track list: that is
    // what a file from before tracks were saved looks like, and emptying the
    // mixer over one would leave the session with nothing to play.
    if (states.isEmpty())
        return false;

    // More tracks than the mixer holds are dropped here rather than one failed
    // addTrack at a time, so the loop below never chases a slot that cannot exist.
    const auto wanted = juce::jmin(states.size(), Mixer::maxTracks);
    auto changed = false;

    // From the end, so the surviving tracks keep their indices while this runs.
    while (mixer.getNumTracks() > wanted)
    {
        if (! mixer.removeTrack(mixer.getNumTracks() - 1))
            break;

        changed = true;
    }

    for (int i = 0; i < wanted; ++i)
    {
        const auto& state = states.getReference(i);
        auto* existing = mixer.getTrack(i);
        const auto kind = kindFor(state, existing);

        if (existing != nullptr && existing->getKind() == kind)
            continue;

        // The name is set again by the per-track restore; it is passed here so a
        // track is never nameless, not even for the moment in between.
        auto fresh = makeTrack(kind, state.name);

        auto* installed = existing == nullptr ? mixer.addTrack(std::move(fresh))
                                             : mixer.replaceTrack(i, std::move(fresh));

        if (installed != nullptr)
            changed = true;
    }

    return changed;
}

} // namespace djr
