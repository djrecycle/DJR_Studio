#include "ZoomScrollBar.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    /** How much of each end of the thumb grabs the zoom rather than the
        scroll. Small enough that a short thumb still has a middle to drag.
    */
    /** Seven pixels is what the drawing needs and about half what a hand can
        reliably hit, so the grip reaches further than it looks.
    */
    constexpr int gripSize = 12;
    /** Nothing smaller than this can be aimed at with a mouse. */
    constexpr float minimumThumbPixels = 18.0f;
}

ZoomScrollBar::ZoomScrollBar(Orientation orientation)
    : axis(orientation)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ZoomScrollBar::setRange(double start, double size)
{
    const auto clampedSize = juce::jlimit(minimumSize, 1.0, size);
    const auto clampedStart = juce::jlimit(0.0, 1.0 - clampedSize, start);

    if (juce::approximatelyEqual(clampedStart, rangeStart) && juce::approximatelyEqual(clampedSize, rangeSize))
        return;

    rangeStart = clampedStart;
    rangeSize = clampedSize;
    repaint();
}

double ZoomScrollBar::getStart() const noexcept
{
    return rangeStart;
}

double ZoomScrollBar::getSize() const noexcept
{
    return rangeSize;
}

void ZoomScrollBar::setMinimumSize(double size)
{
    minimumSize = juce::jlimit(0.00001, 1.0, size);
}

int ZoomScrollBar::getTrackLength() const
{
    return juce::jmax(1, axis == Orientation::horizontal ? getWidth() : getHeight());
}

int ZoomScrollBar::positionOf(const juce::MouseEvent& event) const
{
    return axis == Orientation::horizontal ? event.getPosition().x : event.getPosition().y;
}

juce::Rectangle<float> ZoomScrollBar::getThumbBounds() const
{
    const auto track = static_cast<float>(getTrackLength());
    const auto length = juce::jmax(minimumThumbPixels, static_cast<float>(rangeSize) * track);
    // Kept inside the track after the minimum length has stretched it.
    const auto offset = juce::jlimit(0.0f, track - length, static_cast<float>(rangeStart) * track);

    return axis == Orientation::horizontal
        ? juce::Rectangle<float>(offset, 0.0f, length, static_cast<float>(getHeight()))
        : juce::Rectangle<float>(0.0f, offset, static_cast<float>(getWidth()), length);
}

ZoomScrollBar::Grip ZoomScrollBar::gripAt(juce::Point<int> position) const
{
    const auto thumb = getThumbBounds().reduced(axis == Orientation::horizontal ? 0.0f : 1.0f,
                                                axis == Orientation::horizontal ? 1.0f : 0.0f);

    if (! thumb.expanded(2.0f).contains(position.toFloat()))
        return Grip::none;

    const auto along = static_cast<float>(axis == Orientation::horizontal ? position.x : position.y);
    const auto from = axis == Orientation::horizontal ? thumb.getX() : thumb.getY();
    const auto to = axis == Orientation::horizontal ? thumb.getRight() : thumb.getBottom();

    // A thumb too short to have three parts is all middle: the zoom can be
    // had from the other end or the wheel, but a thumb nobody can scroll is
    // a bar nobody can use.
    if (to - from < static_cast<float>(gripSize) * 3.0f)
        return Grip::middle;

    if (along <= from + static_cast<float>(gripSize))
        return Grip::start;

    if (along >= to - static_cast<float>(gripSize))
        return Grip::end;

    return Grip::middle;
}

void ZoomScrollBar::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    g.setColour(Theme::inset());
    g.fillRoundedRectangle(bounds, radius);

    const auto thumb = getThumbBounds().reduced(axis == Orientation::horizontal ? 0.0f : 1.5f,
                                                axis == Orientation::horizontal ? 1.5f : 0.0f);
    const auto lit = activeGrip != Grip::none || hoveredGrip != Grip::none;

    g.setColour(lit ? Theme::control().brighter(0.35f) : Theme::control());
    g.fillRoundedRectangle(thumb, radius);

    // The ends are marked only when they are worth aiming at, so a thumb that
    // is all middle does not advertise a grip it does not have.
    if (gripAt(thumb.getCentre().roundToInt()) == Grip::middle
        && (axis == Orientation::horizontal ? thumb.getWidth() : thumb.getHeight())
               >= static_cast<float>(gripSize) * 3.0f)
    {
        g.setColour(Theme::mutedText().withAlpha(lit ? 0.9f : 0.55f));

        for (const auto atStart : { true, false })
        {
            const auto centre = axis == Orientation::horizontal
                ? juce::Point<float>(atStart ? thumb.getX() + 3.5f : thumb.getRight() - 3.5f, thumb.getCentreY())
                : juce::Point<float>(thumb.getCentreX(), atStart ? thumb.getY() + 3.5f : thumb.getBottom() - 3.5f);

            const auto grip = axis == Orientation::horizontal
                ? juce::Rectangle<float>(1.0f, thumb.getHeight() * 0.5f).withCentre(centre)
                : juce::Rectangle<float>(thumb.getWidth() * 0.5f, 1.0f).withCentre(centre);

            g.fillRect(grip);
        }
    }
}

