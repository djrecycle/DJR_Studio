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
    /** The last scan's results, kept on disk so starting the app does not mean
        walking every plugin folder again. Scanning 649 plugins takes long
        enough that doing it on every launch is its own reason not to launch.
    */
    void loadCache();
    void saveCache() const;
    static juce::File getCacheFile();

    juce::AudioPluginFormatManager formatManager;
    PluginScanner scanner;
    mutable juce::CriticalSection lock;
    juce::Array<juce::PluginDescription> knownPlugins;
};

} // namespace djr
