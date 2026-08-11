#pragma once

#include "AppCommands.h"
#include "UiControls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Top application bar: logo, drop-down menus, project identity and the two
    header actions ("Projects" and "Preferences").
*/
class MenuBarView final : public juce::Component,
                          private juce::Button::Listener
{
public:
    MenuBarView();

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setCommandHandler(std::function<void(AppCommand)> handler);
    void setProjectInfo(const juce::String& folder, const juce::String& name, bool hasUnsavedChanges);
    void setViewState(ViewState state);

private:
    void buttonClicked(juce::Button* button) override;
    void showMenuFor(int menuIndex, juce::Button& source);
    void invoke(AppCommand command) const;

    juce::OwnedArray<MenuBarItem> menuItems;
    PillButton projectsButton { "Projects", Icon::folder, PillButton::Style::outline };
    PillButton preferencesButton { "Preferences", Icon::gear, PillButton::Style::outline };

    std::function<void(AppCommand)> commandHandler;
    juce::String projectFolder { "~/DJR_Studio Projects/" };
    juce::String projectName { "Untitled.djrs" };
    bool unsavedChanges = true;
    ViewState viewState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MenuBarView)
};

} // namespace djr
