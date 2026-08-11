#pragma once

#include "Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** FL Studio style floating panel: a compact title bar you can drag, resizable
    edges, and roll-up / maximise / close buttons.

    The panel does not own its content - the owner keeps the real view alive and
    hands it over with setContent().
*/
class PanelWindow final : public juce::Component
{
public:
    PanelWindow(const juce::String& panelTitle, Icon panelIcon);
    ~PanelWindow() override;

    void setContent(juce::Component* newContent);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    /** Roll the panel up so only its title bar is left. */
    void setRolledUp(bool shouldRollUp);
    bool isRolledUp() const noexcept;

    /** Fill the whole workspace. */
    void setMaximised(bool shouldMaximise);
    bool isMaximised() const noexcept;

    void toggleRollUp();
    void toggleMaximise();

    /** Bounds the panel returns to when un-maximised / un-rolled. */
    void setRestoredBounds(juce::Rectangle<int> bounds);
    juce::Rectangle<int> getRestoredBounds() const noexcept;

    juce::Rectangle<int> getContentBounds() const;
    juce::String getPanelTitle() const;

    std::function<void()> onCloseRequested;
    std::function<void(PanelWindow&)> onBroughtToFront;
    std::function<void(PanelWindow&)> onLayoutChanged;

private:
    class TitleButton final : public juce::Button
    {
    public:
        TitleButton(const juce::String& buttonName, Icon buttonIcon);
        void setIcon(Icon newIcon);
        void setDangerHover(bool shouldUseDanger);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        Icon icon;
        bool danger = false;
    };

    juce::Rectangle<int> getTitleBarBounds() const;
    void applyRestoredBounds();
    void notifyLayoutChanged();

    juce::String title;
    Icon icon;
    juce::Component* content = nullptr;

    TitleButton rollUpButton { "Gulung panel", Icon::minimise };
    TitleButton maximiseButton { "Perbesar penuh", Icon::restore };
    TitleButton closeButton { "Tutup panel", Icon::close };

    std::unique_ptr<juce::ResizableBorderComponent> resizer;
    juce::ComponentBoundsConstrainer constrainer;
    juce::ComponentDragger dragger;

    juce::Rectangle<int> restoredBounds;
    bool rolledUp = false;
    bool maximised = false;
    bool draggingTitle = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelWindow)
};

} // namespace djr
