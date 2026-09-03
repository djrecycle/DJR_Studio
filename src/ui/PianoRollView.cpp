#include "PianoRollView.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace djr
{

namespace
{
    constexpr int noteHeightInset = 1;
    constexpr int resizeHandleWidth = 5;

    /** How much of a note's right edge resizes rather than moves.

        A fixed five pixels is a third of a sixteenth note zoomed out, so a
        grab meant to move it resized it instead. Never more than a third of
        the note, so the middle is always the middle.
    */
    int resizeZoneFor(juce::Rectangle<int> noteBounds)
    {
        return juce::jlimit(2, resizeHandleWidth, noteBounds.getWidth() / 3);
    }
    /** The bar-number strip along the top, the same height as the playlist's so
        the two read as one timeline seen twice.
    */
    constexpr int rulerHeight = 18;
    /** The chord badge's own corner, pinned to the ruler's right end. */
    constexpr int chordBadgeWidth = 56;
}

PianoRollView::PianoRollView(PianoRollModel& modelToUse, Transport& transportToUse)
    : model(modelToUse), transport(transportToUse)
{
    setWantsKeyboardFocus(true);
    model.addChangeListener(this);

    horizontalBar.onRangeChanged = [this] (double start, double size) { applyHorizontalRange(start, size); };
    verticalBar.onRangeChanged = [this] (double start, double size) { applyVerticalRange(start, size); };
    addAndMakeVisible(horizontalBar);
    addAndMakeVisible(verticalBar);

    startTimerHz(30);
}

juce::Rectangle<int> PianoRollView::getRulerBounds() const
{
    return getContentBounds().withHeight(rulerHeight).withTrimmedLeft(keyboardWidth)
                             .withTrimmedRight(chordBadgeWidth);
}

juce::Rectangle<int> PianoRollView::getChordBadgeBounds() const
{
    const auto bounds = getContentBounds().withHeight(rulerHeight);
    return juce::Rectangle<int>(bounds.getRight() - chordBadgeWidth, bounds.getY(),
                                chordBadgeWidth, rulerHeight);
}

juce::Rectangle<int> PianoRollView::getContentBounds() const
{
    return getLocalBounds()
        .withTrimmedRight(ZoomScrollBar::thickness)
        .withTrimmedBottom(ZoomScrollBar::thickness);
}

void PianoRollView::resized()
{
    auto bottomStrip = getLocalBounds().removeFromBottom(ZoomScrollBar::thickness);
    auto rightStrip = getLocalBounds().removeFromRight(ZoomScrollBar::thickness);

    horizontalBar.setBounds(bottomStrip.withTrimmedLeft(keyboardWidth)
                                       .withTrimmedRight(ZoomScrollBar::thickness));
    verticalBar.setBounds(rightStrip.withTrimmedTop(rulerHeight)
                                    .withTrimmedBottom(ZoomScrollBar::thickness));
    refreshScrollBars();
}

double PianoRollView::getTimelineBeats() const
{
    // The pattern's own length, with room past it: a note can be written after
    // the last one there is.
    auto lastBeat = 16.0;

    for (const auto& note : model.getNotes())
        lastBeat = juce::jmax(lastBeat, note.startBeat + note.lengthBeats);

    return lastBeat * 1.25;
}

void PianoRollView::refreshScrollBars()
{
    const auto grid = juce::jmax(1, getContentBounds().getWidth() - keyboardWidth);
    const auto timeline = juce::jmax(1.0, getTimelineBeats());

    horizontalBar.setMinimumSize(juce::jlimit(0.0001, 1.0, 1.0 / timeline));
    horizontalBar.setRange(scrollBeats / timeline, (grid / pixelsPerBeat) / timeline);

    // Pitch runs the other way round: the top of the bar is the top of the
    // keyboard, which is the highest note.
    const auto visibleKeys = juce::jmax(1, (getContentBounds().getHeight() - rulerHeight)
                                              / juce::jmax(1, keyHeight));
    verticalBar.setRange((127.0 - topPitch) / 128.0, visibleKeys / 128.0);
}

void PianoRollView::applyHorizontalRange(double start, double size)
{
    const auto grid = juce::jmax(1, getContentBounds().getWidth() - keyboardWidth);
    const auto timeline = juce::jmax(1.0, getTimelineBeats());

    pixelsPerBeat = juce::jlimit(6.0, 96.0, grid / juce::jmax(0.25, size * timeline));
    scrollBeats = juce::jmax(0.0, start * timeline);
    refreshScrollBars();
    repaint();
}

void PianoRollView::applyVerticalRange(double start, double size)
{
    const auto height = juce::jmax(1, getContentBounds().getHeight() - rulerHeight);
    const auto wantedKeys = juce::jlimit(4.0, 128.0, size * 128.0);

    keyHeight = juce::jlimit(4, 40, juce::roundToInt(height / wantedKeys));
    topPitch = juce::jlimit(11, 127, 127 - juce::roundToInt(start * 128.0));
    refreshScrollBars();
    repaint();
}

PianoRollView::~PianoRollView()
{
    model.removeChangeListener(this);
}

juce::Colour PianoRollView::velocityColour(float velocity)
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, velocity);

    if (clamped > 0.87f)
        return Theme::pink();

    if (clamped > 0.67f)
        return Theme::purple();

    return Theme::cyan();
}

