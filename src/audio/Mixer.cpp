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

void Mixer::prepare(double sampleRate, int blockSize)
{
    scratchBuffer.setSize(2, blockSize, false, false, true);
    preFaderBuffer.setSize(2, blockSize, false, false, true);

    const juce::SpinLock::ScopedLockType scoped(trackLock);
    preparedSampleRate = sampleRate;
    preparedBlockSize = blockSize;

    // One summing buffer per slot, allocated here because the audio thread
    // cannot: a bus that appears mid-session must already have somewhere to sum.
    busBuffers.resize(static_cast<size_t>(maxTracks));

    for (auto& buffer : busBuffers)
        buffer.setSize(2, blockSize, false, false, true);

    audible.resize(static_cast<size_t>(maxTracks));

    for (auto& track : tracks)
        track->prepare(sampleRate, blockSize);

    rebuildProcessOrder();
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

        juce::MidiBuffer midi;
        track->processAudio(scratchBuffer, midi, trackContext,
                            wantsPreFader ? &preFaderBuffer : nullptr);

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
