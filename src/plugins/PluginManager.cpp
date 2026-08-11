#include "PluginManager.h"

#include "utils/Logger.h"

namespace djr
{

PluginManager::PluginManager()
{
    formatManager.addDefaultFormats();
}

void PluginManager::scanVst3Async()
{
    scanner.startVst3Scan([this] (juce::Array<juce::PluginDescription> results)
    {
        {
            const juce::ScopedLock scoped(lock);
            knownPlugins = std::move(results);
        }
        sendChangeMessage();
    });
}

bool PluginManager::isScanning() const noexcept
{
    return scanner.isScanning();
}

juce::Array<juce::PluginDescription> PluginManager::getKnownPlugins() const
{
    const juce::ScopedLock scoped(lock);
    return knownPlugins;
}

void PluginManager::createPluginAsync(const juce::PluginDescription& description,
                                      double sampleRate,
                                      int blockSize,
                                      std::function<void(std::unique_ptr<juce::AudioPluginInstance>, juce::String)> completion)
{
    formatManager.createPluginInstanceAsync(description, sampleRate, blockSize,
        [completion = std::move(completion)] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                              const juce::String& error)
        {
            if (error.isNotEmpty())
                Logger::write("Plugin load failed: " + error);

            if (completion)
                completion(std::move(instance), error);
        });
}

} // namespace djr
