#pragma once

#include "UiControls.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** Left hand browser: search field, section chips, a file/plugin tree and the
    Plugin scan progress footer.
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
    /** Fired when a plugin row is double clicked, with its index in the library.
        Without this the list was only ever something to look at.
    */
    void setPluginActivatedCallback(std::function<void(int)> callback);
    void setPluginList(const juce::Array<juce::PluginDescription>& plugins);
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
        /** Index into the plugin library for a plugin row, -1 otherwise. Kept
            here rather than derived from the row position because the search
            box filters the list, so the two stop lining up.
        */
        int pluginIndex = -1;
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

    juce::StringArray sections { "Projects", "Samples", "Recordings", "Presets", "Plugins" };
    juce::OwnedArray<TabChip> sectionChips;
    IconChipButton dockButton { "Move browser dock", Icon::dockLeft };
    IconChipButton minimizeButton { "Minimize browser", Icon::minimise };
    IconChipButton collapseButton { "Collapse browser", Icon::chevronLeft };
    juce::TextEditor searchBox;
    juce::ListBox rowsList;
    RowsModel rowsModel { *this };

    std::vector<Row> rows;
    juce::StringArray pluginNames;
    juce::StringArray pluginMetas;
    /** Parallel to the two above; decides which group a plugin is listed under. */
    juce::Array<bool> pluginIsInstrument;

    std::function<void()> collapseToggleCallback;
    std::function<void()> minimizeToggleCallback;
    std::function<void()> dockCycleCallback;
    std::function<void(const juce::File&)> fileActivatedCallback;
    std::function<void(int)> pluginActivatedCallback;

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
