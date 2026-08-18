#pragma once

#include "PluginScanner.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

namespace djr
{

class PluginManager final : public juce::ChangeBroadcaster
{
public:
    PluginManager();

    void scanPluginsAsync();
    bool isScanning() const noexcept;
    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    void createPluginAsync(const juce::PluginDescription& description,
                           double sampleRate,
                           int blockSize,
                           std::function<void(std::unique_ptr<juce::AudioPluginInstance>, juce::String)> completion);

private:
    juce::AudioPluginFormatManager formatManager;
    PluginScanner scanner;
    mutable juce::CriticalSection lock;
    juce::Array<juce::PluginDescription> knownPlugins;
};

} // namespace djr
