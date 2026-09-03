#include "Mixer.h"

#include "AudioTrack.h"
#include "BusTrack.h"
#include "MidiTrack.h"

namespace djr
{

Mixer::Mixer()
{
    tracks.reserve(static_cast<size_t>(maxTracks));

    // These six are also what a new project describes, so File > New keeps the
    // session it started with - see ProjectManager::newProject.
    //
    // Drums is a MIDI track so the step sequencer's pads have somewhere to write,
    // and it answers with the drum kit rather than the tonal preview voice.
    auto drums = std::make_unique<MidiTrack>("Drums");
    drums->setPreviewDrumKit(true);
    addTrack(std::move(drums));

    addTrack(std::make_unique<MidiTrack>("Bass"));
    addTrack(std::make_unique<MidiTrack>("Pad"));
    addTrack(std::make_unique<AudioTrack>("Vox"));
    addTrack(std::make_unique<AudioTrack>("FX"));
    addTrack(std::make_unique<MidiTrack>("Keys"));
}

void Mixer::prepare(double sampleRate, int blockSize, int numChannels)
{
    // Clamped to what a track's own plugin chain can carry - PluginChain's
    // scratch buffers are this wide regardless, so sizing past it would just
    // add channels nothing downstream can fill.
    const auto channels = juce::jlimit(1, PluginChain::maxPluginChannels, numChannels);

    scratchBuffer.setSize(channels, blockSize, false, false, true);
    preFaderBuffer.setSize(channels, blockSize, false, false, true);

    // Room for far more events than a block can carry - an all-notes-off is 129
    // of them, the densest pattern nothing like that - so the audio thread never
    // grows this. clear() keeps the storage, so paying once here is the whole cost.
    scratchMidi.ensureSize(8192);

    const juce::SpinLock::ScopedLockType scoped(trackLock);
    preparedSampleRate = sampleRate;
    preparedBlockSize = blockSize;
    preparedNumChannels = channels;

    // One summing buffer per slot, allocated here because the audio thread
    // cannot: a bus that appears mid-session must already have somewhere to sum.
    busBuffers.resize(static_cast<size_t>(maxTracks));

    for (auto& buffer : busBuffers)
        buffer.setSize(channels, blockSize, false, false, true);

    audible.resize(static_cast<size_t>(maxTracks));

    // Reserved rather than sized: the latency pass assigns into these every
    // time, and an assign that fits inside the capacity never allocates. Taking
    // that capacity here is what keeps the refresh's lock hold empty of malloc.
    latencyOwn.reserve(static_cast<size_t>(maxTracks));
    latencyIsBus.reserve(static_cast<size_t>(maxTracks));
    latencyDestinations.reserve(static_cast<size_t>(maxTracks));
    latencyHolds.reserve(static_cast<size_t>(maxTracks));

    // A value no hold can take, so the first refresh after a device change
    // always writes: prepare re-sizes the delay lines, and whatever they were
    // holding for the old sample rate does not carry over.
    latencyApplied.assign(static_cast<size_t>(maxTracks), -1);

    // One delay line per slot, sized here for the same reason as the buffers:
    // a track that appears mid-session must already have somewhere to be held.
    outputDelays.resize(static_cast<size_t>(maxTracks));
    preFaderDelays.resize(static_cast<size_t>(maxTracks));

    for (auto* lines : { &outputDelays, &preFaderDelays })
    {
        for (auto& delay : *lines)
        {
            if (delay == nullptr)
                delay = std::make_unique<AlignmentDelay>();

            delay->prepare(channels, sampleRate);
        }
    }

    for (auto& track : tracks)
        track->prepare(sampleRate, blockSize);

    rebuildProcessOrder();
}

std::vector<int> Mixer::computeLatencyHolds(const std::vector<int>& ownLatency,
                                           const std::vector<bool>& isBus,
                                           const std::vector<int>& destinations,
                                           int& longestPathOut)
{
    std::vector<int> holds;
    computeLatencyHolds(ownLatency, isBus, destinations, holds, longestPathOut);
    return holds;
}

void Mixer::computeLatencyHolds(const std::vector<int>& ownLatency,
                                const std::vector<bool>& isBus,
                                const std::vector<int>& destinations,
                                std::vector<int>& holds,
                                int& longestPathOut)
{
    const auto count = static_cast<int>(ownLatency.size());

    // assign rather than construct: called with a buffer that already has the
    // capacity, this never reaches for the heap.
    holds.assign(static_cast<size_t>(juce::jmax(0, count)), 0);
    longestPathOut = 0;

    if (count <= 0 || static_cast<int>(isBus.size()) != count
        || static_cast<int>(destinations.size()) != count)
        return;

    // What a signal leaving `index` still has to pass through before it reaches
    // the master, following the main output.
    const auto downstreamOf = [&ownLatency, &destinations, count] (int index)
    {
        auto total = 0;
        auto at = destinations[static_cast<size_t>(index)];

        // Bounded by the track count: routing is validated acyclic, and a cycle
        // that slipped through must not spin forever here.
        for (int step = 0; step < count && juce::isPositiveAndBelow(at, count); ++step)
        {
            total += ownLatency[static_cast<size_t>(at)];
            at = destinations[static_cast<size_t>(at)];
        }

        return total;
    };

    // The slowest path from a real source decides how long everything waits.
    for (int i = 0; i < count; ++i)
        if (! isBus[static_cast<size_t>(i)])
            longestPathOut = juce::jmax(longestPathOut,
                                        ownLatency[static_cast<size_t>(i)] + downstreamOf(i));

    for (int i = 0; i < count; ++i)
    {
        // Buses are held back by whatever fed them - every source arrives at a
        // bus already aligned - so compensating one again delays it twice.
        if (isBus[static_cast<size_t>(i)])
            continue;

        holds[static_cast<size_t>(i)] =
            juce::jmax(0, longestPathOut - (ownLatency[static_cast<size_t>(i)] + downstreamOf(i)));
    }
}

void Mixer::refreshLatencyCompensation()
{
    auto trackCount = 0;

    {
        // A snapshot, and nothing else. This runs on the message thread at UI
        // rate, and the lock it takes is the one the audio thread only ever
        // try-locks - a failed try there costs a whole block of silence. So the
        // hold has to be as short as it can be made: the allocations and the
        // arithmetic that used to sit inside it are both out of it now.
        const juce::SpinLock::ScopedLockType scoped(trackLock);

        trackCount = static_cast<int>(tracks.size());

        // assign, not resize: the capacity was taken in prepare, so none of
        // these three touch the heap.
        latencyOwn.assign(static_cast<size_t>(trackCount), 0);
        latencyIsBus.assign(static_cast<size_t>(trackCount), false);
        latencyDestinations.assign(static_cast<size_t>(trackCount), Track::masterDestination);

        for (int i = 0; i < trackCount; ++i)
        {
            const auto& track = tracks[static_cast<size_t>(i)];
            latencyOwn[static_cast<size_t>(i)] = track->getPluginLatencySamples();
            latencyIsBus[static_cast<size_t>(i)] = track->getKind() == TrackKind::bus;
            latencyDestinations[static_cast<size_t>(i)] = track->getOutputDestination();
        }
    }

    if (trackCount <= 0)
    {
        reportedLatency.store(0, std::memory_order_release);
        return;
    }

    // Outside the lock: from here on it is arithmetic on the copy above.
    auto longest = 0;
    computeLatencyHolds(latencyOwn, latencyIsBus, latencyDestinations, latencyHolds, longest);

    reportedLatency.store(longest, std::memory_order_release);

    // Nearly every tick finds the graph exactly as it left it - the timer is
    // only here to catch a plugin that changes its own latency while it runs -
    // so taking the lock a second time to write back the same numbers would be
    // contention for nothing.
    if (latencyHolds == latencyApplied)
        return;

    latencyApplied = latencyHolds;

    const juce::SpinLock::ScopedLockType scoped(trackLock);

    // Tracks may have come or gone since the snapshot; the delay lines are sized
    // once in prepare and never shrink, but bound the walk by all of them rather
    // than trusting the count we started with.
    const auto limit = juce::jmin(trackCount,
                                  static_cast<int>(latencyHolds.size()),
                                  static_cast<int>(outputDelays.size()),
                                  static_cast<int>(preFaderDelays.size()));

    for (int i = 0; i < limit; ++i)
    {
        const auto hold = latencyHolds[static_cast<size_t>(i)];

        if (auto& delay = outputDelays[static_cast<size_t>(i)])
            delay->setDelaySamples(hold);

        if (auto& delay = preFaderDelays[static_cast<size_t>(i)])
            delay->setDelaySamples(hold);
    }
}

int Mixer::getLatencyCompensationSamples(int index) const
{
    const juce::SpinLock::ScopedLockType scoped(trackLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(outputDelays.size())))
        return 0;

