#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace djr
{

/** A scrollbar whose ends are also the zoom, the way FL's are.

    Two controls in one, because they are two halves of one question: which
    part of this am I looking at, and how much of it. Dragging the middle
    answers the first, dragging either end answers the second, and the bar
    draws the answer - a long thumb is zoomed out, a short one is zoomed in.
    A separate zoom slider can only ever say how much, never where.

    The bar knows nothing about beats, pitches or samples: it works in
    fractions of whatever it is scrolling, and the view it belongs to converts.
    That is what lets the playlist, the piano roll and the audio editor share
    one of these instead of growing three.
*/
class ZoomScrollBar final : public juce::Component
{
public:
    enum class Orientation
    {
        horizontal,
        vertical
    };

    explicit ZoomScrollBar(Orientation orientation);

    /** The visible slice, both in 0..1 of the whole. Does not call back: this
        is the view telling the bar where it already is.
    */
    void setRange(double start, double size);
    double getStart() const noexcept;
    double getSize() const noexcept;

    /** The smallest slice a drag may zoom to - the view's own zoom limit
        expressed as a fraction. Below about a thousandth the thumb is too
        small to grab back.
    */
    void setMinimumSize(double size);

    /** Called when a drag changes either number. */
    std::function<void(double start, double size)> onRangeChanged;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    /** How thick one of these wants to be. */
    static constexpr int thickness = 12;

private:
    /** Which part of the thumb a press landed on. */
    enum class Grip
    {
        none,
        start,   ///< the near end: zooms, keeping the far end still
        middle,  ///< scrolls
        end,     ///< the far end
        zoom     ///< a right drag anywhere on the thumb, about its centre
    };

    /** Length along the bar's own axis, which is width or height depending. */
    int getTrackLength() const;
    /** Where along the axis a mouse event is. */
    int positionOf(const juce::MouseEvent& event) const;
    juce::Rectangle<float> getThumbBounds() const;
    Grip gripAt(juce::Point<int> position) const;
    void applyRange(double newStart, double newSize);

    Orientation axis;
    double rangeStart = 0.0;
    double rangeSize = 1.0;
    double minimumSize = 0.002;

    Grip activeGrip = Grip::none;
    Grip hoveredGrip = Grip::none;
    /** Where the drag started, so a drag is measured rather than followed. */
    int dragStartPosition = 0;
    double dragStartRangeStart = 0.0;
    double dragStartRangeSize = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ZoomScrollBar)
};

} // namespace djr