void ZoomScrollBar::mouseMove(const juce::MouseEvent& event)
{
    const auto grip = gripAt(event.getPosition());

    if (grip == hoveredGrip)
        return;

    hoveredGrip = grip;

    setMouseCursor(grip == Grip::start || grip == Grip::end
                       ? (axis == Orientation::horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                                          : juce::MouseCursor::UpDownResizeCursor)
                       : juce::MouseCursor::NormalCursor);
    repaint();
}

void ZoomScrollBar::mouseDown(const juce::MouseEvent& event)
{
    activeGrip = gripAt(event.getPosition());

    // A right drag zooms from wherever it starts, which is how FL does it and
    // what saves the reader from having to hit a grip at all.
    if (event.mods.isRightButtonDown() && activeGrip != Grip::none)
        activeGrip = Grip::zoom;
    dragStartPosition = positionOf(event);
    dragStartRangeStart = rangeStart;
    dragStartRangeSize = rangeSize;

    // A press on the track jumps there, centred, which is what every scrollbar
    // that does not page has always done.
    if (activeGrip == Grip::none)
    {
        const auto fraction = static_cast<double>(dragStartPosition) / getTrackLength();
        applyRange(fraction - rangeSize * 0.5, rangeSize);
        activeGrip = Grip::middle;
        dragStartRangeStart = rangeStart;
    }

    repaint();
}

void ZoomScrollBar::mouseDrag(const juce::MouseEvent& event)
{
    if (activeGrip == Grip::none)
        return;

    const auto travelled = static_cast<double>(positionOf(event) - dragStartPosition) / getTrackLength();

    switch (activeGrip)
    {
        case Grip::middle:
            applyRange(dragStartRangeStart + travelled, dragStartRangeSize);
            break;

        case Grip::start:
        {
            // The far end stays where it is, so zooming from this end does not
            // also scroll the thing the reader is looking at.
            const auto farEnd = dragStartRangeStart + dragStartRangeSize;
            const auto newStart = juce::jlimit(0.0, farEnd - minimumSize, dragStartRangeStart + travelled);
            applyRange(newStart, farEnd - newStart);
            break;
        }

        case Grip::end:
        {
            const auto newSize = juce::jmax(minimumSize, dragStartRangeSize + travelled);
            applyRange(dragStartRangeStart, newSize);
            break;
        }

        case Grip::zoom:
        {
            // The middle stays put, so the thing being looked at stays where
            // it is while there is more or less of it.
            const auto centre = dragStartRangeStart + dragStartRangeSize * 0.5;
            const auto newSize = juce::jlimit(minimumSize, 1.0,
                                              dragStartRangeSize * std::exp(-travelled * 3.0));
            applyRange(centre - newSize * 0.5, newSize);
            break;
        }

        case Grip::none:
            break;
    }
}

void ZoomScrollBar::mouseUp(const juce::MouseEvent&)
{
    activeGrip = Grip::none;
    repaint();
}

void ZoomScrollBar::mouseDoubleClick(const juce::MouseEvent&)
{
    applyRange(0.0, 1.0);
}

void ZoomScrollBar::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto amount = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;

    if (juce::approximatelyEqual(amount, 0.0f))
        return;

    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        // Zooms about the middle of what is showing, which is the part the
        // reader is looking at.
        const auto centre = rangeStart + rangeSize * 0.5;
        const auto newSize = juce::jlimit(minimumSize, 1.0, rangeSize * (amount > 0.0f ? 0.8 : 1.25));
        applyRange(centre - newSize * 0.5, newSize);
        return;
    }

    applyRange(rangeStart - amount * rangeSize * 0.5, rangeSize);
}

void ZoomScrollBar::applyRange(double newStart, double newSize)
{
    const auto clampedSize = juce::jlimit(minimumSize, 1.0, newSize);
    const auto clampedStart = juce::jlimit(0.0, 1.0 - clampedSize, newStart);

    if (juce::approximatelyEqual(clampedStart, rangeStart) && juce::approximatelyEqual(clampedSize, rangeSize))
        return;

    rangeStart = clampedStart;
    rangeSize = clampedSize;
    repaint();

    if (onRangeChanged)
        onRangeChanged(rangeStart, rangeSize);
}

} // namespace djr