void PianoRollView::paint(juce::Graphics& g)
{
    const auto bounds = getContentBounds();
    g.fillAll(Theme::panelDeep());

    const auto visibleRows = (bounds.getHeight() - rulerHeight) / keyHeight + 2;

    // Key rows ---------------------------------------------------------------
    for (int row = 0; row < visibleRows; ++row)
    {
        const auto pitch = topPitch - row;
        if (pitch < 0)
            break;

        const auto y = rulerHeight + row * keyHeight;
        const auto black = juce::MidiMessage::isMidiNoteBlack(pitch);

        g.setColour(black ? juce::Colour::fromString("bf141922") : juce::Colour::fromString("731c232f"));
        g.fillRect(keyboardWidth, y, bounds.getWidth() - keyboardWidth, keyHeight);

        g.setColour(juce::Colour::fromString("ff1b2230"));
        g.fillRect(0, y + keyHeight - 1, bounds.getWidth(), 1);

        g.setColour(black ? juce::Colour::fromString("ff171c26") : juce::Colour::fromString("ffe7edf7"));
        g.fillRect(0, y, keyboardWidth, keyHeight - 1);

        g.setColour(black ? Theme::mutedText() : juce::Colour::fromString("ff3a4152"));
        g.setFont(Theme::mono(9.0f));
        g.drawText(juce::MidiMessage::getMidiNoteName(pitch, true, true, 3),
                   0, y, keyboardWidth - 4, keyHeight,
                   juce::Justification::centredRight, false);
    }

    // Vertical grid ----------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.withTrimmedLeft(keyboardWidth));

        const auto firstBeat = std::floor(scrollBeats);
        const auto lastBeat = scrollBeats + (bounds.getWidth() - keyboardWidth) / pixelsPerBeat + 1.0;

        // Draw the snap the model is set to, so the lines match where notes land.
        const auto step = getGridStepBeats();

        for (auto beat = firstBeat; beat <= lastBeat; beat += step)
        {
            const auto isBar = std::abs(std::fmod(beat, transport.getBeatsPerBar())) < 1.0e-6;
            const auto isBeat = std::abs(beat - std::round(beat)) < 1.0e-6;

            g.setColour(isBar  ? juce::Colour::fromString("ff33405a")
                      : isBeat ? juce::Colour::fromString("ff242c3b")
                               : juce::Colour::fromString("ff1c2230"));
            g.fillRect(beatToX(beat), rulerHeight, 1, bounds.getHeight() - rulerHeight);
        }
    }

    // Notes ------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.withTrimmedLeft(keyboardWidth));

        const auto notes = model.getNotes();
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto noteBounds = noteToBounds(notes[i]);
            if (! noteBounds.intersects(bounds))
                continue;

            const auto alpha = 0.35f + juce::jlimit(0.0f, 1.0f, notes[i].velocity) * 0.5f;

            // A muted note stays visible but drains of colour, like a muted clip.
            g.setColour(notes[i].muted ? Theme::mutedText().withAlpha(0.22f)
                                       : Theme::purple().withAlpha(alpha));
            g.fillRoundedRectangle(noteBounds.toFloat(), 2.0f);

            g.setColour(isNoteSelected(i)   ? Theme::text()
                      : i == draggedNote    ? Theme::green()
                      : notes[i].muted      ? Theme::mutedText()
                                            : juce::Colour::fromString("ffc79bff"));
            g.drawRoundedRectangle(noteBounds.toFloat().reduced(0.5f), 2.0f,
                                   isNoteSelected(i) ? 1.8f : 1.0f);
        }
    }

    // Playhead ---------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.withTrimmedLeft(keyboardWidth));

        g.setColour(Theme::green().withAlpha(0.75f));
        g.fillRect(beatToX(transport.getPositionBeats()), rulerHeight, 1, bounds.getHeight() - rulerHeight);
    }

    // Ruler --------------------------------------------------------------------
    // Drawn after the notes: the grid scrolls under it, and a note dragged up
    // past the top edge should disappear behind the numbers rather than over them.
    {
        const auto ruler = getRulerBounds();

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(ruler);

        g.setColour(Theme::panelHeader());
        g.fillRect(ruler);
        g.setColour(Theme::outline());
        g.fillRect(ruler.withHeight(1).withY(ruler.getBottom() - 1));

        const auto beatsPerBar = transport.getBeatsPerBar();
        const auto firstBeat = std::floor(scrollBeats);
        const auto lastBeat = scrollBeats + ruler.getWidth() / pixelsPerBeat + 1.0;

        for (auto beat = std::ceil(firstBeat); beat <= lastBeat; beat += 1.0)
        {
            const auto x = beatToX(beat);
            const auto isBar = std::abs(std::fmod(beat, beatsPerBar)) < 1.0e-6;

            // Beats get a short tick, bars a full one and a number - the same
            // reading FL gives, and the same the playlist gives here.
            g.setColour(isBar ? Theme::gridBar() : Theme::divider());
            g.fillRect(x, isBar ? ruler.getY() : ruler.getBottom() - 5,
                       1, isBar ? ruler.getHeight() : 4);

            if (! isBar)
                continue;

            g.setColour(Theme::mutedText());
            g.setFont(Theme::mono(10.0f));
            g.drawText(juce::String(static_cast<int>(std::round(beat / beatsPerBar)) + 1),
                       x + 4, ruler.getY(), 34, ruler.getHeight(),
                       juce::Justification::centredLeft, false);
        }

        // The playhead's own marker, so where it is can be read from the ruler
        // without hunting for the line down in the notes.
        const auto playheadX = beatToX(transport.getPositionBeats());

        if (ruler.contains(playheadX, ruler.getCentreY()))
        {
            juce::Path marker;
            marker.addTriangle(static_cast<float>(playheadX - 4), static_cast<float>(ruler.getBottom() - 7),
                               static_cast<float>(playheadX + 5), static_cast<float>(ruler.getBottom() - 7),
                               static_cast<float>(playheadX + 0.5f), static_cast<float>(ruler.getBottom() - 1));
            g.setColour(Theme::green());
            g.fillPath(marker);
        }
    }

    // Chord badge --------------------------------------------------------------
    {
        const auto badge = getChordBadgeBounds();
        g.setColour(Theme::panelHeader());
        g.fillRect(badge);
        g.setColour(Theme::outlineStrong());
        g.drawRect(badge, 1);

        g.setColour(chordModeEnabled ? Theme::accent() : Theme::mutedText());
        g.setFont(Theme::mono(9.0f));
        g.drawText(chordModeEnabled ? Chord::shortNameFor(activeChordType)
                                    : juce::String(TRANS("Chord")),
                   badge, juce::Justification::centred, false);
    }

    // Rubber bands ------------------------------------------------------------
    if (! zoomDrag.isEmpty())
    {
        g.setColour(Theme::accent().withAlpha(0.18f));
        g.fillRect(zoomDrag);
        g.setColour(Theme::accent());
        g.drawRect(zoomDrag, 1);
    }

    if (marqueeActive && ! marquee.isEmpty())
    {
        g.setColour(Theme::text().withAlpha(0.10f));
        g.fillRect(marquee);
        g.setColour(Theme::text().withAlpha(0.8f));
        g.drawRect(marquee, 1);
    }

    g.setColour(Theme::divider());
    g.fillRect(keyboardWidth - 1, 0, 1, bounds.getHeight());
}

