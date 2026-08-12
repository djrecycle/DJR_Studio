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
