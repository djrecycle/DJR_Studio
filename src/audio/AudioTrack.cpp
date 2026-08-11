#include "AudioTrack.h"

namespace djr
{

AudioTrack::AudioTrack(juce::String trackName)
    : Track(std::move(trackName), TrackKind::audio)
{
    clips.reserve(static_cast<size_t>(maxClips));
}

void AudioTrack::addClip(std::unique_ptr<AudioClip> clip)
{
    if (clip == nullptr)
        return;

    const juce::SpinLock::ScopedLockType scoped(clipLock);

    if (static_cast<int>(clips.size()) >= maxClips)
        return;

    clips.push_back(std::move(clip));
}

void AudioTrack::clearClips()
{
    // Detach under the lock, free outside it: releasing megabytes of samples
    // must not stall the audio thread.
    std::vector<std::unique_ptr<AudioClip>> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(clipLock);
        detached = std::move(clips);
        clips.clear();
        clips.reserve(static_cast<size_t>(maxClips));
    }

    detached.clear();
}

bool AudioTrack::removeClip(int index)
{
    std::unique_ptr<AudioClip> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(clipLock);

        if (! juce::isPositiveAndBelow(index, static_cast<int>(clips.size())))
            return false;

        detached = std::move(clips[static_cast<size_t>(index)]);
        clips.erase(clips.begin() + index);
    }

    detached.reset();
    return true;
}

std::vector<std::unique_ptr<AudioClip>> AudioTrack::cloneClips() const
{
    std::vector<std::unique_ptr<AudioClip>> copies;

    const juce::SpinLock::ScopedLockType scoped(clipLock);
    copies.reserve(clips.size());

    for (const auto& clip : clips)
        if (auto copy = clip->duplicate())
            copies.push_back(std::move(copy));

    return copies;
}

void AudioTrack::replaceClips(std::vector<std::unique_ptr<AudioClip>> newClips)
{
    if (static_cast<int>(newClips.size()) > maxClips)
        newClips.resize(static_cast<size_t>(maxClips));

    // The old clips are freed after the lock is released, not inside it.
    std::vector<std::unique_ptr<AudioClip>> discarded;

    {
        const juce::SpinLock::ScopedLockType scoped(clipLock);
        discarded.swap(clips);
        clips.swap(newClips);
    }
}

bool AudioTrack::canSliceClip(int index, double beat, double tempoBpm) const
{
    const juce::SpinLock::ScopedLockType scoped(clipLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(clips.size())))
        return false;

    return clips[static_cast<size_t>(index)]->canSplitAt(beat, tempoBpm);
}

bool AudioTrack::sliceClip(int index, double beat, double tempoBpm)
{
    // Cut on the message thread, publish the new piece under the lock.
    std::unique_ptr<AudioClip> right;

    {
        const juce::SpinLock::ScopedLockType scoped(clipLock);

        if (! juce::isPositiveAndBelow(index, static_cast<int>(clips.size())))
            return false;

        right = clips[static_cast<size_t>(index)]->splitAt(beat, tempoBpm);

        if (right == nullptr)
            return false;

        if (static_cast<int>(clips.size()) < maxClips)
            clips.push_back(std::move(right));
    }

    return true;
}

AudioClip* AudioTrack::getClip(int index)
{
    const juce::SpinLock::ScopedLockType scoped(clipLock);
    return juce::isPositiveAndBelow(index, static_cast<int>(clips.size()))
        ? clips[static_cast<size_t>(index)].get()
        : nullptr;
}

int AudioTrack::getNumClips() const
{
    const juce::SpinLock::ScopedLockType scoped(clipLock);
    return static_cast<int>(clips.size());
}

std::vector<const AudioClip*> AudioTrack::getClipsSnapshot() const
{
    const juce::SpinLock::ScopedLockType scoped(clipLock);

    std::vector<const AudioClip*> snapshot;
    snapshot.reserve(clips.size());

    for (const auto& clip : clips)
        snapshot.push_back(clip.get());

    return snapshot;
}

void AudioTrack::renderAudio(juce::AudioBuffer<float>& buffer,
                             juce::MidiBuffer& midi,
                             const TrackPlaybackContext& context)
{
    juce::ignoreUnused(midi);

    if (! context.isPlaying)
        return;

    // Skipping a block beats blocking the device while a clip is being added.
    const juce::SpinLock::ScopedTryLockType scoped(clipLock);

    if (! scoped.isLocked())
        return;

    for (const auto& clip : clips)
    {
        if (clip == nullptr)
            continue;

        const auto clipStart = clip->getStartBeat();
        const auto clipEnd = clipStart + clip->getLengthBeats(context.tempoBpm);

        if (clipEnd <= context.startBeat || clipStart >= context.endBeat)
            continue;

        clip->addToBuffer(buffer, context.startBeat, context.tempoBpm, context.sampleRate);
    }
}

} // namespace djr
