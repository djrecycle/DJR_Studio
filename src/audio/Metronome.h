#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace djr

{

/** The click track.

    The click is synthesised rather than loaded from a file, so it costs no disk
    access, no sample memory, and cannot go missing from an install. The audio
    thread only reads atomics here; nothing allocates.
*/
class Metronome
{
public:
    void prepare(double sampleRate) noexcept;

    void setEnabled(bool shouldBeEnabled) noexcept;
    bool isEnabled() const noexcept;

    /** Adds clicks for every beat that falls inside this block.

        `startBeat` and `endBeat` bound the block on the timeline, and
        `beatsPerBar` decides which of those beats gets the accented pitch.
    */
    void process(juce::AudioBuffer<float>& output,
                 double startBeat,
                 double endBeat,
                 double beatsPerBar) noexcept;

    /** Silences a click that is still ringing, e.g. when the transport stops. */
    void reset() noexcept;

    /** Counts `beats` in before recording starts, clicking on the spot without
        moving the playhead.

        Rolling the transport backwards would be the usual way to do this, but
        the playhead cannot go below zero here, which is exactly where people
        record from most often. Counting in place works from anywhere.
    */
    void startCountIn(int beats, double tempoBpm, double beatsPerBar) noexcept;
    bool isCountingIn() const noexcept;
    void cancelCountIn() noexcept;

private:
    /** Starts a click; the tail may run past the end of this block. */
    void trigger(bool accented) noexcept;
    /** Writes the ringing click into `output` from `startSample` onwards. */
    void renderInto(juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept;

    /** Runs the count-in for one block. Returns true when it handled the block. */
    bool processCountIn(juce::AudioBuffer<float>& output) noexcept;

    std::atomic<bool> enabled { false };
    double preparedSampleRate = 44100.0;

    // Count-in handshake: the message thread posts a request, the audio thread
    // picks it up and clears the flag when the count is done.
    std::atomic<int> pendingCountInBeats { 0 };
    std::atomic<double> pendingTempoBpm { 120.0 };
    std::atomic<double> pendingBeatsPerBar { 4.0 };
    std::atomic<bool> countingIn { false };

    // Count-in state, audio thread only.
    int countInBeatsLeft = 0;
    double countInSamplesPerBeat = 0.0;
    double countInSamplesToNext = 0.0;
    double countInBarBeats = 4.0;
    int countInBeatIndex = 0;

    // Voice state, touched only by the audio thread.
    double phase = 0.0;
    double phaseDelta = 0.0;
    double envelope = 0.0;
    double envelopeDecay = 0.0;
    int remainingSamples = 0;
};

} // namespace djr
