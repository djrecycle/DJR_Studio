#include "ArrangementView.h"

#include "BrowserPanel.h"
#include "Theme.h"
#include "audio/AudioTrack.h"
#include "audio/BusTrack.h"
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
    /** How near an automation point or tension handle counts as grabbing it. */
    constexpr int pointGrab = 6;
    /** A segment narrower than this has no room for a tension handle. */
    constexpr int curveHandleMinWidth = 22;
    /** Top and bottom margin of an automation lane's drawable band. */
    constexpr int curveInset = 6;
    /** Pixels of vertical drag that bend a segment all the way to full tension. */
    constexpr double curveDragTravel = 60.0;

    // Automation menu ids. Anything at or above the base is an encoded target,
    // so the track menu's own items stay small numbers and never collide.
    constexpr int automationMenuBase = 100;
    constexpr int automationMenuVolume = 100;
    constexpr int automationMenuPan = 101;
    constexpr int automationMenuPluginBase = 1000;
    /** Room for this many parameters per plugin inside one id. */
    constexpr int automationMenuSlotStride = 512;
    /** A menu longer than this stops being something anyone can read. */
    constexpr int maxAutomatableParameters = 64;
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
    addTrackButton.setTooltip(TRANS("Add track"));
    addAndMakeVisible(addTrackButton);

    followButton.setIconInset(4.0f);
    followButton.addListener(this);
    followButton.setTooltip(TRANS("Follow playhead during playback"));
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

    horizontalBar.onRangeChanged = [this] (double start, double size) { applyHorizontalRange(start, size); };
    verticalBar.onRangeChanged = [this] (double start, double size) { applyVerticalRange(start, size); };
    addAndMakeVisible(horizontalBar);
    addAndMakeVisible(verticalBar);

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
    rebuildRows();

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

    for (int rowIndex = 0; rowIndex < getRowCount(); ++rowIndex)
    {
        // A reference, not a copy: each row carries its curve snapshot, and
        // copying that per row per frame is the allocation this avoids.
        const auto& rowEntry = rows[static_cast<size_t>(rowIndex)];
        const auto i = rowEntry.trackIndex;

        const auto* track = mixer.getTrack(i);
        if (track == nullptr)
            continue;

        const auto row = getRowBoundsAt(rowIndex);
        if (! row.intersects(lanes))
            continue;

        if (rowEntry.automationLane >= 0)
        {
            juce::Graphics::ScopedSaveState automationState(g);
            g.reduceClipRegion(lanes.withTrimmedTop(rulerHeight));
            drawAutomationRow(g, rowIndex, rowEntry);
            continue;
        }

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

                // One pixel usually covers several buckets now, so it takes the
                // loudest of them rather than whichever one it happened to land
                // on. Sampling a single bucket per pixel drops peaks between
                // them, and a waveform that misses its own transients is worse
                // than a coarse one.
                for (int x = 0; x < body.getWidth(); ++x)
                {
                    const auto span = juce::jmax(1, body.getWidth());
                    const auto from = firstBucket + bucketSpan * x / span;
                    const auto to = firstBucket + bucketSpan * (x + 1) / span;

                    const auto firstIndex = juce::jlimit(0, bucketCount - 1, static_cast<int>(from));
                    const auto lastIndex = juce::jlimit(firstIndex, bucketCount - 1,
                                                        static_cast<int>(std::ceil(to)) - 1);

                    auto peak = 0.0f;

                    for (int bucket = firstIndex; bucket <= lastIndex; ++bucket)
                        peak = juce::jmax(peak, (*clip.peaks)[static_cast<size_t>(bucket)]);

                    const auto amplitude = peak * halfHeight;

                    g.drawVerticalLine(body.getX() + x,
                                       centreY - amplitude,
                                       centreY + juce::jmax(0.5f, amplitude));
                }

                // The fades, drawn as the wedge they take out of the waveform.
                // A fade you cannot see is one you forget you set.
                const auto drawFade = [&g, &body] (double fraction, bool fromLeft)
                {
                    if (fraction <= 0.0)
                        return;

                    const auto width = static_cast<float>(fraction) * body.getWidth();

                    if (width < 1.0f)
                        return;

                    juce::Path wedge;

                    // The wedge covers what the fade takes away: loudest at the
                    // outer edge of the clip, nothing by the time the ramp ends.
                    if (fromLeft)
                    {
                        wedge.startNewSubPath(static_cast<float>(body.getX()),
                                              static_cast<float>(body.getY()));
                        wedge.lineTo(body.getX() + width, static_cast<float>(body.getY()));
                        wedge.lineTo(static_cast<float>(body.getX()),
                                     static_cast<float>(body.getBottom()));
                    }
                    else
                    {
                        wedge.startNewSubPath(static_cast<float>(body.getRight()),
                                              static_cast<float>(body.getY()));
                        wedge.lineTo(body.getRight() - width, static_cast<float>(body.getY()));
                        wedge.lineTo(static_cast<float>(body.getRight()),
                                     static_cast<float>(body.getBottom()));
                    }

                    wedge.closeSubPath();
                    g.setColour(Theme::windowBackground().withAlpha(0.62f));
                    g.fillPath(wedge);

                    // The ramp itself, stroked. The shading alone reads as a
                    // smudge on a small clip; the line is what says "fade".
                    g.setColour(Theme::text().withAlpha(0.85f));
                    g.drawLine(fromLeft ? body.getX() : body.getRight(),
                               static_cast<float>(body.getBottom()),
                               fromLeft ? body.getX() + width : body.getRight() - width,
                               static_cast<float>(body.getY()),
                               1.2f);
                };

                drawFade(clip.fadeInFraction, true);
                drawFade(clip.fadeOutFraction, false);

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
        g.drawText(TRANS("No tracks yet"), grid, juce::Justification::centred, false);
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

    // Last, so the row a dragged file would land on is drawn over the clips
    // rather than under them.
    paintFileDropRow(g);
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

    auto body = getLocalBounds().withTrimmedTop(panelHeaderHeight);
    auto bottomStrip = body.removeFromBottom(ZoomScrollBar::thickness);
    auto rightStrip = body.removeFromRight(ZoomScrollBar::thickness);

    horizontalBar.setBounds(bottomStrip.withTrimmedLeft(headerWidth)
                                       .withTrimmedRight(ZoomScrollBar::thickness));
    verticalBar.setBounds(rightStrip.withTrimmedTop(rulerHeight));

    rebuildRows();
    clampScroll();
    refreshScrollBars();
}

