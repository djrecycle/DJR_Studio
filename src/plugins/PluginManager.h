#pragma once

#include "PluginScanner.h"
#include "AudioEditorProcessor.h"

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
    /** Everything the browser may load: the plugins we ship first, then what
        the last scan found. Built-ins lead because they are always there -
        a list that starts with them says so without a heading.
    */
    juce::Array<juce::PluginDescription> getKnownPlugins() const;

    /** Whether a description names a plugin we ship rather than one a scan
        found. The browser asks so it can still say when nothing has been
        scanned: the list is never empty any more, and "no plugins yet" would
        otherwise be a sentence that can never appear.
    */
    static bool isBuiltIn(const juce::PluginDescription& description);

    void createPluginAsync(const juce::PluginDescription& description,
                           double sampleRate,
                           int blockSize,
                           std::function<void(std::unique_ptr<juce::AudioPluginInstance>, juce::String)> completion);

private:
    /** The plugins that are part of the app rather than found on disk. Built
        each time rather than cached: they cannot go missing, and a stale entry
        for one would be a bug nobody could clear by rescanning.
    */
    static juce::Array<juce::PluginDescription> getBuiltInPlugins();
    /** Creates a built-in by description, or nullptr when none matches. */
    static std::unique_ptr<juce::AudioPluginInstance> createBuiltIn(const juce::PluginDescription& description);

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
