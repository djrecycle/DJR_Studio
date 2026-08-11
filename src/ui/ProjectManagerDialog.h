#pragma once

#include "UiControls.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** In-app modal listing recent .djrs projects, with new/open actions. */
class ProjectManagerDialog final : public juce::Component,
                                   private juce::Button::Listener
{
public:
    ProjectManagerDialog();
    ~ProjectManagerDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void refresh();
    void setCloseCallback(std::function<void()> callback);
    void setOpenProjectCallback(std::function<void(const juce::File&)> callback);
    void setNewProjectCallback(std::function<void()> callback);
    void setBrowseCallback(std::function<void()> callback);

private:
    struct Entry
    {
        juce::File file;
        juce::String name;
        juce::String meta;
        juce::String when;
        juce::String tempo;
        juce::Colour colour;
    };

    void buttonClicked(juce::Button* button) override;
    juce::Rectangle<int> getCardBounds() const;
    juce::Rectangle<int> getEntryBounds(int index) const;

    IconChipButton closeButton { "Tutup", Icon::close };
    TabChip recentChip { "Recent" };
    TabChip templatesChip { "Templates" };
    TabChip backupsChip { "Backups" };
    PillButton newProjectButton { "+ New project", std::nullopt, PillButton::Style::filled };
    PillButton browseButton { "Browse...", Icon::folder, PillButton::Style::outline };

    std::vector<Entry> entries;
    int selectedTab = 0;

    std::function<void()> closeCallback;
    std::function<void(const juce::File&)> openProjectCallback;
    std::function<void()> newProjectCallback;
    std::function<void()> browseCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectManagerDialog)
};

} // namespace djr
