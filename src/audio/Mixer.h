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
    bool removeTrack(int index);
    int getNumTracks() const noexcept;
    Track* getTrack(int index) noexcept;
    const Track* getTrack(int index) const noexcept;
    int indexOf(const Track* track) const noexcept;

    /** Track that receives live MIDI when nothing is record-armed. */
    void setLiveMidiTarget(int trackIndex) noexcept;
    int getLiveMidiTarget() const noexcept;

    MasterBus& getMasterBus() noexcept;
    const MasterBus& getMasterBus() const noexcept;

private:
    std::vector<std::unique_ptr<Track>> tracks;
    MasterBus masterBus;
    juce::AudioBuffer<float> scratchBuffer;
    /** Guards the track vector. The audio thread only ever try-locks it. */
    mutable juce::SpinLock trackLock;
    std::atomic<int> liveMidiTarget { 0 };
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 512;
};

} // namespace djr
