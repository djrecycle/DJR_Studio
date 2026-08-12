#pragma once

#include "audio/AudioClip.h"
#include "audio/Mixer.h"
#include "audio/PatternPlacement.h"
#include "midi/MidiNote.h"

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

namespace djr
{

/** Undo and redo for everything the playlist and the note editors change.

    This keeps whole snapshots rather than one hand written command per
    operation. A snapshot is cheap - a placement is five doubles, audio clips
    share their samples through duplicate(), notes are plain structs - and it
    cannot silently miss an edit, which is the usual way a command per operation
    scheme rots as new tools are added.

    Snapshots are taken *before* an edit and at the start of a gesture, so a
    drag or a paint sweep collapses into a single undo step.
*/
class EditHistory
{
public:
    explicit EditHistory(Mixer& mixerToWatch);

    /** Records the state as it is now, tagged with what is about to happen.
        Call this immediately before mutating anything.
    */
    void pushSnapshot(const juce::String& actionName);
    /** Drops the newest snapshot again, for a gesture that changed nothing. */
    void abandonSnapshot();

    /** Opens a gesture: further pushes are ignored until endGesture, so a drag
        or a paint sweep leaves exactly one undo step. Views call this on mouse
        down and endGesture on mouse up; forgetting only costs extra steps, it
        never loses an edit.
    */
    void beginGesture(const juce::String& actionName);
    void endGesture();

    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    /** Name of the edit undo would reverse, for the menu item. */
    juce::String getUndoName() const;
    juce::String getRedoName() const;

    bool undo();
    bool redo();
    void clear();

    /** Fired after undo or redo, so the UI can re-read the model. */
    std::function<void()> onChanged;

    /** How far back you can go before the oldest step is dropped. */
    static constexpr int maxDepth = 64;

private:
    struct TrackState
    {
        std::vector<PatternPlacement> placements;
        std::vector<std::unique_ptr<AudioClip>> audioClips;
        std::vector<juce::Array<MidiNote>> patterns;
    };

    struct Snapshot
    {
        juce::String name;
        std::vector<TrackState> tracks;
    };

    Snapshot capture(const juce::String& actionName) const;
    void restore(const Snapshot& snapshot);

    Mixer& mixer;
    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
    bool gestureOpen = false;
    bool gestureCaptured = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditHistory)
};

} // namespace djr
