#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace djr
{

/** How big the whole interface is drawn, applied through JUCE's global scale
    factor so every window - the main one, popup menus, tooltips, dialogs and
    detached panels - grows together.

    Why the app needs its own: on Linux JUCE only follows an integer desktop
    scale (GNOME's `scaling-factor`, or X11's Gdk/WindowScalingFactor). A
    GNOME/KDE Wayland session with fractional scaling, or any desktop left at
    100% on a 1440p/4K monitor, reports 1 - and the app came up at its 1080p
    size, i.e. tiny. "Auto" picks a factor from the screen itself; the other
    choices are fixed percentages on top of whatever the desktop already does.
*/
namespace UiScale
{
    /** The choice value meaning "work it out from the screen". */
    constexpr int autoChoice = 0;

    /** The window can never be made smaller than this (logical pixels). */
    constexpr int minimumWindowWidth = 1024;
    constexpr int minimumWindowHeight = 620;

    /** autoChoice first, then the fixed percentages, in order. */
    const juce::Array<int>& getChoices();

    /** What one screen looks like to the scaling maths, independent of
        whatever global factor happens to be installed right now.
    */
    struct Metrics
    {
        /** In real device pixels. */
        juce::Rectangle<int> physicalTotalArea;
        juce::Rectangle<int> physicalUserArea;
        /** Physical DPI as the display reports it; 0 if unknown. */
        double dpi = 0.0;
        /** The scale the desktop itself already applies (2.0 on a 200% GNOME). */
        double platformScale = 1.0;
    };

    /** The global factor "Auto" chooses for this screen. Never below 1, so it
        only ever enlarges what the desktop already set; never so large that
        the work area drops under the 1280 x 720 logical pixels the panel
        layout needs.
    */
    double chooseAutoFactor(const Metrics& metrics);

    /** The global factor a choice turns into on this screen. A fixed
        percentage above 100 is lowered until the panel layout fits again (a
        250% choice saved on a 4K monitor, then opened on a laptop).
    */
    double resolveFactor(int choice, const Metrics& metrics);

    /** Whether a fixed percentage still leaves room for the panel layout. */
    bool fits(int percent, const Metrics& metrics);

    /** The primary display, measured; empty when headless. */
    std::optional<Metrics> measurePrimaryDisplay();

    int loadSavedChoice();
    void saveChoice(int choice);

    /** Installs the choice as the desktop's global scale and returns the
        factor actually applied.
    */
    double apply(int choice);
}

} // namespace djr
