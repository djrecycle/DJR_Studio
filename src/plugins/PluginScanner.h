#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <functional>

namespace djr
{

/** Finds every plugin the app can host, in every format it can host.

    Deliberately format-agnostic: it asks the format manager what it knows and
    scans each one, rather than naming a format. The first version reached for
    `getFormat(0)` and iterated for "*.vst3" by hand, which quietly stopped
    being correct the moment a second format was enabled.
*/
class PluginScanner final : private juce::Thread
{
public:
    using ResultCallback = std::function<void(juce::Array<juce::PluginDescription>)>;
    /** Reports the folder being scanned, so the browser can show progress. */
    using ProgressCallback = std::function<void(juce::String)>;

    PluginScanner();
    ~PluginScanner() override;

    void startScan(ResultCallback callback);
    bool isScanning() const noexcept;
    void setProgressCallback(ProgressCallback callback);

    /** Extra places to look, on top of whatever each format already knows.

        JUCE's own defaults are usually right, but a distro can put plugins
        somewhere it does not expect, and a missing folder costs nothing.
    */
    static juce::FileSearchPath getExtraPathsFor(const juce::String& formatName);
    /** The paths this build knows about on its own: the format's own defaults
        plus getExtraPathsFor. These are never removable in the UI - drop one
        and the app stops finding the plugins the distro installed.
    */
    static juce::FileSearchPath getBuiltInPathsFor(juce::AudioPluginFormat& format);

    /** Folders the user added by hand, remembered between runs. */
    static juce::FileSearchPath getUserPathsFor(const juce::String& formatName);
    /** Returns false when the folder is already searched, built-in or not. */
    static bool addUserPath(const juce::String& formatName, const juce::File& folder);
    static void removeUserPath(const juce::String& formatName, const juce::File& folder);

    /** Every search path actually used for `formatName`: built-in, then the
        user's own. What the scanner walks and what the preferences dialog shows.
    */
    static juce::FileSearchPath getSearchPathsFor(juce::AudioPluginFormat& format);
    /** Names of the formats this build can host, for the UI to describe itself. */
    static juce::StringArray getHostedFormatNames();

private:
    void run() override;
    void reportProgress(const juce::String& what);

    juce::CriticalSection callbackLock;
    ResultCallback resultCallback;
    ProgressCallback progressCallback;
    juce::AudioPluginFormatManager formatManager;
    std::atomic<bool> scanning { false };
};

} // namespace djr
