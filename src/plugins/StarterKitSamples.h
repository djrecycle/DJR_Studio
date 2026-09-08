#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace djr::StarterKitSamples
{

/** Every function here synthesises a short, mono, one-shot sound entirely in
    code - sine/noise plus a hand-written envelope, nothing sampled or
    downloaded - so a fresh track can be given something to play without
    bundling (or licensing) any actual recording. Deliberately simple: these
    exist to make a default track audible, not to be a serious drum machine
    or synth voice - the user's own samples are the real instrument.
*/

/** A short pitch-swept sine + click, the usual shape of a kick drum. */
juce::AudioBuffer<float> makeKick(double sampleRate);

/** A tone plus filtered noise burst, the usual shape of a snare. */
juce::AudioBuffer<float> makeSnare(double sampleRate);

/** High-passed noise with a fast decay. */
juce::AudioBuffer<float> makeClosedHihat(double sampleRate);

/** High-passed noise with a slower decay than the closed hat. */
juce::AudioBuffer<float> makeOpenHihat(double sampleRate);

/** Several short noise bursts layered close together, the usual shape of a
    hand clap.
*/
juce::AudioBuffer<float> makeClap(double sampleRate);

/** A low sine plus its second harmonic with a plucked envelope, around A1. */
juce::AudioBuffer<float> makeBassPluck(double sampleRate);

/** Three slightly detuned sines with a slow attack and release, for a pad
    voice.
*/
juce::AudioBuffer<float> makePadSwell(double sampleRate);

/** A sine plus its second harmonic with a plucked envelope, around C4 - the
    same shape as makeBassPluck, an octave and a half up.
*/
juce::AudioBuffer<float> makeKeysPluck(double sampleRate);

} // namespace djr::StarterKitSamples
