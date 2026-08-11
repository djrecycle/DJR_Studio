#pragma once

#include "UiControls.h"

#include "audio/Mixer.h"
#include "plugins/PluginManager.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Plugin library card: scan action plus the list of discovered VST3 plugins. */
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

    void buttonClicked(juce::Button* button) override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshList();

    PluginManager& pluginManager;
    Mixer& mixer;

    PillButton scanButton { "Scan", Icon::plus, PillButton::Style::filled };
    juce::ListBox listBox;
    Model listModel { *this };
    juce::Array<juce::PluginDescription> plugins;

    std::function<void(int, int)> loadPluginCallback;
    std::function<void(int)> openEditorCallback;
    std::function<void(const juce::String&)> statusCallback;
    int targetTrack = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginBrowserView)
};

} // namespace djr
