#pragma once

#include "Theme.h"
#include "UiControls.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** In-app preferences modal: audio device, MIDI, plugin paths, appearance and a
    shortcut reference.
*/
class PreferencesDialog final : public juce::Component,
                                private juce::Button::Listener,
                                private juce::Slider::Listener
{
public:
    explicit PreferencesDialog(juce::AudioDeviceManager& deviceManager);
    ~PreferencesDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    void showPage(int pageIndex);
    void setScalePercent(int percent);
    void setToggleStates(bool tooltips, bool autoScroll, bool autoOpenEditor);

    void setCloseCallback(std::function<void()> callback);
    void setThemeChangedCallback(std::function<void(ThemeVariant)> callback);
    void setScaleChangedCallback(std::function<void(int)> callback);
    void setTooltipsChangedCallback(std::function<void(bool)> callback);
    void setAutoScrollChangedCallback(std::function<void(bool)> callback);
    void setAutoOpenEditorChangedCallback(std::function<void(bool)> callback);
    void setScanRequestedCallback(std::function<void()> callback);

private:
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void refreshPageVisibility();

    juce::Rectangle<int> getCardBounds() const;
    juce::Rectangle<int> getSidebarBounds() const;
    juce::Rectangle<int> getContentBounds() const;
    juce::Rectangle<int> getSidebarItemBounds(int index) const;
    juce::Rectangle<int> getThemeCardBounds(int index) const;
    juce::Rectangle<int> getToggleRowBounds(int index) const;

    juce::AudioDeviceManager& deviceManager;

    juce::StringArray pageNames { "Audio Device", "MIDI", "Plugins", "Appearance", "Shortcuts" };
    int currentPage = 3;

    IconChipButton closeButton { "Tutup", Icon::close };
    PillButton scanButton { "Scan VST3 sekarang", Icon::plus, PillButton::Style::filled };
    juce::Slider scaleSlider;
    SwitchButton tooltipsSwitch { "tooltips" };
    SwitchButton autoScrollSwitch { "autoScroll" };
    SwitchButton autoOpenEditorSwitch { "autoOpenEditor" };

    std::unique_ptr<juce::AudioDeviceSelectorComponent> audioSelector;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> midiSelector;

    std::function<void()> closeCallback;
    std::function<void(ThemeVariant)> themeChangedCallback;
    std::function<void(int)> scaleChangedCallback;
    std::function<void(bool)> tooltipsChangedCallback;
    std::function<void(bool)> autoScrollChangedCallback;
    std::function<void(bool)> autoOpenEditorChangedCallback;
    std::function<void()> scanRequestedCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesDialog)
};

} // namespace djr