void PianoRollView::refreshCursor(juce::Point<int> position)
{
    // The keyboard and the ruler are not the grid: one plays notes, the other
    // moves the playhead, and neither is where a tool applies.
    if (position.x < keyboardWidth || getRulerBounds().contains(position))
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (activeTool == Tool::select)
    {
        const auto noteIndex = noteAtPosition(position);

        if (noteIndex >= 0)
        {
            const auto notes = model.getNotes();

            if (juce::isPositiveAndBelow(noteIndex, notes.size()))
            {
                const auto bounds = noteToBounds(notes[noteIndex]);
                const auto atEnd = position.x > bounds.getRight() - resizeZoneFor(bounds);

                setMouseCursor(atEnd ? juce::MouseCursor::LeftRightResizeCursor
                                     : juce::MouseCursor::DraggingHandCursor);
                return;
            }
        }

        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // The rest say what they are: JUCE has no eraser or knife, so the ones
    // without a cursor of their own keep the crosshair - still a promise that
    // this click will not be an ordinary one.
    switch (activeTool)
    {
        case Tool::draw:     setMouseCursor(juce::MouseCursor::PointingHandCursor); break;
        case Tool::erase:
        case Tool::mute:
        case Tool::slice:    setMouseCursor(juce::MouseCursor::CrosshairCursor); break;
        case Tool::zoom:     setMouseCursor(juce::MouseCursor::CrosshairCursor); break;
        case Tool::playback: setMouseCursor(juce::MouseCursor::IBeamCursor); break;
        case Tool::select:   break;
    }
}

void PianoRollView::mouseMove(const juce::MouseEvent& event)
{
    refreshCursor(event.getPosition());
}

void PianoRollView::mouseDown(const juce::MouseEvent& event)
{
    if (getChordBadgeBounds().contains(event.getPosition()))
    {
        showChordMenu();
        return;
    }

    if (event.x < keyboardWidth)
        return;

    // The ruler moves the playhead and does nothing else - no note is ever
    // written up there, whichever tool is chosen.
    if (getRulerBounds().contains(event.getPosition()))
    {
        scrubbingRuler = true;
        transport.setPositionBeats(juce::jmax(0.0, xToBeat(event.x)));
        repaint();
        return;
    }

    grabKeyboardFocus();

    // Open the gesture before any edit, so a drag is one undo step.
    if (onEditGesture)
        onEditGesture(true);

    const auto beat = xToBeat(event.x);
    const auto noteIndex = noteAtPosition(event.getPosition());
    draggedNote = -1;
    resizingNote = false;

    // Zoom and Playback act on the timeline, never on the note under the cursor.
    if (activeTool == Tool::zoom)
    {
        zoomDrag = juce::Rectangle<int>(event.x, 0, 1, getContentBounds().getHeight());
        return;
    }

    if (activeTool == Tool::playback)
    {
        transport.setPositionBeats(juce::jmax(0.0, beat));

        if (! transport.isPlaying())
            transport.play();

        repaint();
        return;
    }

    // Right click always deletes, whichever tool is active.
    if (event.mods.isRightButtonDown())
    {
        if (noteIndex >= 0)
        {
            clearNoteSelection();
            model.deleteNoteAt(noteIndex);
        }

        return;
    }

    if (noteIndex >= 0)
    {
        // Ctrl/shift click adds or removes one note from the selection.
        if (activeTool == Tool::select && (event.mods.isCtrlDown() || event.mods.isShiftDown()))
        {
            if (isNoteSelected(noteIndex))
                selectedNotes.removeFirstMatchingValue(noteIndex);
            else
                selectedNotes.add(noteIndex);

            repaint();
            return;
        }

        if (applyToolToNote(noteIndex))
            return;

        // Grabbing a note outside the selection starts a fresh one.
        if (! isNoteSelected(noteIndex))
            clearNoteSelection();

        const auto notes = model.getNotes();
        draggedNote = noteIndex;

        if (juce::isPositiveAndBelow(noteIndex, notes.size()))
        {
            const auto noteBounds = noteToBounds(notes[noteIndex]);
            resizingNote = event.x > noteBounds.getRight() - resizeZoneFor(noteBounds);
            dragGrabBeat = beat;
            dragGrabOffsetBeats = beat - notes[noteIndex].startBeat;
            dragGrabPitch = yToPitch(event.y);
        }

        if (! resizingNote)
            beginGroupDrag();

        return;
    }

    // Empty grid ------------------------------------------------------------
    if (activeTool == Tool::select)
    {
        if (! (event.mods.isCtrlDown() || event.mods.isShiftDown()))
            clearNoteSelection();

        marqueeActive = true;
        marquee = juce::Rectangle<int>(event.x, event.y, 1, 1);
        return;
    }

    if (activeTool == Tool::draw)
    {
        drawing = true;
        drawNoteAt(event.getPosition());
    }
}

void PianoRollView::mouseDrag(const juce::MouseEvent& event)
{
    if (scrubbingRuler)
    {
        transport.setPositionBeats(juce::jmax(0.0, xToBeat(event.x)));
        repaint();
        return;
    }

    if (activeTool == Tool::zoom && ! zoomDrag.isEmpty())
    {
        const auto left = juce::jmin(event.getMouseDownPosition().x, event.x);
        const auto right = juce::jmax(event.getMouseDownPosition().x, event.x);
        zoomDrag = juce::Rectangle<int>(left, 0, right - left, getContentBounds().getHeight());
        repaint();
        return;
    }

    if (marqueeActive)
    {
        marquee = juce::Rectangle<int>(event.getMouseDownPosition(), event.getPosition());
        repaint();
        return;
    }

    if (drawing)
    {
        drawNoteAt(event.getPosition());
        return;
    }

    if (activeTool == Tool::erase)
    {
        // Sweeping with the eraser takes out whatever it crosses.
        const auto noteIndex = noteAtPosition(event.getPosition());

        if (noteIndex >= 0)
            model.deleteNoteAt(noteIndex);

        return;
    }

    if (draggedNote < 0)
        return;

    if (resizingNote)
    {
        const auto notes = model.getNotes();

        if (juce::isPositiveAndBelow(draggedNote, notes.size()))
            model.setNoteLength(draggedNote, xToBeat(event.x) - notes[draggedNote].startBeat);

        return;
    }

    // A grabbed note that is part of a selection carries the rest with it.
    if (selectedNotes.size() > 1 && isNoteSelected(draggedNote))
    {
        moveSelectedNotes(model.snapToGrid(xToBeat(event.x)) - model.snapToGrid(dragGrabBeat),
                          yToPitch(event.y) - dragGrabPitch);
        repaint();
        return;
    }

    // Minus where the note was grabbed, so it moves with the pointer rather
    // than snapping its own start underneath it.
    model.dragNote(draggedNote, xToBeat(event.x) - dragGrabOffsetBeats, yToPitch(event.y));
}

void PianoRollView::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (scrubbingRuler)
    {
        scrubbingRuler = false;
        return;
    }

    if (activeTool == Tool::zoom && ! zoomDrag.isEmpty())
    {
        if (zoomDrag.getWidth() > 8)
        {
            const auto fromBeat = xToBeat(zoomDrag.getX());
            const auto beats = juce::jmax(0.5, xToBeat(zoomDrag.getRight()) - fromBeat);

            pixelsPerBeat = juce::jlimit(6.0, 96.0, (getContentBounds().getWidth() - keyboardWidth) / beats);
            scrollBeats = juce::jmax(0.0, fromBeat);
        }

        zoomDrag = {};
    }

    if (marqueeActive)
    {
        marqueeActive = false;

        // A click with no drag only clears, which mouseDown already did.
        if (marquee.getWidth() > 3 || marquee.getHeight() > 3)
            selectNotesInMarquee();

        marquee = {};
    }

    if (onEditGesture)
        onEditGesture(false);

    drawing = false;
    lastDrawnBeat = -1.0;
    lastDrawnPitch = -1;
    draggedNote = -1;
    resizingNote = false;
    repaint();
}


