#include "ArrangementView.h"

#include "Theme.h"
#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"

#include <algorithm>
#include <cmath>

namespace djr
{

namespace
{
    constexpr int panelHeaderHeight = Metrics::panelToolbarHeight;
    /** How close to an edge counts as grabbing it rather than the clip body. */
    constexpr int clipEdgeGrab = 6;
    /** How close to a lane's bottom edge starts a height drag. */
    constexpr int rowEdgeGrab = 4;
    constexpr int minRowHeight = 18;
    constexpr int maxRowHeight = 200;
}

ArrangementView::ArrangementView(Mixer& mixerToUse, Transport& transportToUse)
    : mixer(mixerToUse), transport(transportToUse)
{
    buildToolButtons();

    snapButton.setIconInset(3.5f);
    snapButton.addListener(this);
    addAndMakeVisible(snapButton);

    addTrackButton.setIconInset(4.0f);
    addTrackButton.addListener(this);
    addTrackButton.setTooltip("Tambah track");
    addAndMakeVisible(addTrackButton);

    followButton.setIconInset(4.0f);
    followButton.addListener(this);
    followButton.setTooltip("Ikuti playhead saat playback");
    addAndMakeVisible(followButton);

    zoomFitButton.setIconInset(4.0f);
    zoomFitButton.addListener(this);
    zoomFitButton.setTooltip("Zoom to fit");
    addAndMakeVisible(zoomFitButton);

    zoomSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    zoomSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    zoomSlider.setRange(24.0, 320.0, 1.0);
    zoomSlider.setValue(pixelsPerBar, juce::dontSendNotification);
    zoomSlider.setTooltip("Zoom playlist");
    zoomSlider.addListener(this);
    addAndMakeVisible(zoomSlider);

    // Delete acts on the selection, so the grid needs keys. Anything this view
    // does not claim still bubbles up to the window shortcuts.
    setWantsKeyboardFocus(true);

    startTimerHz(30);
}

ArrangementView::~ArrangementView()
{
    for (auto* button : toolButtons)
        button->removeListener(this);

    snapButton.removeListener(this);
    addTrackButton.removeListener(this);
    followButton.removeListener(this);
    zoomFitButton.removeListener(this);
    zoomSlider.removeListener(this);
}

void ArrangementView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(Theme::panelDeep());

    // Toolbar ----------------------------------------------------------------
    auto header = bounds.withHeight(panelHeaderHeight);
    g.setColour(Theme::panel());
    g.fillRect(header);
    g.setColour(Theme::outline());
    g.fillRect(header.removeFromBottom(1));

    g.setColour(Theme::mutedText());
    g.setFont(Theme::ui(11.0f));
    g.drawText("Zoom",
               juce::Rectangle<int>(zoomSlider.getX() - 36, header.getY(), 32, header.getHeight()),
               juce::Justification::centredRight,
               false);

    // Grid background --------------------------------------------------------
    const auto grid = getGridArea();
    const auto lanes = bounds.withTrimmedTop(panelHeaderHeight);

    g.setColour(Theme::panel());
    g.fillRect(lanes.withWidth(headerWidth));
    g.fillRect(juce::Rectangle<int>(grid.getX(), lanes.getY(), grid.getWidth(), rulerHeight));

    g.setColour(Theme::divider());
    g.fillRect(juce::Rectangle<int>(grid.getX() - 1, lanes.getY(), 1, lanes.getHeight()));
    g.fillRect(juce::Rectangle<int>(grid.getX(), grid.getY() - 1, grid.getWidth(), 1));

    // Bar ruler + vertical grid ---------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(juce::Rectangle<int>(grid.getX(), lanes.getY(), grid.getWidth(), lanes.getHeight()));

        const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();
        const auto firstBeat = std::floor(scrollBeats / transport.getBeatsPerBar()) * transport.getBeatsPerBar();
        const auto lastBeat = scrollBeats + grid.getWidth() / pixelsPerBeat + transport.getBeatsPerBar();

        // The grid draws whatever the snap is set to, so what you see is what
        // edits land on. Sub-beat lines are dropped once they crowd together.
        const auto step = getGridStepBeats();

