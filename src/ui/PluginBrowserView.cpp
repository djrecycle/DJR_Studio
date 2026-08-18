#include "PluginBrowserView.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int headerHeight = Metrics::panelToolbarHeight;
    constexpr int rowHeight = 32;
    constexpr int filterRowHeight = 22;
}

//==============================================================================
PluginBrowserView::Model::Model(PluginBrowserView& ownerToUse)
    : owner(ownerToUse)
{
}

int PluginBrowserView::Model::getNumRows()
{
    return juce::jmax(1, owner.visibleIndices.size());
}

void PluginBrowserView::Model::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(0, 1);

    if (owner.visibleIndices.isEmpty())
    {
        // Two different empties: nothing scanned yet, or nothing left after the
        // filter. Saying "press Scan" to someone who just filtered is a lie.
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(12.0f));
        g.drawText(owner.plugins.isEmpty() ? TRANS("No plugins yet - press Scan")
                                           : TRANS("No plugins match this filter"),
                   bounds.reduced(8, 0), juce::Justification::centredLeft, true);
        return;
    }

    const auto libraryIndex = owner.libraryIndexForRow(row);

    if (libraryIndex < 0)
        return;

    const auto& plugin = owner.plugins.getReference(libraryIndex);
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
    const auto libraryIndex = owner.libraryIndexForRow(row);

    if (libraryIndex < 0)
        return;

    owner.listBox.selectRow(row);

    // The library index, never the row: the filter makes those two different
    // things, and everything downstream loads by library index.
    if (owner.loadPluginCallback)
        owner.loadPluginCallback(libraryIndex, owner.getSelectedTrackIndex());
}

//==============================================================================
PluginBrowserView::PluginBrowserView(PluginManager& manager, Mixer& mixerToUse)
    : pluginManager(manager), mixer(mixerToUse)
{
    scanButton.setFillColour(Theme::purple());
    scanButton.addListener(this);
    addAndMakeVisible(scanButton);

    for (auto* chip : { &allChip, &generatorChip, &effectChip })
    {
        chip->setClickingTogglesState(true);
        chip->setRadioGroupId(0xF11);
        chip->addListener(this);
        addAndMakeVisible(chip);
    }

    allChip.setToggleState(true, juce::dontSendNotification);

    categoryButton.addListener(this);
    addAndMakeVisible(categoryButton);

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
    categoryButton.removeListener(this);

    for (auto* chip : { &allChip, &generatorChip, &effectChip })
        chip->removeListener(this);

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

    // Both numbers when a filter is on, so it is obvious the list is a subset
    // rather than the whole library having shrunk.
    const auto filtered = visibleIndices.size() != plugins.size();

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(10.0f));
    g.drawText(filtered ? juce::String(visibleIndices.size()) + " / " + juce::String(plugins.size()) + " plugin"
                        : juce::String(plugins.size()) + " plugin",
               header.reduced(7, 0),
               juce::Justification::centredLeft,
               false);
}

void PluginBrowserView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight).reduced(5, 0);
    scanButton.setBounds(header.removeFromRight(scanButton.getPreferredWidth()).withSizeKeepingCentre(scanButton.getPreferredWidth(), 17));

    auto filterRow = area.removeFromTop(filterRowHeight).reduced(5, 0);

    for (auto* chip : { &allChip, &generatorChip, &effectChip })
    {
        chip->setBounds(filterRow.removeFromLeft(chip->getPreferredWidth())
                            .withSizeKeepingCentre(chip->getPreferredWidth(), 16));
        filterRow.removeFromLeft(3);
    }

    // The category button takes whatever is left, so a long name is truncated
    // rather than pushing the chips off the panel.
    if (filterRow.getWidth() > 40)
        categoryButton.setBounds(filterRow.withSizeKeepingCentre(filterRow.getWidth(), 16));
    else
        categoryButton.setBounds({});

    listBox.setBounds(area.reduced(5).withTrimmedTop(0));
}

void PluginBrowserView::buttonClicked(juce::Button* button)
{
    if (button == &categoryButton)
    {
        showCategoryMenu();
        return;
    }

    if (button == &allChip || button == &generatorChip || button == &effectChip)
    {
        // Read the chips' resulting state rather than which one was clicked: a
        // radio group also notifies the chip it switched off, and the order the
        // two arrive in is JUCE's business, not ours.
        group = generatorChip.getToggleState() ? Group::generators
              : effectChip.getToggleState()    ? Group::effects
                                               : Group::all;

        // The category may not exist inside the new group, and leaving it would
        // show an empty list with a filter nobody chose.
        if (categoryFilter.isNotEmpty() && ! getAvailableCategories().contains(categoryFilter))
            categoryFilter = {};

        applyFilter();
        return;
    }

    if (button != &scanButton)
        return;

    scanButton.setButtonText("Scanning");
    scanButton.setEnabled(false);
    setStatusText(TRANS("Scanning plugin folders..."));
    pluginManager.scanPluginsAsync();
}

