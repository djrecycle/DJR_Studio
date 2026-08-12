#pragma once

#include "PanelWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Workspace that hosts the floating PanelWindows, keeps them inside its bounds
    and raises whichever one you click.
*/
class PanelHost final : public juce::Component
{
public:
    PanelHost();

    /** Adds a panel wrapping `content`. The host owns the window, not the content. */
    PanelWindow& addPanel(const juce::String& panelId,
                          const juce::String& title,
                          Icon icon,
                          juce::Component& content);

    PanelWindow* findPanel(const juce::String& panelId) const;
    juce::StringArray getPanelIds() const;

    bool isPanelOpen(const juce::String& panelId) const;
    void setPanelOpen(const juce::String& panelId, bool shouldBeOpen);
    void togglePanel(const juce::String& panelId);

    /** Supplies the tiling used by resetLayout() and on first show. */
    void setLayoutBuilder(std::function<void(PanelHost&, juce::Rectangle<int>)> builder);
    void resetLayout();

    /** Panel positions and states, for saving with the project. */
    juce::var getLayoutState() const;
    void applyLayoutState(const juce::var& state);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    std::function<void()> onPanelStateChanged;

private:
    void keepPanelOnScreen(PanelWindow& panel);
    void notifyStateChanged();

    juce::OwnedArray<PanelWindow> panels;
    juce::StringArray panelIds;
    std::function<void(PanelHost&, juce::Rectangle<int>)> layoutBuilder;
    /** Until the user arranges a panel by hand, resizing the host re-tiles. */
    bool userArranged = false;
    bool applyingLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelHost)
};

} // namespace djr
