#include "MainWindow.h"

#include "ui/MainComponent.h"
#include "ui/Theme.h"

namespace djr
{

MainWindow::MainWindow(juce::String name)
    : DocumentWindow(std::move(name),
                     Theme::windowBackground(),
                     DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentOwned(new MainComponent(), true);
    setResizeLimits(1024, 620, 10000, 10000);

    // Never open larger than the available desktop, otherwise the window manager
    // clamps the frame while the content keeps its oversized layout.
    auto userArea = juce::Rectangle<int>(1600, 1000);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userArea;

    centreWithSize(juce::jmin(1600, userArea.getWidth()),
                   juce::jmin(1000, userArea.getHeight()));
    setVisible(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace djr
