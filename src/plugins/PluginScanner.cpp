#include "PluginScanner.h"

#include "utils/Logger.h"

namespace djr
{

PluginScanner::PluginScanner()
    : Thread("DJR VST3 Scanner")
{
    formatManager.addDefaultFormats();
}

PluginScanner::~PluginScanner()
{
    stopThread(4000);
}

void PluginScanner::startVst3Scan(ResultCallback callback)
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

juce::FileSearchPath PluginScanner::getDefaultVst3Paths()
{
    juce::FileSearchPath paths;
    paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3"));
    paths.add(juce::File("/usr/lib/vst3"));
    paths.add(juce::File("/usr/local/lib/vst3"));
    return paths;
}

void PluginScanner::run()
{
    juce::Array<juce::PluginDescription> found;

    if (auto* format = formatManager.getFormat(0))
    {
        const auto paths = getDefaultVst3Paths();
        for (int i = 0; i < paths.getNumPaths() && ! threadShouldExit(); ++i)
        {
            const auto root = paths[i];
            if (! root.exists())
                continue;

            juce::RangedDirectoryIterator iterator(root, true, "*.vst3",
                                                   juce::File::findFilesAndDirectories);

            for (const auto& entry : iterator)
            {
                if (threadShouldExit())
                    break;

                const auto file = entry.getFile();
                juce::OwnedArray<juce::PluginDescription> descriptions;
                format->findAllTypesForFile(descriptions, file.getFullPathName());
                for (auto* description : descriptions)
                    found.add(*description);
            }
        }
    }

    scanning.store(false, std::memory_order_release);
    Logger::write("VST3 scan complete. Found " + juce::String(found.size()) + " plugin(s).");

    ResultCallback callback;
    {
        const juce::ScopedLock scoped(callbackLock);
        callback = resultCallback;
    }

    if (callback)
        juce::MessageManager::callAsync([callback = std::move(callback), found] { callback(found); });
}

} // namespace djr