void ArrangementView::mouseDown(const juce::MouseEvent& event)
{
    rebuildRows();

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
            resizeStartHeight = getRowHeightAt(resizeRow);
            resizeGrabY = position.y;
            return;
        }
    }

    const auto rowIndex = rowAt(position);

    if (rowIndex >= 0)
    {
        // Copied out as two ints rather than held as a reference into `rows`:
        // selecting a track calls out to the host, which may rebuild the list.
        const auto rowTrack = rows[static_cast<size_t>(rowIndex)].trackIndex;
        const auto rowLane = rows[static_cast<size_t>(rowIndex)].automationLane;
        auto* track = mixer.getTrack(rowTrack);

        // Mute and solo only exist on a track's own row; its automation lanes
        // sit underneath it and have their own header.
        if (track != nullptr && rowLane < 0)
        {
            if (getMuteBounds(rowTrack).contains(position))
            {
                track->setMuted(! track->isMuted());
                repaint();
                return;
            }

            if (getSoloBounds(rowTrack).contains(position))
            {
                track->setSoloed(! track->isSoloed());
                repaint();
                return;
            }
        }

        setSelectedTrack(rowTrack);

        if (event.mods.isRightButtonDown() && position.x < headerWidth)
        {
            if (rowLane >= 0)
                showAutomationMenu(rowTrack, rowLane, -1);
            else
                showTrackContextMenu(rowTrack);

            return;
        }

        // An automation lane holds no clips, so it takes the whole event -
        // unless the active tool is one that belongs to the timeline.
        if (rowLane >= 0 && position.x >= getGridArea().getX()
            && handleAutomationMouseDown(rowIndex, position, event.mods))
            return;
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
    rebuildRows();
    updateSlicePreview(event.getPosition());

    if (hitTestRowResize(event.getPosition()) >= 0)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    if (hitTestAutomation(event.getPosition()).mode != AutomationDrag::Mode::none)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
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

    if (automationDrag.mode != AutomationDrag::Mode::none)
    {
        automationDrag = {};
        notifyClipEdited();
        repaint();
        return;
    }

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
        setRowHeightAt(resizingRow, resizeStartHeight + event.getPosition().y - resizeGrabY);
        clampScroll();
        repaint();
        return;
    }

    if (automationDrag.mode != AutomationDrag::Mode::none)
    {
        auto* lane = getLane(automationDrag.trackIndex, automationDrag.laneIndex);

        if (lane == nullptr)
            return;

        if (automationDrag.mode == AutomationDrag::Mode::point)
        {
            // A point dragged past its neighbour changes places with it, so the
            // lane hands back where it ended up rather than the index we had.
            const auto index = lane->movePoint(automationDrag.pointIndex,
                                               snapBeat(xToBeat(event.getPosition().x)),
                                               valueFromY(automationDrag.rowIndex, event.getPosition().y));

            if (index >= 0)
                automationDrag.pointIndex = index;
        }
        else
        {
            // Positive tension sags a rising segment but lifts a falling one, so
            // the drag is flipped to keep "down" meaning "down" on screen.
            const auto travel = (event.getPosition().y - automationDrag.grabY) / curveDragTravel;
            lane->setPointCurve(automationDrag.pointIndex,
                                automationDrag.grabCurve + (automationDrag.falling ? -travel : travel));
        }

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
        pushUndo(clipDrag.mode == ClipDragMode::move ? TRANS("Move clip") : "Trim clip");

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
    rebuildRows();

    const auto position = event.getPosition();

    // Double clicking a track's name renames it, the way FL's playlist does.
    // Only on the track's own row: an automation lane is named by its parameter.
    if (position.x < headerWidth)
    {
        const auto rowIndex = rowAt(position);

        if (rowIndex >= 0 && rows[static_cast<size_t>(rowIndex)].automationLane < 0 && trackRenameCallback)
            trackRenameCallback(rows[static_cast<size_t>(rowIndex)].trackIndex);

        return;
    }

    int trackIndex = -1;
    int clipIndex = -1;

    if (hitTestClip(position, trackIndex, clipIndex) == ClipDragMode::none)
        return;

    auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));

    // A drag may have been armed by the first click; the double click cancels it.
    clipDrag = {};
    setSelectedTrack(trackIndex);

    // An audio clip has no notes to open, but it does have samples.
    if (midiTrack == nullptr)
    {
        if (audioClipOpenRequestCallback)
            audioClipOpenRequestCallback(trackIndex, clipIndex);

        return;
    }

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
        refreshScrollBars();
        repaint();
        return;
    }

    scrollY -= juce::roundToInt(wheel.deltaY * 60.0f);
    clampScroll();
    repaint();
}

void ArrangementView::timerCallback()
{
    rebuildRows();

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
    menu.addItem(1, TRANS("New MIDI track"));
    menu.addItem(2, TRANS("New audio track"));
    menu.addSeparator();
    menu.addItem(3, TRANS("New bus"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&addTrackButton)
                           .withMinimumWidth(150)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (result < 1 || result > 3)
                return;

            const auto number = mixer.getNumTracks() + 1;

            // A bus is named for what it is rather than numbered with the rest:
            // "Bus 7" says nothing, and a bus is picked from a routing menu by
            // its name.
            std::unique_ptr<Track> fresh;

            if (result == 1)
                fresh = std::make_unique<MidiTrack>("MIDI " + juce::String(number));
            else if (result == 2)
                fresh = std::make_unique<AudioTrack>("Audio " + juce::String(number));
            else
                fresh = std::make_unique<BusTrack>("Bus " + juce::String(number));

            auto* added = mixer.addTrack(std::move(fresh));

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

    juce::PopupMenu automationMenu;
    fillAutomationTargetMenu(automationMenu, trackIndex);

    juce::PopupMenu menu;
    menu.addSectionHeader(track->getName());
    menu.addItem(4, TRANS("Rename track..."));
    menu.addItem(1, TRANS("Delete track"), mixer.getNumTracks() > 1);
    menu.addSeparator();
    // Opens whatever the channel holds - a plugin, or nothing but the preview
    // synth a MIDI channel falls back on. An audio track has neither unless
    // something was loaded onto it, and the channel pages do not act on one.
    const auto hasChannel = track->getKind() == TrackKind::midi
                         || track->getKind() == TrackKind::instrument
                         || track->getPluginCount() > 0;
    menu.addItem(7, TRANS("Channel settings..."), hasChannel);
    menu.addItem(2, TRANS("Monitor input"), true, track->isInputMonitoring());
    menu.addItem(3, TRANS("Remove all plugins"), track->getPluginCount() > 0);
    menu.addSeparator();
    // A bus makes no sound of its own until something feeds it, so rendering one
    // on its own would only ever produce silence.
    const auto renderable = track->getKind() != TrackKind::bus;
    menu.addItem(5, track->isFrozen() ? TRANS("Unfreeze track") : TRANS("Freeze track"), renderable);
    menu.addItem(6, TRANS("Bounce to audio..."), renderable);
    menu.addSeparator();
    menu.addSubMenu(TRANS("Add automation"), automationMenu);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(160)
                           .withStandardItemHeight(21),
        [this, trackIndex] (int result)
        {
            auto* selected = mixer.getTrack(trackIndex);
            if (selected == nullptr)
                return;

            // Everything from the automation submenu is an encoded target.
            if (result >= automationMenuBase)
            {
                addAutomationTarget(trackIndex, result);
                return;
            }

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

                case 4:
                    if (trackRenameCallback)
                        trackRenameCallback(trackIndex);
                    break;

                case 5:
                    if (trackFreezeCallback)
                        trackFreezeCallback(trackIndex);
                    break;

                case 6:
                    if (trackBounceCallback)
                        trackBounceCallback(trackIndex);
                    break;

                case 7:
                    if (trackChannelCallback)
                        trackChannelCallback(trackIndex);
                    break;

                default:
                    break;
            }

            repaint();
        });
}

