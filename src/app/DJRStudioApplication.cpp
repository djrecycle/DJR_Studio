#include "DJRStudioApplication.h"

#include "BinaryData.h"
#include "Localisation.h"
#include "MainWindow.h"
#include "utils/Logger.h"

namespace djr
{

DJRStudioApplication::DJRStudioApplication() = default;
DJRStudioApplication::~DJRStudioApplication() = default;

const juce::String DJRStudioApplication::getApplicationName()
{
    return "DJR_Studio";
}

const juce::String DJRStudioApplication::getApplicationVersion()
{
    // Comes from CMake, which derives it from git; see DJR_VERSION_BASE.
    return DJR_STUDIO_VERSION_STRING;
}

bool DJRStudioApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void DJRStudioApplication::initialise(const juce::String& commandLine)
{
    juce::ignoreUnused(commandLine);
    Logger::write("Starting DJR_Studio " + getApplicationVersion());

    // Up first, before anything that takes real time to build - plugin
    // scanning and MainComponent's own panel tree included - the way a DAW's
    // splash covers its own startup rather than leaving a blank screen.
    splashScreen = new juce::SplashScreen(
        getApplicationName(),
        juce::ImageCache::getFromMemory(BinaryData::splash_png, BinaryData::splash_pngSize),
        false);

    // Before the window exists. The panels are members of MainComponent, so
    // their constructors - which is where chip labels and placeholders are set
    // - run before MainComponent's own constructor body ever would.
    Localisation::setLanguage(Localisation::loadSavedChoice());

    mainWindow = std::make_unique<MainWindow>(getApplicationName());

    // Measured from the splash's own construction, not from here - so a slow
    // first-run plugin scan doesn't linger any longer than it already did,
    // and a fast startup still shows the logo long enough to register.
    splashScreen->deleteAfterDelay(juce::RelativeTime::seconds(1.2), true);
}

void DJRStudioApplication::shutdown()
{
    mainWindow.reset();
    Logger::write("DJR_Studio shutdown complete");
}

void DJRStudioApplication::systemRequestedQuit()
{
    quit();
}

void DJRStudioApplication::anotherInstanceStarted(const juce::String& commandLine)
{
    Logger::write("Another DJR_Studio instance started with command line: " + commandLine);
}

} // namespace djr