void PianoRollView::setTool(Tool tool)
{
    if (activeTool == tool)
        return;

    activeTool = tool;
    refreshCursor(getMouseXYRelative());
    marqueeActive = false;
    marquee = {};
    zoomDrag = {};
    drawing = false;
    draggedNote = -1;
    repaint();
}

PianoRollView::Tool PianoRollView::getTool() const noexcept
{
    return activeTool;
}

bool PianoRollView::isNoteSelected(int noteIndex) const
{
    return selectedNotes.contains(noteIndex);
}

void PianoRollView::clearNoteSelection()
{
    if (selectedNotes.isEmpty())
        return;

    selectedNotes.clearQuick();
    repaint();
}

void PianoRollView::selectNotesInMarquee()
{
    const auto notes = model.getNotes();

    for (int i = 0; i < notes.size(); ++i)
        if (noteToBounds(notes[i]).intersects(marquee) && ! isNoteSelected(i))
            selectedNotes.add(i);

    repaint();
}

void PianoRollView::beginGroupDrag()
{
    // Remember the notes themselves: indices alone would not survive an edit.
    groupDragOrigins.clearQuick();

    const auto notes = model.getNotes();

    for (const auto index : selectedNotes)
        if (juce::isPositiveAndBelow(index, notes.size()))
            groupDragOrigins.add(notes[index]);
}

