#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace djr
{

class MainWindow;

class DJRStudioApplication final : public juce::JUCEApplication
{
public:
    DJRStudioApplication();
    ~DJRStudioApplication() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;

    void initialise(const juce::String& commandLine) override;
    void shutdown() override;
    void systemRequestedQuit() override;
    void anotherInstanceStarted(const juce::String& commandLine) override;

private:
    // Not owned via a smart pointer: SplashScreen::deleteAfterDelay() has it
    // call `delete this` on its own timer once its time is up, so a
    // unique_ptr here would double-free it once this application object is
    // destroyed. Matches JUCE's own documented usage pattern for the class.
    juce::SplashScreen* splashScreen = nullptr;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace djr
