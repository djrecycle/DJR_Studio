#pragma once

#include "PatternPlacement.h"
#include "SimpleSynth.h"
#include "Track.h"
#include "midi/MidiClip.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace djr
{

/** A MIDI track. It holds one clip per pattern; which one you hear and edit
    depends on the active pattern in pattern mode, and on the placements laid
    out along the timeline in song mode.
*/
class MidiTrack final : public Track
{
public:
    static constexpr int maxPatterns = 16;

    explicit MidiTrack(juce::String trackName);

    /** The clip for the active pattern. */
    MidiClip& getClip() noexcept;
    const MidiClip& getClip() const noexcept;
    MidiClip& getClip(int patternIndex) noexcept;
    const MidiClip& getClip(int patternIndex) const noexcept;
    void setClipNotes(const juce::Array<MidiNote>& notes);

    void setActivePattern(int patternIndex) noexcept;
    int getActivePattern() const noexcept;
    /** True when the pattern holds at least one note. */
    bool patternHasContent(int patternIndex) const;

    // Song timeline ----------------------------------------------------------
    void addPlacement(PatternPlacement placement);
    bool removePlacementAt(int index);
    void clearPlacements();
    std::vector<PatternPlacement> getPlacements() const;
    /** Replaces the whole timeline in one go, for undo. */
    void setPlacements(const std::vector<PatternPlacement>& newPlacements);
    /** Index of the placement covering `beat`, or -1. */
    int findPlacementAt(double beat) const;
    /** Free beats from `startBeat` up to the next placement, or 0 when that beat
        is already inside one. Painting uses this to fill a gap exactly: a clip
        wider than the gap gets cut down instead of refused.
    */
    double getFreeSpanFrom(double startBeat) const;
    PatternPlacement getPlacement(int index) const;
    bool updatePlacement(int index, const PatternPlacement& placement);

    /** Whether the preview synth answers with drums or with a tonal voice.
        Only affects the built-in preview; a loaded instrument decides for itself.
    */
    void setPreviewDrumKit(bool shouldUseDrums);
    bool isPreviewDrumKit() const noexcept;

    /** The preview instrument itself, so the channel window can shape it and a
        project can save what it was shaped into.
    */
    SimpleSynth& getPreviewSynth() noexcept;
    const SimpleSynth& getPreviewSynth() const noexcept;

    void prepare(double sampleRate, int blockSize) override;

protected:
    void renderAudio(juce::AudioBuffer<float>& buffer,
                     juce::MidiBuffer& midi,
                     const TrackPlaybackContext& context) override;

private:
    static constexpr int maxPlacements = 256;

    void renderPatternMode(juce::MidiBuffer& midi, const TrackPlaybackContext& context, int numSamples);
    void renderSongMode(juce::MidiBuffer& midi, const TrackPlaybackContext& context, int numSamples);
    /** Emits note on/off for one clip.

        `localStart/EndBeat` is the block in clip time; `windowStart/EndBeat`
        bounds the slice of the clip a placement actually plays.
    */
    void emitNotes(juce::MidiBuffer& midi,
                   const juce::Array<MidiNote>& notes,
                   double localStartBeat,
                   double localEndBeat,
                   double windowStartBeat,
                   double windowEndBeat,
                   int numSamples);
    /** Emits a note-off for everything this track is still holding down, and
        forgets it. Whenever the playhead stops following the notes it started -
        a loop wrapping, a seek backwards - the note-off those notes were waiting
        for is behind us and will never be written, so it has to be sent here.
    */
    void releaseHeldNotes(juce::MidiBuffer& midi);
    void sendAllNotesOff(juce::MidiBuffer& midi);
    void resetTransportState();

    // Clips live in unique_ptrs so the array stays movable-free and stable.
    std::array<std::unique_ptr<MidiClip>, maxPatterns> patternClips;
    std::atomic<int> activePattern { 0 };

    mutable juce::SpinLock placementLock;
    std::vector<PatternPlacement> placements;

    SimpleSynth previewSynth;
    std::array<bool, 128> activeNotes {};
    double lastBlockStartBeat = 0.0;
    bool transportPrepared = false;
    bool wasPlaying = false;
};

} // namespace djr
