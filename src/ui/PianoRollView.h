#pragma once

#include "app/SnapSetting.h"
#include "midi/PianoRollModel.h"
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
    /** Adds a note under the pointer if that spot is free; used by draw sweeps. */
    bool drawNoteAt(juce::Point<int> position);
    bool isNoteSelected(int noteIndex) const;
    void clearNoteSelection();
    void selectNotesInMarquee();
    /** Records where each selected note started, so a group drag stays rigid. */
    void beginGroupDrag();
    void moveSelectedNotes(double deltaBeats, int deltaPitch);
    void deleteSelectedNotes();

    Tool activeTool = Tool::draw;
    int draggedNote = -1;
    bool resizingNote = false;
    /** Live rubber band for the select tool, and what it caught. */
    juce::Rectangle<int> marquee;
    bool marqueeActive = false;
    juce::Array<int> selectedNotes;
    /** Note starts captured when a group drag began, parallel to selectedNotes. */
    juce::Array<MidiNote> groupDragOrigins;
    double dragGrabBeat = 0.0;
    int dragGrabPitch = 60;
    /** Live drag rectangle for the zoom tool. */
    juce::Rectangle<int> zoomDrag;
    /** Sweep state for the draw tool. */
    bool drawing = false;
    double lastDrawnBeat = -1.0;
    int lastDrawnPitch = -1;
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
    int topPitch = 84;

    /** What the drawing and the mouse both work inside: everything but the
        strips the two bars sit in.
    */
    juce::Rectangle<int> getContentBounds() const;
    /** Beats the horizontal bar treats as the whole roll. */
    double getTimelineBeats() const;
    void refreshScrollBars();
    void applyHorizontalRange(double start, double size);
    void applyVerticalRange(double start, double size);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollView)
};

} // namespace djr
