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
    verticalBar.setBounds(rightStrip.withTrimmedBottom(ZoomScrollBar::thickness));
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
    const auto visibleKeys = juce::jmax(1, getContentBounds().getHeight() / juce::jmax(1, keyHeight));
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
    const auto height = juce::jmax(1, getContentBounds().getHeight());
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

    const auto visibleRows = bounds.getHeight() / keyHeight + 2;

    // Key rows ---------------------------------------------------------------
    for (int row = 0; row < visibleRows; ++row)
    {
        const auto pitch = topPitch - row;
        if (pitch < 0)
            break;

        const auto y = row * keyHeight;
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
            g.fillRect(beatToX(beat), 0, 1, bounds.getHeight());
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
        g.fillRect(beatToX(transport.getPositionBeats()), 0, 1, bounds.getHeight());
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

void PianoRollView::mouseDown(const juce::MouseEvent& event)
{
    if (event.x < keyboardWidth)
        return;

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
            resizingNote = event.x > noteBounds.getRight() - resizeHandleWidth;
            dragGrabBeat = beat;
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

    model.dragNote(draggedNote, xToBeat(event.x), yToPitch(event.y));
}

void PianoRollView::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

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

    if (noteAtPosition(position) >= 0)
        return false;

    model.addNote(pitch, beat, juce::jmax(0.25, model.getSnapBeats() * 4.0), 0.85f);
    lastDrawnBeat = beat;
    lastDrawnPitch = pitch;
    return true;
}

bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
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
        repaint();
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
    return juce::jlimit(0, 127, topPitch - (y / keyHeight));
}

int PianoRollView::pitchToY(int pitch) const
{
    return (topPitch - pitch) * keyHeight;
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

} // namespace djr
