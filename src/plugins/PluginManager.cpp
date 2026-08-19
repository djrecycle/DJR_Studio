#include "PluginManager.h"

#include "utils/Logger.h"

namespace djr
{

PluginManager::PluginManager()
{
    formatManager.addDefaultFormats();
    loadCache();
}

juce::File PluginManager::getCacheFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DJR_Studio")
        .getChildFile("plugin-cache.xml");
}

void PluginManager::loadCache()
{
    const auto file = getCacheFile();

    if (! file.existsAsFile())
        return;

    auto xml = juce::parseXML(file);

    if (xml == nullptr)
        return;

    juce::Array<juce::PluginDescription> restored;

    for (auto* child : xml->getChildIterator())
    {
        juce::PluginDescription description;

        // A description that no longer parses is skipped rather than failing
        // the whole cache: one bad entry should not cost the other 648.
        if (child != nullptr && description.loadFromXml(*child))
            restored.add(description);
    }

    const juce::ScopedLock scoped(lock);
    knownPlugins = std::move(restored);
}

void PluginManager::saveCache() const
{
    juce::XmlElement root("PluginCache");

    {
        const juce::ScopedLock scoped(lock);

        for (const auto& description : knownPlugins)
            root.addChildElement(description.createXml().release());
    }

    const auto file = getCacheFile();
    file.getParentDirectory().createDirectory();
    root.writeTo(file);
}

void PluginManager::scanPluginsAsync()
{
    scanner.startScan([this] (juce::Array<juce::PluginDescription> results)
    {
        {
            const juce::ScopedLock scoped(lock);
            knownPlugins = std::move(results);
        }

        // Written here rather than on shutdown: a scan is the only thing that
        // changes the list, and a crash later should not cost the user a rescan.
        saveCache();
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
