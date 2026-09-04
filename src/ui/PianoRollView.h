#pragma once

#include "app/SnapSetting.h"
#include "midi/Chord.h"
#include "midi/PianoRollModel.h"
#include "midi/Scale.h"
#include "audio/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include "ZoomScrollBar.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Piano roll grid with a drawn keyboard, bar/beat grid, velocity shaded notes
    and the transport playhead.
*/
class PianoRollView final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    /** What a click or drag on the grid does. The playlist's Slip has no
        meaning for a note, so this set is one shorter than the playlist's.
    */
    enum class Tool
    {
        select,     ///< marquee, move and resize notes
        draw,       ///< add notes, sweep to add a run of them
        erase,      ///< remove notes
        mute,       ///< silence a note without removing it
        slice,      ///< cut a note in two
        zoom,       ///< drag out a range to zoom into
        playback    ///< click to move the playhead and roll
    };

    PianoRollView(PianoRollModel& model, Transport& transport);
    ~PianoRollView() override;

    void setTool(Tool tool);
    Tool getTool() const noexcept;

    /** Brackets a drag: true on mouse down, false on mouse up, so a whole drag
        collapses into one undo step.
    */
    std::function<void(bool)> onEditGesture;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    /** Resolves the snap entry against this view's zoom and pushes the length
        into the model, which is what actually snaps the notes.
    */
    void setSnapUnit(SnapUnit unit);

    int getKeyboardWidth() const noexcept;
    double getPixelsPerBeat() const noexcept;
    double getScrollBeats() const noexcept;
    /** Bar length from the transport, so the velocity lane can line its bar
        markers up with the roll without needing the transport itself.
    */
    double getBeatsPerBar() const noexcept;
    /** Colour used for a note of the given velocity, shared with the velocity lane. */
    static juce::Colour velocityColour(float velocity);

    /** Keep the roll scrolled to the playhead while it plays, like the
        playlist's own follow toggle. On by default.
    */
    void setFollowPlayhead(bool shouldFollow);
    bool isFollowingPlayhead() const noexcept;

    /** The scale highlighted in the key rows; chromatic means no highlight. */
    void setScale(const Scale& newScale);
    const Scale& getScale() const noexcept;
    /** Fired with the user's pick from the scale badge's menu - the host owns
        where this is remembered, the roll only asks.
    */
    std::function<void(Scale)> onScaleChanged;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    /** Beats between grid lines, widened when they would crowd together. */
    double getGridStepBeats() const noexcept;
    /** Re-resolves Line snapping after a zoom change. */
    void applySnapToModel();
    int noteAtPosition(juce::Point<int> position) const;
    juce::Rectangle<int> noteToBounds(const MidiNote& note) const;
    double xToBeat(int x) const;
    int beatToX(double beat) const;
    int yToPitch(int y) const;
    int pitchToY(int pitch) const;

    PianoRollModel& model;
    Transport& transport;

    /** Applies the active tool to the note under the pointer. True if handled. */
    bool applyToolToNote(int noteIndex);
    /** Adds a note under the pointer if that spot is free; used by draw sweeps.
        With the chord stamp on, writes every interval of the active chord
        instead of the one pitch clicked.
    */
    bool drawNoteAt(juce::Point<int> position);
    /** Whether an existing note at `pitch` already covers `beat` - a chord
        tone lands away from the click, so it needs its own check rather than
        noteAtPosition's screen-space one.
    */
    bool hasNoteAt(int pitch, double beat) const;
    /** One higher than the highest chord group id already in use, so a new
        stamp never collides with notes loaded from a saved project.
    */
    int nextChordGroupId(const juce::Array<MidiNote>& notes) const;
    /** Flips the chord stamp on or off with whatever chord type was last
        picked, so writing a run of the same chord needs no trip back to
        the menu. A plain click on the badge does this; right click opens
        the full scale + chord picker.
    */
    void toggleChordStamp();
    /** The scale and the chord stamp share one badge and one menu, the way
        FL keeps its own key and chord helpers in a single tool.
    */
    void showHelperMenu();
    bool isNoteSelected(int noteIndex) const;
    void clearNoteSelection();
    /** Selects every note sharing `groupId`, so grabbing one tone of a
        chord stamp carries the whole chord with it without the user having
        to marquee-select it first.
    */
    void selectChordGroup(int groupId);
    /** Drops the chord-group tag from the selected notes, freeing them to be
        dragged or resized on their own again.
    */
    void ungroupSelectedNotes();
    void selectNotesInMarquee();
    /** Records where each selected note started, so a group drag or a group
        resize both stay rigid.
    */
    void beginGroupDrag();
    void moveSelectedNotes(double deltaBeats, int deltaPitch);
    /** Applies the same length change to every selected note, relative to
        the length each one had when the resize began.
    */
    void resizeSelectedNotes(double deltaBeats);
    void deleteSelectedNotes();
    /** Snapshots the selected notes into the clipboard, pitch and position as
        they are - paste puts them back untouched, ready to be dragged to
        another octave or, once the target track changes, another instrument.
    */
    void copySelectedNotes();
    /** Appends the clipboard notes to the current clip and selects just the
        new copies, so they can be dragged straight away without disturbing
        what was already there.
    */
    void pasteNotes();
    void shiftSelectedNotesByOctave(int direction);

    Tool activeTool = Tool::draw;
    int draggedNote = -1;
    bool resizingNote = false;
    /** Live rubber band for the select tool, and what it caught. */
    juce::Rectangle<int> marquee;
    bool marqueeActive = false;
    juce::Array<int> selectedNotes;
    /** Note starts captured when a group drag began, parallel to selectedNotes. */
    juce::Array<MidiNote> groupDragOrigins;
    /** Survives a track switch, so a block copied from one instrument can be
        pasted into another's pattern.
    */
    juce::Array<MidiNote> clipboardNotes;
    double dragGrabBeat = 0.0;
    /** How far into the note it was grabbed. Without this a note dragged by
        its middle jumps so its start lands under the pointer, which reads as
        the note running away from the mouse.
    */
    double dragGrabOffsetBeats = 0.0;
    int dragGrabPitch = 60;
    /** Live drag rectangle for the zoom tool. */
    juce::Rectangle<int> zoomDrag;
    /** Sweep state for the draw tool. */
    bool drawing = false;
    double lastDrawnBeat = -1.0;
    int lastDrawnPitch = -1;
    /** The chord stamp: off by default, so the draw tool writes single notes
        until it is turned on from its own badge.
    */
    bool chordModeEnabled = false;
    ChordType activeChordType = ChordType::major;
    /** Where the roll is and how much of it is showing - the same two bars the
        playlist has, so navigating one is navigating the other.
    */
    ZoomScrollBar horizontalBar { ZoomScrollBar::Orientation::horizontal };
    ZoomScrollBar verticalBar { ZoomScrollBar::Orientation::vertical };

    int keyHeight = 12;
    int keyboardWidth = 44;
    SnapUnit snapUnit = SnapUnit::step;
    double pixelsPerBeat = 14.0;
    double scrollBeats = 0.0;
    bool followPlayhead = true;
    Scale scale;
    int topPitch = 84;
    /** A drag that started on the ruler keeps moving the playhead, even once
        the pointer has wandered down into the notes.
    */
    bool scrubbingRuler = false;

    /** What the drawing and the mouse both work inside: everything but the
        strips the two bars sit in.
    */
    juce::Rectangle<int> getContentBounds() const;
    /** The bar numbers along the top, short of the helper badge's own corner.
        Clicking it moves the playhead, which is what everyone who has used a
        piano roll before will try first.
    */
    juce::Rectangle<int> getRulerBounds() const;
    /** The corner above the keyboard and left of the ruler - dead space
        otherwise, so it is where the scale/chord badge lives.
    */
    juce::Rectangle<int> getHelperBadgeBounds() const;
    /** The pointer for wherever it is now: the tool it is holding, or the
        resize arrows when it is over the end of a note.

        A cursor that never changes makes the reader guess which of the two a
        click will do, and the answer is a pixel wide.
    */
    void refreshCursor(juce::Point<int> position);
    /** Beats the horizontal bar treats as the whole roll. */
    double getTimelineBeats() const;
    void refreshScrollBars();
    void applyHorizontalRange(double start, double size);
    void applyVerticalRange(double start, double size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollView)
};

} // namespace djr