    const auto& delay = outputDelays[static_cast<size_t>(index)];
    return delay != nullptr ? delay->getDelaySamples() : 0;
}

int Mixer::getReportedLatencySamples() const noexcept
{
    return reportedLatency.load(std::memory_order_acquire);
}

int Mixer::getPreparedChannelCount() const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(trackLock);
    return preparedNumChannels;
}

void Mixer::process(juce::AudioBuffer<float>& output, const TrackPlaybackContext& context)
{
    output.clear();

    // A failed try-lock means the message thread is adding or removing a track;
    // dropping this one block is better than blocking the audio device.
    const juce::SpinLock::ScopedTryLockType scoped(trackLock);
    if (! scoped.isLocked())
        return;

    const auto numChannels = output.getNumChannels();
    const auto numSamples = output.getNumSamples();
    const auto trackCount = static_cast<int>(tracks.size());

    scratchBuffer.setSize(numChannels, numSamples, false, false, true);
    preFaderBuffer.setSize(numChannels, numSamples, false, false, true);

    const auto anySolo = std::any_of(tracks.begin(), tracks.end(),
                                    [] (const auto& track) { return track->isSoloed(); });

    // Armed tracks capture live playing; with nothing armed it follows the
    // selected track, which is what makes the keyboard audible by default.
    const auto anyArmed = std::any_of(tracks.begin(), tracks.end(),
                                      [] (const auto& track) { return track->isRecordArmed(); });
    const auto target = liveMidiTarget.load(std::memory_order_acquire);

    audible.assign(static_cast<size_t>(trackCount), ! anySolo);

    if (anySolo)
    {
        for (int index = 0; index < trackCount; ++index)
            audible[static_cast<size_t>(index)] = tracks[static_cast<size_t>(index)]->isSoloed();

        // A soloed track routed to a bus needs that bus open too, or soloing it
        // would silence the very thing being soloed. The process order is
        // topological, so carrying the flag forward once is enough.
        for (const auto index : processOrder)
        {
            if (! juce::isPositiveAndBelow(index, trackCount) || ! audible[static_cast<size_t>(index)])
                continue;

            int destinations[Track::maxSends + 1];
            const auto count = collectDestinations(index, destinations);

            for (int i = 0; i < count; ++i)
                if (juce::isPositiveAndBelow(destinations[i], trackCount))
                    audible[static_cast<size_t>(destinations[i])] = true;
        }
    }

    // Buses sum into these, so they start empty every block.
    for (int index = 0; index < trackCount; ++index)
    {
        auto& busBuffer = busBuffers[static_cast<size_t>(index)];
        busBuffer.setSize(numChannels, numSamples, false, false, true);
        busBuffer.clear();
    }

    for (const auto index : processOrder)
    {
        if (! juce::isPositiveAndBelow(index, trackCount))
            continue;

        auto& track = tracks[static_cast<size_t>(index)];

        if (! audible[static_cast<size_t>(index)])
            continue;

        const auto receivesLiveMidi = anyArmed ? track->isRecordArmed() : index == target;

        auto trackContext = context;
        if (! receivesLiveMidi)
            trackContext.liveMidi = nullptr;

        // A bus plays what was summed into it, which the process order
        // guarantees is already complete by the time we get here.
        if (track->getKind() == TrackKind::bus)
            trackContext.busInput = &busBuffers[static_cast<size_t>(index)];

        const auto wantsPreFader = track->hasPreFaderSend();

        scratchBuffer.clear();

        if (wantsPreFader)
            preFaderBuffer.clear();

        track->processAudio(scratchBuffer, scratchMidi, trackContext,
                            wantsPreFader ? &preFaderBuffer : nullptr);

        // Held back here, before the signal is summed anywhere: sends and the
        // main output both come off this buffer, and a bus further down is
        // already lined up by the time it reads what fed it.
        if (auto& delay = outputDelays[static_cast<size_t>(index)])
            delay->process(scratchBuffer);

        if (wantsPreFader)
            if (auto& delay = preFaderDelays[static_cast<size_t>(index)])
                delay->process(preFaderBuffer);

        const auto addInto = [numChannels, numSamples] (juce::AudioBuffer<float>& destination,
                                                        const juce::AudioBuffer<float>& source,
                                                        float gain)
        {
            for (int channel = 0; channel < numChannels; ++channel)
                destination.addFrom(channel, 0, source, channel, 0, numSamples, gain);
        };

        for (int slot = 0; slot < Track::maxSends; ++slot)
        {
            const auto send = track->getSend(slot);

            if (! send.isActive() || ! juce::isPositiveAndBelow(send.destination, trackCount))
                continue;

            addInto(busBuffers[static_cast<size_t>(send.destination)],
                    send.preFader ? preFaderBuffer : scratchBuffer,
                    send.level);
        }

        const auto destination = track->getOutputDestination();

        if (juce::isPositiveAndBelow(destination, trackCount))
            addInto(busBuffers[static_cast<size_t>(destination)], scratchBuffer, 1.0f);
        else
            addInto(output, scratchBuffer, 1.0f);
    }

    masterBus.process(output);
}