        for (auto beat = firstBeat; beat <= lastBeat; beat += step)
        {
            const auto x = beatToX(beat);
            const auto isBar = std::abs(std::fmod(beat, transport.getBeatsPerBar())) < 1.0e-6;
            const auto isBeat = std::abs(beat - std::round(beat)) < 1.0e-6;

            g.setColour(isBar ? Theme::gridBar()
                              : isBeat ? Theme::gridBeat()
                                       : Theme::gridBeat().withMultipliedAlpha(0.45f));
            g.fillRect(x, grid.getY(), 1, grid.getHeight());

            if (isBar)
            {
                g.setColour(Theme::gridBar());
                g.fillRect(x, lanes.getY(), 1, rulerHeight);
                g.setColour(Theme::mutedText());
                g.setFont(Theme::mono(10.0f));
                g.drawText(juce::String(static_cast<int>(beat / transport.getBeatsPerBar()) + 1),
                           x + 4, lanes.getY(), 34, rulerHeight,
                           juce::Justification::centredLeft, false);
            }
        }
    }

    // Track rows -------------------------------------------------------------
    const auto numTracks = mixer.getNumTracks();

    for (int i = 0; i < numTracks; ++i)
    {
        const auto* track = mixer.getTrack(i);
        if (track == nullptr)
            continue;

        const auto row = getRowBounds(i);
        if (! row.intersects(lanes))
            continue;

        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(lanes.withTrimmedTop(rulerHeight));

        const auto colour = Theme::trackColour(i);

        if (i == selectedTrack)
        {
            g.setColour(colour.withAlpha(0.06f));
            g.fillRect(row);
        }

        g.setColour(juce::Colour::fromString("ff1e2532"));
        g.fillRect(row.withHeight(1).withY(row.getBottom() - 1));

        // Header cell
        auto headerCell = row.withWidth(headerWidth);
        g.setColour(Theme::panel());
        g.fillRect(headerCell);

        if (i == selectedTrack)
        {
            g.setColour(colour.withAlpha(0.10f));
            g.fillRect(headerCell);
        }

        g.setColour(juce::Colour::fromString("ff1e2532"));
        g.fillRect(headerCell.withHeight(1).withY(headerCell.getBottom() - 1));

        auto cell = headerCell.reduced(5, 0);
        auto chip = cell.removeFromLeft(3).withSizeKeepingCentre(3, row.getHeight() - 10);
        g.setColour(colour);
        g.fillRoundedRectangle(chip.toFloat(), 1.5f);
        cell.removeFromLeft(6);

        const auto drawToggle = [&g] (juce::Rectangle<int> area, const juce::String& label, bool on, juce::Colour onColour)
        {
            g.setColour(on ? onColour : Theme::control());
            g.fillRoundedRectangle(area.toFloat(), 2.5f);
            g.setColour(on ? Theme::windowBackground() : Theme::mutedText());
            g.setFont(Theme::ui(10.5f, true));
            g.drawText(label, area, juce::Justification::centred, false);
        };

        drawToggle(getMuteBounds(i), "M", track->isMuted(), Theme::pink());
        drawToggle(getSoloBounds(i), "S", track->isSoloed(), Theme::amber());

        auto textArea = cell.withTrimmedRight(34);
        g.setColour(Theme::text());
        g.setFont(Theme::ui(12.0f, true));
        g.drawText(track->getName(), textArea.removeFromTop(row.getHeight() / 2).withTrimmedTop(3),
                   juce::Justification::bottomLeft, true);
        g.setColour(Theme::faintText());
        g.setFont(Theme::mono(9.0f));
        g.drawText(describeKind(*track, i), textArea, juce::Justification::topLeft, true);

        // Clips
        juce::Graphics::ScopedSaveState clipRegion(g);
        g.reduceClipRegion(juce::Rectangle<int>(grid.getX(), row.getY(), grid.getWidth(), row.getHeight()));

        for (const auto& clip : getClipsForTrack(i))
        {
            const auto left = beatToX(clip.startBeat);
            const auto width = juce::jmax(20, beatToX(clip.startBeat + clip.lengthBeats) - left);
            auto clipBounds = juce::Rectangle<int>(left, row.getY() + 3, width, row.getHeight() - 6);

            // A muted clip stays visible but drains of colour, like FL.
            const auto clipColour = clip.muted ? colour.withSaturation(0.12f).withBrightness(0.45f) : colour;

            g.setColour(clipColour.withAlpha(clip.muted ? 0.10f : 0.16f));
            g.fillRoundedRectangle(clipBounds.toFloat(), 3.0f);
            g.setColour(clipColour);
            g.drawRoundedRectangle(clipBounds.toFloat().reduced(0.5f), 3.0f, 1.0f);

            // A clip the marquee caught gets a bright ring, so a whole group
            // reads at a glance against the different track colours.
            if (isClipSelected(i, clip.index))
            {
                g.setColour(Theme::text().withAlpha(0.22f));
                g.fillRoundedRectangle(clipBounds.toFloat(), 3.0f);
                g.setColour(Theme::text());
                g.drawRoundedRectangle(clipBounds.toFloat().reduced(0.5f), 3.0f, 1.8f);
            }

            auto labelBar = clipBounds.removeFromTop(10);
            juce::Path labelShape;
            labelShape.addRoundedRectangle(labelBar.getX(), labelBar.getY(),
                                           labelBar.getWidth(), labelBar.getHeight(),
                                           3.0f, 3.0f, true, true, false, false);
            g.setColour(clipColour);
            g.fillPath(labelShape);
            g.setColour(Theme::windowBackground());
            g.setFont(Theme::ui(9.5f, true));
            g.drawText(clip.label, labelBar.reduced(4, 0), juce::Justification::centredLeft, true);

            // Content: dashes for MIDI, the real waveform envelope for audio.
            g.setColour(colour.withAlpha(0.85f));

            if (clip.midi)
            {
                // A miniature of the pattern: the same notes the piano roll shows,
                // laid out by time and pitch instead of decorative dashes.
                const auto body = clipBounds.reduced(2, 1).toFloat();
                const auto pitchSpan = juce::jmax(1, clip.highestPitch - clip.lowestPitch);
                const auto noteHeight = juce::jlimit(1.5f, 4.0f, body.getHeight() / static_cast<float>(pitchSpan + 1));
                const auto travel = juce::jmax(0.0f, body.getHeight() - noteHeight);

                for (const auto& note : clip.notes)
                {
                    if (clip.lengthBeats <= 0.0)
                        break;

                    const auto noteLeft = body.getX()
                                        + static_cast<float>(note.startBeat / clip.lengthBeats) * body.getWidth();
                    const auto noteRight = body.getX()
                                         + static_cast<float>((note.startBeat + note.lengthBeats) / clip.lengthBeats)
                                               * body.getWidth();

                    // Higher pitch sits higher, exactly like the piano roll.
                    const auto normalised = static_cast<float>(note.pitch - clip.lowestPitch)
                                          / static_cast<float>(pitchSpan);
                    const auto top = body.getBottom() - noteHeight - normalised * travel;

                    g.fillRect(juce::Rectangle<float>(noteLeft,
                                                      top,
                                                      juce::jmax(1.5f, noteRight - noteLeft),
                                                      noteHeight));
                }
            }
            else if (clip.peaks != nullptr && ! clip.peaks->empty())
            {
                const auto body = clipBounds.reduced(2, 1);
                const auto centreY = static_cast<float>(body.getCentreY());
                const auto halfHeight = body.getHeight() * 0.5f;
                const auto bucketCount = static_cast<int>(clip.peaks->size());

                // Only the trimmed span of the source is on screen.
                const auto firstBucket = clip.trimStartFraction * bucketCount;
                const auto bucketSpan = juce::jmax(1.0, (clip.trimEndFraction - clip.trimStartFraction) * bucketCount);

                for (int x = 0; x < body.getWidth(); ++x)
                {
                    const auto position = firstBucket + bucketSpan * x / juce::jmax(1, body.getWidth());
                    const auto bucket = juce::jlimit(0, bucketCount - 1, static_cast<int>(position));
                    const auto amplitude = (*clip.peaks)[static_cast<size_t>(bucket)] * halfHeight;

                    g.drawVerticalLine(body.getX() + x,
                                       centreY - amplitude,
                                       centreY + juce::jmax(0.5f, amplitude));
                }

                if (clip.warped)
                {
                    g.setColour(Theme::windowBackground().withAlpha(0.7f));
                    g.setFont(Theme::mono(8.5f));
                    g.drawText("W", clipBounds.removeFromRight(11), juce::Justification::centredRight, false);
                }
            }
            else
            {
                g.fillRect(clipBounds.withSizeKeepingCentre(clipBounds.getWidth() - 4, clipBounds.getHeight() / 5));
            }
        }
    }

    // Empty state ------------------------------------------------------------
    if (numTracks == 0)
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(13.0f));
        g.drawText("Belum ada track", grid, juce::Justification::centred, false);
    }

    // Zoom rubber band --------------------------------------------------------
    if (! zoomDrag.isEmpty())
    {
        g.setColour(Theme::accent().withAlpha(0.18f));
        g.fillRect(zoomDrag);
        g.setColour(Theme::accent());
        g.drawRect(zoomDrag, 1);
    }

    // Slice preview -----------------------------------------------------------
    if (sliceTrack >= 0)
    {
        const auto row = getRowBounds(sliceTrack);

        if (row.intersects(lanes))
        {
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(juce::Rectangle<int>(grid.getX(), row.getY(), grid.getWidth(), row.getHeight()));

            const auto x = beatToX(sliceBeat);

            g.setColour(Theme::amber());
            g.fillRect(x, row.getY() + 2, 1, row.getHeight() - 4);

            // Little nibs top and bottom, so the line reads as a cut and not as
            // another grid line.
            g.fillRect(x - 2, row.getY() + 2, 5, 2);
            g.fillRect(x - 2, row.getBottom() - 4, 5, 2);
        }
    }

    // Selection rubber band ---------------------------------------------------
    if (marqueeActive && ! marquee.isEmpty())
    {
        g.setColour(Theme::text().withAlpha(0.10f));
        g.fillRect(marquee);
        g.setColour(Theme::text().withAlpha(0.8f));
        g.drawRect(marquee, 1);
    }

    // Playhead ---------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(juce::Rectangle<int>(grid.getX(), lanes.getY(), grid.getWidth(), lanes.getHeight()));

        const auto playheadX = beatToX(transport.getPositionBeats());

        g.setColour(Theme::green().withAlpha(0.16f));
        g.fillRect(playheadX - 1, lanes.getY(), 3, lanes.getHeight());
        g.setColour(Theme::green());
        g.fillRect(playheadX, lanes.getY(), 1, lanes.getHeight());

        juce::Path marker;
        marker.addTriangle(static_cast<float>(playheadX - 4), static_cast<float>(lanes.getY()),
                           static_cast<float>(playheadX + 5), static_cast<float>(lanes.getY()),
                           static_cast<float>(playheadX + 0.5f), static_cast<float>(lanes.getY() + 7));
        g.fillPath(marker);
    }
}

