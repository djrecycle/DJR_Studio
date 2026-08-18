#pragma once

#include "UiControls.h"

#include "audio/Mixer.h"
#include "plugins/PluginManager.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Plugin library card: scan action, filters, and the list of what was found.

    With hundreds of plugins the list is only usable filtered, so it carries
    FL's split - generators against effects - plus the plugin's own category.
*/
class PluginBrowserView final : public juce::Component,
                                private juce::Button::Listener,
                                private juce::ChangeListener
{
public:
    PluginBrowserView(PluginManager& pluginManager, Mixer& mixer);
    ~PluginBrowserView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setLoadPluginCallback(std::function<void(int, int)> callback);
    void setOpenEditorCallback(std::function<void(int)> callback);
    void setStatusCallback(std::function<void(const juce::String&)> callback);

    void setTargetTrack(int trackIndex);
    int getSelectedPluginIndex() const noexcept;
    int getSelectedTrackIndex() const noexcept;
    juce::String getSelectedPluginDisplayName() const;
    void setStatusText(const juce::String& text);
    void refreshTrackList();

private:
    class Model final : public juce::ListBoxModel
    {
    public:
        explicit Model(PluginBrowserView& ownerToUse);
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    private:
        PluginBrowserView& owner;
    };

    /** The coarse split, the one FL's browser makes. */
    enum class Group
    {
        all,
        generators,
        effects
    };

    void buttonClicked(juce::Button* button) override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshList();
    /** Re-runs the filters over `plugins` into `visibleIndices`. */
    void applyFilter();
    /** Distinct categories among the plugins that pass the group filter, so the
        menu never offers a category with nothing behind it.
    */
    juce::StringArray getAvailableCategories() const;
    void showCategoryMenu();
    /** The plugin's own category, tidied: JUCE hands back things like
        "Fx|Reverb" for VST3 and a plain class name for LV2.
    */
    static juce::String describeCategory(const juce::PluginDescription& description);
    /** Library index behind a visible row, or -1. */
    int libraryIndexForRow(int row) const;

    PluginManager& pluginManager;
    Mixer& mixer;

    PillButton scanButton { "Scan", Icon::plus, PillButton::Style::filled };
    TabChip allChip { TRANS("All") };
    TabChip generatorChip { TRANS("Generators") };
    TabChip effectChip { TRANS("Effects") };
    PillButton categoryButton { TRANS("All types"), Icon::chevronDown, PillButton::Style::ghost };
    juce::ListBox listBox;
    Model listModel { *this };
    /** The whole library, in the order the manager reports it. Row indices are
        deliberately not used as library indices: the filter breaks that, and
        the insert slot loads by library index.
    */
    juce::Array<juce::PluginDescription> plugins;
    juce::Array<int> visibleIndices;
    Group group = Group::all;
    juce::String categoryFilter;

    std::function<void(int, int)> loadPluginCallback;
    std::function<void(int)> openEditorCallback;
    std::function<void(const juce::String&)> statusCallback;
    int targetTrack = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserView)
};

} // namespace djr