int Mixer::collectDestinations(int trackIndex, int* destinationsOut) const
{
    if (! juce::isPositiveAndBelow(trackIndex, static_cast<int>(tracks.size())))
        return 0;

    const auto& track = tracks[static_cast<size_t>(trackIndex)];
    auto count = 0;

    if (const auto output = track->getOutputDestination(); output >= 0)
        destinationsOut[count++] = output;

    for (int slot = 0; slot < Track::maxSends; ++slot)
        if (const auto send = track->getSend(slot); send.destination >= 0)
            destinationsOut[count++] = send.destination;

    return count;
}

bool Mixer::reaches(int from, int to) const
{
    if (from == to)
        return true;

    const auto trackCount = static_cast<int>(tracks.size());
    std::vector<bool> seen(static_cast<size_t>(trackCount), false);
    std::vector<int> pending { from };

    while (! pending.empty())
    {
        const auto current = pending.back();
        pending.pop_back();

        if (! juce::isPositiveAndBelow(current, trackCount) || seen[static_cast<size_t>(current)])
            continue;

        seen[static_cast<size_t>(current)] = true;

        int destinations[Track::maxSends + 1];
        const auto count = collectDestinations(current, destinations);

        for (int i = 0; i < count; ++i)
        {
            if (destinations[i] == to)
                return true;

            pending.push_back(destinations[i]);
        }
    }

    return false;
}

