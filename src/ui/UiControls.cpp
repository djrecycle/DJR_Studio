#include "UiControls.h"

#include "Theme.h"

namespace djr
{

namespace
{
    // Big enough to read at a glance rather than to be identified on inspection.
    // FL's are chunkier than this was, and an icon nobody can make out is a
    // decoration with a tooltip.
    constexpr float pillIconSize = 14.0f;
    constexpr int pillIconGap = 5;
    constexpr int pillPadding = 8;

    int textWidth(const juce::Font& font, const juce::String& text)
    {
        return Theme::textWidth(font, text);
    }
}

//==============================================================================
PillButton::PillButton(const juce::String& buttonText, std::optional<Icon> icon, Style style)
    : juce::Button(buttonText), iconKind(icon), buttonStyle(style)
{
    setButtonText(buttonText);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PillButton::setStyle(Style newStyle)
{
    buttonStyle = newStyle;
    repaint();
}

void PillButton::setFillColour(juce::Colour colour)
{
    fillColour = colour;
    repaint();
}

void PillButton::setIcon(std::optional<Icon> icon)
{
    iconKind = icon;
    repaint();
}

void PillButton::setCornerSize(float radius)
{
    cornerSize = radius;
    repaint();
}

int PillButton::getPreferredWidth() const
{
    auto width = pillPadding * 2 + textWidth(Theme::ui(12.5f, buttonStyle == Style::filled), getButtonText());

    if (iconKind.has_value())
        width += juce::roundToInt(pillIconSize) + pillIconGap;

    return width;
}

void PillButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto enabled = isEnabled();

    juce::Colour background;
    juce::Colour border;
    juce::Colour foreground;

    switch (buttonStyle)
    {
        case Style::filled:
        {
            const auto base = fillColour.value_or(Theme::accent());
            background = down ? base.brighter(0.2f) : highlighted ? base.brighter(0.1f) : base;
            border = background;
            foreground = Theme::windowBackground();
            break;
        }

        case Style::ghost:
            background = down ? Theme::controlHover() : highlighted ? Theme::control() : juce::Colours::transparentBlack;
            border = juce::Colours::transparentBlack;
            foreground = highlighted ? Theme::text() : Theme::textSoft();
            break;

        case Style::outline:
        default:
            background = down ? Theme::controlHover() : highlighted ? Theme::controlHover() : Theme::control();
            border = highlighted ? Theme::accent() : Theme::outlineStrong();
            foreground = Theme::text();
            break;
    }

    if (! enabled)
    {
        background = background.withMultipliedAlpha(0.45f);
        border = border.withMultipliedAlpha(0.45f);
        foreground = foreground.withMultipliedAlpha(0.45f);
    }

    if (! background.isTransparent())
    {
        g.setColour(background);
        g.fillRoundedRectangle(bounds, cornerSize);
    }

    if (! border.isTransparent())
    {
        g.setColour(border);
        g.drawRoundedRectangle(bounds, cornerSize, 1.0f);
    }

    const auto bold = buttonStyle == Style::filled;
    const auto font = Theme::ui(12.5f, bold);
    const auto labelWidth = textWidth(font, getButtonText());
    auto contentWidth = labelWidth;

    if (iconKind.has_value())
        contentWidth += juce::roundToInt(pillIconSize) + pillIconGap;

    auto content = bounds.withSizeKeepingCentre(static_cast<float>(contentWidth), bounds.getHeight());

    if (iconKind.has_value())
    {
        const auto iconArea = content.removeFromLeft(pillIconSize)
                                     .withSizeKeepingCentre(pillIconSize, pillIconSize);
        g.setColour(buttonStyle == Style::filled ? foreground : Theme::accent());
        Icons::draw(g, *iconKind, iconArea, 1.7f);
        content.removeFromLeft(static_cast<float>(pillIconGap));
    }

    g.setColour(foreground);
    g.setFont(font);
    g.drawText(getButtonText(), content, juce::Justification::centredLeft, false);
}

//==============================================================================
IconChipButton::IconChipButton(const juce::String& tooltipText, Icon icon)
    : juce::Button(tooltipText), iconKind(icon)
{
    setTooltip(tooltipText);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void IconChipButton::setIcon(Icon icon)
{
    iconKind = icon;
    repaint();
}

void IconChipButton::setDangerHover(bool shouldUseDangerHover)
{
    dangerHover = shouldUseDangerHover;
}

void IconChipButton::setHighlighted(bool shouldHighlight)
{
    if (active == shouldHighlight)
        return;

    active = shouldHighlight;
    repaint();
}

void IconChipButton::setCornerSize(float radius)
{
    cornerSize = radius;
    repaint();
}

void IconChipButton::setIconInset(float inset)
{
    iconInset = inset;
    repaint();
}

void IconChipButton::paintButton(juce::Graphics& g, bool mouseOver, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    // `active` is the selected-tool state; `mouseOver` is only hover feedback.
    auto background = active ? Theme::accent() : Theme::control();

    if (down)
        background = dangerHover ? Theme::pink().darker(0.1f) : background.brighter(0.15f);
    else if (mouseOver)
        background = dangerHover ? Theme::pink() : background.brighter(0.12f);

    g.setColour(background);
    g.fillRoundedRectangle(bounds, cornerSize);

    const auto tint = active                     ? Theme::windowBackground()
                    : (mouseOver && dangerHover) ? Theme::windowBackground()
                    : mouseOver                  ? Theme::text()
                                                 : Theme::textSoft();

    g.setColour(isEnabled() ? tint : tint.withMultipliedAlpha(0.4f));
    Icons::draw(g, iconKind, bounds.reduced(iconInset), 1.7f);
}

//==============================================================================
TabChip::TabChip(const juce::String& chipText)
    : juce::Button(chipText)
{
    setButtonText(chipText);
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int TabChip::getPreferredWidth() const
{
    return 14 + textWidth(Theme::ui(12.0f, true), getButtonText());
}

void TabChip::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    const auto active = getToggleState();

    auto background = active ? Theme::accent() : Theme::control();
    if (down || highlighted)
        background = background.brighter(active ? 0.12f : 0.18f);

    g.setColour(background);
    g.fillRoundedRectangle(bounds, 4.0f);

    g.setColour(active ? Theme::windowBackground() : Theme::textSoft());
    g.setFont(Theme::ui(12.0f, true));
    g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
UnderlineTab::UnderlineTab(const juce::String& tabText, Icon icon)
    : juce::Button(tabText), iconKind(icon)
{
    setButtonText(tabText);
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int UnderlineTab::getPreferredWidth() const
{
    return 18 + 11 + 5 + textWidth(Theme::ui(12.0f, true), getButtonText());
}

void UnderlineTab::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused(down);

    auto bounds = getLocalBounds().toFloat();
    const auto active = getToggleState();
    const auto foreground = active ? Theme::text() : highlighted ? Theme::textSoft() : Theme::mutedText();

    auto content = bounds.reduced(9.0f, 0.0f);
    const auto iconArea = content.removeFromLeft(13.0f).withSizeKeepingCentre(13.0f, 13.0f);
    content.removeFromLeft(5.0f);

    g.setColour(active ? Theme::accent() : foreground);
    Icons::draw(g, iconKind, iconArea, 1.7f);

    g.setColour(foreground);
    g.setFont(Theme::ui(12.0f, true));
    g.drawText(getButtonText(), content, juce::Justification::centredLeft, false);

    if (active)
    {
        g.setColour(Theme::accent());
        g.fillRect(bounds.removeFromBottom(2.0f));
    }
}

//==============================================================================
MenuBarItem::MenuBarItem(const juce::String& itemText)
    : juce::Button(itemText)
{
    setButtonText(itemText);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int MenuBarItem::getPreferredWidth() const
{
    return 15 + textWidth(Theme::ui(12.5f), getButtonText());
}

void MenuBarItem::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();

    if (highlighted || down)
    {
        g.setColour(down ? Theme::controlHover() : Theme::control());
        g.fillRoundedRectangle(bounds, 5.0f);
    }

    g.setColour(highlighted || down ? Theme::text() : Theme::textSoft());
    g.setFont(Theme::ui(13.0f));
    g.drawText(getButtonText(), bounds, juce::Justification::centred, false);
}

//==============================================================================
HitAreaButton::HitAreaButton(const juce::String& buttonName)
    : juce::Button(buttonName)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void HitAreaButton::setCornerSize(float radius)
{
    cornerSize = radius;
    repaint();
}

void HitAreaButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    if (! (highlighted || down))
        return;

    g.setColour(Theme::text().withAlpha(down ? 0.10f : 0.06f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), cornerSize);
}

//==============================================================================
SwitchButton::SwitchButton(const juce::String& switchName)
    : juce::Button(switchName)
{
    setClickingTogglesState(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SwitchButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    juce::ignoreUnused(down);

    const auto track = getLocalBounds().toFloat().withSizeKeepingCentre(28.0f, 15.0f);
    const auto on = getToggleState();

    auto trackColour = on ? Theme::accent() : Theme::outlineStrong();
    if (highlighted)
        trackColour = trackColour.brighter(0.15f);

    g.setColour(trackColour);
    g.fillRoundedRectangle(track, 9.0f);

    const auto knobX = on ? track.getRight() - 16.0f : track.getX() + 2.0f;
    g.setColour(Theme::text());
    g.fillEllipse(knobX, track.getY() + 2.0f, 14.0f, 14.0f);
}

} // namespace djr
