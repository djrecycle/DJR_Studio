#pragma once

#include "MixerChannelStrip.h"

#include "audio/Mixer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Horizontal mixer rack with one strip per track plus the master bus. */
class MixerView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MixerView(Mixer& mixer);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void refreshStrips();
    /** Passed down to every strip so the input menu matches the real device. */
    void setDeviceInputCount(int count);

    /** How tall the view needs to be for every strip to show whole - its
        header, padding and the tallest strip's minimum - without scrolling.
    */
    int getPreferredHeight() const;

    void setSelectedTrack(int trackIndex);
    void setTrackSelectedCallback(std::function<void(int)> callback);
    /** Fired when a strip creates or removes an automation lane. */
    void setAutomationChangedCallback(std::function<void()> callback);
    /** Fired when a strip's name is double clicked: open that channel. */
    void setOpenChannelCallback(std::function<void(int)> callback);
    /** A strip's "+ Send" > "New bus": add a bus and point (track, slot) at
        it. Called on a later message-loop turn, after the strip's menu has
        gone, because doing it rebuilds every strip.
    */
    void setCreateBusForSendCallback(std::function<void(int trackIndex, int slot)> callback);

    /** A strip's insert section, reported with the strip's track index. */
    struct InsertCallbacks
    {
        std::function<void(int trackIndex)> add;
        std::function<void(int trackIndex, int insertIndex)> open;
        std::function<void(int trackIndex)> changed;
        std::function<void(int trackIndex)> showChain;
    };
    void setInsertCallbacks(InsertCallbacks callbacks);

private:
    void timerCallback() override;
    /** The header's live master readout - "MASTER -12.3 dB". */
    juce::String getMasterReadout() const;

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
    std::function<void(int)> openChannelCallback;
    std::function<void(int, int)> createBusForSendCallback;
    InsertCallbacks insertCallbacks;
    int selectedTrack = 0;

    int deviceInputCount = 0;
    /** The tallest strip's minimum, as the strips were last laid out for. */
    int laidOutMinimumHeight = 0;
    int getStripsMinimumHeight() const;
    juce::String lastMasterReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

} // namespace djr