bool Mixer::canRoute(int trackIndex, int destination) const
{
    const juce::SpinLock::ScopedLockType scoped(trackLock);
    return canRouteUnlocked(trackIndex, destination);
}

bool Mixer::canRouteUnlocked(int trackIndex, int destination) const
{
    const auto trackCount = static_cast<int>(tracks.size());

    if (! juce::isPositiveAndBelow(trackIndex, trackCount))
        return false;

    // Master is the one destination that can never loop.
    if (destination < 0)
        return true;

    if (! juce::isPositiveAndBelow(destination, trackCount) || destination == trackIndex)
        return false;

    // Only a bus has somewhere to put what it receives.
    if (tracks[static_cast<size_t>(destination)]->getKind() != TrackKind::bus)
        return false;

    // The route is legal unless following it comes back round to where it started.
    return ! reaches(destination, trackIndex);
}

bool Mixer::setTrackOutput(int trackIndex, int destination)
{
    // Checked and applied under one lock: a check that let go first could be
    // answering about a graph that no longer exists by the time we write.
    const juce::SpinLock::ScopedLockType scoped(trackLock);

    if (! canRouteUnlocked(trackIndex, destination))
        return false;

    tracks[static_cast<size_t>(trackIndex)]->setOutputDestination(destination);
    rebuildProcessOrder();
    return true;
}

bool Mixer::setTrackSend(int trackIndex, int slot, const TrackSend& send)
{
    if (! juce::isPositiveAndBelow(slot, Track::maxSends))
        return false;

    const juce::SpinLock::ScopedLockType scoped(trackLock);

    if (! juce::isPositiveAndBelow(trackIndex, static_cast<int>(tracks.size())))
        return false;

    // Switching a slot off is always allowed; it removes an edge, so it cannot
    // create a loop.
    if (send.destination >= 0 && ! canRouteUnlocked(trackIndex, send.destination))
        return false;

    tracks[static_cast<size_t>(trackIndex)]->setSend(slot, send);
    rebuildProcessOrder();
    return true;
}

std::vector<int> Mixer::getProcessOrder() const
{
    const juce::SpinLock::ScopedLockType scoped(trackLock);
    return processOrder;
}