void PianoRollView::moveSelectedNotes(double deltaBeats, int deltaPitch)
{
    if (selectedNotes.size() != groupDragOrigins.size())
        return;

    // The group keeps its shape: whatever is closest to bar one, or to the
    // edges of the keyboard, sets the limit.
    auto beats = deltaBeats;
    auto pitch = deltaPitch;

    for (const auto& origin : groupDragOrigins)
    {
        beats = juce::jmax(beats, -origin.startBeat);
        pitch = juce::jlimit(-origin.pitch, 127 - origin.pitch, pitch);
    }

    for (int i = 0; i < selectedNotes.size(); ++i)
    {
        const auto& origin = groupDragOrigins.getReference(i);
        model.dragNote(selectedNotes[i], origin.startBeat + beats, origin.pitch + pitch);
    }
}

void PianoRollView::deleteSelectedNotes()
{
    if (selectedNotes.isEmpty())
        return;

    // Removing shifts the indices behind it, so delete back to front.
    auto ordered = selectedNotes;
    std::sort(ordered.begin(), ordered.end(), std::greater<int>());

    for (const auto index : ordered)
        model.deleteNoteAt(index);

    selectedNotes.clearQuick();
    repaint();
}

void PianoRollView::copySelectedNotes()
{
    if (selectedNotes.isEmpty())
        return;

    const auto notes = model.getNotes();
    clipboardNotes.clearQuick();

    for (const auto index : selectedNotes)
        if (juce::isPositiveAndBelow(index, notes.size()))
            clipboardNotes.add(notes[index]);
}

