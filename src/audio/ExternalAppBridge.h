#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace djr
{

/** One of the standalone JACK applications the External Apps browser
    section can launch and patch in - real, separate OS processes, not
    plugins DJR_Studio hosts itself.
*/
struct ExternalApp
{
    /** Stable id, used as the row's identity - never shown to the user and
        never changed once shipped.
    */
    juce::String id;
    juce::String displayName;
    /** The binary this app is launched from, found on $PATH. */
    juce::String executableName;
    /** The JACK client name this app registers under once its own audio
        engine is actually running - not always true immediately after
        launch. Carla in particular needs its own Engine set to the JACK
        driver first, a one-time setup in Carla's own preferences, before it
        registers any ports at all.
    */
    juce::String jackClientName;
};

/** Launches and JACK-patches the apps ExternalAppBridge::knownApps() lists
    into DJR_Studio's own JACK input ports (see JUCE_JACK_CLIENT_NAME in
    CMakeLists.txt) - real, separate processes, connected the same way any
    two JACK clients are, the way Ardour and friends have always combined
    standalone synth/effect apps.

    Chosen deliberately over in-process plugin hosting after a real plugin
    (Carla, loaded as an LV2) crashed the app: Carla's own bundled JUCE
    refuses to coexist with a second JUCE instance (this app) in the same
    process - see docs on the crash this reproduced (numScopedInitInstances)
    - an upstream limitation with no fix on DJR_Studio's side. A separate
    process has no such conflict: no shared JUCE, no shared anything, just
    two JACK clients.
*/
namespace ExternalAppBridge
{
    const std::vector<ExternalApp>& knownApps();

    /** True if `app`'s executable can actually be found on $PATH - what the
        browser row's installed/not-installed state is built from.
    */
    bool isInstalled(const ExternalApp& app);

    /** True if a process matching `app`'s executable is already running -
        checked so connecting does not launch a second copy of something
        already open.
    */
    bool isRunning(const ExternalApp& app);

    /** Starts `app` as an independent process, fully detached - it outlives
        this call and this app's own lifetime, the same as launching it from
        a desktop launcher would. False (with `error` set) only if the
        executable is missing or could not be started at all.
    */
    bool launch(const ExternalApp& app, juce::String& error);

    /** Asks `app`'s process to exit - a plain SIGTERM via pkill, the same
        signal closing its window sends, not a forced kill: most of these
        apps handle it as a normal quit request and can still prompt to
        save unsaved work if they have any. Returns false when no matching
        process was found to signal in the first place.
    */
    bool quit(const ExternalApp& app);

    /** One JACK port, as reported by `jack_lsp -pt` - see parseJackLspOutput(). */
    struct JackPortInfo
    {
        juce::String name;
        bool isOutput = false;
        bool isAudio = false;
    };

    /** Parses `jack_lsp -pt`'s own text output into a list of ports. A free
        function taking a string, rather than something that runs jack_lsp
        itself, so the format it understands - name line, then an indented
        "properties: ..." line, then an indented type-description line, in
        any run of blank lines around them - is testable against real
        captured output without a JACK server.
    */
    std::vector<JackPortInfo> parseJackLspOutput(const juce::String& output);

    /** Every currently-live JACK audio output port belonging to a client
        whose name starts with `clientNamePrefix` - runs jack_lsp for real,
        so nothing is returned when no JACK server is reachable or no such
        client is registered yet.
    */
    juce::StringArray findAudioOutputPorts(const juce::String& clientNamePrefix);

    /** Connects up to two of `app`'s own audio output ports (see
        findAudioOutputPorts(), using app.jackClientName) to DJR_Studio's own
        JACK input ports starting at `firstChannel` (0-based, matching
        Track::getInputChannel()'s own numbering - "DJR_Studio:in_1" is
        firstChannel 0). Lets each track that wants its own live source get
        one, instead of every connection landing on the same shared pair.
        Returns how many pairs were actually connected; zero with `error` set
        to why when the app has not registered any audio output ports yet.
    */
    int connectToDjrInputs(const ExternalApp& app, int firstChannel, juce::String& error);
}

} // namespace djr
