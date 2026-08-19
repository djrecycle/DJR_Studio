#include "PluginWindow.h"

#include "ui/Theme.h"

namespace djr
{

PluginWindow::PluginWindow(juce::AudioProcessor& processor)
    : DocumentWindow(processor.getName(), Theme::panel(), DocumentWindow::closeButton),
      audioProcessor(processor)
{
    setUsingNativeTitleBar(true);

    // hasEditor() can say yes and still hand back nothing, so the result is
    // what decides, not the promise.
    if (audioProcessor.hasEditor())
    {
        if (auto* editor = audioProcessor.createEditorIfNeeded())
        {
            setContentOwned(editor, true);
            centreWithSize(760, 520);
            setVisible(true);
            return;
        }
    }

    // No GUI of its own is not the same as nothing to show: the parameters are
    // still there, and a window saying otherwise leaves a plugin that makes
    // sound but cannot be adjusted. Every DAW puts up a generic panel here.
    auto* generic = new juce::GenericAudioProcessorEditor(audioProcessor);

    // It sizes itself to fit every parameter, which for a big plugin is taller
    // than the screen. Capped here; the panel scrolls, and the window resizes.
    const auto height = juce::jlimit(200, 620, generic->getHeight());
    generic->setSize(juce::jmax(460, generic->getWidth()), height);

    setContentOwned(generic, true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());

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