void ArrangementView::resized()
{
    auto header = getLocalBounds().withHeight(panelHeaderHeight).reduced(6, 0);

    addTrackButton.setBounds(header.removeFromLeft(19).withSizeKeepingCentre(19, 19));
    header.removeFromLeft(7);

    snapButton.setBounds(header.removeFromLeft(19).withSizeKeepingCentre(19, 19));
    header.removeFromLeft(7);

    for (auto* button : toolButtons)
    {
        button->setBounds(header.removeFromLeft(19).withSizeKeepingCentre(19, 19));
        header.removeFromLeft(2);
    }

    header.removeFromLeft(4);
    followButton.setBounds(header.removeFromLeft(18).withSizeKeepingCentre(18, 18));
    header.removeFromLeft(3);
    zoomFitButton.setBounds(header.removeFromLeft(18).withSizeKeepingCentre(18, 18));

    zoomSlider.setBounds(header.removeFromRight(80).withSizeKeepingCentre(80, 16));

    clampScroll();
}

void ArrangementView::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    // Everything one click does is a single undo step, however many edits the
    // drag that follows makes.
    if (undoGestureCallback)
        undoGestureCallback(true);

    // The lane edge sits on top of the header cell below it, so claim it first.
    if (! event.mods.isRightButtonDown())
    {
        const auto resizeRow = hitTestRowResize(position);

        if (resizeRow >= 0)
        {
            resizingRow = resizeRow;
            resizeStartHeight = getRowHeight(resizeRow);
            resizeGrabY = position.y;
            return;
        }
    }

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        auto* track = mixer.getTrack(i);
        if (track == nullptr)
            continue;

        if (getMuteBounds(i).contains(position))
        {
            track->setMuted(! track->isMuted());
            repaint();
            return;
        }

        if (getSoloBounds(i).contains(position))
        {
            track->setSoloed(! track->isSoloed());
            repaint();
            return;
        }

        if (getRowBounds(i).contains(position))
        {
            setSelectedTrack(i);

            if (event.mods.isRightButtonDown() && position.x < headerWidth)
            {
                showTrackContextMenu(i);
                return;
            }

            break;
        }
    }

    // A clip under the pointer takes priority over scrubbing the playhead.
    int clipTrack = -1;
    int clipIndex = -1;
    const auto mode = hitTestClip(position, clipTrack, clipIndex);

    // Zoom and Playback act on the timeline, never on the clip under the cursor.
    const auto toolIgnoresClips = activeTool == Tool::zoom || activeTool == Tool::playback;

    if (mode != ClipDragMode::none && ! toolIgnoresClips)
    {
        setSelectedTrack(clipTrack);

        // Ctrl or shift click adds or removes one clip, leaving the rest alone.
        if (activeTool == Tool::select && ! event.mods.isRightButtonDown()
            && (event.mods.isCtrlDown() || event.mods.isShiftDown()))
        {
            toggleClipSelection(clipTrack, clipIndex);
            return;
        }

        // Erase, mute, slice and slip act on the clip straight away.
        if (! event.mods.isRightButtonDown()
            && applyToolToClip(clipTrack, clipIndex, xToBeat(position.x), mode))
            return;

        if (event.mods.isRightButtonDown())
        {
            if (dynamic_cast<MidiTrack*>(mixer.getTrack(clipTrack)) != nullptr)
                showPlacementContextMenu(clipTrack, clipIndex);
            else
                showClipContextMenu(clipTrack, clipIndex);

            return;
        }

        // Grabbing a clip outside the selection starts a fresh one; grabbing one
        // inside it picks up the whole group.
        if (activeTool == Tool::select && ! isClipSelected(clipTrack, clipIndex))
            clearClipSelection();

        clipDrag.mode = mode;
        clipDrag.trackIndex = clipTrack;
        clipDrag.clipIndex = clipIndex;
        clipDrag.grabBeat = xToBeat(position.x);

        for (const auto& clip : getClipsForTrack(clipTrack))
        {
            if (clip.index != clipIndex || clip.midi != (dynamic_cast<MidiTrack*>(mixer.getTrack(clipTrack)) != nullptr))
                continue;

            clipDrag.originalStart = clip.startBeat;
            clipDrag.originalEnd = clip.startBeat + clip.lengthBeats;
            break;
        }

        beginGroupDrag();
        return;
    }

    const auto inGrid = position.x >= getGridArea().getX() && position.y >= getGridArea().getY();

    if (activeTool == Tool::zoom && inGrid)
    {
        zoomDrag = juce::Rectangle<int>(position.x, getGridArea().getY(), 1, getGridArea().getHeight());
        return;
    }

    if (activeTool == Tool::playback && inGrid)
    {
        transport.setPositionBeats(juce::jmax(0.0, xToBeat(position.x)));

        if (! transport.isPlaying())
            transport.play();

        repaint();
        return;
    }

    // Select drags a rubber band over empty grid. Placing clips belongs to
    // Paint, the way FL splits draw from select.
    if (activeTool == Tool::select && inGrid)
    {
        if (! (event.mods.isCtrlDown() || event.mods.isShiftDown()))
            clearClipSelection();

        marqueeActive = true;
        marquee = juce::Rectangle<int>(position.x, position.y, 1, 1);
        return;
    }

    // An empty spot on a MIDI lane is where a pattern gets placed.
    if (activeTool == Tool::paint && inGrid)
    {
        // Paint keeps laying clips for as long as the drag lasts, so the stroke
        // starts here even when this first spot is already taken.
        painting = true;
        lastPaintPosition = position;
        paintPatternAt(position);
        return;
    }

    if (getGridArea().contains(position) || position.y < getGridArea().getY())
    {
        if (position.x >= getGridArea().getX())
        {
            transport.setPositionBeats(juce::jmax(0.0, xToBeat(position.x)));
            repaint();
        }
    }
}

void ArrangementView::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    clearSlicePreview();
}

void ArrangementView::mouseMove(const juce::MouseEvent& event)
{
    updateSlicePreview(event.getPosition());

    if (hitTestRowResize(event.getPosition()) >= 0)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    int track = -1;
    int clip = -1;
    const auto mode = hitTestClip(event.getPosition(), track, clip);

    setMouseCursor(mode == ClipDragMode::trimStart || mode == ClipDragMode::trimEnd
                       ? juce::MouseCursor::LeftRightResizeCursor
                       : juce::MouseCursor::NormalCursor);
}

void ArrangementView::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (undoGestureCallback)
        undoGestureCallback(false);

    painting = false;

    if (marqueeActive)
    {
        marqueeActive = false;

        // A click with no drag only clears, which mouseDown already did.
        if (marquee.getWidth() > 3 || marquee.getHeight() > 3)
            selectClipsInMarquee();

        marquee = {};
        repaint();
        return;
    }

    if (resizingRow >= 0)
    {
        resizingRow = -1;
        return;
    }

    if (activeTool == Tool::zoom && ! zoomDrag.isEmpty())
    {
        // Fit the dragged range to the grid, if it is wide enough to mean something.
        if (zoomDrag.getWidth() > 8)
        {
            const auto fromBeat = xToBeat(zoomDrag.getX());
            const auto toBeat = xToBeat(zoomDrag.getRight());
            const auto beats = juce::jmax(0.5, toBeat - fromBeat);

            zoomSlider.setValue(juce::jlimit(24.0, 320.0,
                                             getGridArea().getWidth() / (beats / transport.getBeatsPerBar())));
            scrollBeats = juce::jmax(0.0, fromBeat);
        }

        zoomDrag = {};
        repaint();
        return;
    }

    if (clipDrag.mode == ClipDragMode::none)
        return;

    clipDrag = {};
    notifyClipEdited();
}

