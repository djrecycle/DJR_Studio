#pragma once

#include "Theme.h"
#include "UiControls.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>
#include <vector>

namespace djr
{

class AudioEngine;

/** In-app preferences modal: audio device, MIDI, plugin paths, appearance and a
    shortcut reference.
*/
class PreferencesDialog final : public juce::Component,
                                private juce::Button::Listener,
                                private juce::Slider::Listener
{
public:
    explicit PreferencesDialog(juce::AudioDeviceManager& deviceManager,
                                  AudioEngine& audioEngine);
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
    /** 0 = English, 1 = Bahasa Indonesia. */
    void setLanguageIndex(int index);
    void setLanguageChangedCallback(std::function<void(int)> callback);

    /** The computer-keyboard-as-MIDI settings on the MIDI page. */
    void setTypingKeyboardState(bool enabled, int keymapIndex, int octave);
    void setTypingKeyboardEnabledCallback(std::function<void(bool)> callback);
    /** 0 = FL Studio layout, 1 = the plain A S D F one. */
    void setTypingKeymapCallback(std::function<void(int)> callback);
    void setTypingOctaveCallback(std::function<void(int)> callback);

private:
    /** One line on the Plugins page. Built-in rows carry no remove button:
        dropping a distro's own folder would quietly lose every plugin in it.
    */
    struct PluginPathRow
    {
        juce::String formatName;
        juce::File path;
        bool removable = false;
        juce::Rectangle<int> bounds;
    };

    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void refreshPageVisibility();

    /** Rebuilds the row list and the buttons that go with it, after a path is
        added or removed and once at construction.
    */
    void refreshPluginPaths();
    /** Places the rows and their buttons. Called from resized() so paint() only
        ever reads rectangles somebody else worked out.
    */
    void layOutPluginPaths();
    void chooseFolderForFormat(const juce::String& formatName);

    juce::Rectangle<int> getCardBounds() const;
    juce::Rectangle<int> getSidebarBounds() const;
    juce::Rectangle<int> getContentBounds() const;
    juce::Rectangle<int> getSidebarItemBounds(int index) const;
    juce::Rectangle<int> getThemeCardBounds(int index) const;
    juce::Rectangle<int> getToggleRowBounds(int index) const;

    juce::AudioDeviceManager& deviceManager;
    AudioEngine& audioEngine;
    float smoothedInputLevel = 0.0f;

    juce::StringArray pageNames { "Audio Device", "MIDI", "Plugins", "Appearance", "Shortcuts" };
    int currentPage = 0;  // Audio Device halaman default

    IconChipButton closeButton { "Tutup", Icon::close };
    PillButton scanButton { TRANS("Scan plugins now"), Icon::plus, PillButton::Style::filled };
    SwitchButton typingKeyboardSwitch { "typing keyboard" };
    TabChip englishChip { "English" };
    TabChip indonesianChip { "Bahasa Indonesia" };
    TabChip flKeymapChip { "FL Studio" };
    TabChip simpleKeymapChip { "A S D F" };
    IconChipButton octaveDownButton { TRANS("Octave down"), Icon::chevronLeft };
    IconChipButton octaveUpButton { TRANS("Octave up"), Icon::chevronRight };
    int typingOctave = 4;
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
    std::function<void(int)> languageChangedCallback;
    std::function<void(bool)> typingKeyboardEnabledCallback;
    std::function<void(int)> typingKeymapCallback;
    std::function<void(int)> typingOctaveCallback;

    std::vector<PluginPathRow> pluginPathRows;
    juce::StringArray pluginFormatNames;
    std::vector<juce::Rectangle<int>> pluginFormatHeaders;
    juce::OwnedArray<IconChipButton> removePathButtons;
    juce::OwnedArray<PillButton> addPathButtons;
    std::unique_ptr<juce::FileChooser> pathChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesDialog)
};

} // namespace djr
