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

    void setSelectedTrack(int trackIndex);
    void setTrackSelectedCallback(std::function<void(int)> callback);

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
    int selectedTrack = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerView)
};

} // namespace djr
