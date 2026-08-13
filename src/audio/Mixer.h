#pragma once

#include "MasterBus.h"
#include "Track.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>
#include <vector>

namespace djr
{

class Mixer
{
public:
    static constexpr int maxTracks = 64;

    Mixer();

    void prepare(double sampleRate, int blockSize);
    void process(juce::AudioBuffer<float>& output, const TrackPlaybackContext& context);

    /** Adds a track at any time; it is prepared before the audio thread can see it. */
    Track* addTrack(std::unique_ptr<Track> track);
    /** Swaps the track at `index` for another one, keeping every other index
        where it was. Loading a project uses this to turn a slot into the kind of
        track the file asks for without shuffling the ones around it.
    */
    Track* replaceTrack(int index, std::unique_ptr<Track> track);
    bool removeTrack(int index);
    int getNumTracks() const noexcept;
    Track* getTrack(int index) noexcept;
    const Track* getTrack(int index) const noexcept;
    int indexOf(const Track* track) const noexcept;

    /** Track that receives live MIDI when nothing is record-armed. */
    void setLiveMidiTarget(int trackIndex) noexcept;
    int getLiveMidiTarget() const noexcept;

    // Routing ----------------------------------------------------------------
    /** Whether `trackIndex` may feed `destination`.

        False for anything that is not a bus, for a track feeding itself, and
        for any route that would come back round to `trackIndex` - the mixer is
        the only place that can see the whole graph, which is why routing is set
        through here rather than straight on the track.
    */
    bool canRoute(int trackIndex, int destination) const;
    /** Points a track's main output at a bus, or at master with -1. Returns
        false and changes nothing when the route would feed back.
    */
    bool setTrackOutput(int trackIndex, int destination);
    /** Sets one send slot. A destination of -1 switches the slot off. */
    bool setTrackSend(int trackIndex, int slot, const TrackSend& send);

    /** The order tracks are processed in, sources before the buses they feed.
        Exposed so the tests can check the ordering itself rather than only its
        audible result.
    */
    std::vector<int> getProcessOrder() const;

    MasterBus& getMasterBus() noexcept;
    const MasterBus& getMasterBus() const noexcept;

private:
    /** Recomputes the process order. The caller must hold `trackLock`.

        A topological sort over "track feeds bus" edges, so a bus is never asked
        for its sound before everything routed into it has produced any. Routing
        validation keeps the graph acyclic; if a cycle ever did get in, the
        tracks left over are appended in index order rather than dropped, so the
        audio thread is never handed a partial list.
    */
    void rebuildProcessOrder();
    /** canRoute without taking the lock, so a check and the write that follows
        it can share one.
    */
    bool canRouteUnlocked(int trackIndex, int destination) const;
    /** Every destination `trackIndex` feeds, main output first. Returns how many
        were written. Caller must hold `trackLock`.
    */
    int collectDestinations(int trackIndex, int* destinationsOut) const;
    /** Whether `from` reaches `to` by following the routing. Caller holds the lock. */
    bool reaches(int from, int to) const;

    std::vector<std::unique_ptr<Track>> tracks;
    MasterBus masterBus;
    juce::AudioBuffer<float> scratchBuffer;
    /** The pre-fader copy, filled only for tracks that have a pre-fader send. */
    juce::AudioBuffer<float> preFaderBuffer;
    /** One summing buffer per track index; only the bus ones are ever used.
        Allocated once in prepare, because the audio thread cannot allocate.
    */
    std::vector<juce::AudioBuffer<float>> busBuffers;
    /** Source-before-destination order, rebuilt whenever the graph changes. */
    std::vector<int> processOrder;
    /** Audio-thread scratch for the solo pass; a member so it never allocates. */
    std::vector<bool> audible;
    /** Guards the track vector, the process order and the routing. The audio
        thread only ever try-locks it.
    */
    mutable juce::SpinLock trackLock;
    std::atomic<int> liveMidiTarget { 0 };
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
};

} // namespace djr
