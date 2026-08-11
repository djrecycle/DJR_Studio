#include "PluginWindow.h"

#include "ui/Theme.h"

namespace djr
{

PluginWindow::PluginWindow(juce::AudioProcessor& processor)
    : DocumentWindow(processor.getName(), Theme::panel(), DocumentWindow::closeButton),
      audioProcessor(processor)
{
    setUsingNativeTitleBar(true);

    if (audioProcessor.hasEditor())
    {
        setContentOwned(audioProcessor.createEditorIfNeeded(), true);
        centreWithSize(760, 520);
    }
    else
    {
        auto* label = new juce::Label({}, "Plugin has no custom editor");
        label->setJustificationType(juce::Justification::centred);
        setContentOwned(label, true);
        centreWithSize(420, 180);
    }

    setVisible(true);
}

PluginWindow::~PluginWindow()
{
    clearContentComponent();
}

juce::AudioProcessor& PluginWindow::getProcessor() noexcept
{
    return audioProcessor;
}

void PluginWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace djr
