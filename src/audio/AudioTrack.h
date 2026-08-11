#pragma once

#include "AudioClip.h"
#include "Track.h"

#include <memory>
#include <vector>

namespace djr
{

/** A track that plays audio clips laid out along the timeline. */
class AudioTrack final : public Track
{
public:
    explicit AudioTrack(juce::String trackName);

    /** Takes a clip that is already decoded, so this is safe while playing. */
    void addClip(std::unique_ptr<AudioClip> clip);
    void clearClips();
    bool removeClip(int index);
    /** Cuts the clip at `beat` into two. Returns false if the cut misses. */
    bool sliceClip(int index, double beat, double tempoBpm);
    /** Whether sliceClip would accept this cut, for the slice tool's preview. */
    bool canSliceClip(int index, double beat, double tempoBpm) const;
    int getNumClips() const;
    /** Editing handle for the playlist; null when the index is out of range. */
    AudioClip* getClip(int index);
    /** Snapshot for drawing; pointers stay valid until the clip list changes. */
    std::vector<const AudioClip*> getClipsSnapshot() const;
    /** Independent copies of every clip, taken under the lock. Undo stores these
        instead of raw pointers, which would dangle as soon as a clip is removed.
        Copies share their samples, so this is cheap.
    */
    std::vector<std::unique_ptr<AudioClip>> cloneClips() const;
    /** Replaces the whole clip list in one go, for undo. */
    void replaceClips(std::vector<std::unique_ptr<AudioClip>> newClips);

protected:
    void renderAudio(juce::AudioBuffer<float>& buffer,
                     juce::MidiBuffer& midi,
                     const TrackPlaybackContext& context) override;

private:
    static constexpr int maxClips = 128;

    mutable juce::SpinLock clipLock;
    std::vector<std::unique_ptr<AudioClip>> clips;
};

} // namespace djr
