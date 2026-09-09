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

    // Never open larger than the available desktop, otherwise the window manager
    // clamps the frame while the content keeps its oversized layout.
    auto userArea = juce::Rectangle<int>(1600, 1000);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        userArea = display->userArea;

    centreWithSize(juce::jmin(1600, userArea.getWidth()),
                   juce::jmin(1000, userArea.getHeight()));
    setVisible(true);

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
