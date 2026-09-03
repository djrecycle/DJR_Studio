#pragma once

#include "RecordingBuffer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace djr
{

/** Audio taken off a running signal and kept in memory, for the sample editor.

    Recording to disk is what `Recorder` does, and it is a different problem: a
    take is written straight through to a file and nobody looks at it until it
    is finished. This is the other half - a sample editor's capture - where the point is
    to hold the audio so it can be drawn, cut about, and only then written
    somewhere, if ever.

    Two threads, and the split between them is the whole design:

    - the audio thread copies each block into a fixed ring and does nothing
      else - no allocation, no waiting, no growing;
    - the message thread drains the ring into a buffer that does grow, which is
      where a long capture's memory actually comes from.

    Growing on the side that is allowed to wait is what makes the audio side
    free. The ring is the only thing both touch, and it is guarded the way the
    rest of this codebase guards audio-thread reads: a try-lock the audio thread
    is allowed to lose, where losing costs one block and is counted.

    If the message thread stops draining - a modal dialog, a long repaint - the
    ring fills and the newest audio is lost rather than the oldest. That count
    is kept too, because audio with a silent hole in it is worse than audio that
    says where the hole is.
*/
class SampleCapture
{
public:
    /** How long one capture may run before it stops itself.

        Two minutes of stereo at 44.1 kHz is about 42 MB, which is a lot to hold
        and still a short recording. The limit exists so a capture left armed
        overnight cannot quietly eat the machine.
    */
    static constexpr double maxCaptureSeconds = 120.0;

    /** Sizes the ring for a device rate. Called from wherever the host prepares
        its audio - which is the audio thread, with the stream stopped - so it
        touches only the ring, never the captured audio.

        Stops any capture in progress: what arrives next is a different stream.
    */
    void prepareRing(double sampleRate, int channels);

    /** Message thread. Arms the capture, throwing away audio captured at a rate
        that is no longer the device's - it would draw at the wrong length and
        play at the wrong speed, and there is no honest way to keep it.
    */
    void start();
    /** Safe from any thread: one atomic, and the audio side reads it per block. */
    void stop();
    bool isCapturing() const noexcept;
    /** Message thread. Throws away what was captured. */
    void clear();

    /** Message thread. Replaces what is held with audio from somewhere else -
        a file opened from disk, a clip sent in from the timeline, or a capture
        being read back out of a project.

        The store is the one place the audio lives: the clip the editor draws is
        built from it, and a recording started later appends to it. Anything
        that puts audio in has to come through here, or the two would disagree
        about what is being edited.
    */
    void adopt(const juce::AudioBuffer<float>& source, int numSamples, double sourceSampleRate);

    /** Audio thread. Copies the block into the ring when armed, and returns
        having done nothing when not.
    */
    void captureBlock(const juce::AudioBuffer<float>& buffer) noexcept;

    /** Message thread. Moves what the audio thread has pushed into the growing
        store and returns how many samples arrived - zero when nothing did, so
        a caller can skip the work that would follow.

        Stops the capture when it reaches the limit; `hasReachedLimit` says so
        afterwards, since a capture that ends on its own has to be visible.
    */
    int drain();

    /** The audio captured so far. Message thread, and only valid until the next
        drain grows it.
    */
    const juce::AudioBuffer<float>& getAudio() const noexcept;
    int getNumCapturedSamples() const noexcept;
    double getCapturedSeconds() const noexcept;
    /** The rate the captured audio was recorded at, which after a device change
        is not the rate the device is running at now.
    */
    double getSampleRate() const noexcept;

    /** Samples lost since the capture started, whether to a full ring or to a
        block that arrived while the ring was being resized.
    */
    int getDroppedSamples() const noexcept;
    bool hasReachedLimit() const noexcept;

private:
    /** How much the ring holds. Long enough that an ordinary hitch on the
        message thread costs nothing, short enough that it is not where a
        capture's memory goes.
    */
    static constexpr double ringSeconds = 2.0;
    /** How much room the store gains each time it runs out. Bigger than a drain
        so growing is rare, small enough that a short capture does not reserve
        minutes.
    */
    static constexpr double growthSeconds = 4.0;

    int capacityLimitSamples() const noexcept;

    /** Guards the ring's own storage against being resized under a push. Taken
        outright by the two threads that may wait, and only ever tried by the
        audio thread.
    */
    juce::SpinLock ringLock;
    RecordingBuffer ring;
    /** Read by the audio thread, written where the host prepares. */
    std::atomic<double> deviceSampleRate { 44100.0 };
    std::atomic<int> ringChannels { 2 };
    /** Blocks the audio thread had to throw away because the ring was busy. */
    std::atomic<int> lockedOutSamples { 0 };
    std::atomic<bool> capturing { false };

    // Message thread only, from here down.
    juce::AudioBuffer<float> store;
    juce::AudioBuffer<float> drained;
    int capturedSamples = 0;
    int droppedSamples = 0;
    /** The rate the audio in `store` was captured at, which is the rate the
        clip built from it has to carry.
    */
    double storeSampleRate = 44100.0;
    bool reachedLimit = false;
};

} // namespace djr
