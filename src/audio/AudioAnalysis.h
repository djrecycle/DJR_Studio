#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace djr
{

/** Reading a tempo and a pitch out of audio that is already recorded.

    Both are guesses, and the code says so: they come back with a confidence and
    with zero when nothing convincing was found. A sampler that quietly reports
    120 BPM for a field recording of rain is worse than one that reports nothing.

    Message thread only. Neither of these is cheap enough for the audio thread
    and neither needs to be: they answer a question the reader just asked.
*/
namespace AudioAnalysis
{
    struct Pitch
    {
        /** Zero when nothing periodic enough was found. */
        double frequencyHz = 0.0;
        /** The nearest MIDI note, or -1 with no pitch. */
        int midiNote = -1;
        /** How far the note is from the frequency, in cents. Useful for saying
            "A3, fourteen cents sharp" rather than pretending it is exactly A3.
        */
        double centsOff = 0.0;
        /** 0 to 1. Below about a third the answer is not worth showing. */
        double confidence = 0.0;
    };

    /** The strongest periodicity between 40 Hz and 2 kHz.

        Autocorrelation, on a window taken from the loudest part of the audio:
        the loudest part is the part most likely to be the note, and a window
        that lands in a gap finds the pitch of silence.
    */
    Pitch detectPitch(const juce::AudioBuffer<float>& audio, int numSamples, double sampleRate);

    struct Tempo
    {
        /** Zero when the audio had no beat worth reporting. */
        double bpm = 0.0;
        double confidence = 0.0;
    };

    /** The tempo between 60 and 200 BPM that best explains where the onsets are.

        Energy envelope, then its rise from one hop to the next - what goes up
        is an onset, what goes down is a note ending and says nothing about
        tempo - then autocorrelation over that.
    */
    Tempo detectTempo(const juce::AudioBuffer<float>& audio, int numSamples, double sampleRate);

    /** Slowest and fastest the tempo search will answer with. Outside this the
        result is folded in half or doubled, which is the honest way to handle a
        pattern that is the same at both.
    */
    constexpr double minimumBpm = 60.0;
    constexpr double maximumBpm = 200.0;
}

} // namespace djr