void ArrangementView::fillAutomationTargetMenu(juce::PopupMenu& menu, int trackIndex) const
{
    auto* track = mixer.getTrack(trackIndex);

    if (track == nullptr)
        return;

    menu.addItem(automationMenuVolume, "Volume");
    menu.addItem(automationMenuPan, "Pan");

    // The plugin's own parameters, one submenu per slot. Ids carry the slot and
    // the parameter index so the callback never has to rebuild this list.
    const auto addPluginSubMenu = [&menu] (juce::AudioPluginInstance& plugin, int slot)
    {
        const auto& parameters = plugin.getParameters();

        if (parameters.isEmpty())
            return;

        juce::PopupMenu parameterMenu;
        const auto shown = juce::jmin(parameters.size(), maxAutomatableParameters);

        for (int i = 0; i < shown; ++i)
        {
            auto name = parameters[i]->getName(28);
            parameterMenu.addItem(automationMenuPluginBase + (slot + 1) * automationMenuSlotStride + i,
                                  name.isEmpty() ? "Param " + juce::String(i + 1) : name);
        }

        if (parameters.size() > shown)
            parameterMenu.addItem(-1, juce::String(parameters.size() - shown) + " parameter lainnya", false);

        menu.addSubMenu(plugin.getName(), parameterMenu);
    };

    if (auto* instrument = track->getInstrument())
        addPluginSubMenu(*instrument, AutomationTarget::instrumentSlot);

    for (int slot = 0; slot < track->getPluginCount(); ++slot)
        if (auto* insert = track->getPlugin(slot))
            addPluginSubMenu(*insert, slot);
}

void ArrangementView::addAutomationTarget(int trackIndex, int menuId)
{
    auto* track = mixer.getTrack(trackIndex);

    if (track == nullptr)
        return;

    AutomationTarget target;
    auto current = 0.0;

    if (menuId == automationMenuVolume)
    {
        target.kind = AutomationTarget::Kind::trackVolume;
        target.label = "Volume";
        current = track->getVolume();
    }
    else if (menuId == automationMenuPan)
    {
        target.kind = AutomationTarget::Kind::trackPan;
        target.label = "Pan";
        current = track->getPan();
    }
    else if (menuId >= automationMenuPluginBase)
    {
        const auto encoded = menuId - automationMenuPluginBase;
        target.kind = AutomationTarget::Kind::pluginParameter;
        target.pluginSlot = encoded / automationMenuSlotStride - 1;
        target.parameterIndex = encoded % automationMenuSlotStride;

        auto* plugin = target.pluginSlot == AutomationTarget::instrumentSlot
            ? track->getInstrument()
            : track->getPlugin(target.pluginSlot);

        if (plugin == nullptr)
            return;

        const auto& parameters = plugin->getParameters();

        if (! juce::isPositiveAndBelow(target.parameterIndex, parameters.size()))
            return;

        auto* parameter = parameters[target.parameterIndex];
        auto name = parameter->getName(28);
        target.label = plugin->getName() + ": " + (name.isEmpty() ? "Param " + juce::String(target.parameterIndex + 1)
                                                                 : name);
        current = parameter->getValue();
    }
    else
    {
        return;
    }

    pushUndo(TRANS("Add automation lane"));
    auto* lane = track->addAutomationLane(target);

    if (lane == nullptr)
        return;

    // Seeded with one point at the value the control already has, so switching
    // automation on never moves the sound by itself.
    if (lane->isEmpty())
        lane->addPoint(0.0, target.fromParameterValue(current));

    rebuildRows();
    clampScroll();
    notifyClipEdited();
    repaint();
}

