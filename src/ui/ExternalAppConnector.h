#pragma once

#include "audio/ExternalAppBridge.h"

#include <juce_events/juce_events.h>

#include <functional>

namespace djr
{

/** Drives ExternalAppBridge's launch-then-connect sequence without blocking
    the message thread: launching an app is instant, but it can take a
    moment before its own JACK ports actually exist to connect - this polls
    for them instead of guessing a fixed delay.
*/
class ExternalAppConnector final : private juce::Timer
{
public:
    ExternalAppConnector() = default;
    ~ExternalAppConnector() override = default;

    /** Launches `app` (if it is not already running) and polls for its JACK
        audio outputs, connecting up to two of them into DJR_Studio's own
        JACK inputs starting at `firstChannel` (0-based - see
        ExternalAppBridge::connectToDjrInputs()) as soon as they appear.
        `onFinished` fires exactly once, with how many pairs got connected
        and - only when none did - why: the app could not be launched, or
        its ports never showed up within the poll window.
    */
    void begin(const ExternalApp& appToConnect, int firstChannel,
              std::function<void(int connected, juce::String error)> onFinished);

private:
    void timerCallback() override;
    void finish(int connected, const juce::String& error);

    ExternalApp app;
    int targetChannel = 0;
    std::function<void(int, juce::String)> callback;
    int elapsedMs = 0;

    static constexpr int pollIntervalMs = 400;
    static constexpr int timeoutMs = 8000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExternalAppConnector)
};

} // namespace djr