void PluginBrowserView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refreshList();
}

juce::String PluginBrowserView::describeCategory(const juce::PluginDescription& description)
{
    // VST3 reports a path like "Fx|Reverb"; the last part is the useful one.
    // LV2 hands back its class name already. Anything blank is grouped together
    // rather than shown as an empty entry nobody can read.
    const auto raw = description.category.trim();

    if (raw.isEmpty())
        return description.isInstrument ? juce::String("Instrument") : juce::String("Lainnya");

    return raw.fromLastOccurrenceOf("|", false, false).trim();
}

void PluginBrowserView::applyFilter()
{
    visibleIndices.clearQuick();

    for (int i = 0; i < plugins.size(); ++i)
    {
        const auto& plugin = plugins.getReference(i);

        if (group == Group::generators && ! plugin.isInstrument)
            continue;

        if (group == Group::effects && plugin.isInstrument)
            continue;

        if (categoryFilter.isNotEmpty() && describeCategory(plugin) != categoryFilter)
            continue;

        visibleIndices.add(i);
    }

    // The chips' own radio group owns which one looks active. Writing their
    // toggle state from here re-entered the notification that called us and the
    // two fought: the pressed chip filtered the list while the old one stayed lit.
    categoryButton.setButtonText(categoryFilter.isEmpty() ? TRANS("All types") : categoryFilter);

    listBox.deselectAllRows();
    listBox.updateContent();
    listBox.repaint();
    resized();
    repaint();
}

juce::StringArray PluginBrowserView::getAvailableCategories() const
{
    juce::StringArray categories;

    for (const auto& plugin : plugins)
    {
        // Only what the current group can show, so the menu never offers a
        // category that would come back empty.
        if (group == Group::generators && ! plugin.isInstrument)
            continue;

        if (group == Group::effects && plugin.isInstrument)
            continue;

        categories.addIfNotAlreadyThere(describeCategory(plugin));
    }

    categories.sortNatural();
    return categories;
}

void PluginBrowserView::showCategoryMenu()
{
    const auto categories = getAvailableCategories();

    juce::PopupMenu menu;
    menu.addItem(1, TRANS("All types"), true, categoryFilter.isEmpty());

    if (! categories.isEmpty())
        menu.addSeparator();

    for (int i = 0; i < categories.size(); ++i)
        menu.addItem(2 + i, categories[i], true, categoryFilter == categories[i]);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&categoryButton)
                           .withMinimumWidth(160)
                           .withStandardItemHeight(21),
        [this, categories] (int result)
        {
            if (result == 0)
                return;

            categoryFilter = result == 1 ? juce::String()
                                         : categories[juce::jlimit(0, categories.size() - 1, result - 2)];
            applyFilter();
        });
}

void PluginBrowserView::refreshList()
{
    plugins = pluginManager.getKnownPlugins();

    // A category that no longer exists after a rescan would hide everything
    // with nothing on screen explaining why.
    if (categoryFilter.isNotEmpty() && ! getAvailableCategories().contains(categoryFilter))
        categoryFilter = {};

    scanButton.setButtonText("Scan");
    scanButton.setEnabled(! pluginManager.isScanning());
    applyFilter();

    setStatusText(pluginManager.isScanning()
                      ? juce::String(TRANS("Scanning plugins..."))
                      : juce::String(plugins.size()) + TRANS(" plugins found."));
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
    return libraryIndexForRow(listBox.getSelectedRow());
}

int PluginBrowserView::libraryIndexForRow(int row) const
{
    return juce::isPositiveAndBelow(row, visibleIndices.size()) ? visibleIndices[row] : -1;
}

int PluginBrowserView::getSelectedTrackIndex() const noexcept
{
    return targetTrack;
}

juce::String PluginBrowserView::getSelectedPluginDisplayName() const
{
    const auto index = getSelectedPluginIndex();
    return juce::isPositiveAndBelow(index, plugins.size()) ? plugins.getReference(index).name : juce::String();
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
