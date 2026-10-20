#include "MainWindow.h"

#include "ui/MainComponent.h"
#include "ui/Theme.h"

#include "BinaryData.h"

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

    auto userArea = juce::Rectangle<int>(1600, 1000);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userArea;

    // Never open as large as the whole screen. Under XWayland the "user area"
    // is the full screen - the desktop's own panels are not reported - and
    // KWin treats an X11 window exactly that size as legacy fullscreen: no
    // title bar, so no close or minimise buttons, and its top row ends up
    // under the panel. 90% leaves a normal window to restore back to.
    const auto width = juce::jmin(1600, juce::roundToInt(userArea.getWidth() * 0.9));
    const auto height = juce::jmin(1000, juce::roundToInt(userArea.getHeight() * 0.9));
    centreWithSize(width, height);
    setVisible(true);

    // On a screen no bigger than that default, start maximised instead -
    // asked of the window manager (the native title bar makes setFullScreen
    // send _NET_WM_STATE_MAXIMIZED), which knows where its panels are.
    if (userArea.getWidth() <= 1600 || userArea.getHeight() <= 1000)
        setFullScreen(true);

    // DocumentWindow::setIcon() only feeds JUCE's own title-bar painting,
    // which setUsingNativeTitleBar(true) above disables - the taskbar/
    // window-list icon the OS actually shows is set on the peer itself
    // (X11's _NET_WM_ICON on Linux), which only exists once the window is
    // visible, hence this coming after setVisible() rather than before it.
    if (auto* peer = getPeer())
        peer->setIcon(juce::ImageCache::getFromMemory(BinaryData::djr_studio_png, BinaryData::djr_studio_pngSize));
}

MainWindow::~MainWindow() = default;

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace djr
