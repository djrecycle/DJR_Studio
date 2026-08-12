#pragma once

#include "Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace djr
{

/** Rounded pill button with an optional leading icon, as used in the redesign's
    header ("Projects", "Preferences") and panel headers ("Scan").
*/
class PillButton final : public juce::Button
{
public:
    enum class Style
    {
        outline,  ///< dark control fill, accent border on hover
        filled,   ///< solid accent fill with dark text
        ghost     ///< no fill until hovered
    };

    PillButton(const juce::String& buttonText,
               std::optional<Icon> icon = std::nullopt,
               Style style = Style::outline);

    void setStyle(Style newStyle);
    /** Only used by Style::filled. Defaults to the current theme accent. */
    void setFillColour(juce::Colour colour);
    void setIcon(std::optional<Icon> icon);
    void setCornerSize(float radius);
    int getPreferredWidth() const;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    std::optional<Icon> iconKind;
    Style buttonStyle;
    std::optional<juce::Colour> fillColour;
    float cornerSize = 6.0f;
};

/** Small square icon button (browser header, tempo nudges, modal close). */
class IconChipButton final : public juce::Button
{
public:
    IconChipButton(const juce::String& tooltipText, Icon icon);

    void setIcon(Icon icon);
    void setDangerHover(bool shouldUseDangerHover);
    /** Marks the chip as the active choice in a group, like a selected tool. */
    void setHighlighted(bool shouldHighlight);
    void setCornerSize(float radius);
    void setIconInset(float inset);

    void paintButton(juce::Graphics& g, bool mouseOver, bool down) override;

private:
    Icon iconKind;
    bool dangerHover = false;
    bool active = false;
    float cornerSize = 5.0f;
    float iconInset = 6.0f;
};

/** Rounded chip used for the browser section tabs and the modal tab rows. */
class TabChip final : public juce::Button
{
public:
    explicit TabChip(const juce::String& chipText);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    int getPreferredWidth() const;
};

/** Editor tab drawn as text with a 2px accent underline when selected. */
class UnderlineTab final : public juce::Button
{
public:
    UnderlineTab(const juce::String& tabText, Icon icon);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    int getPreferredWidth() const;

private:
    Icon iconKind;
};

/** Top menu bar entry: flat text that gains a rounded hover background. */
class MenuBarItem final : public juce::Button
{
public:
    explicit MenuBarItem(const juce::String& itemText);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    int getPreferredWidth() const;
};

/** Transparent click target that only shows a subtle hover wash. */
class HitAreaButton final : public juce::Button
{
public:
    explicit HitAreaButton(const juce::String& buttonName);

    void setCornerSize(float radius);
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    float cornerSize = 9.0f;
};

/** iOS style toggle used in the preferences dialog. */
class SwitchButton final : public juce::Button
{
public:
    explicit SwitchButton(const juce::String& switchName);

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
};

} // namespace djr
