#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <functional>

namespace djr
{

class PluginScanner final : private juce::Thread
{
public:
    using ResultCallback = std::function<void(juce::Array<juce::PluginDescription>)>;

    PluginScanner();
    ~PluginScanner() override;

    void startVst3Scan(ResultCallback callback);
    bool isScanning() const noexcept;

    static juce::FileSearchPath getDefaultVst3Paths();

private:
    void run() override;

    juce::CriticalSection callbackLock;
    ResultCallback resultCallback;
    juce::AudioPluginFormatManager formatManager;
    std::atomic<bool> scanning { false };
};

} // namespace djr
