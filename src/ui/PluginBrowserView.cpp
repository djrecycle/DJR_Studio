#include "PluginBrowserView.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int headerHeight = Metrics::panelToolbarHeight;
    constexpr int rowHeight = 32;
}

//==============================================================================
PluginBrowserView::Model::Model(PluginBrowserView& ownerToUse)
    : owner(ownerToUse)
{
}

int PluginBrowserView::Model::getNumRows()
{
    return juce::jmax(1, owner.plugins.size());
}

void PluginBrowserView::Model::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(0, 1);

    if (owner.plugins.isEmpty())
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(12.0f));
        g.drawText("Belum ada VST3 - tekan Scan", bounds.reduced(8, 0), juce::Justification::centredLeft, true);
        return;
    }

    if (! juce::isPositiveAndBelow(row, owner.plugins.size()))
        return;

    const auto& plugin = owner.plugins.getReference(row);
    const auto isInstrument = plugin.isInstrument;
    const auto accent = isInstrument ? Theme::purple() : Theme::cyan();

    Theme::drawCard(g, bounds,
                    rowIsSelected ? accent.withAlpha(0.14f) : Theme::panelAlt(),
                    rowIsSelected ? accent : Theme::divider(),
                    4.0f);

    auto content = bounds.reduced(6, 0);

    auto iconArea = content.removeFromLeft(18).withSizeKeepingCentre(18, 18);
    g.setColour(rowIsSelected ? accent.withAlpha(0.28f) : Theme::inset());
    g.fillRoundedRectangle(iconArea.toFloat(), 4.0f);
    g.setColour(accent);
    Icons::draw(g, Icon::plug, iconArea.toFloat().reduced(4.0f), 1.4f);
    content.removeFromLeft(7);

    const auto badge = isInstrument ? juce::String("SYNTH") : juce::String("FX");
    const auto badgeFont = Theme::mono(9.5f);
    const auto badgeWidth = Theme::textWidth(badgeFont, badge) + 8;
    auto badgeArea = content.removeFromRight(badgeWidth).withSizeKeepingCentre(badgeWidth, 13);
    g.setColour(Theme::inset());
    g.fillRoundedRectangle(badgeArea.toFloat(), 3.0f);
    g.setColour(Theme::mutedText());
    g.setFont(badgeFont);
    g.drawText(badge, badgeArea, juce::Justification::centred, false);
    content.removeFromRight(6);

    g.setColour(Theme::text());
    g.setFont(Theme::ui(12.0f, true));
    g.drawText(plugin.name, content.removeFromTop(height / 2).withTrimmedTop(2), juce::Justification::bottomLeft, true);
    g.setColour(Theme::mutedText());
    g.setFont(Theme::ui(10.0f));
    g.drawText(plugin.manufacturerName, content, juce::Justification::topLeft, true);
}

void PluginBrowserView::Model::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow(row, owner.plugins.size()))
        return;

    owner.listBox.selectRow(row);

    if (owner.loadPluginCallback)
        owner.loadPluginCallback(row, owner.getSelectedTrackIndex());
}

//==============================================================================
PluginBrowserView::PluginBrowserView(PluginManager& manager, Mixer& mixerToUse)
    : pluginManager(manager), mixer(mixerToUse)
{
    scanButton.setFillColour(Theme::purple());
    scanButton.addListener(this);
    addAndMakeVisible(scanButton);

    listBox.setModel(&listModel);
    listBox.setRowHeight(rowHeight);
    listBox.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    listBox.setOutlineThickness(0);
    addAndMakeVisible(listBox);

    pluginManager.addChangeListener(this);
    refreshList();
}

PluginBrowserView::~PluginBrowserView()
{
    scanButton.removeListener(this);
    pluginManager.removeChangeListener(this);
}

void PluginBrowserView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(Theme::panel());

    auto header = bounds.withHeight(headerHeight);
    g.setColour(Theme::panelHeader());
    g.fillRect(header);
    g.setColour(Theme::outline());
    g.fillRect(header.removeFromBottom(1));

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(10.0f));
    g.drawText(juce::String(plugins.size()) + " VST3",
               header.reduced(7, 0),
               juce::Justification::centredLeft,
               false);
}

void PluginBrowserView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight).reduced(5, 0);
    scanButton.setBounds(header.removeFromRight(scanButton.getPreferredWidth()).withSizeKeepingCentre(scanButton.getPreferredWidth(), 17));

    listBox.setBounds(area.reduced(5));
}

void PluginBrowserView::buttonClicked(juce::Button* button)
{
    if (button != &scanButton)
        return;

    scanButton.setButtonText("Scanning");
    scanButton.setEnabled(false);
    setStatusText("Scanning folder VST3...");
    pluginManager.scanVst3Async();
}

void PluginBrowserView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refreshList();
}

void PluginBrowserView::refreshList()
{
    plugins = pluginManager.getKnownPlugins();

    scanButton.setButtonText("Scan");
    scanButton.setEnabled(! pluginManager.isScanning());
    listBox.updateContent();
    listBox.repaint();

    setStatusText(pluginManager.isScanning()
                      ? juce::String("Scanning VST3...")
                      : juce::String(plugins.size()) + " plugin VST3 terdeteksi.");
    repaint();
}

void PluginBrowserView::setLoadPluginCallback(std::function<void(int, int)> callback)
{
    loadPluginCallback = std::move(callback);
}

void PluginBrowserView::setOpenEditorCallback(std::function<void(int)> callback)
{
    openEditorCallback = std::move(callback);
}

void PluginBrowserView::setStatusCallback(std::function<void(const juce::String&)> callback)
{
    statusCallback = std::move(callback);
}

void PluginBrowserView::setTargetTrack(int trackIndex)
{
    targetTrack = juce::jlimit(0, juce::jmax(0, mixer.getNumTracks() - 1), trackIndex);
}

int PluginBrowserView::getSelectedPluginIndex() const noexcept
{
    return listBox.getSelectedRow();
}

int PluginBrowserView::getSelectedTrackIndex() const noexcept
{
    return targetTrack;
}

juce::String PluginBrowserView::getSelectedPluginDisplayName() const
{
    const auto row = getSelectedPluginIndex();
    return juce::isPositiveAndBelow(row, plugins.size()) ? plugins.getReference(row).name : juce::String();
}

void PluginBrowserView::setStatusText(const juce::String& text)
{
    if (statusCallback)
        statusCallback(text);
}

void PluginBrowserView::refreshTrackList()
{
    setTargetTrack(targetTrack);
    repaint();
}

} // namespace djr
