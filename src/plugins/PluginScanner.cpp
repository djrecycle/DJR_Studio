#include "PluginScanner.h"

#include "utils/Logger.h"

namespace djr
{

PluginScanner::PluginScanner()
    : Thread("DJR Plugin Scanner")
{
    formatManager.addDefaultFormats();
}

PluginScanner::~PluginScanner()
{
    stopThread(4000);
}

void PluginScanner::startScan(ResultCallback callback)
{
    if (isThreadRunning())
        return;

    {
        const juce::ScopedLock scoped(callbackLock);
        resultCallback = std::move(callback);
    }

    scanning.store(true, std::memory_order_release);
    startThread(juce::Thread::Priority::low);
}

bool PluginScanner::isScanning() const noexcept
{
    return scanning.load(std::memory_order_acquire);
}

void PluginScanner::setProgressCallback(ProgressCallback callback)
{
    const juce::ScopedLock scoped(callbackLock);
    progressCallback = std::move(callback);
}

juce::FileSearchPath PluginScanner::getExtraPathsFor(const juce::String& formatName)
{
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::FileSearchPath paths;

    if (formatName == "VST3")
    {
        paths.add(home.getChildFile(".vst3"));
        paths.add(juce::File("/usr/lib/vst3"));
        paths.add(juce::File("/usr/local/lib/vst3"));
    }
    else if (formatName == "LV2")
    {
        paths.add(home.getChildFile(".lv2"));
        paths.add(juce::File("/usr/lib/lv2"));
        paths.add(juce::File("/usr/local/lib/lv2"));
        // Debian and Ubuntu put the packaged plugins under the multiarch dir on
        // some releases, and Ubuntu Studio is exactly where that shows up.
        paths.add(juce::File("/usr/lib/x86_64-linux-gnu/lv2"));
    }

    return paths;
}

juce::FileSearchPath PluginScanner::getSearchPathsFor(juce::AudioPluginFormat& format)
{
    auto paths = format.getDefaultLocationsToSearch();

    // FileSearchPath::add refuses duplicates, so this only fills real gaps.
    const auto extra = getExtraPathsFor(format.getName());

    for (int i = 0; i < extra.getNumPaths(); ++i)
        paths.addIfNotAlreadyThere(extra[i]);

    return paths;
}

juce::StringArray PluginScanner::getHostedFormatNames()
{
    juce::AudioPluginFormatManager manager;
    manager.addDefaultFormats();

    juce::StringArray names;

    for (auto* format : manager.getFormats())
        if (format != nullptr)
            names.add(format->getName());

    return names;
}

void PluginScanner::reportProgress(const juce::String& what)
{
    ProgressCallback callback;

    {
        const juce::ScopedLock scoped(callbackLock);
        callback = progressCallback;
    }

    if (callback)
        juce::MessageManager::callAsync([callback = std::move(callback), what] { callback(what); });
}

void PluginScanner::run()
{
    juce::Array<juce::PluginDescription> found;

    // Every format the build can host, not a hard coded one. Each format knows
    // how to recognise its own plugins - an LV2 is a folder of turtle files, a
    // VST3 is a bundle - so searchPathsForPlugins does the walking.
    for (auto* format : formatManager.getFormats())
    {
        if (format == nullptr || threadShouldExit())
            continue;

        const auto paths = getSearchPathsFor(*format);
        reportProgress("Scanning " + format->getName() + "...");

        const auto identifiers = format->searchPathsForPlugins(paths, true, false);

        for (const auto& identifier : identifiers)
        {
            if (threadShouldExit())
                break;

            reportProgress(identifier);

            // Reads the plugin's metadata; for LV2 that is its manifest rather
            // than loading any code, so a broken plugin cannot take us with it.
            juce::OwnedArray<juce::PluginDescription> descriptions;
            format->findAllTypesForFile(descriptions, identifier);

            for (auto* description : descriptions)
                if (description != nullptr)
                    found.add(*description);
        }

        Logger::write(format->getName() + " scan: " + juce::String(identifiers.size())
                      + " file(s) searched.");
    }

    scanning.store(false, std::memory_order_release);
    Logger::write("Plugin scan complete. Found " + juce::String(found.size()) + " plugin(s).");

    ResultCallback callback;
    {
        const juce::ScopedLock scoped(callbackLock);
        callback = resultCallback;
    }

    if (callback)
        juce::MessageManager::callAsync([callback = std::move(callback), found] { callback(found); });
}

} // namespace djr
