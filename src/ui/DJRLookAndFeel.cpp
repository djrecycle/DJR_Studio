#include "DJRLookAndFeel.h"

#include "Theme.h"

namespace djr
{

DJRLookAndFeel::DJRLookAndFeel()
{
    refreshColours();
}

void DJRLookAndFeel::refreshColours()
{
    setColourScheme({ Theme::windowBackground(), Theme::panel(), Theme::panelAlt(),
                      Theme::outlineStrong(), Theme::text(), Theme::accent(),
                      Theme::windowBackground(), Theme::accent(), Theme::text() });

    setColour(juce::ResizableWindow::backgroundColourId, Theme::windowBackground());
    setColour(juce::DocumentWindow::textColourId, Theme::text());

    setColour(juce::PopupMenu::backgroundColourId, Theme::panel());
    setColour(juce::PopupMenu::textColourId, Theme::textSoft());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::control());
    setColour(juce::PopupMenu::highlightedTextColourId, Theme::text());
    setColour(juce::PopupMenu::headerTextColourId, Theme::accent());

    setColour(juce::TooltipWindow::backgroundColourId, Theme::panelAlt());
    setColour(juce::TooltipWindow::textColourId, Theme::textSoft());
    setColour(juce::TooltipWindow::outlineColourId, Theme::outlineStrong());

    setColour(juce::TextEditor::backgroundColourId, Theme::inset());
    setColour(juce::TextEditor::textColourId, Theme::text());
    setColour(juce::TextEditor::highlightColourId, Theme::accent().withAlpha(0.30f));
    setColour(juce::TextEditor::highlightedTextColourId, Theme::text());
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, Theme::accent());

    setColour(juce::ComboBox::backgroundColourId, Theme::control());
    setColour(juce::ComboBox::textColourId, Theme::text());
    setColour(juce::ComboBox::outlineColourId, Theme::outlineStrong());
    setColour(juce::ComboBox::arrowColourId, Theme::mutedText());
    setColour(juce::ComboBox::buttonColourId, Theme::control());

    setColour(juce::Label::textColourId, Theme::text());

    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, Theme::outlineStrong());
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, Theme::control());
    setColour(juce::TextButton::buttonOnColourId, Theme::accent());
    setColour(juce::TextButton::textColourOffId, Theme::text());
    setColour(juce::TextButton::textColourOnId, Theme::windowBackground());

    setColour(juce::Slider::backgroundColourId, Theme::inset());
    setColour(juce::Slider::trackColourId, Theme::accent());
    setColour(juce::Slider::thumbColourId, Theme::text());
    setColour(juce::Slider::rotarySliderFillColourId, Theme::accent());
    setColour(juce::Slider::rotarySliderOutlineColourId, Theme::outlineStrong());
    setColour(juce::Slider::textBoxTextColourId, Theme::text());
    setColour(juce::Slider::textBoxBackgroundColourId, Theme::inset());
    setColour(juce::Slider::textBoxOutlineColourId, Theme::outlineStrong());

    setColour(juce::AlertWindow::backgroundColourId, Theme::panel());
    setColour(juce::AlertWindow::textColourId, Theme::text());
    setColour(juce::AlertWindow::outlineColourId, Theme::outlineStrong());

    setColour(juce::ToggleButton::textColourId, Theme::text());
    setColour(juce::ToggleButton::tickColourId, Theme::accent());
    setColour(juce::ToggleButton::tickDisabledColourId, Theme::outlineStrong());

    setColour(juce::GroupComponent::outlineColourId, Theme::outline());
    setColour(juce::GroupComponent::textColourId, Theme::mutedText());
}

void DJRLookAndFeel::drawStretchableLayoutResizerBar(juce::Graphics& g,
                                                     int w,
                                                     int h,
                                                     bool isVerticalBar,
                                                     bool isMouseOver,
                                                     bool isMouseDragging)
{
    if (! (isMouseOver || isMouseDragging))
        return;

    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
    const auto grip = isVerticalBar ? bounds.withSizeKeepingCentre(2.0f, bounds.getHeight() * 0.5f)
                                    : bounds.withSizeKeepingCentre(bounds.getWidth() * 0.5f, 2.0f);

    g.setColour(Theme::accent().withAlpha(isMouseDragging ? 0.9f : 0.5f));
    g.fillRoundedRectangle(grip, 1.0f);
}

void DJRLookAndFeel::drawScrollbar(juce::Graphics& g,
                                   juce::ScrollBar& scrollbar,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   bool isScrollbarVertical,
                                   int thumbStartPosition,
                                   int thumbSize,
                                   bool isMouseOver,
                                   bool isMouseDown)
{
    juce::ignoreUnused(scrollbar);

    if (thumbSize <= 0)
        return;

    auto thumb = isScrollbarVertical
        ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize).reduced(3, 1)
        : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height).reduced(1, 3);

    auto colour = Theme::outlineStrong();
    if (isMouseDown)
        colour = Theme::accent().withAlpha(0.8f);
    else if (isMouseOver)
        colour = Theme::outlineStrong().brighter(0.4f);

    g.setColour(colour);
    g.fillRoundedRectangle(thumb.toFloat(), thumb.getWidth() * 0.5f);
}

bool DJRLookAndFeel::areScrollbarButtonsVisible()
{
    return false;
}

int DJRLookAndFeel::getDefaultScrollbarWidth()
{
    return 10;
}

juce::Font DJRLookAndFeel::getPopupMenuFont()
{
    return Theme::ui(13.5f);
}

void DJRLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);

    g.setColour(Theme::panel());
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(Theme::outlineStrong());
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);
}

void DJRLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);

    g.setColour(Theme::panelAlt());
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(Theme::outlineStrong());
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    g.setColour(Theme::textSoft());
    g.setFont(Theme::ui(12.0f));
    g.drawFittedText(text, bounds.toNearestInt().reduced(8, 4), juce::Justification::centred, 2);
}

} // namespace djr
