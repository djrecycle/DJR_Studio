#pragma once

#include "MidiClip.h"

#include <juce_events/juce_events.h>
#include <functional>

namespace djr
{

/** Editing view onto whichever track's clip is currently selected.

    The model owns no notes of its own: the piano roll, the velocity lane and
    the step sequencer all edit the selected MidiTrack's clip through here, so
    the playlist and the audio engine see every change immediately.
*/
class PianoRollModel final : public juce::ChangeBroadcaster
{
public:
    /** Points the editors at a clip, or at nothing when a non-MIDI track is selected. */
    void setTargetClip(MidiClip* clip);
    MidiClip* getTargetClip() const noexcept;
    bool hasTargetClip() const noexcept;

    void addNote(int pitch, double startBeat, double lengthBeats, float velocity);
    /** Adds several notes as one undo step, e.g. every tone of a chord stamp. */
    void addNoteGroup(const juce::Array<MidiNote>& newNotes);
    /** Chops the given notes' combined time span into equal steps and lines
        them up one after another, lowest pitch first - a stacked chord
        turned into a run rather than played all at once. Permanent, unlike
        the live arpeggiator in ChannelSettings: this rewrites the notes
        themselves. A no-op below two notes: there is nothing to sequence.
    */
    void arpeggiateNotes(const juce::Array<int>& indices);
    void deleteNoteAt(int index);
    void dragNote(int index, double startBeat, int pitch);
    void setNoteVelocity(int index, float velocity);
    void setNoteLength(int index, double lengthBeats);
    void setNotes(const juce::Array<MidiNote>& notes);
    /** Called just before any change to the notes, so the host can record an
        undo point. Every editor - piano roll, velocity lane, step sequencer -
        goes through this model, so one hook here catches all of them.
    */
    std::function<void()> onBeforeEdit;

    /** Snap length in beats; zero means notes land exactly where you put them. */
    void setSnapBeats(double beats);
    /** Rounds `beats` to the snap grid, or returns it untouched when snap is off. */
    double snapToGrid(double beats) const noexcept;
    double getSnapBeats() const noexcept;

    juce::Array<MidiNote> getNotes() const;

    /** Announces an edit made straight on the clip, e.g. by the step sequencer. */
    void notifyClipChanged();

private:
    double snapBeats = 0.25;
    MidiClip* targetClip = nullptr;
};

} // namespace djr