void ArrangementView::mouseDrag(const juce::MouseEvent& event)
{
    if (resizingRow >= 0)
    {
        setRowHeight(resizingRow, resizeStartHeight + event.getPosition().y - resizeGrabY);
        clampScroll();
        repaint();
        return;
    }

    if (painting)
    {
        paintSweep(event.getPosition());
        return;
    }

    if (marqueeActive)
    {
        marquee = juce::Rectangle<int>(event.getMouseDownPosition(), event.getPosition());
        repaint();
        return;
    }

    if (activeTool == Tool::zoom && ! zoomDrag.isEmpty())
    {
        const auto left = juce::jmin(event.getMouseDownPosition().x, event.getPosition().x);
        const auto right = juce::jmax(event.getMouseDownPosition().x, event.getPosition().x);
        zoomDrag = juce::Rectangle<int>(left, getGridArea().getY(), right - left, getGridArea().getHeight());
        repaint();
        return;
    }

    if (activeTool == Tool::slip && clipDrag.mode != ClipDragMode::none)
    {
        // Slip slides the audio or the pattern inside a clip that stays put.
        const auto tempo = transport.getTempoBpm();
        const auto deltaBeats = xToBeat(event.getPosition().x) - clipDrag.grabBeat;
        clipDrag.grabBeat = xToBeat(event.getPosition().x);

        if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(clipDrag.trackIndex)))
        {
            auto placement = midiTrack->getPlacement(clipDrag.clipIndex);
            placement.sourceOffsetBeats = juce::jmax(0.0, placement.sourceOffsetBeats - deltaBeats);
            midiTrack->updatePlacement(clipDrag.clipIndex, placement);
        }
        else if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(clipDrag.trackIndex)))
        {
            if (auto* clip = audioTrack->getClip(clipDrag.clipIndex))
            {
                const auto deltaSeconds = deltaBeats * (60.0 / tempo);
                clip->setSourceOffsetSeconds(clip->getSourceOffsetSeconds() - deltaSeconds);
            }
        }

        repaint();
        return;
    }

    // A grabbed clip that belongs to a multi clip selection carries the rest
    // with it. Trims stay single: only the grabbed edge moves.
    if (clipDrag.mode == ClipDragMode::move
        && selectedClips.size() > 1
        && isClipSelected(clipDrag.trackIndex, clipDrag.clipIndex))
    {
        const auto pointerBeat = xToBeat(event.getPosition().x);
        const auto target = snapBeat(clipDrag.originalStart + pointerBeat - clipDrag.grabBeat);
        moveSelectedClips(target - clipDrag.originalStart);
        repaint();
        return;
    }

    if (clipDrag.mode != ClipDragMode::none)
    {
        pushUndo(clipDrag.mode == ClipDragMode::move ? "Geser clip" : "Trim clip");

        const auto tempo = transport.getTempoBpm();
        const auto pointerBeat = xToBeat(event.getPosition().x);
        const auto deltaBeats = pointerBeat - clipDrag.grabBeat;

        if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(clipDrag.trackIndex)))
        {
            // Edit the placement itself, never the pattern's notes: one pattern
            // can be laid out many times, each with its own trim.
            auto placement = midiTrack->getPlacement(clipDrag.clipIndex);

            switch (clipDrag.mode)
            {
                case ClipDragMode::move:
                    placement.startBeat = snapBeat(clipDrag.originalStart + deltaBeats);
                    break;

                case ClipDragMode::trimStart:
                    placement.trimStart(snapBeat(clipDrag.originalStart + deltaBeats));
                    break;

                case ClipDragMode::trimEnd:
                    placement.trimEnd(snapBeat(clipDrag.originalEnd + deltaBeats));
                    break;

                case ClipDragMode::none:
                default:
                    return;
            }

            midiTrack->updatePlacement(clipDrag.clipIndex, placement);
            repaint();
            return;
        }

        auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(clipDrag.trackIndex));
        auto* clip = audioTrack != nullptr ? audioTrack->getClip(clipDrag.clipIndex) : nullptr;

        if (clip == nullptr)
            return;

        switch (clipDrag.mode)
        {
            case ClipDragMode::move:
                clip->setStartBeat(snapBeat(clipDrag.originalStart + deltaBeats));
                break;

            case ClipDragMode::trimStart:
                clip->trimStart(snapBeat(clipDrag.originalStart + deltaBeats), tempo);
                break;

            case ClipDragMode::trimEnd:
                clip->trimEnd(snapBeat(clipDrag.originalEnd + deltaBeats), tempo);
                break;

            case ClipDragMode::none:
            default:
                break;
        }

        repaint();
        return;
    }

    if (event.getMouseDownPosition().x < getGridArea().getX())
        return;

    transport.setPositionBeats(juce::jmax(0.0, xToBeat(event.getPosition().x)));
    repaint();
}

void ArrangementView::mouseDoubleClick(const juce::MouseEvent& event)
{
    int trackIndex = -1;
    int clipIndex = -1;

    if (hitTestClip(event.getPosition(), trackIndex, clipIndex) == ClipDragMode::none)
        return;

    auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));

    // Audio clips have no note editor to open.
    if (midiTrack == nullptr)
        return;

    // A drag may have been armed by the first click; the double click cancels it.
    clipDrag = {};
    setSelectedTrack(trackIndex);

    if (clipOpenRequestCallback)
        clipOpenRequestCallback(trackIndex, midiTrack->getPlacement(clipIndex).patternIndex);
}

bool ArrangementView::keyPressed(const juce::KeyPress& key)
{
    if (selectedClips.empty())
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedClips();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        clearClipSelection();
        return true;
    }

    // Everything else belongs to the window: space, record, save.
    return false;
}

void ArrangementView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        zoomSlider.setValue(pixelsPerBar * (wheel.deltaY > 0.0f ? 1.12 : 0.89));
        return;
    }

    if (event.mods.isShiftDown())
    {
        scrollBeats = juce::jmax(0.0, scrollBeats - wheel.deltaY * 8.0);
        repaint();
        return;
    }

    scrollY -= juce::roundToInt(wheel.deltaY * 60.0f);
    clampScroll();
    repaint();
}

void ArrangementView::timerCallback()
{
    if (followPlayhead && transport.isPlaying())
    {
        const auto grid = getGridArea();
        const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();
        const auto visibleBeats = grid.getWidth() / juce::jmax(1.0, pixelsPerBeat);
        const auto position = transport.getPositionBeats();

        if (position < scrollBeats || position > scrollBeats + visibleBeats * 0.85)
            scrollBeats = juce::jmax(0.0, position - visibleBeats * 0.15);
    }

    repaint();
}

void ArrangementView::buttonClicked(juce::Button* button)
{
    static const Tool order[] = { Tool::select, Tool::paint, Tool::erase, Tool::mute,
                                  Tool::slip, Tool::slice, Tool::zoom, Tool::playback };

    for (int i = 0; i < toolButtons.size(); ++i)
    {
        if (button == toolButtons[i])
        {
            setTool(order[i]);
            return;
        }
    }

    if (button == &snapButton)
    {
        setSnapEnabled(! snapEnabled);
        return;
    }

    if (button == &addTrackButton)
    {
        showAddTrackMenu();
        return;
    }

    if (button == &followButton)
    {
        setFollowPlayhead(! followPlayhead);
        return;
    }

    if (button == &zoomFitButton)
        zoomToFit();
}

void ArrangementView::showAddTrackMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Track MIDI baru");
    menu.addItem(2, "Track Audio baru");

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&addTrackButton)
                           .withMinimumWidth(150)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (result != 1 && result != 2)
                return;

            const auto number = mixer.getNumTracks() + 1;
            auto* added = result == 1
                ? mixer.addTrack(std::make_unique<MidiTrack>("MIDI " + juce::String(number)))
                : mixer.addTrack(std::make_unique<AudioTrack>("Audio " + juce::String(number)));

            if (added == nullptr)
                return;

            notifyTrackListChanged();
            setSelectedTrack(mixer.getNumTracks() - 1);
        });
}

