#pragma once

#include <juce_graphics/juce_graphics.h>

namespace djr
{

enum class Icon
{
    play,
    pause,
    stop,
    record,
    loop,
    metronome,
    folder,
    gear,
    search,
    panel,
    chevronLeft,
    chevronRight,
    chevronDown,
    caretUp,
    caretDown,
    minimise,
    restore,
    dockLeft,
    dockRight,
    dockBottom,
    grid,
    lines,
    plus,
    close,
    plug,
    waveform,
    notes,
    steps,
    undo,
    redo,
    popOut,

    // Playlist tools
    magnet,
    pencil,
    eraser,
    speakerMute,
    slip,
    slice,
    marquee,
    zoom
};

/** Vector icon set matching the SVG shapes used in the DJR_Studio redesign. */
class Icons
{
public:
    /** The DJR logo cut down to what still reads at menu-bar size: the two
        "recycle" arrows circling, and the music note from the middle of the
        R. The letters, waveforms and wordmark of the full logo
        (docs/branding/logo.png) turn to mush below ~48 px. Drawn as paths
        rather than a shrunk PNG so it stays sharp at every interface scale.
    */
    static void drawLogoMark(juce::Graphics& g, juce::Rectangle<float> area,
                             juce::Colour from, juce::Colour to);

    /** Builds the icon path inside the given bounds. */
    static juce::Path path(Icon icon, juce::Rectangle<float> bounds);

    /** True when the icon is designed to be filled rather than stroked. */
    static bool isFilled(Icon icon) noexcept;

    /** Draws the icon using the graphics context's current colour. */
    static void draw(juce::Graphics& g,
                     Icon icon,
                     juce::Rectangle<float> bounds,
                     float strokeThickness = 1.6f);
};

} // namespace djr