void ArrangementView::showAutomationMenu(int trackIndex, int laneIndex, int pointIndex)
{
    auto* track = mixer.getTrack(trackIndex);
    auto* lane = track != nullptr ? track->getAutomationLane(laneIndex) : nullptr;

    if (lane == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(lane->getTarget().describe());

    if (pointIndex >= 0)
    {
        menu.addItem(1, TRANS("Delete point"));
        menu.addItem(2, "Luruskan kurva");
        menu.addSeparator();
    }

    menu.addItem(3, TRANS("On"), true, lane->isEnabled());
    menu.addItem(4, TRANS("Delete all points"), lane->getNumPoints() > 0);
    menu.addSeparator();
    menu.addItem(5, TRANS("Remove automation lane"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(180)
                           .withStandardItemHeight(21),
        [this, trackIndex, laneIndex, pointIndex] (int result)
        {
            auto* selected = mixer.getTrack(trackIndex);
            auto* selectedLane = selected != nullptr ? selected->getAutomationLane(laneIndex) : nullptr;

            if (selectedLane == nullptr || result == 0)
                return;

            switch (result)
            {
                case 1:
                    pushUndo(TRANS("Delete automation point"));
                    selectedLane->removePoint(pointIndex);
                    break;

                case 2:
                    pushUndo(TRANS("Straighten automation curve"));
                    selectedLane->setPointCurve(pointIndex, 0.0);
                    break;

                case 3:
                    pushUndo(selectedLane->isEnabled() ? TRANS("Bypass automation") : TRANS("Enable automation"));
                    selectedLane->setEnabled(! selectedLane->isEnabled());
                    break;

                case 4:
                    pushUndo(TRANS("Clear automation lane"));
                    selectedLane->clearPoints();
                    break;

                case 5:
                    pushUndo(TRANS("Remove automation lane"));
                    selected->removeAutomationLane(laneIndex);
                    rebuildRows();
                    clampScroll();
                    break;

                default:
                    return;
            }

            notifyClipEdited();
            repaint();
        });
}

void ArrangementView::notifyTrackListChanged()
{
    // Track indices move when the list changes, so the selection cannot be
    // trusted to still point at the same clips.
    clearClipSelection();
    rebuildRows();
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
    refreshScrollBars();
    repaint();
}

juce::Rectangle<int> ArrangementView::getGridArea() const
{
    // The bars live inside the panel rather than over the grid, so nothing is
    // ever hidden underneath them.
    return getLocalBounds()
        .withTrimmedTop(panelHeaderHeight + rulerHeight)
        .withTrimmedLeft(headerWidth)
        .withTrimmedRight(1 + ZoomScrollBar::thickness)
        .withTrimmedBottom(1 + ZoomScrollBar::thickness);
}

juce::Rectangle<int> ArrangementView::getRowBounds(int trackIndex) const
{
    return getRowBoundsAt(getRowIndexForTrack(trackIndex));
}

void ArrangementView::rebuildRows()
{
    // Rows are refilled in place rather than cleared: the point snapshots keep
    // the capacity they grew to, so a steady frame allocates nothing.
    size_t used = 0;

    const auto nextRow = [this, &used] (int trackIndex, int laneIndex) -> Row&
    {
        if (used >= rows.size())
            rows.emplace_back();

        auto& row = rows[used++];
        row.trackIndex = trackIndex;
        row.automationLane = laneIndex;
        return row;
    };

    auto top = panelHeaderHeight + rulerHeight - scrollY;

    for (int trackIndex = 0; trackIndex < mixer.getNumTracks(); ++trackIndex)
    {
        auto& clipRow = nextRow(trackIndex, -1);
        clipRow.height = getRowHeight(trackIndex);
        clipRow.top = top;
        clipRow.points.clear();
        clipRow.laneEnabled = true;
        top += clipRow.height;

        auto* track = mixer.getTrack(trackIndex);

        if (track == nullptr)
            continue;

        for (int laneIndex = 0; laneIndex < track->getNumAutomationLanes(); ++laneIndex)
        {
            auto& laneRow = nextRow(trackIndex, laneIndex);
            auto* lane = track->getAutomationLane(laneIndex);

            const auto stored = lane != nullptr ? lane->getLaneHeight() : 0;
            laneRow.height = stored <= 0 ? defaultAutomationRowHeight
                                         : juce::jlimit(minRowHeight, maxRowHeight, stored);
            laneRow.top = top;
            laneRow.laneEnabled = lane != nullptr && lane->isEnabled();

            if (lane != nullptr)
            {
                laneRow.target = lane->getTarget();
                lane->copyPointsInto(laneRow.points);
            }
            else
            {
                laneRow.points.clear();
            }

            top += laneRow.height;
        }
    }

    rows.resize(used);
}

void ArrangementView::refreshRowTops()
{
    auto top = panelHeaderHeight + rulerHeight - scrollY;

    for (auto& row : rows)
    {
        row.top = top;
        top += row.height;
    }
}

int ArrangementView::getRowCount() const noexcept
{
    return static_cast<int>(rows.size());
}

int ArrangementView::getRowIndexForTrack(int trackIndex) const
{
    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (rows[static_cast<size_t>(i)].trackIndex == trackIndex
            && rows[static_cast<size_t>(i)].automationLane < 0)
            return i;

    return -1;
}

AutomationLane* ArrangementView::getLane(int trackIndex, int laneIndex) const
{
    if (laneIndex < 0)
        return nullptr;

    auto* track = mixer.getTrack(trackIndex);
    return track != nullptr ? track->getAutomationLane(laneIndex) : nullptr;
}

int ArrangementView::getRowHeightAt(int rowIndex) const
{
    return juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size()))
        ? rows[static_cast<size_t>(rowIndex)].height
        : defaultRowHeight;
}

void ArrangementView::setRowHeightAt(int rowIndex, int height)
{
    if (! juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size())))
        return;

    const auto& row = rows[static_cast<size_t>(rowIndex)];

    if (row.automationLane < 0)
    {
        setRowHeight(row.trackIndex, height);
        return;
    }

    if (auto* lane = getLane(row.trackIndex, row.automationLane))
        lane->setLaneHeight(juce::jlimit(minRowHeight, maxRowHeight, height));

    // The heights live in the row cache, so it has to be re-read before the
    // scroll is clamped against the new total.
    rebuildRows();
    clampScroll();
    repaint();
}

int ArrangementView::getRowTopAt(int rowIndex) const
{
    return juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size()))
        ? rows[static_cast<size_t>(rowIndex)].top
        : panelHeaderHeight + rulerHeight - scrollY;
}

juce::Rectangle<int> ArrangementView::getRowBoundsAt(int rowIndex) const
{
    if (! juce::isPositiveAndBelow(rowIndex, static_cast<int>(rows.size())))
        return {};

    return { 0, getRowTopAt(rowIndex), getWidth(), getRowHeightAt(rowIndex) };
}

int ArrangementView::rowAt(juce::Point<int> position) const
{
    if (position.y < panelHeaderHeight + rulerHeight)
        return -1;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (getRowBoundsAt(i).contains(position))
            return i;

    return -1;
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
    return getRowTopAt(getRowIndexForTrack(trackIndex));
}