void ArrangementView::showTrackContextMenu(int trackIndex)
{
    auto* track = mixer.getTrack(trackIndex);
    if (track == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(track->getName());
    menu.addItem(1, "Hapus track", mixer.getNumTracks() > 1);
    menu.addSeparator();
    menu.addItem(2, "Monitor input", true, track->isInputMonitoring());
    menu.addItem(3, "Hapus semua plugin", track->getPluginCount() > 0);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withMinimumWidth(160)
                           .withStandardItemHeight(21),
        [this, trackIndex] (int result)
        {
            auto* selected = mixer.getTrack(trackIndex);
            if (selected == nullptr)
                return;

            switch (result)
            {
                case 1:
                    if (mixer.removeTrack(trackIndex))
                    {
                        selectedTrack = juce::jlimit(0, juce::jmax(0, mixer.getNumTracks() - 1), selectedTrack);
                        notifyTrackListChanged();
                    }
                    break;

                case 2:
                    selected->setInputMonitoring(! selected->isInputMonitoring());
                    break;

                case 3:
                    selected->clearPlugins();
                    notifyTrackListChanged();
                    break;

                default:
                    break;
            }

            repaint();
        });
}

void ArrangementView::notifyTrackListChanged()
{
    // Track indices move when the list changes, so the selection cannot be
    // trusted to still point at the same clips.
    clearClipSelection();
    clampScroll();
    repaint();

    if (trackListChangedCallback)
        trackListChangedCallback();
}

void ArrangementView::setTrackListChangedCallback(std::function<void()> callback)
{
    trackListChangedCallback = std::move(callback);
}

void ArrangementView::sliderValueChanged(juce::Slider* slider)
{
    if (slider != &zoomSlider)
        return;

    pixelsPerBar = zoomSlider.getValue();
    repaint();
}

juce::Rectangle<int> ArrangementView::getGridArea() const
{
    return getLocalBounds()
        .withTrimmedTop(panelHeaderHeight + rulerHeight)
        .withTrimmedLeft(headerWidth)
        .withTrimmedRight(1)
        .withTrimmedBottom(1);
}

juce::Rectangle<int> ArrangementView::getRowBounds(int trackIndex) const
{
    return { 0, getRowTop(trackIndex), getWidth(), getRowHeight(trackIndex) };
}

int ArrangementView::getRowHeight(int trackIndex) const
{
    return juce::isPositiveAndBelow(trackIndex, static_cast<int>(rowHeights.size()))
        ? rowHeights[static_cast<size_t>(trackIndex)]
        : defaultRowHeight;
}

void ArrangementView::setRowHeight(int trackIndex, int height)
{
    if (trackIndex < 0)
        return;

    if (static_cast<int>(rowHeights.size()) <= trackIndex)
        rowHeights.resize(static_cast<size_t>(trackIndex) + 1, defaultRowHeight);

    // Zero is what a project without a stored height looks like: use the default
    // rather than clamping it up to the minimum.
    rowHeights[static_cast<size_t>(trackIndex)] = height <= 0
        ? defaultRowHeight
        : juce::jlimit(minRowHeight, maxRowHeight, height);

    clampScroll();
    repaint();
}

int ArrangementView::getRowTop(int trackIndex) const
{
    auto top = panelHeaderHeight + rulerHeight - scrollY;

    for (int i = 0; i < trackIndex; ++i)
        top += getRowHeight(i);

    return top;
}

int ArrangementView::hitTestRowResize(juce::Point<int> position) const
{
    // Only from the headers: inside the grid that band belongs to the clips.
    if (position.x >= headerWidth || position.y < panelHeaderHeight + rulerHeight)
        return -1;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        const auto bottom = getRowBounds(i).getBottom();

        if (std::abs(position.y - bottom) <= rowEdgeGrab)
            return i;
    }

    return -1;
}

juce::Rectangle<int> ArrangementView::getMuteBounds(int trackIndex) const
{
    const auto row = getRowBounds(trackIndex);
    return { headerWidth - 5 - 30, row.getCentreY() - 6, 14, 12 };
}

juce::Rectangle<int> ArrangementView::getSoloBounds(int trackIndex) const
{
    const auto row = getRowBounds(trackIndex);
    return { headerWidth - 5 - 14, row.getCentreY() - 6, 14, 12 };
}

double ArrangementView::xToBeat(int x) const
{
    const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();
    return scrollBeats + (x - getGridArea().getX()) / juce::jmax(1.0, pixelsPerBeat);
}

int ArrangementView::beatToX(double beat) const
{
    const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();
    return getGridArea().getX() + juce::roundToInt((beat - scrollBeats) * pixelsPerBeat);
}

int ArrangementView::getContentHeight() const
{
    auto total = 0;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        total += getRowHeight(i);

    return total;
}

void ArrangementView::clampScroll()
{
    const auto viewportHeight = juce::jmax(0, getHeight() - panelHeaderHeight - rulerHeight);
    scrollY = juce::jlimit(0, juce::jmax(0, getContentHeight() - viewportHeight), scrollY);
}

void ArrangementView::zoomToFit()
{
    double lastBeat = 16.0;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        for (const auto& clip : getClipsForTrack(i))
            lastBeat = juce::jmax(lastBeat, clip.startBeat + clip.lengthBeats);

    const auto grid = juce::jmax(120, getGridArea().getWidth());
    scrollBeats = 0.0;
    zoomSlider.setValue(juce::jlimit(24.0, 320.0, grid / (lastBeat / transport.getBeatsPerBar()) * 0.95));
}

std::vector<ArrangementView::Clip> ArrangementView::getClipsForTrack(int trackIndex) const
{
    std::vector<Clip> clips;

    const auto* track = mixer.getTrack(trackIndex);
    if (track == nullptr)
        return clips;

    if (const auto* midiTrack = dynamic_cast<const MidiTrack*>(track))
    {
        // The timeline always shows the placements, whichever mode the transport
        // is in. Pattern and Song only decide what plays, not what is laid out.
        auto index = 0;

        for (const auto& placement : midiTrack->getPlacements())
        {
            Clip songClip;
            songClip.startBeat = placement.startBeat;
            songClip.lengthBeats = placement.lengthBeats;
            songClip.label = patternLabel(placement.patternIndex)
                           + (placement.sourceOffsetBeats > 0.0
                                  ? " +" + juce::String(placement.sourceOffsetBeats, 2)
                                  : juce::String());
            songClip.midi = true;
            songClip.muted = placement.muted;
            songClip.index = index++;
            fillClipNotes(songClip,
                          midiTrack->getClip(placement.patternIndex).getNotesSnapshot(),
                          placement.sourceOffsetBeats,
                          placement.sourceOffsetBeats + placement.lengthBeats);
            clips.push_back(songClip);
        }

        return clips;
    }

    if (const auto* audioTrack = dynamic_cast<const AudioTrack*>(track))
    {
        const auto tempo = transport.getTempoBpm();
        auto index = 0;

        for (const auto* source : audioTrack->getClipsSnapshot())
        {
            if (source == nullptr)
            {
                ++index;
                continue;
            }

            Clip clip;
            clip.startBeat = source->getStartBeat();
            clip.lengthBeats = source->getLengthBeats(tempo);
            clip.label = source->getName();
            clip.midi = false;
            clip.index = index++;
            clip.peaks = &source->getPeaks();
            clip.trimStartFraction = source->getTrimStartFraction();
            clip.trimEndFraction = source->getTrimEndFraction();
            clip.warped = source->isWarpEnabled();
            clip.muted = source->isMuted();
            clips.push_back(clip);
        }
    }

    return clips;
}

