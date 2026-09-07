#include "PluginScanner.h"

#include "app/Settings.h"

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

juce::FileSearchPath PluginScanner::getBuiltInPathsFor(juce::AudioPluginFormat& format)
{
    auto paths = format.getDefaultLocationsToSearch();

    // FileSearchPath::add refuses duplicates, so this only fills real gaps.
    const auto extra = getExtraPathsFor(format.getName());

    for (int i = 0; i < extra.getNumPaths(); ++i)
        paths.addIfNotAlreadyThere(extra[i]);

    return paths;
}

namespace
{
    /** One key per format, so adding an LV2 folder cannot disturb the VST3 list. */
    juce::String userPathsKeyFor(const juce::String& formatName)
    {
        return "pluginPaths." + formatName;
    }

    /** A file of its own rather than a couple of Settings keys: run() rewrites
        this on every single plugin file it scans, and the shared settings
        file is not the place for that much churn.
    */
    juce::File getScanStateFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DJR_Studio")
            .getChildFile("plugin-scan-state.xml");
    }

    juce::PropertySet loadScanState()
    {
        juce::PropertySet state;
        const auto file = getScanStateFile();

        if (! file.existsAsFile())
            return state;

        if (auto xml = juce::parseXML(file))
            state.restoreFromXml(*xml);

        return state;
    }

    void saveScanState(const juce::PropertySet& state)
    {
        const auto file = getScanStateFile();
        file.getParentDirectory().createDirectory();

        if (auto xml = state.createXml("PluginScanState"))
            xml->writeTo(file);
    }

    void addToCrashedPluginBlacklist(const juce::String& identifier)
    {
        auto state = loadScanState();
        auto blacklist = juce::StringArray::fromLines(state.getValue("blacklist"));

        if (! blacklist.contains(identifier))
        {
            blacklist.add(identifier);
            state.setValue("blacklist", blacklist.joinIntoString("\n"));
            saveScanState(state);
        }
    }

    /** Marks `identifier` as the one about to go through the one call in the
        scan loop that can crash the whole app - or, called with an empty
        string, clears that mark once the call returns safely. Written
        before the risky call and read back at the very top of the next
        run(), a crash on this exact identifier is the only way the mark can
        still be set when that happens - every other exit from run() (finishing,
        threadShouldExit()) clears it first.
    */
    void setInProgressMarker(const juce::String& identifier)
    {
        auto state = loadScanState();
        state.setValue("inProgress", identifier);
        saveScanState(state);
    }

    /** Reads the mark left behind by a scan that never came back to clear
        it, and clears it in the same call so a later crash elsewhere (not
        while scanning a plugin) cannot be mistaken for this one again.
    */
    juce::String takeInProgressMarker()
    {
        auto state = loadScanState();
        const auto identifier = state.getValue("inProgress");

        if (identifier.isNotEmpty())
        {
            state.setValue("inProgress", {});
            saveScanState(state);
        }

        return identifier;
    }
}

juce::StringArray PluginScanner::getCrashedPluginBlacklist()
{
    return juce::StringArray::fromLines(loadScanState().getValue("blacklist"));
}

void PluginScanner::clearCrashedPluginBlacklist(const juce::String& identifier)
{
    auto state = loadScanState();
    auto blacklist = juce::StringArray::fromLines(state.getValue("blacklist"));

    if (! blacklist.contains(identifier))
        return;

    blacklist.removeString(identifier);
    state.setValue("blacklist", blacklist.joinIntoString("\n"));
    saveScanState(state);
}

juce::FileSearchPath PluginScanner::getUserPathsFor(const juce::String& formatName)
{
    // FileSearchPath round-trips through a semicolon-separated string, which is
    // exactly what the settings file wants to hold.
    return juce::FileSearchPath(Settings::get(userPathsKeyFor(formatName)));
}

bool PluginScanner::addUserPath(const juce::String& formatName, const juce::File& folder)
{
    if (! folder.isDirectory())
        return false;

    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    // Already covered means nothing to add: a folder listed twice would be
    // walked twice and every plugin in it reported twice.
    for (auto* format : formats.getFormats())
    {
        if (format == nullptr || format->getName() != formatName)
            continue;

        const auto builtIn = getBuiltInPathsFor(*format);

        for (int i = 0; i < builtIn.getNumPaths(); ++i)
            if (builtIn[i] == folder)
                return false;
    }

    auto paths = getUserPathsFor(formatName);

    for (int i = 0; i < paths.getNumPaths(); ++i)
        if (paths[i] == folder)
            return false;

    paths.add(folder);
    Settings::set(userPathsKeyFor(formatName), paths.toString());
    return true;
}

void PluginScanner::removeUserPath(const juce::String& formatName, const juce::File& folder)
{
    const auto paths = getUserPathsFor(formatName);
    juce::FileSearchPath remaining;

    for (int i = 0; i < paths.getNumPaths(); ++i)
        if (paths[i] != folder)
            remaining.add(paths[i]);

    Settings::set(userPathsKeyFor(formatName), remaining.toString());
}

juce::FileSearchPath PluginScanner::getSearchPathsFor(juce::AudioPluginFormat& format)
{
    auto paths = getBuiltInPathsFor(format);
    const auto userPaths = getUserPathsFor(format.getName());

    for (int i = 0; i < userPaths.getNumPaths(); ++i)
        paths.addIfNotAlreadyThere(userPaths[i]);

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
    // If the last scan never got back here to clear this, this exact file
    // took the whole app down last time - blacklist it before touching
    // anything else, or this attempt just crashes on it again too, without
    // ever reaching whatever came after it in the list.
    const auto crashedLastTime = takeInProgressMarker();

    if (crashedLastTime.isNotEmpty())
    {
        addToCrashedPluginBlacklist(crashedLastTime);
        Logger::write("Plugin scan: \"" + crashedLastTime
                      + "\" crashed the previous scan - skipping it from now on.");
    }

    const auto blacklist = getCrashedPluginBlacklist();
    juce::StringArray skipped;
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

            if (blacklist.contains(identifier))
            {
                skipped.add(identifier);
                continue;
            }

            reportProgress(identifier);

            // Reads the plugin's metadata; for LV2 that is its manifest rather
            // than loading any code, so a broken plugin cannot take us with
            // it - but for a binary format this can mean dlopen-ing and
            // running the plugin's own code, which is the one place left
            // that can bring the whole app down. Marked before the call, not
            // after: the mark surviving a crash is the whole point of it.
            setInProgressMarker(identifier);

            juce::OwnedArray<juce::PluginDescription> descriptions;
            format->findAllTypesForFile(descriptions, identifier);

            setInProgressMarker({});

            for (auto* description : descriptions)
                if (description != nullptr)
                    found.add(*description);
        }

        Logger::write(format->getName() + " scan: " + juce::String(identifiers.size())
                      + " file(s) searched.");
    }

    scanning.store(false, std::memory_order_release);

    if (! skipped.isEmpty())
        Logger::write("Plugin scan: skipped " + juce::String(skipped.size())
                      + " file(s) that crashed a previous scan: " + skipped.joinIntoString("; "));

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