int ArrangementView::hitTestRowResize(juce::Point<int> position) const
{
    // Only from the headers: inside the grid that band belongs to the clips.
    if (position.x >= headerWidth || position.y < panelHeaderHeight + rulerHeight)
        return -1;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
    {
        const auto bottom = getRowBoundsAt(i).getBottom();

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

    for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        total += getRowHeightAt(i);

    return total;
}

void ArrangementView::clampScroll()
{
    const auto viewportHeight = juce::jmax(0, getHeight() - panelHeaderHeight - rulerHeight);
    scrollY = juce::jlimit(0, juce::jmax(0, getContentHeight() - viewportHeight), scrollY);

    // The cached row positions are relative to scrollY, so they move with it.
    refreshRowTops();
    refreshScrollBars();
}

double ArrangementView::getTimelineBeats() const
{
    auto lastBeat = 16.0 * transport.getBeatsPerBar();

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        for (const auto& clip : getClipsForTrack(i))
            lastBeat = juce::jmax(lastBeat, clip.startBeat + clip.lengthBeats);

    // Room past the end, so the view can be scrolled to where the next thing
    // would go rather than stopping dead at the last clip.
    return lastBeat * 1.25;
}

void ArrangementView::refreshScrollBars()
{
    const auto grid = getGridArea();
    const auto pixelsPerBeat = pixelsPerBar / transport.getBeatsPerBar();
    const auto timeline = juce::jmax(1.0, getTimelineBeats());
    const auto visibleBeats = grid.getWidth() / juce::jmax(1.0, pixelsPerBeat);

    horizontalBar.setMinimumSize(juce::jlimit(0.0001, 1.0, 1.0 / juce::jmax(1.0, timeline)));
    horizontalBar.setRange(scrollBeats / timeline, visibleBeats / timeline);

    const auto content = juce::jmax(1, getContentHeight());
    verticalBar.setRange(static_cast<double>(scrollY) / content,
                         static_cast<double>(grid.getHeight()) / content);
}

void ArrangementView::applyHorizontalRange(double start, double size)
{
    const auto grid = getGridArea();
    const auto timeline = juce::jmax(1.0, getTimelineBeats());
    const auto visibleBeats = juce::jmax(0.25, size * timeline);

    // The bar says how much is showing; the zoom is whatever makes that much
    // fit. Written through the slider so both controls keep the same answer.
    const auto beatsPerPixel = visibleBeats / juce::jmax(1, grid.getWidth());
    const auto newPixelsPerBar = transport.getBeatsPerBar() / juce::jmax(1.0e-6, beatsPerPixel);

    scrollBeats = juce::jmax(0.0, start * timeline);
    zoomSlider.setValue(juce::jlimit(zoomSlider.getMinimum(), zoomSlider.getMaximum(), newPixelsPerBar),
                        juce::sendNotificationSync);

    // A zoom the slider refused still has to leave the scroll where it was put.
    pixelsPerBar = zoomSlider.getValue();
    rebuildRows();
    repaint();
}

void ArrangementView::applyVerticalRange(double start, double size)
{
    const auto grid = getGridArea();
    const auto content = juce::jmax(1, getContentHeight());
    const auto wanted = juce::jmax(1.0, size * content);
    const auto factor = grid.getHeight() / wanted;

    // Zooming down this edge is the rows getting taller or shorter together,
    // which is what a playlist has instead of a vertical scale.
    if (! juce::approximatelyEqual(factor, 1.0))
    {
        for (int row = 0; row < getRowCount(); ++row)
            setRowHeightAt(row, juce::roundToInt(getRowHeightAt(row) * factor));

        rebuildRows();
    }

    scrollY = juce::roundToInt(start * juce::jmax(1, getContentHeight()));
    clampScroll();
    refreshScrollBars();
    repaint();
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
    refreshScrollBars();
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

            const auto playLength = juce::jmax(1.0e-9, source->getPlayLengthSeconds());
            clip.fadeInFraction = juce::jlimit(0.0, 1.0, source->getFadeInSeconds() / playLength);
            clip.fadeOutFraction = juce::jlimit(0.0, 1.0, source->getFadeOutSeconds() / playLength);

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

juce::Rectangle<int> ArrangementView::getCurveArea(int rowIndex) const
{
    const auto bounds = getRowBoundsAt(rowIndex);
    const auto grid = getGridArea();

    // Inset top and bottom so a point parked at 0 or 1 still draws as a whole
    // dot instead of half of one hanging off the lane.
    return { grid.getX(),
             bounds.getY() + curveInset,
             grid.getWidth(),
             juce::jmax(4, bounds.getHeight() - curveInset * 2) };
}

double ArrangementView::valueFromY(int rowIndex, int y) const
{
    const auto curve = getCurveArea(rowIndex);

    return juce::jlimit(0.0, 1.0,
                        1.0 - static_cast<double>(y - curve.getY()) / juce::jmax(1, curve.getHeight()));
}

int ArrangementView::yFromValue(int rowIndex, double value) const
{
    const auto curve = getCurveArea(rowIndex);
    return curve.getBottom() - juce::roundToInt(juce::jlimit(0.0, 1.0, value) * curve.getHeight());
}

void ArrangementView::drawAutomationRow(juce::Graphics& g, int rowIndex, const Row& row)
{
    // Everything drawn here comes from the snapshot taken in rebuildRows, so a
    // repaint never contends with the audio thread for the lane's points.
    const auto bounds = getRowBoundsAt(rowIndex);
    const auto grid = getGridArea();
    const auto trackColour = Theme::trackColour(row.trackIndex);
    // A bypassed lane keeps its shape but stops looking live.
    const auto laneColour = row.laneEnabled ? Theme::amber() : Theme::mutedText();

    g.setColour(Theme::panelDeep());
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromString("ff1e2532"));
    g.fillRect(bounds.withHeight(1).withY(bounds.getBottom() - 1));

    // Header -----------------------------------------------------------------
    auto headerCell = bounds.withWidth(headerWidth);
    g.setColour(Theme::panel().withAlpha(0.75f));
    g.fillRect(headerCell);
    g.setColour(juce::Colour::fromString("ff1e2532"));
    g.fillRect(headerCell.withHeight(1).withY(headerCell.getBottom() - 1));

    auto cell = headerCell.reduced(5, 0);

    // Indented, so a lane reads as belonging to the track above it.
    cell.removeFromLeft(10);
    auto chip = cell.removeFromLeft(3).withSizeKeepingCentre(3, bounds.getHeight() - 10);
    g.setColour(trackColour.withAlpha(0.55f));
    g.fillRoundedRectangle(chip.toFloat(), 1.5f);
    cell.removeFromLeft(6);

    g.setColour(row.laneEnabled ? Theme::text() : Theme::faintText());
    g.setFont(Theme::ui(10.5f, true));
    g.drawText(row.target.describe(),
               cell.removeFromTop(juce::jmax(11, bounds.getHeight() / 2)).withTrimmedTop(2),
               juce::Justification::bottomLeft, true);

    // The value under the playhead, so the lane says what it is doing right now.
    g.setColour(laneColour.withAlpha(0.85f));
    g.setFont(Theme::mono(9.0f));
    g.drawText(row.laneEnabled
                   ? row.target.describeValue(AutomationLane::valueAt(row.points, transport.getPositionBeats()))
                   : juce::String("bypass"),
               cell, juce::Justification::topLeft, true);

    // Curve ------------------------------------------------------------------
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion({ grid.getX(), bounds.getY(), grid.getWidth(), bounds.getHeight() });

    const auto curve = getCurveArea(rowIndex);

    g.setColour(Theme::inset().withAlpha(0.4f));
    g.fillRect(juce::Rectangle<int>(grid.getX(), bounds.getY() + 1, grid.getWidth(), bounds.getHeight() - 2));

    // Half way marks the middle of the parameter's range - the reference the
    // eye needs to tell a small rise from a large one.
    g.setColour(Theme::divider().withAlpha(0.6f));
    g.fillRect(grid.getX(), curve.getCentreY(), grid.getWidth(), 1);

    const auto& points = row.points;

    const auto toX = [this] (double beat) { return static_cast<float>(beatToX(beat)); };
    const auto toY = [rowIndex, this] (double value) { return static_cast<float>(yFromValue(rowIndex, value)); };

    if (points.empty())
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(10.0f));
        g.drawText(TRANS("Click to place a point"),
                   juce::Rectangle<int>(grid.getX() + 8, bounds.getY(), 200, bounds.getHeight()),
                   juce::Justification::centredLeft, false);
        return;
    }

    juce::Path path;
    path.startNewSubPath(static_cast<float>(grid.getX()), toY(points.front().value));
    path.lineTo(toX(points.front().beat), toY(points.front().value));

    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& from = points[i - 1];
        const auto& to = points[i];

        if (std::abs(to.curve) < 1.0e-6)
        {
            path.lineTo(toX(to.beat), toY(to.value));
            continue;
        }

        // A bent segment is sampled; how finely depends on how wide it is on
        // screen, so a curve costs the same whatever the zoom.
        const auto span = juce::jmax(1, static_cast<int>(toX(to.beat) - toX(from.beat)));
        const auto steps = juce::jlimit(4, 96, span / 3);

        for (int step = 1; step <= steps; ++step)
        {
            const auto beat = from.beat + (to.beat - from.beat) * (static_cast<double>(step) / steps);
            path.lineTo(toX(beat), toY(AutomationLane::interpolate(from, to, beat)));
        }
    }

    path.lineTo(static_cast<float>(grid.getRight()), toY(points.back().value));

    // Shading under the line makes the lane readable at a glance even when it
    // is squeezed down to a couple of dozen pixels.
    auto filled = path;
    filled.lineTo(static_cast<float>(grid.getRight()), static_cast<float>(curve.getBottom()));
    filled.lineTo(static_cast<float>(grid.getX()), static_cast<float>(curve.getBottom()));
    filled.closeSubPath();

    g.setColour(laneColour.withAlpha(row.laneEnabled ? 0.13f : 0.06f));
    g.fillPath(filled);

    g.setColour(laneColour.withAlpha(row.laneEnabled ? 0.95f : 0.5f));
    g.strokePath(path, juce::PathStrokeType(1.6f));

    // Tension handles, only where there is room to grab one.
    for (size_t i = 1; i < points.size(); ++i)
    {
        const auto& from = points[i - 1];
        const auto& to = points[i];

        if (toX(to.beat) - toX(from.beat) < curveHandleMinWidth)
            continue;

        const auto midBeat = (from.beat + to.beat) * 0.5;
        const auto centre = juce::Point<float>(toX(midBeat), toY(AutomationLane::interpolate(from, to, midBeat)));

        g.setColour(Theme::windowBackground().withAlpha(0.8f));
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre));
        g.setColour(laneColour.withAlpha(0.7f));
        g.drawEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre), 1.0f);
    }

    for (const auto& point : points)
    {
        const auto centre = juce::Point<float>(toX(point.beat), toY(point.value));

        if (centre.x < grid.getX() - 8.0f || centre.x > grid.getRight() + 8.0f)
            continue;

        g.setColour(Theme::windowBackground());
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(centre));
        g.setColour(laneColour);
        g.fillEllipse(juce::Rectangle<float>(4.6f, 4.6f).withCentre(centre));
    }
}

