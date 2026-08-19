#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace djr
{

/** The rotary control FL's channel settings are built out of.

    A Slider subclass rather than a component of its own: the drag handling,
    the value range, double-click-to-reset and the listener plumbing are all
    already right in juce::Slider, and only the drawing needed replacing.

    Carries its own caption because every knob in that window has one, and a
    separate Label per knob would double the component count for no gain.
*/
class Knob final : public juce::Slider
{
public:
    enum class Style
    {
        unipolar,  ///< arc grows from the left: volume, attack, amount
        bipolar    ///< arc grows from the top, both ways: pan, pitch, tuning
    };

    Knob(const juce::String& captionText, Style style = Style::unipolar);

    /** Height of the caption strip under the dial, so layouts can reserve it. */
    static constexpr int captionHeight = 12;
    /** What a knob needs to stay legible: the dial plus its caption. */
    static constexpr int preferredSize = 34 + captionHeight;

    /** Greys the knob out and stops it responding, for the controls whose
        engine side does not exist yet. Kept visible on purpose: the layout is
        the point, and a hidden control cannot say "not yet".
    */
    void setAwaitingEngine(bool isAwaiting);
    bool isAwaitingEngine() const noexcept;

    void paint(juce::Graphics& g) override;

private:
    juce::String caption;
    Style knobStyle;
    bool awaitingEngine = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Knob)
};

} // namespace djr