void Mixer::rebuildProcessOrder()
{
    const auto trackCount = static_cast<int>(tracks.size());

    processOrder.clear();
    processOrder.reserve(static_cast<size_t>(trackCount));

    // Kahn's algorithm: a track can be processed once nothing left feeds it.
    std::vector<int> incoming(static_cast<size_t>(trackCount), 0);

    for (int index = 0; index < trackCount; ++index)
    {
        int destinations[Track::maxSends + 1];
        const auto count = collectDestinations(index, destinations);

        for (int i = 0; i < count; ++i)
            if (juce::isPositiveAndBelow(destinations[i], trackCount))
                ++incoming[static_cast<size_t>(destinations[i])];
    }

    std::vector<int> ready;

    for (int index = 0; index < trackCount; ++index)
        if (incoming[static_cast<size_t>(index)] == 0)
            ready.push_back(index);

    while (! ready.empty())
    {
        const auto current = ready.front();
        ready.erase(ready.begin());
        processOrder.push_back(current);

        int destinations[Track::maxSends + 1];
        const auto count = collectDestinations(current, destinations);

        for (int i = 0; i < count; ++i)
        {
            if (! juce::isPositiveAndBelow(destinations[i], trackCount))
                continue;

            if (--incoming[static_cast<size_t>(destinations[i])] == 0)
                ready.push_back(destinations[i]);
        }
    }

    // Anything still here is part of a cycle, which routing validation is meant
    // to make impossible. Append it rather than drop it: a track missing from
    // the order would go silent with nothing on screen to explain why.
    if (static_cast<int>(processOrder.size()) < trackCount)
    {
        std::vector<bool> placed(static_cast<size_t>(trackCount), false);

        for (const auto index : processOrder)
            placed[static_cast<size_t>(index)] = true;

        for (int index = 0; index < trackCount; ++index)
            if (! placed[static_cast<size_t>(index)])
                processOrder.push_back(index);
    }
}

Track* Mixer::addTrack(std::unique_ptr<Track> track)
{
    if (track == nullptr || getNumTracks() >= maxTracks)
        return nullptr;

    // Prepare before publishing, so the audio thread never sees an unprepared track.
    track->prepare(preparedSampleRate, preparedBlockSize);

    auto* raw = track.get();

    {
        const juce::SpinLock::ScopedLockType scoped(trackLock);
        tracks.push_back(std::move(track));
        rebuildProcessOrder();
    }

    return raw;
}

Track* Mixer::replaceTrack(int index, std::unique_ptr<Track> track)
{
    if (track == nullptr)
        return nullptr;

    // Prepared before it is published, for the same reason as in addTrack.
    track->prepare(preparedSampleRate, preparedBlockSize);

    auto* raw = track.get();
    std::unique_ptr<Track> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(trackLock);

        if (! juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
            return nullptr;

        detached = std::move(tracks[static_cast<size_t>(index)]);
        tracks[static_cast<size_t>(index)] = std::move(track);

        // The slot keeps its index but may no longer be a bus, so anything
        // aimed at it has to let go - otherwise a send would feed a MIDI track
        // that has no way to pass it on.
        if (tracks[static_cast<size_t>(index)]->getKind() != TrackKind::bus)
            for (auto& other : tracks)
                other->dropRoutesTo(index);

        rebuildProcessOrder();
    }

    // Destroying the old track releases its plugins, which can block - do it
    // here, outside the lock the audio thread contends for.
    detached.reset();
    return raw;
}

bool Mixer::removeTrack(int index)
{
    std::unique_ptr<Track> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(trackLock);

        if (! juce::isPositiveAndBelow(index, static_cast<int>(tracks.size())))
            return false;

        detached = std::move(tracks[static_cast<size_t>(index)]);
        tracks.erase(tracks.begin() + index);

        // Removing a track shifts every index above it, so routing has to move
        // with it. Routes that pointed at the track itself go back to master;
        // the rest slide down one. Skipping this silently re-aims a send at
        // whatever track happens to inherit the index.
        for (auto& other : tracks)
        {
            other->dropRoutesTo(index);
            other->remapDestinations(index);
        }

        rebuildProcessOrder();
    }

    // Destroying the track releases its plugins, which can block - do it here,
    // outside the lock the audio thread contends for.
    detached.reset();
    return true;
}

int Mixer::getNumTracks() const noexcept
{
    return static_cast<int>(tracks.size());
}

void Mixer::setLiveMidiTarget(int trackIndex) noexcept
{
    liveMidiTarget.store(trackIndex, std::memory_order_release);
}

int Mixer::getLiveMidiTarget() const noexcept
{
    return liveMidiTarget.load(std::memory_order_acquire);
}

int Mixer::indexOf(const Track* track) const noexcept
{
    for (int i = 0; i < getNumTracks(); ++i)
        if (tracks[static_cast<size_t>(i)].get() == track)
            return i;

    return -1;
}

Track* Mixer::getTrack(int index) noexcept
{
    return juce::isPositiveAndBelow(index, getNumTracks()) ? tracks[static_cast<size_t>(index)].get() : nullptr;
}

const Track* Mixer::getTrack(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, getNumTracks()) ? tracks[static_cast<size_t>(index)].get() : nullptr;
}

MasterBus& Mixer::getMasterBus() noexcept
{
    return masterBus;
}

const MasterBus& Mixer::getMasterBus() const noexcept
{
    return masterBus;
}

} // namespace djr