void ArrangementView::fillClipNotes(Clip& clip,
                                    const juce::Array<MidiNote>& notes,
                                    double windowStartBeat,
                                    double windowEndBeat)
{
    clip.notes.clear();

    auto lowest = 127;
    auto highest = 0;

    for (const auto& note : notes)
    {
        const auto noteEnd = note.startBeat + note.lengthBeats;

        // Only what the clip actually plays: a trimmed placement shows a slice.
        if (note.startBeat >= windowEndBeat || noteEnd <= windowStartBeat)
            continue;

        const auto clippedStart = juce::jmax(note.startBeat, windowStartBeat);
        const auto clippedEnd = juce::jmin(noteEnd, windowEndBeat);

        clip.notes.push_back({ clippedStart - windowStartBeat,
                               juce::jmax(0.02, clippedEnd - clippedStart),
                               note.pitch });

        lowest = juce::jmin(lowest, note.pitch);
        highest = juce::jmax(highest, note.pitch);
    }

    if (clip.notes.empty())
        return;

    // A single pitch would divide by zero, so give it a little room either side.
    if (highest - lowest < 2)
    {
        lowest = juce::jmax(0, lowest - 1);
        highest = juce::jmin(127, highest + 1);
    }

    clip.lowestPitch = lowest;
    clip.highestPitch = highest;
}

juce::Rectangle<int> ArrangementView::getClipBounds(int trackIndex, const Clip& clip) const
{
    const auto row = getRowBounds(trackIndex);
    const auto left = beatToX(clip.startBeat);
    const auto width = juce::jmax(20, beatToX(clip.startBeat + clip.lengthBeats) - left);

    return { left, row.getY() + 3, width, row.getHeight() - 6 };
}

ArrangementView::ClipDragMode ArrangementView::hitTestClip(juce::Point<int> position,
                                                           int& trackIndexOut,
                                                           int& clipIndexOut) const
{
    trackIndexOut = -1;
    clipIndexOut = -1;

    if (position.x < getGridArea().getX())
        return ClipDragMode::none;

    for (int track = 0; track < mixer.getNumTracks(); ++track)
    {
        if (! getRowBounds(track).contains(position))
            continue;

        for (const auto& clip : getClipsForTrack(track))
        {
            const auto bounds = getClipBounds(track, clip);

            if (! bounds.contains(position))
                continue;

            trackIndexOut = track;
            clipIndexOut = clip.index;

            if (position.x <= bounds.getX() + clipEdgeGrab)
                return ClipDragMode::trimStart;

            if (position.x >= bounds.getRight() - clipEdgeGrab)
                return ClipDragMode::trimEnd;

            return ClipDragMode::move;
        }

        break;
    }

    return ClipDragMode::none;
}

double ArrangementView::getGridStepBeats() const noexcept
{
    // Below this the lines merge into a smear and cost paint time for nothing.
    constexpr double minimumLineSpacing = 5.0;

    const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();

    // Line means "whatever the grid can show", so it starts from the finest
    // division and opens up until the lines are far enough apart to read. The
    // other entries draw at their own length. None still needs a grid, so it
    // falls back to beats.
    auto step = snapUnit == SnapUnit::line   ? stepBeats / 4.0
              : snapUnit == SnapUnit::cell   ? transport.getBeatsPerBar()
              : snapUnit == SnapUnit::none   ? 1.0
                                             : getSnapUnitBeats(snapUnit);

    if (step <= 0.0)
        step = 1.0;

    while (step * pixelsPerBeat < minimumLineSpacing && step < transport.getBeatsPerBar())
        step *= 2.0;

    return step;
}

double ArrangementView::getEffectiveSnapBeats() const noexcept
{
    if (! snapEnabled || snapUnit == SnapUnit::none)
        return 0.0;

    // Line snaps to the lines actually on screen, so it tracks the zoom.
    if (snapUnit == SnapUnit::line)
        return getGridStepBeats();

    // A playlist cell is one bar.
    if (snapUnit == SnapUnit::cell)
        return transport.getBeatsPerBar();

    return getSnapUnitBeats(snapUnit);
}

double ArrangementView::snapBeat(double beat) const noexcept
{
    const auto snap = getEffectiveSnapBeats();

    if (snap <= 0.0)
        return juce::jmax(0.0, beat);

    return juce::jmax(0.0, std::round(beat / snap) * snap);
}

void ArrangementView::setSnapUnit(SnapUnit unit)
{
    if (snapUnit == unit)
        return;

    // The grid is drawn from this, so the view has to follow the setting.
    snapUnit = unit;
    repaint();
}

void ArrangementView::buildToolButtons()
{
    static const ToolButton definitions[] = {
        { Tool::select,   Icon::marquee,     "Select - geser & trim clip" },
        { Tool::paint,    Icon::pencil,      "Paint - taruh clip" },
        { Tool::erase,    Icon::eraser,      "Delete - hapus clip" },
        { Tool::mute,     Icon::speakerMute, "Mute - bisukan clip" },
        { Tool::slip,     Icon::slip,        "Slip - geser isi di dalam clip" },
        { Tool::slice,    Icon::slice,       "Slice - potong clip jadi dua" },
        { Tool::zoom,     Icon::zoom,        "Zoom - tarik area untuk memperbesar" },
        { Tool::playback, Icon::play,        "Playback - klik untuk memutar dari situ" }
    };

    for (const auto& definition : definitions)
    {
        auto* button = toolButtons.add(new IconChipButton(definition.tooltip, definition.icon));
        // The default inset leaves a 6px glyph on an 18px chip - far too small
        // to tell a razor from a magnet.
        button->setIconInset(3.5f);
        button->addListener(this);
        addAndMakeVisible(button);
    }

    refreshToolButtons();
}

void ArrangementView::refreshToolButtons()
{
    static const Tool order[] = { Tool::select, Tool::paint, Tool::erase, Tool::mute,
                                  Tool::slip, Tool::slice, Tool::zoom, Tool::playback };

    for (int i = 0; i < toolButtons.size(); ++i)
        if (auto* button = toolButtons[i])
            button->setHighlighted(order[i] == activeTool);

    snapButton.setHighlighted(snapEnabled);
    snapButton.setTooltip(snapEnabled ? "Snap aktif - klik untuk mematikan"
                                      : "Snap mati - klik untuk menyalakan");
}

void ArrangementView::setTool(Tool tool)
{
    if (activeTool == tool)
        return;

    activeTool = tool;
    clipDrag = {};
    zoomDrag = {};
    painting = false;
    marqueeActive = false;
    marquee = {};
    clearClipSelection();
    clearSlicePreview();
    refreshToolButtons();
    repaint();
}

ArrangementView::Tool ArrangementView::getTool() const noexcept
{
    return activeTool;
}

void ArrangementView::setSnapEnabled(bool shouldSnap)
{
    if (snapEnabled == shouldSnap)
        return;

    snapEnabled = shouldSnap;
    refreshToolButtons();
}

bool ArrangementView::isSnapEnabled() const noexcept
{
    return snapEnabled;
}

void ArrangementView::setActivePattern(int patternIndex) noexcept
{
    activePattern = juce::jlimit(0, 15, patternIndex);
    repaint();
}

void ArrangementView::setPatternLengthBeats(double beats) noexcept
{
    patternLengthBeats = juce::jmax(1.0, beats);
    repaint();
}

void ArrangementView::setClipOpenRequestCallback(std::function<void(int, int)> callback)
{
    clipOpenRequestCallback = std::move(callback);
}

void ArrangementView::setUndoHooks(std::function<void(const juce::String&)> push,
                                   std::function<void(bool)> gesture)
{
    pushUndoCallback = std::move(push);
    undoGestureCallback = std::move(gesture);
}

void ArrangementView::pushUndo(const juce::String& actionName)
{
    if (pushUndoCallback)
        pushUndoCallback(actionName);
}

void ArrangementView::setPatternNameProvider(std::function<juce::String(int)> provider)
{
    patternNameProvider = std::move(provider);
    repaint();
}

void ArrangementView::setPatternLengthProvider(std::function<double(int)> provider)
{
    patternLengthProvider = std::move(provider);
    repaint();
}

