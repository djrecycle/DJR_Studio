#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace djr
{

/** The preview instrument a MIDI track uses until a real one is loaded.

    Driven entirely by MIDI, so sequenced notes and live keyboard playing both
    go through the same path. Two flavours: a tonal voice for melodic parts and
    a synthesised drum kit for the step sequencer's pads.

    The tonal voice is shaped from the channel window's Generator page: one
    waveform and one ADSR, which is what a channel gets in FL before a real
    generator is loaded. The drum kit ignores both - each hit owns its own
    shape, and giving a kick an attack knob would only mean muffling it.
*/
class SimpleSynth
{
public:
    /** The shapes a generator is expected to have. Sine is first because it is
        what every project made before this existed already sounds like.
    */
    enum class Waveform
    {
        sine = 0,
        triangle,
        saw,
        square,
        noise,
        numWaveforms
    };

    /** The tonal voice's amplitude envelope, in seconds except sustain, which
        is the level the note holds at.
    */
    struct Envelope
    {
        float attack = 0.005f;
        float decay = 0.12f;
        float sustain = 0.75f;
        float release = 0.18f;
    };

    SimpleSynth();

    void prepare(double sampleRate);
    /** Const, because the synthesiser underneath only reads the notes. Taking it
        by reference is what lets the caller hand over its own buffer instead of
        copying one per block.
    */
    void render(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    void allNotesOff();

    /** Drum mode maps the General MIDI percussion pitches to synthesised hits. */
    void setDrumMode(bool shouldUseDrums);
    bool isDrumMode() const noexcept;

    // Message thread ---------------------------------------------------------
    void setWaveform(Waveform newWaveform) noexcept;
    void setEnvelope(const Envelope& newEnvelope) noexcept;
    /** True while nothing has been changed from what the synth has always
        sounded like, so a project need not carry the defaults around.
    */
    bool isDefault() const noexcept;
    /** Puts the waveform and envelope back to what a channel sounds like
        before anyone has touched its Generator page.
    */
    void resetToDefault() noexcept;

    juce::var toVar() const;
    /** A void or malformed value leaves the defaults in place, which is what an
        older project has to read back as.
    */
    void fromVar(const juce::var& value);

    // Both threads -----------------------------------------------------------
    /** Plain atomic loads: the voices read these while they render, and the
        window writes them while they do. Nothing here allocates or locks.
    */
    Waveform getWaveform() const noexcept;
    Envelope getEnvelope() const noexcept;

private:
    juce::Synthesiser synth;
    bool drumMode = false;

    std::atomic<int> waveform { static_cast<int>(Waveform::sine) };
    std::atomic<float> attack { 0.005f };
    std::atomic<float> decay { 0.12f };
    std::atomic<float> sustain { 0.75f };
    std::atomic<float> release { 0.18f };
};

} // namespace djr
