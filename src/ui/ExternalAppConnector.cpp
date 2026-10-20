#include "ExternalAppConnector.h"

namespace djr
{

void ExternalAppConnector::begin(const ExternalApp& appToConnect, int firstChannel,
                                 std::function<void(int, juce::String)> onFinished)
{
    stopTimer();

    app = appToConnect;
    targetChannel = firstChannel;
    callback = std::move(onFinished);
    elapsedMs = 0;

    if (! ExternalAppBridge::isRunning(app))
    {
        juce::String error;

        if (! ExternalAppBridge::launch(app, error))
        {
            finish(0, error);
            return;
        }
    }

    startTimer(pollIntervalMs);
}

void ExternalAppConnector::timerCallback()
{
    juce::String error;
    const auto connected = ExternalAppBridge::connectToDjrInputs(app, targetChannel, error);

    if (connected > 0)
    {
        finish(connected, {});
        return;
    }

    elapsedMs += pollIntervalMs;

    if (elapsedMs >= timeoutMs)
        finish(0, error);
}

void ExternalAppConnector::finish(int connected, const juce::String& error)
{
    stopTimer();

    // Copied out and cleared before the call, not after: the callback is
    // free to call begin() again (a retry, say), and a callback still set
    // from this run would make that look like it was already finishing.
    auto toCall = std::move(callback);
    callback = nullptr;

    if (toCall)
        toCall(connected, error);
}

} // namespace djr
