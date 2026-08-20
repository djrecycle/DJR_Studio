#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <utility>

namespace djr
{

/** The channel's own envelope, LFO, filter and arpeggiator.

    FL's channel settings are not the generator's: whatever makes the sound -
    our preview synth or a hosted VST - is shaped afterwards by controls that
    belong to the channel. This is that stage, and it is why the same knobs
    appear over every instrument.

    Two halves, called from two places in the block. The arpeggiator rewrites
    MIDI before anything turns it into sound; the envelope, the LFO and the
    filter shape the audio that comes back, ahead of the inserts.

    Values are written from the message thread and read by the audio thread, so
    every parameter is an atomic and nothing here allocates once prepared.
*/
class ChannelSettings
{
public:
    /** What an envelope or an LFO is aimed at, in the order of the page's tabs. */
    enum class Target
    {
        panning = 0,
        volume,
        modX,   ///< filter cutoff
        modY,   ///< filter resonance
        pitch,  ///< drawn, but nothing shifts a rendered channel's pitch yet
        numTargets
    };

    enum class ArpDirection
    {
        off = 0,
        up,
        down,
        upDown,
        random
    };

    /** DAHDSR, in knob units: everything 0..1, sustain a level and the rest
        times. The engine turns them into seconds so the UI never has to.
    */
    struct Envelope
    {
        bool enabled = false;
        float delay = 0.0f;
        float attack = 0.1f;
        float hold = 0.0f;
        float decay = 0.3f;
        float sustain = 1.0f;
        float release = 0.2f;
    };

    struct Lfo
    {
        bool enabled = false;
        float delay = 0.0f;
        float attack = 0.1f;
        /** Bipolar: which way round the modulation goes, and how far. */
        float amount = 0.0f;
        float speed = 0.4f;
    };

    ChannelSettings();

    // Parameters -------------------------------------------------------------
    void setEnvelope(Target target, const Envelope& envelope) noexcept;
    Envelope getEnvelope(Target target) const noexcept;
    void setLfo(Target target, const Lfo& lfo) noexcept;
    Lfo getLfo(Target target) const noexcept;

    void setFilterEnabled(bool shouldBeEnabled) noexcept;
    bool isFilterEnabled() const noexcept;
    /** 0..1, exponential across the audible range rather than linear in hertz. */
    void setFilterCutoff(float normalised) noexcept;
    float getFilterCutoff() const noexcept;
    void setFilterResonance(float normalised) noexcept;
    float getFilterResonance() const noexcept;

    void setArpDirection(ArpDirection direction) noexcept;
    ArpDirection getArpDirection() const noexcept;
    /** How many octaves the pattern climbs before it starts over: 1..4. */
    void setArpRange(int octaves) noexcept;
    int getArpRange() const noexcept;
    /** 0..1 across a table of note lengths, from a sixteenth to a whole bar. */
    void setArpTime(float normalised) noexcept;
    float getArpTime() const noexcept;
    /** How much of each step the note actually sounds for. */
    void setArpGate(float normalised) noexcept;
    float getArpGate() const noexcept;

    /** True when any of this would change the sound, so the caller can skip the
        whole stage on a channel nobody has touched.
    */
    bool isActive() const noexcept;
    bool isArpActive() const noexcept;

    // Whole state, for projects and undo -------------------------------------
    juce::var toVar() const;
    void fromVar(const juce::var& value);
    /** Back to defaults: every envelope and LFO off, filter open, arp off. */
    void resetToDefaults() noexcept;

    // Audio thread -----------------------------------------------------------
    void prepare(double newSampleRate);
    /** Clears the running state - envelope stage, filter memory, held notes.
        For a transport stop, where a note left sounding would drone.
    */
    void reset() noexcept;

    /** Runs the arpeggiator over `midi`, and notes where the channel's gate
        opens and closes so the envelopes can follow it. Called before anything
        renders the MIDI, and always before processAudio for the same block.
    */
    void processMidi(juce::MidiBuffer& midi, int numSamples, double tempoBpm);

    /** Envelope, LFO and filter over the channel's own output. */
    void processAudio(juce::AudioBuffer<float>& buffer);

private:
    static constexpr int numTargets = static_cast<int>(Target::numTargets);
    /** One block cannot hold more gate changes than this without the envelope
        being retriggered faster than it can be heard.
    */
    static constexpr int maxGateEvents = 64;
    /** How often the modulation is recomputed. At 44.1 kHz that is ~0.7 ms -
        the same resolution the automation already runs at - and the gains are
        ramped across the chunk, so nothing steps.
    */
    static constexpr int modulationChunk = 32;

