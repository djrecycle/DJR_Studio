#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace djr
{

class PluginWindow final : public juce::DocumentWindow
{
public:
    explicit PluginWindow(juce::AudioProcessor& processor);
    ~PluginWindow() override;

    juce::AudioProcessor& getProcessor() noexcept;
    void closeButtonPressed() override;

private:
    juce::AudioProcessor& audioProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace djr