void ArrangementView::setPatternRenameCallback(std::function<void(int)> callback)
{
    patternRenameCallback = std::move(callback);
}

double ArrangementView::patternLengthFor(int patternIndex) const
{
    if (patternLengthProvider)
        if (const auto beats = patternLengthProvider(patternIndex); beats > 0.0)
            return beats;

    return patternLengthBeats;
}

juce::String ArrangementView::patternLabel(int patternIndex) const
{
    if (patternNameProvider)
        return patternNameProvider(patternIndex);

    return "PAT " + juce::String(patternIndex + 1);
}

void ArrangementView::setClipEditedCallback(std::function<void()> callback)
{
    clipEditedCallback = std::move(callback);
}

void ArrangementView::notifyClipEdited()
{
    repaint();

    if (clipEditedCallback)
        clipEditedCallback();
}

bool ArrangementView::isClipSelected(int trackIndex, int clipIndex) const
{
    for (const auto& ref : selectedClips)
        if (ref.trackIndex == trackIndex && ref.clipIndex == clipIndex)
            return true;

    return false;
}

void ArrangementView::clearClipSelection()
{
    if (selectedClips.empty())
        return;

    selectedClips.clear();
    repaint();
}

void ArrangementView::toggleClipSelection(int trackIndex, int clipIndex)
{
    for (auto it = selectedClips.begin(); it != selectedClips.end(); ++it)
    {
        if (it->trackIndex == trackIndex && it->clipIndex == clipIndex)
        {
            selectedClips.erase(it);
            repaint();
            return;
        }
    }

    selectedClips.push_back({ trackIndex, clipIndex, 0.0 });
    repaint();
}

void ArrangementView::selectClipsInMarquee()
{
    for (int track = 0; track < mixer.getNumTracks(); ++track)
        for (const auto& clip : getClipsForTrack(track))
            if (getClipBounds(track, clip).intersects(marquee) && ! isClipSelected(track, clip.index))
                selectedClips.push_back({ track, clip.index, 0.0 });

    repaint();
}

bool ArrangementView::getClipStartBeat(int trackIndex, int clipIndex, double& startBeatOut) const
{
    if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex)))
    {
        const auto placements = midiTrack->getPlacements();

        if (! juce::isPositiveAndBelow(clipIndex, static_cast<int>(placements.size())))
            return false;

        startBeatOut = placements[static_cast<size_t>(clipIndex)].startBeat;
        return true;
    }

    if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex)))
    {
        if (auto* clip = audioTrack->getClip(clipIndex))
        {
            startBeatOut = clip->getStartBeat();
            return true;
        }
    }

    return false;
}

void ArrangementView::setClipStartBeat(int trackIndex, int clipIndex, double startBeat)
{
    if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex)))
    {
        // An out of range index comes back as a default placement, and the write
        // below is refused, so a stale selection cannot corrupt anything.
        auto placement = midiTrack->getPlacement(clipIndex);
        placement.startBeat = juce::jmax(0.0, startBeat);
        midiTrack->updatePlacement(clipIndex, placement);
        return;
    }

    if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex)))
        if (auto* clip = audioTrack->getClip(clipIndex))
            clip->setStartBeat(juce::jmax(0.0, startBeat));
}

void ArrangementView::beginGroupDrag()
{
    for (auto& ref : selectedClips)
        if (! getClipStartBeat(ref.trackIndex, ref.clipIndex, ref.dragOriginBeat))
            ref.dragOriginBeat = 0.0;
}

void ArrangementView::moveSelectedClips(double deltaBeats)
{
    pushUndo("Geser clip terpilih");

    auto applied = deltaBeats;

    // The group keeps its shape: whatever sits closest to bar one sets the limit.
    for (const auto& ref : selectedClips)
        applied = juce::jmax(applied, -ref.dragOriginBeat);

    for (const auto& ref : selectedClips)
        setClipStartBeat(ref.trackIndex, ref.clipIndex, ref.dragOriginBeat + applied);
}

void ArrangementView::deleteSelectedClips()
{
    if (selectedClips.empty())
        return;

    pushUndo(selectedClips.size() > 1 ? "Hapus clip terpilih" : "Hapus clip");

    // Removing a clip shifts the indices behind it, so delete back to front.
    auto ordered = selectedClips;
    std::sort(ordered.begin(), ordered.end(), [] (const ClipRef& a, const ClipRef& b)
    {
        return a.trackIndex != b.trackIndex ? a.trackIndex < b.trackIndex
                                            : a.clipIndex > b.clipIndex;
    });

    for (const auto& ref : ordered)
    {
        if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(ref.trackIndex)))
            midiTrack->removePlacementAt(ref.clipIndex);
        else if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(ref.trackIndex)))
            audioTrack->removeClip(ref.clipIndex);
    }

    selectedClips.clear();
    notifyClipEdited();
}

void ArrangementView::clearSlicePreview()
{
    if (sliceTrack < 0)
        return;

    sliceTrack = -1;
    sliceClipIndex = -1;
    repaint();
}

void ArrangementView::updateSlicePreview(juce::Point<int> position)
{
    if (activeTool != Tool::slice)
    {
        clearSlicePreview();
        return;
    }

    int trackIndex = -1;
    int clipIndex = -1;

    if (hitTestClip(position, trackIndex, clipIndex) == ClipDragMode::none)
    {
        clearSlicePreview();
        return;
    }

    const auto beat = snapBeat(xToBeat(position.x));

    // Only show a line where the cut would actually be accepted, so no line
    // means no cut rather than a click that quietly does nothing.
    auto legal = false;

    if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex)))
        legal = midiTrack->getPlacement(clipIndex).canSplitAt(beat);
    else if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex)))
        legal = audioTrack->canSliceClip(clipIndex, beat, transport.getTempoBpm());

    if (! legal)
    {
        clearSlicePreview();
        return;
    }

    if (sliceTrack == trackIndex && sliceClipIndex == clipIndex && std::abs(sliceBeat - beat) < 1.0e-9)
        return;

    sliceTrack = trackIndex;
    sliceClipIndex = clipIndex;
    sliceBeat = beat;
    repaint();
}

void ArrangementView::paintSweep(juce::Point<int> position)
{
    // A gap exactly one clip wide only accepts a clip when the pointer lands
    // inside it, and that window can be a few pixels. Moving the mouse quickly
    // jumps clean over it, so walk every snap step between the last position
    // and this one instead of only sampling where the pointer ended up.
    const auto snap = getEffectiveSnapBeats();
    const auto fromBeat = xToBeat(lastPaintPosition.x);
    const auto toBeat = xToBeat(position.x);
    const auto step = snap > 0.0 ? snap : 0.25;
    const auto distance = std::abs(toBeat - fromBeat);

    // Guard against a huge jump turning into thousands of steps.
    const auto stepCount = juce::jmin(256, static_cast<int>(distance / step));

    for (int i = 1; i <= stepCount; ++i)
    {
        const auto beat = fromBeat + (toBeat > fromBeat ? i * step : -i * step);
        paintPatternAt({ beatToX(beat), position.y });
    }

    paintPatternAt(position);
    lastPaintPosition = position;
}

