#include "DJRStudioApplication.h"

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
    return "0.1.0";
}

bool DJRStudioApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void DJRStudioApplication::initialise(const juce::String& commandLine)
{
    juce::ignoreUnused(commandLine);
    Logger::write("Starting DJR_Studio 0.1.0");
    mainWindow = std::make_unique<MainWindow>(getApplicationName());
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
