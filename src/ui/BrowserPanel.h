#pragma once

#include "UiControls.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** Left hand browser: search field, section chips, a file/plugin tree and the
    VST3 scan progress footer.
*/
class BrowserPanel final : public juce::Component,
                           private juce::Button::Listener,
                           private juce::TextEditor::Listener
{
public:
    enum class DockPosition
    {
        left,
        right,
        bottom
    };

    BrowserPanel();
    ~BrowserPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setCollapsed(bool shouldCollapse);
    void setMinimized(bool shouldMinimize);
    void setDockPosition(DockPosition position);
    void setCollapseToggleCallback(std::function<void()> callback);
    void setMinimizeToggleCallback(std::function<void()> callback);
    void setDockCycleCallback(std::function<void()> callback);
    void setFileActivatedCallback(std::function<void(const juce::File&)> callback);
    void setVst3Plugins(const juce::Array<juce::PluginDescription>& plugins);
    void setScanProgress(bool scanning, int scanned, int total, const juce::String& currentItem);
    void refreshContent();
    juce::String getSelectedSection() const;

private:
    struct Row
    {
        juce::String name;
        juce::String meta;
        bool isGroup = false;
        juce::File file;
        juce::Colour dot;
    };

    class RowsModel final : public juce::ListBoxModel
    {
    public:
        explicit RowsModel(BrowserPanel& ownerToUse);
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    private:
        BrowserPanel& owner;
    };

    void buttonClicked(juce::Button* button) override;
    void textEditorTextChanged(juce::TextEditor& editor) override;
    void selectSection(int index);
    void rebuildRows();
    void appendFolder(const juce::File& folder, const juce::String& wildcard);
    void refreshControls();
    static juce::String describeFile(const juce::File& file);

    juce::StringArray sections { "Projects", "Samples", "Recordings", "Presets", "VST3" };
    juce::OwnedArray<TabChip> sectionChips;
    IconChipButton dockButton { "Move browser dock", Icon::dockLeft };
    IconChipButton minimizeButton { "Minimize browser", Icon::minimise };
    IconChipButton collapseButton { "Collapse browser", Icon::chevronLeft };
    juce::TextEditor searchBox;
    juce::ListBox rowsList;
    RowsModel rowsModel { *this };

    std::vector<Row> rows;
    juce::StringArray vst3PluginNames;
    juce::StringArray vst3PluginMakers;

    std::function<void()> collapseToggleCallback;
    std::function<void()> minimizeToggleCallback;
    std::function<void()> dockCycleCallback;
    std::function<void(const juce::File&)> fileActivatedCallback;

    DockPosition dockPosition = DockPosition::left;
    int selectedSectionIndex = 1;
    bool collapsed = false;
    bool minimized = false;
    bool scanning = false;
    int scanScanned = 0;
    int scanTotal = 0;
    juce::String scanCurrentItem;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BrowserPanel)
};

} // namespace djr