bool ArrangementView::paintPatternAt(juce::Point<int> position)
{
    // Row bounds run the full width, so without this a stroke that wanders into
    // the track headers would keep dropping clips at beat zero.
    if (! getGridArea().contains(position))
        return false;

    for (int track = 0; track < mixer.getNumTracks(); ++track)
    {
        if (! getRowBounds(track).contains(position))
            continue;

        auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(track));

        if (midiTrack == nullptr)
            return false;

        setSelectedTrack(track);

        PatternPlacement placement;
        placement.patternIndex = activePattern;
        placement.startBeat = snapBeat(xToBeat(position.x));
        placement.lengthBeats = patternLengthBeats;

        // Sweeping back over what was just painted must not stack a second copy.
        const auto freeSpan = midiTrack->getFreeSpanFrom(placement.startBeat);

        if (freeSpan <= 0.0)
            return false;

        // A gap narrower than the pattern gets a clip cut to fit, like FL, rather
        // than nothing at all.
        placement.lengthBeats = juce::jmin(placement.lengthBeats, freeSpan);

        if (placement.lengthBeats < PatternPlacement::minimumLengthBeats)
            return false;

        pushUndo("Taruh clip");
        midiTrack->addPlacement(placement);
        notifyClipEdited();
        return true;
    }

    return false;
}

bool ArrangementView::applyToolToClip(int trackIndex, int clipIndex, double beat, ClipDragMode mode)
{
    auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));
    auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex));
    const auto tempo = transport.getTempoBpm();

    switch (activeTool)
    {
        case Tool::erase:
            pushUndo("Hapus clip");

            if (midiTrack != nullptr)
                midiTrack->removePlacementAt(clipIndex);
            else if (audioTrack != nullptr)
                audioTrack->removeClip(clipIndex);
            else
                return false;

            // Indices behind the removed clip shift, so the selection is stale.
            clearClipSelection();
            notifyClipEdited();
            return true;

        case Tool::mute:
            pushUndo("Mute clip");

            if (midiTrack != nullptr)
            {
                auto placement = midiTrack->getPlacement(clipIndex);
                placement.muted = ! placement.muted;
                midiTrack->updatePlacement(clipIndex, placement);
            }
            else if (audioTrack != nullptr)
            {
                if (auto* clip = audioTrack->getClip(clipIndex))
                    clip->setMuted(! clip->isMuted());
                else
                    return false;
            }
            else
            {
                return false;
            }

            notifyClipEdited();
            return true;

        case Tool::slice:
            pushUndo("Potong clip");

            if (midiTrack != nullptr)
            {
                auto placement = midiTrack->getPlacement(clipIndex);
                PatternPlacement right;

                if (! placement.splitAt(snapBeat(beat), right))
                    return false;

                midiTrack->updatePlacement(clipIndex, placement);
                midiTrack->addPlacement(right);
            }
            else if (audioTrack != nullptr)
            {
                if (! audioTrack->sliceClip(clipIndex, snapBeat(beat), tempo))
                    return false;
            }
            else
            {
                return false;
            }

            // Slicing adds a clip, so anything selected behind it has moved, and
            // the preview line now points at a clip that no longer exists.
            clearClipSelection();
            clearSlicePreview();
            notifyClipEdited();
            return true;

        case Tool::slip:
            pushUndo("Slip clip");
            // Handled as a drag, not a click.
            clipDrag.mode = ClipDragMode::move;
            clipDrag.trackIndex = trackIndex;
            clipDrag.clipIndex = clipIndex;
            clipDrag.grabBeat = beat;
            return true;

        case Tool::select:
        case Tool::paint:
        case Tool::zoom:
        case Tool::playback:
        default:
            juce::ignoreUnused(mode);
            return false;
    }
}

void ArrangementView::showPlacementContextMenu(int trackIndex, int placementIndex)
{
    auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));

    if (midiTrack == nullptr)
        return;

    const auto placement = midiTrack->getPlacement(placementIndex);

    juce::PopupMenu menu;
    menu.addSectionHeader(patternLabel(placement.patternIndex));
    const auto fullLength = patternLengthFor(placement.patternIndex);
    menu.addItem(1, "Kembalikan panjang penuh",
                 placement.sourceOffsetBeats > 0.0 || placement.lengthBeats < fullLength - 1.0e-9);
    menu.addItem(2, "Panjangkan 1 bar");
    menu.addSeparator();
    menu.addItem(4, "Ganti nama pattern...");
    menu.addSeparator();
    menu.addItem(3, "Hapus penempatan");

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this, trackIndex, placementIndex] (int result)
        {
            auto* track = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));

            if (track == nullptr)
                return;

            auto edited = track->getPlacement(placementIndex);

            if (result >= 1 && result <= 3)
                pushUndo("Ubah clip");

            switch (result)
            {
                case 1:
                    // Undo the trim: back to the head of the pattern, four beats.
                    edited.startBeat = juce::jmax(0.0, edited.startBeat - edited.sourceOffsetBeats);
                    edited.sourceOffsetBeats = 0.0;
                    // The pattern's own loop length, not a fixed four beats:
                    // that was wrong for every signature except 4/4.
                    edited.lengthBeats = patternLengthFor(edited.patternIndex);
                    track->updatePlacement(placementIndex, edited);
                    break;

                case 2:
                    edited.lengthBeats += transport.getBeatsPerBar();
                    track->updatePlacement(placementIndex, edited);
                    break;

                case 3:
                    track->removePlacementAt(placementIndex);
                    break;

                case 4:
                    if (patternRenameCallback)
                        patternRenameCallback(edited.patternIndex);

                    return;

                default:
                    return;
            }

            notifyClipEdited();
        });
}

void ArrangementView::showClipContextMenu(int trackIndex, int clipIndex)
{
    auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex));
    auto* clip = audioTrack != nullptr ? audioTrack->getClip(clipIndex) : nullptr;

    if (clip == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(clip->getName());
    menu.addItem(1, "Warp ikut tempo", true, clip->isWarpEnabled());
    menu.addItem(2, "Kembalikan panjang penuh");
    menu.addSeparator();
    menu.addItem(3, "Hapus clip");

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(this)
                           .withMinimumWidth(180)
                           .withStandardItemHeight(21),
        [this, trackIndex, clipIndex] (int result)
        {
            auto* track = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex));
            auto* selected = track != nullptr ? track->getClip(clipIndex) : nullptr;

            if (selected == nullptr)
                return;

            if (result >= 1 && result <= 3)
                pushUndo(result == 3 ? "Hapus clip" : "Ubah clip");

            switch (result)
            {
                case 1:
                    selected->setWarpEnabled(! selected->isWarpEnabled());
                    break;

                case 2:
                    selected->trimEnd(selected->getStartBeat()
                                          + selected->getSourceLengthSeconds()
                                                * (transport.getTempoBpm() / 60.0),
                                      transport.getTempoBpm());
                    break;

                case 3:
                    track->removeClip(clipIndex);
                    break;

                default:
                    return;
            }

            notifyClipEdited();
        });
}

juce::String ArrangementView::describeKind(const Track& track, int trackIndex)
{
    switch (track.getKind())
    {
        case TrackKind::midi:       return "MIDI - CH " + juce::String(trackIndex + 1);
        case TrackKind::instrument: return "INSTRUMENT";
        case TrackKind::bus:        return "BUS";
        case TrackKind::audio:
        default:                    return "AUDIO";
    }
}

void ArrangementView::setSelectedTrack(int trackIndex)
{
    const auto clamped = juce::jlimit(0, juce::jmax(0, mixer.getNumTracks() - 1), trackIndex);
    if (clamped == selectedTrack)
        return;

    selectedTrack = clamped;

    if (trackSelectedCallback)
        trackSelectedCallback(selectedTrack);

    repaint();
}

int ArrangementView::getSelectedTrack() const noexcept
{
    return selectedTrack;
}

void ArrangementView::setTrackSelectedCallback(std::function<void(int)> callback)
{
    trackSelectedCallback = std::move(callback);
}

void ArrangementView::setFollowPlayhead(bool shouldFollow)
{
    followPlayhead = shouldFollow;
    followButton.setIcon(followPlayhead ? Icon::chevronRight : Icon::minimise);
    repaint();
}

bool ArrangementView::isFollowingPlayhead() const noexcept
{
    return followPlayhead;
}

} // namespace djr