ArrangementView::AutomationDrag ArrangementView::hitTestAutomation(juce::Point<int> position) const
{
    AutomationDrag hit;

    if (position.x < getGridArea().getX())
        return hit;

    const auto rowIndex = rowAt(position);

    if (rowIndex < 0)
        return hit;

    const auto& row = rows[static_cast<size_t>(rowIndex)];

    if (row.automationLane < 0)
        return hit;

    // Filled in even when nothing is hit: an empty spot on a lane is still a
    // place a point can go.
    hit.rowIndex = rowIndex;
    hit.trackIndex = row.trackIndex;
    hit.laneIndex = row.automationLane;

    // The snapshot, not the lane: hit testing runs on every mouse move.
    const auto& points = row.points;

    // Points win over handles: they sit on top of the line and are what the
    // user is most likely aiming at.
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const auto& point = points[static_cast<size_t>(i)];
        const juce::Point<int> centre { beatToX(point.beat), yFromValue(rowIndex, point.value) };

        if (centre.getDistanceFrom(position) <= pointGrab)
        {
            hit.mode = AutomationDrag::Mode::point;
            hit.pointIndex = i;
            return hit;
        }
    }

    for (int i = 1; i < static_cast<int>(points.size()); ++i)
    {
        const auto& from = points[static_cast<size_t>(i - 1)];
        const auto& to = points[static_cast<size_t>(i)];

        if (beatToX(to.beat) - beatToX(from.beat) < curveHandleMinWidth)
            continue;

        const auto midBeat = (from.beat + to.beat) * 0.5;
        const juce::Point<int> centre { beatToX(midBeat),
                                        yFromValue(rowIndex, AutomationLane::interpolate(from, to, midBeat)) };

        if (centre.getDistanceFrom(position) <= pointGrab)
        {
            hit.mode = AutomationDrag::Mode::curve;
            hit.pointIndex = i;
            hit.grabCurve = to.curve;
            hit.falling = to.value < from.value;
            return hit;
        }
    }

    return hit;
}