    /** What the envelopes and LFOs between them are asking for, as one block of
        numbers so the render loop reads the same whichever produced it.
    */
    struct Modulation
    {
        float gain = 1.0f;
        float pan = 0.0f;
        float cutoffOctaves = 0.0f;
        float resonance = 0.0f;
    };

    struct AtomicEnvelope
    {
        std::atomic<bool> enabled { false };
        std::atomic<float> delay { 0.0f };
        std::atomic<float> attack { 0.1f };
        std::atomic<float> hold { 0.0f };
        std::atomic<float> decay { 0.3f };
        std::atomic<float> sustain { 1.0f };
        std::atomic<float> release { 0.2f };
    };

    struct AtomicLfo
    {
        std::atomic<bool> enabled { false };
        std::atomic<float> delay { 0.0f };
        std::atomic<float> attack { 0.1f };
        std::atomic<float> amount { 0.0f };
        std::atomic<float> speed { 0.4f };
    };

    /** One envelope's place in its own curve, in seconds since the stage began. */
    struct EnvelopeState
    {
        enum class Stage { idle, delay, attack, hold, decay, sustain, release };

        Stage stage = Stage::idle;
        double elapsed = 0.0;
        float level = 0.0f;
        /** Where the release started from, so letting go mid-attack does not
            jump to full and then fall.
        */
        float releaseFrom = 0.0f;
    };

    struct LfoState
    {
        double elapsed = 0.0;
        double phase = 0.0;
    };

    /** Topology-preserving-transform state variable filter, one per channel.
        Stable when the cutoff is swept, which a modulated filter always is.
    */
    struct FilterState
    {
        double ic1 = 0.0;
        double ic2 = 0.0;
    };

    struct GateEvent
    {
        int sampleOffset = 0;
        bool opening = false;
    };

    /** A note the arpeggiator is holding down for us, and the one it is playing. */
    struct ArpState
    {
        std::array<bool, 128> held {};
        int heldCount = 0;
        int playingNote = -1;
        int playingChannel = 1;
        int playingVelocity = 100;
        /** Samples since the current step began. Free-running while notes are
            held so the pattern stays in time with the sequence.
        */
        double stepPosition = 0.0;
        int stepIndex = 0;
        bool noteIsSounding = false;
    };

    /** What the modulation asks for as things stand. */
    Modulation currentModulation() const noexcept;
    /** Moves every enabled envelope and LFO on by `seconds`. */
    void advanceModulation(double seconds) noexcept;
    void advanceEnvelope(EnvelopeState& state, const Envelope& shape, double seconds, bool gateOpen) const noexcept;
    float advanceLfo(LfoState& state, const Lfo& shape, double seconds) const noexcept;
    void pushGateEvent(int sampleOffset, bool opening) noexcept;
    /** Rewrites `midi` into the arpeggiated pattern. */
    void runArpeggiator(juce::MidiBuffer& midi, int numSamples, double tempoBpm);
    /** Watches note-ons and note-offs so the gate follows the channel. */
    void trackGate(const juce::MidiBuffer& midi) noexcept;
    int nextArpNote() noexcept;

    std::array<AtomicEnvelope, numTargets> envelopes;
    std::array<AtomicLfo, numTargets> lfos;

    std::atomic<bool> filterEnabled { false };
    std::atomic<float> filterCutoff { 1.0f };
    std::atomic<float> filterResonance { 0.0f };

    std::atomic<int> arpDirection { static_cast<int>(ArpDirection::off) };
    std::atomic<int> arpRange { 1 };
    std::atomic<float> arpTime { 0.25f };
    std::atomic<float> arpGate { 0.6f };

    std::atomic<double> sampleRate { 44100.0 };

    // Audio thread only from here down.
    std::array<EnvelopeState, numTargets> envelopeStates {};
    std::array<LfoState, numTargets> lfoStates {};
    /** Each LFO's last output, so the modulation can be read without advancing. */
    std::array<float, numTargets> lfoValues {};
    std::array<FilterState, 2> filterStates {};
    std::array<GateEvent, maxGateEvents> gateEvents {};
    int gateEventCount = 0;
    /** How many notes are down, so a second note does not retrigger the
        envelope and the first release does not cut the second note short.
    */
    int soundingNotes = 0;
    bool gateOpen = false;
    /** The gate as the render has reached it, which trails the block-level one
        above: the events say where in the block it changes.
    */
    bool renderGate = false;
    ArpState arp;
    juce::Random arpRandom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelSettings)
};

} // namespace djr
