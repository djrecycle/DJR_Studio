#pragma once

#include <juce_graphics/juce_graphics.h>

namespace djr
{

enum class ThemeVariant
{
    neonDark,
    amberStudio,
    iceGrey
};

/** Shared structural sizes. Kept deliberately tight so the shell reads like a
    tracker/DAW toolbar rather than a web dashboard.
*/
namespace Metrics
{
    inline constexpr int menuBarHeight     = 24;
    inline constexpr int transportHeight   = 34;
    inline constexpr int statusBarHeight   = 19;
    inline constexpr int panelTitleHeight  = 19;
    inline constexpr int panelToolbarHeight = 22;
    inline constexpr int browserWidth      = 204;
    inline constexpr int gap               = 3;
    inline constexpr float panelRadius     = 5.0f;
}

/** Central palette, typography and shared painting helpers for the DJR_Studio shell.

    Every colour is served through a function so that switching ThemeVariant at runtime
    only needs a repaint of the top level component.
*/
class Theme
{
public:
    static void setVariant(ThemeVariant variant) noexcept;
    static ThemeVariant getVariant() noexcept;
    static juce::String getVariantName(ThemeVariant variant);
    static juce::Colour getVariantAccent(ThemeVariant variant);

    // Surfaces -----------------------------------------------------------------
    static juce::Colour windowBackground();
    static juce::Colour menuBarTop();
    static juce::Colour menuBarBottom();
    static juce::Colour transportBackground();
    static juce::Colour panel();
    static juce::Colour panelAlt();
    static juce::Colour panelDeep();
    static juce::Colour panelHeader();
    static juce::Colour inset();
    static juce::Colour meterTrack();
    static juce::Colour control();
    static juce::Colour controlHover();
    static juce::Colour outline();
    static juce::Colour outlineStrong();
    static juce::Colour divider();

    // Text --------------------------------------------------------------------
    static juce::Colour text();
    static juce::Colour textSoft();
    static juce::Colour mutedText();
    static juce::Colour faintText();

    // Accents -----------------------------------------------------------------
    static juce::Colour accent();
    static juce::Colour cyan();
    static juce::Colour purple();
    static juce::Colour green();
    static juce::Colour amber();
    static juce::Colour pink();
    static juce::Colour grid();
    static juce::Colour gridBeat();
    static juce::Colour gridBar();
    static juce::Colour trackColour(int index);

    // Typography --------------------------------------------------------------
    /** Body / control font. */
    static juce::Font ui(float height, bool bold = false);
    /** Condensed, letter-spaced font used for the logo and panel titles. */
    static juce::Font display(float height);
    /** Monospaced font for numeric readouts. */
    static juce::Font mono(float height, bool bold = false);
    /** Tiny upper-case caption font ("TEMPO", "POSITION", ...). */
    static juce::Font caps(float height);

    /** Width needed to draw `text` in `font` without clipping. */
    static int textWidth(const juce::Font& font, const juce::String& text);

    // Painting helpers --------------------------------------------------------
    static void drawCard(juce::Graphics& g,
                         juce::Rectangle<int> bounds,
                         float radius = 10.0f);
    static void drawCard(juce::Graphics& g,
                         juce::Rectangle<int> bounds,
                         juce::Colour fill,
                         juce::Colour border,
                         float radius = 10.0f);
    /** Small caps caption, already coloured and positioned. */
    static void drawCaption(juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            const juce::String& caption,
                            juce::Justification justification = juce::Justification::centredLeft);
    /** Panel title such as "BROWSER" or "MIXER". */
    static void drawPanelTitle(juce::Graphics& g,
                               juce::Rectangle<int> bounds,
                               const juce::String& title);
    /** Green -> cyan -> amber -> red level meter. */
    static void drawLevelMeter(juce::Graphics& g,
                               juce::Rectangle<float> bounds,
                               float level,
                               bool vertical,
                               float cornerSize = 2.0f);
};

} // namespace djr
