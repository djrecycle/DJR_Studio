#include "Mixer.h"

#include "AudioTrack.h"
#include "MidiTrack.h"

namespace djr
{

Mixer::Mixer()
{
    tracks.reserve(static_cast<size_t>(maxTracks));

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

    const juce::SpinLock::ScopedLockType scoped(trackLock);
    preparedSampleRate = sampleRate;
    preparedBlockSize = blockSize;

    for (auto& track : tracks)
        track->prepare(sampleRate, blockSize);
}

void Mixer::process(juce::AudioBuffer<float>& output, const TrackPlaybackContext& context)
{
    output.clear();

    // A failed try-lock means the message thread is adding or removing a track;
    // dropping this one block is better than blocking the audio device.
    const juce::SpinLock::ScopedTryLockType scoped(trackLock);
    if (! scoped.isLocked())
        return;

    scratchBuffer.setSize(output.getNumChannels(), output.getNumSamples(), false, false, true);

    const auto anySolo = std::any_of(tracks.begin(), tracks.end(),
                                    [] (const auto& track) { return track->isSoloed(); });

    // Armed tracks capture live playing; with nothing armed it follows the
    // selected track, which is what makes the keyboard audible by default.
    const auto anyArmed = std::any_of(tracks.begin(), tracks.end(),
                                      [] (const auto& track) { return track->isRecordArmed(); });
    const auto target = liveMidiTarget.load(std::memory_order_acquire);

    for (int index = 0; index < static_cast<int>(tracks.size()); ++index)
    {
        auto& track = tracks[static_cast<size_t>(index)];

        if (anySolo && ! track->isSoloed())
            continue;

        const auto receivesLiveMidi = anyArmed ? track->isRecordArmed() : index == target;

        auto trackContext = context;
        if (! receivesLiveMidi)
            trackContext.liveMidi = nullptr;

        scratchBuffer.clear();
        juce::MidiBuffer midi;
        track->processAudio(scratchBuffer, midi, trackContext);

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.addFrom(channel, 0, scratchBuffer, channel, 0, output.getNumSamples());
    }

    masterBus.process(output);
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
    }

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
