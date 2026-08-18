#pragma once

#include "UiControls.h"

#include "app/SnapSetting.h"
#include "audio/AutomationLane.h"
#include "audio/Mixer.h"
#include "midi/MidiNote.h"
#include "audio/Transport.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** Playlist / arrangement grid: track headers with mute+solo, a bar ruler, the
    clip lanes and the playhead.
*/
class ArrangementView final : public juce::Component,
                              private juce::Timer,
                              private juce::Button::Listener,
                              private juce::Slider::Listener
{
public:
    /** FL style playlist tools: what a click or drag on the grid does. */
    enum class Tool
    {
        select,     ///< move and trim clips
        paint,      ///< place clips by clicking or dragging
        erase,      ///< remove clips
        mute,       ///< silence a clip without removing it
        slip,       ///< slide the content inside a clip, leaving it in place
        slice,      ///< cut a clip in two
        zoom,       ///< drag out a range to zoom into
        playback    ///< click to move the playhead and roll
    };

    ArrangementView(Mixer& mixer, Transport& transport);
    ~ArrangementView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    void setTool(Tool tool);
    Tool getTool() const noexcept;
    /** Snap can be switched off entirely with the magnet, like FL. */
    void setSnapEnabled(bool shouldSnap);
    bool isSnapEnabled() const noexcept;

    /** Which snap entry is chosen. Line and Cell are resolved here, against this
        view's own zoom, because they mean different things in each editor.
    */
    void setSnapUnit(SnapUnit unit);
    /** Pattern that clicking an empty song-mode lane will place. */
    void setActivePattern(int patternIndex) noexcept;
    /** How long the active pattern loops for, so the clip shows its real span. */
    void setPatternLengthBeats(double beats) noexcept;
    /** Fired after a clip is moved, trimmed or deleted. */
    void setClipEditedCallback(std::function<void()> callback);
    /** Fired when a MIDI clip is double clicked: its track and its pattern, so
        the host can bring up the piano roll on exactly that pattern.
    */
    void setClipOpenRequestCallback(std::function<void(int, int)> callback);
    /** Undo hooks. `push` records a restore point just before a change; `gesture`
        brackets a drag or a paint sweep so it collapses into one undo step.
    */
    void setUndoHooks(std::function<void(const juce::String&)> push,
                      std::function<void(bool)> gesture);
    /** Supplies a pattern's display name; the playlist has no session state of
        its own, so the host hands it the naming.
    */
    void setPatternNameProvider(std::function<juce::String(int)> provider);
    /** Supplies a pattern's loop length in beats. "Restore full length" needs the
        length of the pattern a clip points at, which may not be the active one.
    */
    void setPatternLengthProvider(std::function<double(int)> provider);
    /** Fired when the user asks to rename a pattern from a clip's menu. */
    void setPatternRenameCallback(std::function<void(int)> callback);
    /** Fired when a track's name is double clicked, or asked for from its menu. */
    void setTrackRenameCallback(std::function<void(int)> callback);
    /** Freeze or unfreeze a track; the host decides which, since it knows. */
    void setTrackFreezeCallback(std::function<void(int)> callback);
    /** Render a track onto a new audio track. */
    void setTrackBounceCallback(std::function<void(int)> callback);

    /** Lane height for one track, so it can be saved with the project. Zero or
        anything out of range restores the default height.
    */
    int getRowHeight(int trackIndex) const;
    void setRowHeight(int trackIndex, int height);

    void setSelectedTrack(int trackIndex);
    int getSelectedTrack() const noexcept;
    void setTrackSelectedCallback(std::function<void(int)> callback);
    /** Fired after a track is added or removed so the mixer can rebuild. */
    void setTrackListChangedCallback(std::function<void()> callback);
    /** Re-reads the mixer's tracks. Public because the track list also changes
        from outside this view: opening a project replaces the whole of it.
    */
    void notifyTrackListChanged();
    void setFollowPlayhead(bool shouldFollow);
    bool isFollowingPlayhead() const noexcept;

private:
    /** One playlist row. A track owns its clip row plus one row per automation
        lane, which is why row indices and track indices are no longer the same
        thing: everything that positions a lane goes through the row list.
    */
    struct Row
    {
        int trackIndex = 0;
        /** -1 for the track's own clips, otherwise its automation lane index. */
        int automationLane = -1;
        /** Resolved once per rebuild. Working these out on demand meant every
            geometry question walked the rows and took the track's automation
            lock on each step - hundreds of times a frame, against the same lock
            the audio thread try-locks every block.
        */
        int height = 0;
        int top = 0;
        /** The lane's curve as of this rebuild, so drawing and hit testing never
            touch the lane itself.
        */
        std::vector<AutomationPoint> points;
        bool laneEnabled = true;
        /** Copied too, so painting a lane needs no lane pointer and therefore
            takes no lock at all.
        */
        AutomationTarget target;
    };

    /** What a drag inside an automation lane is doing. */
    struct AutomationDrag
    {
        enum class Mode
        {
            none,
            point,  ///< moving a breakpoint
            curve   ///< bending the segment that ends at `pointIndex`
        };

        Mode mode = Mode::none;
        int rowIndex = -1;
        int trackIndex = -1;
        int laneIndex = -1;
        int pointIndex = -1;
        double grabCurve = 0.0;
        int grabY = 0;
        /** A falling segment bends the opposite way, so the drag is flipped to
            keep "drag down" meaning "curve dips down" either way.
        */
        bool falling = false;
    };

    /** One note drawn inside a clip, in clip-relative beats. */
    struct ClipNote
    {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        int pitch = 60;
    };

    struct Clip
    {
        double startBeat = 0.0;
        double lengthBeats = 0.0;
        juce::String label;
        bool midi = true;
        int index = 0;
        /** Waveform envelope for audio clips; empty for MIDI. */
        const std::vector<float>* peaks = nullptr;
        double trimStartFraction = 0.0;
        double trimEndFraction = 1.0;
        bool warped = false;
        bool muted = false;
        /** Miniature of the pattern's notes, for MIDI clips. */
        std::vector<ClipNote> notes;
        int lowestPitch = 60;
        int highestPitch = 72;
    };

    /** What a drag on a clip is doing. */
    enum class ClipDragMode
    {
        none,
        move,
        trimStart,
        trimEnd
    };

    struct ClipDrag
    {
        ClipDragMode mode = ClipDragMode::none;
        int trackIndex = -1;
        int clipIndex = -1;
        double grabBeat = 0.0;
        double originalStart = 0.0;
        double originalEnd = 0.0;
    };

    /** A clip the marquee picked up. Both kinds of clip are real objects now, so
        anything the playlist draws can be selected.
    */
    struct ClipRef
    {
        int trackIndex = -1;
        int clipIndex = -1;
        /** Where the clip started when a group drag began. */
        double dragOriginBeat = 0.0;
    };

    void timerCallback() override;
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;

    juce::Rectangle<int> getGridArea() const;
    juce::Rectangle<int> getRowBounds(int trackIndex) const;
    int getRowTop(int trackIndex) const;

    // Rows -------------------------------------------------------------------
    /** Re-reads the mixer's tracks and their lanes into `rows`. Cheap, and
        called from every entry point so a lane added mid-gesture cannot leave
        the geometry pointing at something that is no longer there.
    */
    void rebuildRows();
    /** Re-runs only the vertical positions, for when scrolling moves the rows
        without changing what they are.
    */
    void refreshRowTops();
    int getRowCount() const noexcept;
    int getRowIndexForTrack(int trackIndex) const;
    int getRowHeightAt(int rowIndex) const;
    void setRowHeightAt(int rowIndex, int height);
    int getRowTopAt(int rowIndex) const;
    juce::Rectangle<int> getRowBoundsAt(int rowIndex) const;
    /** Row under `position`, or -1. */
    int rowAt(juce::Point<int> position) const;
    /** The live lane behind a row. Takes the track's lock, so it is only for
        the handful of places that actually mutate a curve - drawing and hit
        testing work from the snapshot in the row instead.
    */
    AutomationLane* getLane(int trackIndex, int laneIndex) const;

    /** Row whose bottom edge is under `position` in the headers, or -1. */
    int hitTestRowResize(juce::Point<int> position) const;
    juce::Rectangle<int> getMuteBounds(int trackIndex) const;
    juce::Rectangle<int> getSoloBounds(int trackIndex) const;
    double xToBeat(int x) const;
    int beatToX(double beat) const;
    int getContentHeight() const;
    void clampScroll();
    void zoomToFit();
    void showAddTrackMenu();
    void showTrackContextMenu(int trackIndex);
    void notifyClipEdited();
    /** Finds the clip under `position` and what a drag there would do. */
    ClipDragMode hitTestClip(juce::Point<int> position, int& trackIndexOut, int& clipIndexOut) const;
    juce::Rectangle<int> getClipBounds(int trackIndex, const Clip& clip) const;
    void showClipContextMenu(int trackIndex, int clipIndex);
    void showPlacementContextMenu(int trackIndex, int placementIndex);
    double snapBeat(double beat) const noexcept;
    /** Beats between grid lines: the snap length, widened when it would draw
        lines closer together than the eye can use.
    */
    double getGridStepBeats() const noexcept;
    /** What edits actually snap to. Zero means no snapping at all. */
    double getEffectiveSnapBeats() const noexcept;
    std::vector<Clip> getClipsForTrack(int trackIndex) const;
    /** Collects the notes a clip should draw, windowed to what it plays. */
    static void fillClipNotes(Clip& clip,
                              const juce::Array<MidiNote>& notes,
                              double windowStartBeat,
                              double windowEndBeat);
    static juce::String describeKind(const Track& track, int trackIndex);
    /** A pattern's name from the host, or the "PAT n" default. */
    juce::String patternLabel(int patternIndex) const;
    /** Loop length of one pattern, falling back to the active pattern's. */
    double patternLengthFor(int patternIndex) const;

    Mixer& mixer;
    Transport& transport;

    struct ToolButton
    {
        Tool tool;
        Icon icon;
        const char* tooltip;
    };

    void buildToolButtons();
    void refreshToolButtons();
    /** Applies the active tool to a clip that was clicked. Returns true if handled. */
    bool applyToolToClip(int trackIndex, int clipIndex, double beat, ClipDragMode mode);
    /** Drops the active pattern on the MIDI lane under `position`, skipping spots
        that are already taken. Returns true when a clip was placed.
    */
    bool paintPatternAt(juce::Point<int> position);
    /** Paints every snap position the pointer swept over since the last drag
        event, not just where it happens to be now.
    */
    void paintSweep(juce::Point<int> position);

    // Multi clip selection ---------------------------------------------------
    bool isClipSelected(int trackIndex, int clipIndex) const;
    void clearClipSelection();
    void toggleClipSelection(int trackIndex, int clipIndex);
    /** Adds every clip the rubber band touched to the selection. */
    void selectClipsInMarquee();
    /** Records where each selected clip starts, so a group drag stays rigid. */
    void beginGroupDrag();
    /** Slides the whole selection, never past the front of the timeline. */
    void moveSelectedClips(double deltaBeats);
    void deleteSelectedClips();
    bool getClipStartBeat(int trackIndex, int clipIndex, double& startBeatOut) const;
    void setClipStartBeat(int trackIndex, int clipIndex, double startBeat);

    // Automation lanes -------------------------------------------------------
    void drawAutomationRow(juce::Graphics& g, int rowIndex, const Row& row);
    /** The drawable band of an automation row, inset so a point sitting at 0 or
        at 1 is still fully inside the lane.
    */
    juce::Rectangle<int> getCurveArea(int rowIndex) const;
    double valueFromY(int rowIndex, int y) const;
    int yFromValue(int rowIndex, double value) const;
    /** What is under `position` in an automation lane. A hit with mode `none`
        but a valid track means an empty spot on a lane, which is where a new
        point goes.
    */
    AutomationDrag hitTestAutomation(juce::Point<int> position) const;
    /** Returns false when the active tool has nothing to do with a curve, so
        the click falls through to the timeline instead.
    */
    bool handleAutomationMouseDown(int rowIndex, juce::Point<int> position, const juce::ModifierKeys& mods);
    /** Menu for a lane; `pointIndex` >= 0 adds the entries for that point. */
    void showAutomationMenu(int trackIndex, int laneIndex, int pointIndex);
    /** The "add automation" submenu: volume, pan, and every plugin parameter. */
    void fillAutomationTargetMenu(juce::PopupMenu& menu, int trackIndex) const;
    /** Turns an id from that menu back into a target, and creates the lane. */
    void addAutomationTarget(int trackIndex, int menuId);

    /** Works out where the slice tool would cut, and whether the cut is legal.
        Clears the preview when the pointer is not over a sliceable clip.
    */
    void updateSlicePreview(juce::Point<int> position);
    void clearSlicePreview();
    /** Records an undo point named `actionName` before changing anything. */
    void pushUndo(const juce::String& actionName);

    juce::OwnedArray<IconChipButton> toolButtons;
    IconChipButton snapButton { TRANS("Snap to grid"), Icon::magnet };
    IconChipButton addTrackButton { TRANS("Add track"), Icon::plus };
    IconChipButton followButton { "Ikuti playhead", Icon::chevronRight };
    IconChipButton zoomFitButton { "Zoom to fit", Icon::grid };
    juce::Slider zoomSlider;

    std::function<void(int)> trackSelectedCallback;
    std::function<void()> trackListChangedCallback;
    std::function<void()> clipEditedCallback;
    std::function<void(int, int)> clipOpenRequestCallback;
    std::function<void(const juce::String&)> pushUndoCallback;
    std::function<void(bool)> undoGestureCallback;
    std::function<juce::String(int)> patternNameProvider;
    std::function<double(int)> patternLengthProvider;
    std::function<void(int)> patternRenameCallback;
    std::function<void(int)> trackRenameCallback;
    std::function<void(int)> trackFreezeCallback;
    std::function<void(int)> trackBounceCallback;
    ClipDrag clipDrag;
    Tool activeTool = Tool::select;
    /** A paint drag in progress: the pointer keeps laying clips as it sweeps. */
    bool painting = false;
    /** Where the last paint step landed, so a fast sweep can be filled in. */
    juce::Point<int> lastPaintPosition;
    bool snapEnabled = true;
    /** Live drag rectangle for the zoom tool. */
    juce::Rectangle<int> zoomDrag;
    /** Live rubber band for the select tool, and what it has caught. */
    juce::Rectangle<int> marquee;
    bool marqueeActive = false;
    std::vector<ClipRef> selectedClips;
    /** Where the slice tool would cut. Track is -1 when there is nothing to cut. */
    int sliceTrack = -1;
    int sliceClipIndex = -1;
    double sliceBeat = 0.0;
    SnapUnit snapUnit = SnapUnit::step;
    int activePattern = 0;
    double patternLengthBeats = 4.0;
    double pixelsPerBar = 56.0;
    double scrollBeats = 0.0;
    int scrollY = 0;
    /** Per track lane heights. Tracks past the end use defaultRowHeight, so the
        list only has to grow when a lane is actually resized.
    */
    std::vector<int> rowHeights;
    int defaultRowHeight = 30;
    /** Automation lanes start taller than a clip lane: a curve needs vertical
        room to be worth drawing at all.
    */
    int defaultAutomationRowHeight = 52;
    /** Track rows and automation rows in the order they are drawn. */
    std::vector<Row> rows;
    AutomationDrag automationDrag;
    /** The row whose edge is being dragged, or -1. */
    int resizingRow = -1;
    int resizeStartHeight = 0;
    int resizeGrabY = 0;
    int headerWidth = 122;
    int rulerHeight = 17;
    int selectedTrack = 0;
    bool followPlayhead = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArrangementView)
};

} // namespace djr
