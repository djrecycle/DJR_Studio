#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace djr
{

/** The preview instrument a MIDI track uses until a real one is loaded.

    Driven entirely by MIDI, so sequenced notes and live keyboard playing both
    go through the same path. Two flavours: a tonal voice for melodic parts and
    a synthesised drum kit for the step sequencer's pads.
*/
class SimpleSynth
{
public:
    SimpleSynth();

    void prepare(double sampleRate);
    void render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void allNotesOff();

    /** Drum mode maps the General MIDI percussion pitches to synthesised hits. */
    void setDrumMode(bool shouldUseDrums);
    bool isDrumMode() const noexcept;

private:
    juce::Synthesiser synth;
    bool drumMode = false;
};

} // namespace djr
