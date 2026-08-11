#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace djr
{

/** Anything that can hand the audio graph a block of freshly played MIDI.

    Implemented by MidiEngine; AudioEngine pulls from it once per block so the
    hardware keyboard and the on-screen keyboard reach the tracks the same way.
*/
class LiveMidiSource
{
public:
    virtual ~LiveMidiSource() = default;

    /** Called from the audio thread when the device starts. */
    virtual void prepareLiveMidi(double sampleRate) = 0;

    /** Called once per audio block; must not block. */
    virtual void fetchLiveMidi(juce::MidiBuffer& destination, int numSamples) = 0;
};

} // namespace djr
