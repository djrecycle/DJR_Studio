#pragma once

#include "audio/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace djr
{

/** Bottom status strip: audio device, buffer/latency, plugin count, CPU meter
    and a transient message area.
*/
class StatusBar final : public juce::Component,
                        private juce::Timer
{
public:
    explicit StatusBar(AudioEngine& audioEngine);

    void paint(juce::Graphics& g) override;

    void setPluginCount(int count);
    void setMessage(const juce::String& message);

private:
    void timerCallback() override;

    AudioEngine& engine;
    int pluginCount = 0;
    juce::String message;
    juce::int64 messageExpiryMs = 0;
    float cpuUsage = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace djr