void PianoRollView::pasteNotes()
{
    if (clipboardNotes.isEmpty())
        return;

    if (onEditGesture)
        onEditGesture(true);

    auto notes = model.getNotes();
    juce::Array<int> pastedIndices;

    for (const auto& note : clipboardNotes)
    {
        pastedIndices.add(notes.size());
        notes.add(note);
    }

    model.setNotes(notes);

    if (onEditGesture)
        onEditGesture(false);

    // Select the copies, not the originals, so the obvious next move -
    // dragging them to another octave or beat - only touches what was pasted.
    selectedNotes = std::move(pastedIndices);
    repaint();
}

void PianoRollView::shiftSelectedNotesByOctave(int direction)
{
    if (selectedNotes.isEmpty())
        return;

    if (onEditGesture)
        onEditGesture(true);

    beginGroupDrag();
    moveSelectedNotes(0.0, direction * 12);

    if (onEditGesture)
        onEditGesture(false);

    repaint();
}

bool PianoRollView::applyToolToNote(int noteIndex)
{
    auto notes = model.getNotes();

    if (! juce::isPositiveAndBelow(noteIndex, notes.size()))
        return false;

    switch (activeTool)
    {
        case Tool::erase:
            clearNoteSelection();
            model.deleteNoteAt(noteIndex);
            return true;

        case Tool::mute:
            notes.getReference(noteIndex).muted = ! notes.getReference(noteIndex).muted;
            model.setNotes(notes);
            return true;

        case Tool::slice:
        {
            // Cut where the pointer is, and refuse slivers on either side.
            const auto& note = notes.getReference(noteIndex);
            const auto cut = model.snapToGrid(xToBeat(getMouseXYRelative().x));
            constexpr double minimumLength = 0.03125;

            if (cut <= note.startBeat + minimumLength
                || cut >= note.startBeat + note.lengthBeats - minimumLength)
                return true;   // handled: a refused cut must not start a drag

            auto right = note;
            right.startBeat = cut;
            right.lengthBeats = note.startBeat + note.lengthBeats - cut;

            notes.getReference(noteIndex).lengthBeats = cut - note.startBeat;
            notes.add(right);

            clearNoteSelection();
            model.setNotes(notes);
            return true;
        }

        case Tool::select:
        case Tool::draw:
        case Tool::zoom:
        case Tool::playback:
        default:
            return false;
    }
}

