#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

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
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace djr