bool ArrangementView::handleAutomationMouseDown(int rowIndex,
                                                juce::Point<int> position,
                                                const juce::ModifierKeys& mods)
{
    const auto rowTrack = rows[static_cast<size_t>(rowIndex)].trackIndex;
    const auto rowLane = rows[static_cast<size_t>(rowIndex)].automationLane;
    auto* lane = getLane(rowTrack, rowLane);

    if (lane == nullptr)
        return false;

    // Zoom and playback act on the timeline wherever the pointer happens to be,
    // the same way they already ignore the clip underneath them.
    if (activeTool == Tool::zoom || activeTool == Tool::playback)
        return false;

    auto hit = hitTestAutomation(position);

    if (mods.isRightButtonDown())
    {
        showAutomationMenu(rowTrack,
                           rowLane,
                           hit.mode == AutomationDrag::Mode::point ? hit.pointIndex : -1);
        return true;
    }

    // Mute is the tool that silences things elsewhere, so on a lane it bypasses
    // the curve rather than editing it.
    if (activeTool == Tool::mute)
    {
        pushUndo(lane->isEnabled() ? TRANS("Bypass automation") : TRANS("Enable automation"));
        lane->setEnabled(! lane->isEnabled());
        notifyClipEdited();
        repaint();
        return true;
    }

    if (activeTool == Tool::erase)
    {
        if (hit.mode == AutomationDrag::Mode::point)
        {
            pushUndo(TRANS("Delete automation point"));
            lane->removePoint(hit.pointIndex);
            notifyClipEdited();
            repaint();
        }

        return true;
    }

    if (hit.mode != AutomationDrag::Mode::none)
    {
        pushUndo(hit.mode == AutomationDrag::Mode::curve ? TRANS("Bend automation") : TRANS("Move automation point"));
        automationDrag = hit;
        automationDrag.grabY = position.y;
        return true;
    }

    // An empty spot places a point and keeps hold of it, so one gesture both
    // puts it down and lands it where it was meant to go.
    pushUndo(TRANS("Add automation point"));
    const auto index = lane->addPoint(snapBeat(xToBeat(position.x)), valueFromY(rowIndex, position.y));

    if (index < 0)
        return true;

    automationDrag = {};
    automationDrag.mode = AutomationDrag::Mode::point;
    automationDrag.rowIndex = rowIndex;
    automationDrag.trackIndex = rowTrack;
    automationDrag.laneIndex = rowLane;
    automationDrag.pointIndex = index;

    notifyClipEdited();
    repaint();
    return true;
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
        { Tool::select,   Icon::marquee,     "Select - move and trim clips" },
        { Tool::paint,    Icon::pencil,      "Paint - place clips" },
        { Tool::erase,    Icon::eraser,      "Delete - remove clips" },
        { Tool::mute,     Icon::speakerMute, "Mute - silence a clip" },
        { Tool::slip,     Icon::slip,        "Slip - slide the audio inside a clip" },
        { Tool::slice,    Icon::slice,       "Slice - cut a clip in two" },
        { Tool::zoom,     Icon::zoom,        "Zoom - drag out an area" },
        { Tool::playback, Icon::play,        "Playback - click to play from there" }
    };

    for (const auto& definition : definitions)
    {
        auto* button = toolButtons.add(new IconChipButton(juce::translate(definition.tooltip), definition.icon));
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
    snapButton.setTooltip(snapEnabled ? TRANS("Snap on - click to turn off")
                                      : TRANS("Snap off - click to turn on"));
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

void ArrangementView::setAudioClipOpenRequestCallback(std::function<void(int, int)> callback)
{
    audioClipOpenRequestCallback = std::move(callback);
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

void ArrangementView::setTrackRenameCallback(std::function<void(int)> callback)
{
    trackRenameCallback = std::move(callback);
}

void ArrangementView::setTrackFreezeCallback(std::function<void(int)> callback)
{
    trackFreezeCallback = std::move(callback);
}

void ArrangementView::setTrackChannelCallback(std::function<void(int)> callback)
{
    trackChannelCallback = std::move(callback);
}

void ArrangementView::setTrackBounceCallback(std::function<void(int)> callback)
{
    trackBounceCallback = std::move(callback);
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
    pushUndo(TRANS("Move selected clips"));

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

    pushUndo(selectedClips.size() > 1 ? TRANS("Delete selected clips") : TRANS("Delete clip"));

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

void ArrangementView::paintFileDropRow(juce::Graphics& g) const
{
    if (! juce::isPositiveAndBelow(fileDropRow, getRowCount()))
        return;

    const auto bounds = getRowBoundsAt(fileDropRow);

    g.setColour(Theme::accent().withAlpha(0.12f));
    g.fillRect(bounds);
    g.setColour(Theme::accent());
    g.drawRect(bounds, 2);
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

        pushUndo(TRANS("Place clip"));
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
            pushUndo(TRANS("Delete clip"));

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
            pushUndo(TRANS("Mute clip"));

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
            pushUndo(TRANS("Slice clip"));

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
            pushUndo(TRANS("Slip clip"));
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
    menu.addItem(1, TRANS("Restore full length"),
                 placement.sourceOffsetBeats > 0.0 || placement.lengthBeats < fullLength - 1.0e-9);
    menu.addItem(2, TRANS("Extend by 1 bar"));
    menu.addSeparator();
    menu.addItem(4, TRANS("Rename pattern..."));
    menu.addSeparator();
    menu.addItem(3, TRANS("Remove placement"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this, trackIndex, placementIndex] (int result)
        {
            auto* track = dynamic_cast<MidiTrack*>(mixer.getTrack(trackIndex));

            if (track == nullptr)
                return;

            auto edited = track->getPlacement(placementIndex);

            if (result >= 1 && result <= 3)
                pushUndo(TRANS("Edit clip"));

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

    // A take almost never starts on a zero crossing, so the edges click without
    // these. Offered as lengths rather than a toggle: how long a fade needs to
    // be depends on what is under it.
    const auto fadeLengths = { 0.005, 0.01, 0.05, 0.25, 1.0 };

    const auto describe = [] (double seconds)
    {
        return seconds < 1.0 ? juce::String(juce::roundToInt(seconds * 1000.0)) + " ms"
                             : juce::String(seconds, 1) + " s";
    };

    const auto buildFadeMenu = [&fadeLengths, &describe] (int baseId, double current)
    {
        juce::PopupMenu fadeMenu;
        fadeMenu.addItem(baseId, TRANS("Off"), true, current <= 0.0);

        auto id = baseId + 1;

        for (const auto seconds : fadeLengths)
            fadeMenu.addItem(id++, describe(seconds), true,
                             std::abs(current - seconds) < 1.0e-6);

        return fadeMenu;
    };

    // Two ways to follow the tempo, and which one is right depends on the
    // material: a drum one-shot survives resampling, a melodic loop does not.
    juce::PopupMenu warpMenu;
    warpMenu.addItem(10, TRANS("Resample (pitch follows tempo)"), true,
                     clip->getWarpMode() == AudioClip::WarpMode::resample);
    warpMenu.addItem(11, TRANS("Stretch (keep pitch)"), true,
                     clip->getWarpMode() == AudioClip::WarpMode::stretch);

    // Semitones, in the range the stretcher still sounds like the source.
    // Written as +5 / -3 rather than as a number on its own: a pitch with no
    // sign reads as a note, and this is a distance.
    juce::PopupMenu pitchMenu;
    const auto currentPitch = clip->getPitchSemitones();

    for (int semitones = AudioClip::maxPitchSemitones; semitones >= -AudioClip::maxPitchSemitones; --semitones)
    {
        const auto label = semitones == 0 ? juce::String(TRANS("None"))
                         : (semitones > 0 ? "+" : "") + juce::String(semitones) + TRANS(" st");

        pitchMenu.addItem(300 + semitones + AudioClip::maxPitchSemitones,
                          label, true, semitones == currentPitch);
    }

    juce::PopupMenu menu;
    menu.addSectionHeader(clip->getName());
    menu.addItem(1, TRANS("Warp to tempo"), true, clip->isWarpEnabled());
    menu.addSubMenu(TRANS("Warp mode"), warpMenu, clip->isWarpEnabled());
    menu.addSubMenu(TRANS("Pitch"), pitchMenu, true, nullptr, currentPitch != 0);
    menu.addItem(2, TRANS("Restore full length"));
    menu.addSeparator();
    menu.addSubMenu(TRANS("Fade in"), buildFadeMenu(100, clip->getFadeInSeconds()));
    menu.addSubMenu(TRANS("Fade out"), buildFadeMenu(200, clip->getFadeOutSeconds()));
    menu.addSeparator();
    menu.addItem(3, TRANS("Delete clip"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(180)
                           .withStandardItemHeight(21),
        [this, trackIndex, clipIndex] (int result)
        {
            auto* track = dynamic_cast<AudioTrack*>(mixer.getTrack(trackIndex));
            auto* selected = track != nullptr ? track->getClip(clipIndex) : nullptr;

            if (selected == nullptr)
                return;

            if (result >= 1 && result <= 3)
                pushUndo(result == 3 ? TRANS("Delete clip") : TRANS("Edit clip"));

            if (result == 10 || result == 11)
            {
                pushUndo(TRANS("Change warp mode"));
                selected->setWarpMode(result == 11 ? AudioClip::WarpMode::stretch
                                                   : AudioClip::WarpMode::resample);

                // Built here rather than waiting for the next tempo change, or
                // switching to stretch would keep resampling until one happened.
                selected->prepareWarp(transport.getTempoBpm());
                notifyClipEdited();
                return;
            }

            if (result >= 300)
            {
                pushUndo(TRANS("Change clip pitch"));
                selected->setPitchSemitones(result - 300 - AudioClip::maxPitchSemitones);

                // Built here rather than at the next tempo change: the pitch is
                // wanted now, and nothing else would ask for it.
                selected->prepareWarp(transport.getTempoBpm());
                notifyClipEdited();
                return;
            }

            if (result >= 100)
            {
                pushUndo(TRANS("Change fade"));

                static constexpr double lengths[] = { 0.005, 0.01, 0.05, 0.25, 1.0 };
                const auto isFadeOut = result >= 200;
                const auto choice = result - (isFadeOut ? 200 : 100);
                const auto seconds = choice == 0 || choice > static_cast<int>(std::size(lengths))
                    ? 0.0
                    : lengths[choice - 1];

                if (isFadeOut)
                    selected->setFadeOutSeconds(seconds);
                else
                    selected->setFadeInSeconds(seconds);

                notifyClipEdited();
                return;
            }

            switch (result)
            {
                case 1:
                    selected->setWarpEnabled(! selected->isWarpEnabled());
                    selected->prepareWarp(transport.getTempoBpm());
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

bool ArrangementView::isAudioFileName(const juce::String& path)
{
    return juce::File(path).hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
}

void ArrangementView::setFileDropCallback(std::function<void(const juce::File&, int trackIndex, double beat)> callback)
{
    fileDropCallback = std::move(callback);
}

namespace
{
    /** The file behind a drag description from the browser, or an empty File. */
    juce::File fileFromDragDescription(const juce::var& description)
    {
        const auto text = description.toString();

        if (! text.startsWith(BrowserPanel::fileDragPrefix))
            return {};

        return juce::File(text.fromFirstOccurrenceOf(BrowserPanel::fileDragPrefix, false, false));
    }
}

bool ArrangementView::isInterestedInDragSource(const SourceDetails& details)
{
    return fileDropCallback != nullptr && fileFromDragDescription(details.description) != juce::File();
}

void ArrangementView::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void ArrangementView::itemDragMove(const SourceDetails& details)
{
    const auto row = rowAt(details.localPosition);

    if (row == fileDropRow)
        return;

    fileDropRow = row;
    repaint();
}

void ArrangementView::itemDragExit(const SourceDetails&)
{
    fileDragExit({});
}

void ArrangementView::itemDropped(const SourceDetails& details)
{
    const auto row = rowAt(details.localPosition);
    fileDropRow = -1;
    repaint();

    const auto file = fileFromDragDescription(details.description);

    if (fileDropCallback == nullptr || file == juce::File() || ! juce::isPositiveAndBelow(row, getRowCount()))
        return;

    fileDropCallback(file,
                     rows[static_cast<size_t>(row)].trackIndex,
                     juce::jmax(0.0, snapBeat(xToBeat(details.localPosition.x))));
}

bool ArrangementView::findDropTarget(juce::Point<int> screenPosition, int& trackIndexOut, double& beatOut) const
{
    const auto local = getLocalPoint(nullptr, screenPosition);

    if (! getLocalBounds().contains(local))
        return false;

    const auto row = rowAt(local);

    if (! juce::isPositiveAndBelow(row, getRowCount()))
        return false;

    trackIndexOut = rows[static_cast<size_t>(row)].trackIndex;
    beatOut = juce::jmax(0.0, snapBeat(xToBeat(local.x)));
    return true;
}

bool ArrangementView::isInterestedInFileDrag(const juce::StringArray& files)
{
    if (fileDropCallback == nullptr)
        return false;

    // One audio file among them is enough to accept the drag; the others are
    // skipped on the drop rather than refusing the whole thing.
    for (const auto& path : files)
        if (isAudioFileName(path))
            return true;

    return false;
}

void ArrangementView::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    fileDragMove(files, x, y);
}

void ArrangementView::fileDragMove(const juce::StringArray&, int x, int y)
{

    // The row under the pointer is outlined while the drag is overhead: a drop
    // that lands on the wrong track is easy to make and tedious to undo.
    const auto row = rowAt({ x, y });

    if (row == fileDropRow)
        return;

    fileDropRow = row;
    repaint();
}

void ArrangementView::fileDragExit(const juce::StringArray&)
{
    if (fileDropRow < 0)
        return;

    fileDropRow = -1;
    repaint();
}

void ArrangementView::filesDropped(const juce::StringArray& files, int x, int y)
{

    const auto row = rowAt({ x, y });
    fileDropRow = -1;
    repaint();

    if (fileDropCallback == nullptr || ! juce::isPositiveAndBelow(row, getRowCount()))
        return;

    const auto trackIndex = rows[static_cast<size_t>(row)].trackIndex;
    const auto beat = juce::jmax(0.0, snapBeat(xToBeat(x)));

    for (const auto& path : files)
        if (isAudioFileName(path))
            fileDropCallback(juce::File(path), trackIndex, beat);
}

} // namespace djr