bool PianoRollView::drawNoteAt(juce::Point<int> position)
{
    if (position.x < keyboardWidth)
        return false;

    const auto beat = model.snapToGrid(xToBeat(position.x));
    const auto pitch = yToPitch(position.y);

    // A sweep must not stack a pile of notes on the same slot.
    if (pitch == lastDrawnPitch && std::abs(beat - lastDrawnBeat) < 1.0e-6)
        return false;

    const auto length = juce::jmax(0.25, model.getSnapBeats() * 4.0);
    auto wroteAny = false;

    if (chordModeEnabled)
    {
        // Each interval that already has something there is skipped rather
        // than aborting the whole chord - one clashing tone should not
        // swallow the rest of the stamp.
        for (const auto interval : Chord::intervalsFor(activeChordType))
        {
            const auto chordPitch = juce::jlimit(0, 127, pitch + interval);

            if (hasNoteAt(chordPitch, beat))
                continue;

            model.addNote(chordPitch, beat, length, 0.85f);
            wroteAny = true;
        }
    }
    else if (noteAtPosition(position) < 0)
    {
        model.addNote(pitch, beat, length, 0.85f);
        wroteAny = true;
    }

    if (wroteAny)
    {
        lastDrawnBeat = beat;
        lastDrawnPitch = pitch;
    }

    return wroteAny;
}

bool PianoRollView::hasNoteAt(int pitch, double beat) const
{
    for (const auto& note : model.getNotes())
        if (note.pitch == pitch && beat >= note.startBeat && beat < note.startBeat + note.lengthBeats)
            return true;

    return false;
}

void PianoRollView::showChordMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, TRANS("Off - single notes"), true, ! chordModeEnabled);
    menu.addSeparator();

    for (int i = 0; i < static_cast<int>(ChordType::count); ++i)
        menu.addItem(2 + i, Chord::nameFor(static_cast<ChordType>(i)), true,
                     chordModeEnabled && static_cast<int>(activeChordType) == i);

    // Anchored to the badge that opened it, like the piano roll's other
    // corner pickers.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(localAreaToGlobal(getChordBadgeBounds()))
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (result <= 0)
                return;

            chordModeEnabled = result != 1;

            if (chordModeEnabled)
                activeChordType = static_cast<ChordType>(result - 2);

            repaint(getChordBadgeBounds());
        });
}

bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown())
    {
        // Not getTextCharacter(): with ctrl held, X11 reports a control code,
        // so matching on the letter never fires on Linux. getKeyCode() gives
        // the plain uppercase key regardless of modifiers.
        const auto character = juce::CharacterFunctions::toLowerCase(
            static_cast<juce::juce_wchar>(key.getKeyCode()));

        if (character == 'c')
        {
            copySelectedNotes();
            return true;
        }

        if (character == 'v')
        {
            pasteNotes();
            return true;
        }

        // Only steals the typing keyboard's octave shortcut (MainComponent's
        // job otherwise) when there is a note block here to move instead.
        if (! selectedNotes.isEmpty()
            && (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)))
        {
            shiftSelectedNotesByOctave(key.isKeyCode(juce::KeyPress::upKey) ? 1 : -1);
            return true;
        }
    }

    if (selectedNotes.isEmpty())
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (onEditGesture)
            onEditGesture(true);

        deleteSelectedNotes();

        if (onEditGesture)
            onEditGesture(false);

        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        clearNoteSelection();
        return true;
    }

    // Everything else is the window's: space, record, save.
    return false;
}

void PianoRollView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        pixelsPerBeat = juce::jlimit(6.0, 96.0, pixelsPerBeat * (wheel.deltaY > 0.0f ? 1.12 : 0.89));
        refreshScrollBars();
        repaint();
        return;
    }

    if (event.mods.isShiftDown())
    {
        scrollBeats = juce::jmax(0.0, scrollBeats - wheel.deltaY * 6.0);
        refreshScrollBars();
        repaint();
        return;
    }

    topPitch = juce::jlimit(11, 127, topPitch + juce::roundToInt(wheel.deltaY * 4.0f));
    refreshScrollBars();
    repaint();
}

