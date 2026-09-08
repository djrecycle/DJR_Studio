#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace djr
{

/** A built-in multi-pad sampler: a fixed number of independent one-shot
    slots, each triggered by its own MIDI note - a kit built from whatever
    WAV/AIFF/FLAC/OGG files the user loads into it, not a fixed factory kit.
    General-purpose rather than drum-specific - a kick on one pad, a vocal
    chop on the next, whatever the track actually needs.

    Each pad plays back at its own file's native sample rate and channel
    count, resampled to the host's rate by plain linear interpolation - good
    enough for a one-shot hit, and small enough to own outright rather than
    reach for a general pitch-shifting library for it.

    A pad normally only answers its own exact note, unlooped, at the
    pitch it was recorded at - a drum machine's factory kit, where playing
    a different note should never re-pitch the kick. A pad can opt into
    "keyboard mode" instead (Pad::keyboardMode): it then catches every note
    nothing else claims and re-pitches its sample by the distance from its
    own note - a single bass or lead sample spread across the whole piano
    roll rather than answering to one key alone. An exact-note match always
    takes priority over a keyboard-mode pad, so a kit and a keyboard-style
    pad can share one instrument without the kit's own notes getting stolen.
*/
class MidiSamplerProcessor final : public juce::AudioPluginInstance
{
public:
    static const char* const identifier;
    static constexpr int numPads = 16;
    /** Pad 0 sits on MIDI note 36 (C1 in most GM maps, the usual kick drum
        note - a familiar starting point for a kit, not a requirement) and
        each pad after it on the next note up. Every pad's note can be moved.
    */
    static constexpr int firstPadNote = 36;
    static constexpr int maxVoices = 32;

    static juce::PluginDescription getDescription();
    /** Whether a description names this plugin - asked before a scanned
        plugin would be created, so the built-in never goes near a format
        manager.
    */
    static bool matches(const juce::PluginDescription& description);

    MidiSamplerProcessor();

    struct Pad
    {
        juce::String name { "Empty" };
        juce::File sourceFile;
        juce::AudioBuffer<float> sample;
        double sampleRate = 44100.0;
        float gain = 1.0f;
        int midiNote = 0;
        /** When true, this pad also answers any note no other pad claims
            exactly, pitched up or down from `midiNote` (its root note in
            this mode) by the played note's distance from it - a keyboard
            spread from one sample rather than one fixed hit.
        */
        bool keyboardMode = false;

        bool hasSample() const noexcept { return sample.getNumSamples() > 0; }
    };

    const Pad& getPad(int index) const noexcept;
    /** Reads a file in and assigns it to `padIndex`, replacing whatever was
        there before. Returns why it failed, or an empty string on success.
    */
    juce::String loadSampleIntoPad(int padIndex, const juce::File& file);
    /** Takes an already-decoded buffer straight into a pad, bypassing file
        I/O - what a procedurally generated starter sound uses, and
        available to anything else that already has audio in memory rather
        than a file on disk to read. `sourceFile` stays empty, so this pad
        is not written into the project's saved state as a file reference -
        there is no file to point back to.
    */
    void loadGeneratedSampleIntoPad(int padIndex, juce::AudioBuffer<float> audio,
                                    double sourceSampleRate, const juce::String& name);
    void clearPad(int padIndex) noexcept;
    void setPadGain(int padIndex, float gain) noexcept;
    /** Moves the pad to a different trigger note. Two pads may not share a
        note - the one already there is bumped to whatever note `padIndex`
        held, so a drag onto an occupied note swaps the two rather than
        silently stealing it.
    */
    void setPadMidiNote(int padIndex, int midiNote) noexcept;
    /** Turns keyboard mode on or off for a pad - see the class comment. */
    void setPadKeyboardMode(int padIndex, bool enabled) noexcept;
    /** The pad whose own note exactly matches, or -1. Keyboard-mode pads are
        not considered here - they are the fallback processBlock() reaches
        for once no exact match exists, not a competing exact match.
    */
    int findPadForNote(int midiNote) const noexcept;
    /** Triggers a pad the way a MIDI note would, from the UI's own preview
        button - audio-thread safe the same way processBlock's own note
        handling is (a lock-free flag array, not a direct write into what
        the audio thread reads).
    */
    void previewPad(int padIndex) noexcept;

    /** Where a save writes the audio behind a pad that has none on disk yet
        (a starter sound generated in code, not loaded from a file) - mirrors
        AudioEditorProcessor::setWorkingFolder, so the host can point both at
        the same project Samples folder.
    */
    void setWorkingFolder(const juce::File& folder);

    void fillInPluginDescription(juce::PluginDescription& description) const override;

    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override;
    /** Only the float path is ours; the double one stays the base class's,
        which would otherwise be hidden rather than inherited.
    */
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    struct Voice
    {
        int padIndex = -1;
        double readPosition = 0.0;
        bool active = false;
        /** 1.0 for a normal exact-note hit; set away from 1.0 only when a
            keyboard-mode pad is answering a note other than its own root.
        */
        float pitchRatio = 1.0f;
    };

    void triggerPad(int padIndex, int playedNote) noexcept;
    /** The first keyboard-mode pad with a sample loaded, or -1 - what a note
        with no exact pad match falls back to.
    */
    int findKeyboardModePad() const noexcept;
    /** The project's Samples folder when the host has said where that is,
        falling back to the music folder - same fallback as
        AudioEditorProcessor::getWorkingFolder, for the same reason.
    */
    juce::File getWorkingFolder() const;

    std::array<Pad, numPads> pads;
    std::array<Voice, maxVoices> voices;
    int nextVoiceToSteal = 0;
    /** Notes queued by previewPad(), read and cleared at the top of the next
        processBlock() - the UI's own "audition this pad" button is not on
        the audio thread, and processBlock is the only place voices are
        started from.
    */
    std::array<std::atomic<bool>, numPads> pendingPreviews;
    juce::AudioFormatManager audioFormats;
    double engineSampleRate = 44100.0;
    juce::File workingFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiSamplerProcessor)
};

} // namespace djr
