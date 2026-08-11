#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace djr
{

/** Application-wide look and feel so that JUCE's stock widgets (scrollbars, popup
    menus, tooltips, resizer bars, file choosers) match the DJR_Studio shell.
*/
class DJRLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DJRLookAndFeel();

    /** Re-applies every colour after a theme variant change. */
    void refreshColours();

    void drawStretchableLayoutResizerBar(juce::Graphics& g,
                                         int w,
                                         int h,
                                         bool isVerticalBar,
                                         bool isMouseOver,
                                         bool isMouseDragging) override;

    void drawScrollbar(juce::Graphics& g,
                       juce::ScrollBar& scrollbar,
                       int x,
                       int y,
                       int width,
                       int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;

    bool areScrollbarButtonsVisible() override;
    int getDefaultScrollbarWidth() override;

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
};

} // namespace djr