void PianoRollView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint();
}

void PianoRollView::timerCallback()
{
    if (transport.isPlaying())
    {
        // Follow the playhead, so it does not simply run off the right edge.
        // Only once it actually leaves the visible range, and it lands a little
        // in from the left so the next bar is already on screen. Gated on
        // followPlayhead so scrolling to look elsewhere during playback sticks
        // instead of snapping back thirty times a second.
        if (followPlayhead)
        {
            const auto visibleWidth = getContentBounds().getWidth() - keyboardWidth;

            if (visibleWidth > 0)
            {
                const auto visibleBeats = visibleWidth / pixelsPerBeat;
                const auto position = transport.getPositionBeats();

                if (position < scrollBeats || position > scrollBeats + visibleBeats * 0.85)
                {
                    scrollBeats = juce::jmax(0.0, position - visibleBeats * 0.15);
                    refreshScrollBars();
                }
            }
        }

        repaint();
    }
}

int PianoRollView::noteAtPosition(juce::Point<int> position) const
{
    const auto notes = model.getNotes();

    for (int i = notes.size(); --i >= 0;)
        if (noteToBounds(notes[i]).contains(position))
            return i;

    return -1;
}

juce::Rectangle<int> PianoRollView::noteToBounds(const MidiNote& note) const
{
    const auto left = beatToX(note.startBeat);
    const auto right = beatToX(note.startBeat + note.lengthBeats);

    return {
        left,
        pitchToY(note.pitch) + noteHeightInset,
        juce::jmax(8, right - left - 1),
        keyHeight - noteHeightInset * 2
    };
}

double PianoRollView::xToBeat(int x) const
{
    return juce::jmax(0.0, scrollBeats + (x - keyboardWidth) / pixelsPerBeat);
}

int PianoRollView::beatToX(double beat) const
{
    return keyboardWidth + juce::roundToInt((beat - scrollBeats) * pixelsPerBeat);
}

int PianoRollView::yToPitch(int y) const
{
    // Measured from under the ruler, so a note lands on the row it was drawn on.
    return juce::jlimit(0, 127, topPitch - ((y - rulerHeight) / keyHeight));
}

int PianoRollView::pitchToY(int pitch) const
{
    return rulerHeight + (topPitch - pitch) * keyHeight;
}

double PianoRollView::getGridStepBeats() const noexcept
{
    // Line opens up from the finest division until the lines can be read; a
    // piano roll cell is one step; none still draws a beat grid to work against.
    auto step = snapUnit == SnapUnit::line ? stepBeats / 4.0
              : snapUnit == SnapUnit::cell ? stepBeats
              : snapUnit == SnapUnit::none ? 1.0
                                           : getSnapUnitBeats(snapUnit);

    if (step <= 0.0)
        step = 1.0;

    while (step * pixelsPerBeat < 5.0 && step < transport.getBeatsPerBar())
        step *= 2.0;

    return step;
}

void PianoRollView::applySnapToModel()
{
    // Zero switches snapping off in the model.
    const auto snap = snapUnit == SnapUnit::none ? 0.0
                    : snapUnit == SnapUnit::line ? getGridStepBeats()
                    : snapUnit == SnapUnit::cell ? stepBeats
                                                 : getSnapUnitBeats(snapUnit);

    model.setSnapBeats(snap);
}

void PianoRollView::setSnapUnit(SnapUnit unit)
{
    snapUnit = unit;
    applySnapToModel();
    repaint();
}

int PianoRollView::getKeyboardWidth() const noexcept
{
    return keyboardWidth;
}

double PianoRollView::getPixelsPerBeat() const noexcept
{
    return pixelsPerBeat;
}

double PianoRollView::getBeatsPerBar() const noexcept
{
    return transport.getBeatsPerBar();
}

double PianoRollView::getScrollBeats() const noexcept
{
    return scrollBeats;
}

void PianoRollView::setFollowPlayhead(bool shouldFollow)
{
    followPlayhead = shouldFollow;
}

bool PianoRollView::isFollowingPlayhead() const noexcept
{
    return followPlayhead;
}

} // namespace djr
