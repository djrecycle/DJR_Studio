#include "ExternalAppBridge.h"

namespace djr::ExternalAppBridge
{

const std::vector<ExternalApp>& knownApps()
{
    // jackClientName verified against each app's real, running JACK client
    // on a live Ubuntu Studio install - Guitarix in particular splits its
    // amp and cab/effects rack into two separate clients ("gx_head_amp" then
    // "gx_head_fx"); gx_head_fx's output is the finished signal. Carla's own
    // name is a best guess - its standalone GUI does not register any JACK
    // ports at all until its own Engine is switched to the JACK driver, a
    // one-time setup in Carla's own preferences this app cannot do for it.
    static const std::vector<ExternalApp> apps = {
        { "carla",     "Carla-Rack", "carla",     "Carla" },
        { "guitarix",  "Guitarix",   "guitarix",  "gx_head_fx" },
        { "rakarrack", "Rakarrack",  "rakarrack", "rakarrack" },
        { "hydrogen",  "Hydrogen",   "hydrogen",  "Hydrogen" },
    };

    return apps;
}

bool isInstalled(const ExternalApp& app)
{
    const auto pathEnv = juce::SystemStats::getEnvironmentVariable("PATH", {});

    for (const auto& dir : juce::StringArray::fromTokens(pathEnv, ":", ""))
        if (juce::File(dir).getChildFile(app.executableName).existsAsFile())
            return true;

    return false;
}

bool isRunning(const ExternalApp& app)
{
    juce::ChildProcess process;

    if (! process.start(juce::StringArray { "pgrep", "-x", app.executableName }))
        return false;

    const auto output = process.readAllProcessOutput();
    process.waitForProcessToFinish(2000);
    return output.trim().isNotEmpty();
}

bool launch(const ExternalApp& app, juce::String& error)
{
    if (! isInstalled(app))
    {
        error = app.displayName + TRANS(" is not installed.");
        return false;
    }

    // Run through a shell's own "nohup ... &" rather than starting the app
    // directly: a directly-started ChildProcess still pipes the child's
    // stdout/stderr back to this process, and closes that pipe as soon as
    // this wrapper object goes out of scope - fine for a process that never
    // writes much, not something to rely on for a whole GUI app's lifetime.
    // The shell backgrounds the real target and exits almost immediately,
    // leaving it fully detached with nothing tying it to this app at all.
    //
    // Passed as a StringArray, not a single quoted String: ChildProcess::
    // start(String) only unquotes argv[0] (see ActiveProcess's use of
    // .unquoted() on just that one token) - a hand-quoted "sh -c \"...\""
    // string reaches sh's -c argument with its own quote characters still
    // literally attached, which sh then parses as one unbroken word instead
    // of a shell command, and fails silently as far as this app can see
    // (ChildProcess::start() only reports whether launching *sh* worked).
    juce::ChildProcess launcher;
    const juce::StringArray command { "sh", "-c", "nohup " + app.executableName + " >/dev/null 2>&1 &" };

    if (! launcher.start(command))
    {
        error = TRANS("Could not start ") + app.displayName + ".";
        return false;
    }

    launcher.waitForProcessToFinish(2000);
    return true;
}

bool quit(const ExternalApp& app)
{
    juce::ChildProcess process;

    if (! process.start(juce::StringArray { "pkill", "-x", app.executableName }))
        return false;

    process.waitForProcessToFinish(3000);

    // pkill's own exit code: 0 means it actually signalled a process, 1
    // means nothing matched - a plain launcher.start() succeeding above
    // only means pkill itself ran, not that it found anything to signal.
    return process.getExitCode() == 0;
}

std::vector<JackPortInfo> parseJackLspOutput(const juce::String& output)
{
    std::vector<JackPortInfo> result;
    const auto lines = juce::StringArray::fromLines(output);

    for (int i = 0; i < lines.size(); ++i)
    {
        const auto& line = lines[i];

        // A port's own name line is never indented - jack_lsp's detail
        // lines (properties, then type) always are.
        if (line.isEmpty() || line.startsWithChar(' ') || line.startsWithChar('\t'))
            continue;

        JackPortInfo info;
        info.name = line.trim();

        for (int j = i + 1; j < lines.size(); ++j)
        {
            const auto& detail = lines[j];

            if (detail.isEmpty() || ! (detail.startsWithChar(' ') || detail.startsWithChar('\t')))
                break;

            const auto trimmed = detail.trim();

            if (trimmed.startsWith("properties:"))
                info.isOutput = trimmed.contains("output");
            else
                info.isAudio = trimmed.containsIgnoreCase("audio");
        }

        result.push_back(info);
    }

    return result;
}

juce::StringArray findAudioOutputPorts(const juce::String& clientNamePrefix)
{
    juce::ChildProcess process;

    if (! process.start(juce::StringArray { "jack_lsp", "-pt", clientNamePrefix + ":" }))
        return {};

    const auto output = process.readAllProcessOutput();
    process.waitForProcessToFinish(3000);

    juce::StringArray result;

    for (const auto& port : parseJackLspOutput(output))
        if (port.isOutput && port.isAudio)
            result.add(port.name);

    return result;
}

int connectToDjrInputs(const ExternalApp& app, int firstChannel, juce::String& error)
{
    const auto ports = findAudioOutputPorts(app.jackClientName);

    if (ports.isEmpty())
    {
        error = app.displayName
              + TRANS(" has not registered any JACK audio outputs yet. If it just launched, "
                      "give it a moment and try again")
              + (app.id == "carla" ? TRANS(" - Carla also needs its own Engine set to the JACK "
                                            "driver first, in Carla's own preferences.")
                                    : juce::String(".") );
        return 0;
    }

    // DJR_Studio's own JACK client name and port naming - see
    // JUCE_JACK_CLIENT_NAME in CMakeLists.txt and JUCE's own "in_<n>" scheme
    // ("in_1" is channel 0, hence the +1 below).
    const juce::String djrInputs[] = {
        "DJR_Studio:in_" + juce::String(firstChannel + 1),
        "DJR_Studio:in_" + juce::String(firstChannel + 2),
    };
    int connected = 0;

    for (int i = 0; i < ports.size() && i < 2; ++i)
    {
        juce::ChildProcess connectProcess;

        if (connectProcess.start(juce::StringArray { "jack_connect", ports[i], djrInputs[i] }))
        {
            connectProcess.waitForProcessToFinish(3000);
            ++connected;
        }
    }

    return connected;
}

} // namespace djr::ExternalAppBridge
