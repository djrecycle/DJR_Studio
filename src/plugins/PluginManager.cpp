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

juce::Array<juce::PluginDescription> PluginManager::getBuiltInPlugins()
{
    juce::Array<juce::PluginDescription> builtIns;
    builtIns.add(AudioEditorProcessor::getDescription());
    return builtIns;
}

bool PluginManager::isBuiltIn(const juce::PluginDescription& description)
{
    return AudioEditorProcessor::matches(description);
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createBuiltIn(const juce::PluginDescription& description)
{
    if (AudioEditorProcessor::matches(description))
        return std::make_unique<AudioEditorProcessor>();

    return nullptr;
}

juce::Array<juce::PluginDescription> PluginManager::getKnownPlugins() const
{
    auto all = getBuiltInPlugins();

    const juce::ScopedLock scoped(lock);
    all.addArray(knownPlugins);
    return all;
}

void PluginManager::createPluginAsync(const juce::PluginDescription& description,
                                      double sampleRate,
                                      int blockSize,
                                      std::function<void(std::unique_ptr<juce::AudioPluginInstance>, juce::String)> completion)
{
    // A built-in has no format behind it, so it never reaches the format
    // manager - which would only report that it cannot find the file. The
    // callback is still deferred: callers expect the async shape either way,
    // and one that completes inline re-enters whatever asked for it. The
    // instance is built inside the message, because a callAsync lambda has to
    // be copyable and a unique_ptr captured in one is not.
    if (isBuiltIn(description))
    {
        juce::MessageManager::callAsync(
            [description, sampleRate, blockSize, completion = std::move(completion)]
            {
                auto instance = createBuiltIn(description);

                if (instance != nullptr)
                    instance->setPlayConfigDetails(2, 2, sampleRate, blockSize);

                if (completion)
                    completion(std::move(instance),
                               instance != nullptr ? juce::String() : juce::String("The built-in editor could not be created."));
            });

        return;
    }

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
