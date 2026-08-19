#pragma once

#include "MixerChannelStrip.h"

#include "audio/Mixer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Horizontal mixer rack with one strip per track plus the master bus. */
class MixerView final : public juce::Component
{
public:
    explicit MixerView(Mixer& mixer);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshStrips();
    /** Passed down to every strip so the input menu matches the real device. */
    void setDeviceInputCount(int count);

    void setSelectedTrack(int trackIndex);
    void setTrackSelectedCallback(std::function<void(int)> callback);
    /** Fired when a strip creates or removes an automation lane. */
    void setAutomationChangedCallback(std::function<void()> callback);

private:
    class StripHolder final : public juce::Component
    {
    public:
        void resized() override;
        juce::OwnedArray<MixerChannelStrip> strips;
    };

    Mixer& mixer;
    juce::Viewport viewport;
    StripHolder holder;
    std::function<void(int)> trackSelectedCallback;
    std::function<void()> automationChangedCallback;
    int selectedTrack = 0;

    int deviceInputCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

} // namespace djr
