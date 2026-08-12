#include "BrowserPanel.h"

#include "Theme.h"
#include "utils/FileUtils.h"

namespace djr
{

namespace
{
    constexpr int headerHeight = 24;
    constexpr int searchHeight = 20;
    constexpr int chipHeight = 18;
    constexpr int rowHeight = 20;
    constexpr int footerHeight = 44;
}

//==============================================================================
BrowserPanel::RowsModel::RowsModel(BrowserPanel& ownerToUse)
    : owner(ownerToUse)
{
}

int BrowserPanel::RowsModel::getNumRows()
{
    return juce::jmax(1, static_cast<int>(owner.rows.size()));
}

void BrowserPanel::RowsModel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(0, 0);

    if (owner.rows.empty())
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(12.0f));
        g.drawText("Tidak ada item", bounds.reduced(8, 0), juce::Justification::centredLeft, true);
        return;
    }

    if (! juce::isPositiveAndBelow(row, static_cast<int>(owner.rows.size())))
        return;

    const auto& item = owner.rows[static_cast<size_t>(row)];

    if (rowIsSelected)
    {
        g.setColour(Theme::control());
        g.fillRoundedRectangle(bounds.toFloat().reduced(1.0f), 4.0f);
    }

    auto content = bounds.reduced(6, 0);

    auto dotArea = content.removeFromLeft(5).withSizeKeepingCentre(5, 5);
    g.setColour(item.dot);
    g.fillRoundedRectangle(dotArea.toFloat(), 1.5f);
    content.removeFromLeft(6);

    const auto metaFont = Theme::mono(10.0f);
    if (item.meta.isNotEmpty())
    {
        const auto metaWidth = Theme::textWidth(metaFont, item.meta) + 4;
        g.setColour(Theme::faintText());
        g.setFont(metaFont);
        g.drawText(item.meta, content.removeFromRight(metaWidth), juce::Justification::centredRight, false);
    }

    if (! item.isGroup)
        content.removeFromLeft(8);

    g.setColour(item.isGroup ? Theme::text() : Theme::textSoft());
    g.setFont(Theme::ui(12.0f, item.isGroup));
    g.drawText(item.name, content, juce::Justification::centredLeft, true);
}

void BrowserPanel::RowsModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow(row, static_cast<int>(owner.rows.size())))
        return;

    const auto& item = owner.rows[static_cast<size_t>(row)];
    if (item.file.existsAsFile() && owner.fileActivatedCallback)
        owner.fileActivatedCallback(item.file);
}

//==============================================================================
BrowserPanel::BrowserPanel()
{
    for (auto* button : { &dockButton, &minimizeButton, &collapseButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    searchBox.setTextToShowWhenEmpty("Cari sample, preset, plugin...", Theme::faintText());
    searchBox.setFont(Theme::ui(12.5f));
    searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    searchBox.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    searchBox.setBorder({});
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.addListener(this);
    addAndMakeVisible(searchBox);

    for (int i = 0; i < sections.size(); ++i)
    {
        auto* chip = sectionChips.add(new TabChip(sections[i]));
        chip->setRadioGroupId(0xD3B);
        chip->addListener(this);
        addAndMakeVisible(chip);
    }

    rowsList.setModel(&rowsModel);
    rowsList.setRowHeight(rowHeight);
    rowsList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    rowsList.setOutlineThickness(0);
    addAndMakeVisible(rowsList);

    selectSection(selectedSectionIndex);
    refreshControls();
}

BrowserPanel::~BrowserPanel()
{
    searchBox.removeListener(this);

    for (auto* button : { &dockButton, &minimizeButton, &collapseButton })
        button->removeListener(this);

    for (auto* chip : sectionChips)
        chip->removeListener(this);
}

void BrowserPanel::paint(juce::Graphics& g)
{
    Theme::drawCard(g, getLocalBounds(), Theme::panel(), Theme::outline(), Metrics::panelRadius);

    if (collapsed)
    {
        juce::Graphics::ScopedSaveState state(g);
        g.setColour(Theme::accent());
        g.setFont(Theme::display(13.0f));
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi,
                                                       static_cast<float>(getWidth()) * 0.5f,
                                                       static_cast<float>(getHeight()) * 0.5f));
        g.drawText("BROWSER",
                   getLocalBounds().withSizeKeepingCentre(getHeight(), getWidth()),
                   juce::Justification::centred,
                   false);
        return;
    }

    auto area = getLocalBounds();
    auto header = area.removeFromTop(headerHeight);
    Theme::drawPanelTitle(g, header.reduced(7, 0).removeFromLeft(70), "Browser");

    g.setColour(Theme::outline());
    g.fillRect(header.removeFromBottom(1));

    // Search field frame -----------------------------------------------------
    const auto searchArea = searchBox.getBounds().expanded(7, 0).withTrimmedLeft(-18);
    Theme::drawCard(g, searchArea, Theme::inset(), Theme::divider(), 4.0f);
    g.setColour(Theme::mutedText());
    Icons::draw(g, Icon::search,
                searchArea.toFloat().withWidth(11.0f).translated(7.0f, 0.0f).withSizeKeepingCentre(11.0f, 11.0f),
                1.4f);

    // Footer -----------------------------------------------------------------
    auto footer = getLocalBounds().removeFromBottom(footerHeight);
    g.setColour(Theme::panelDeep());
    g.fillRect(footer.withTrimmedBottom(1));
    g.setColour(Theme::outline());
    g.fillRect(footer.removeFromTop(1));

    auto footerContent = footer.reduced(7, 6);
    auto titleRow = footerContent.removeFromTop(12);

    const auto scanTitle = scanning ? juce::String("Scanning VST3...") : juce::String("VST3 library");
    const auto scanCount = scanning && scanTotal > 0
        ? juce::String(scanScanned) + "/" + juce::String(scanTotal)
        : juce::String(vst3PluginNames.size()) + " plugin";

    g.setColour(Theme::textSoft());
    g.setFont(Theme::ui(11.0f, true));
    g.drawText(scanTitle, titleRow, juce::Justification::centredLeft, false);
    g.setColour(Theme::mutedText());
    g.setFont(Theme::mono(10.0f));
    g.drawText(scanCount, titleRow, juce::Justification::centredRight, false);

    footerContent.removeFromTop(4);
    auto progress = footerContent.removeFromTop(4);
    g.setColour(Theme::meterTrack());
    g.fillRoundedRectangle(progress.toFloat(), 2.0f);

    const auto ratio = scanning && scanTotal > 0
        ? juce::jlimit(0.0f, 1.0f, static_cast<float>(scanScanned) / static_cast<float>(scanTotal))
        : (vst3PluginNames.isEmpty() ? 0.0f : 1.0f);

    if (ratio > 0.0f)
    {
        auto filled = progress.toFloat().withWidth(progress.getWidth() * ratio);
        g.setGradientFill(juce::ColourGradient(Theme::purple(), filled.getX(), 0.0f,
                                               Theme::accent(), filled.getRight(), 0.0f, false));
        g.fillRoundedRectangle(filled, 2.0f);
    }

    footerContent.removeFromTop(4);
    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(9.5f));
    g.drawText(scanCurrentItem.isNotEmpty() ? scanCurrentItem : FileUtils::getUserVst3Folder().getFullPathName(),
               footerContent,
               juce::Justification::centredLeft,
               true);
}

void BrowserPanel::resized()
{
    auto area = getLocalBounds();

    if (collapsed)
    {
        auto rail = area.reduced(4).withTrimmedTop(7);
        dockButton.setBounds(rail.removeFromTop(19).withSizeKeepingCentre(19, 19));
        rail.removeFromTop(4);
        minimizeButton.setBounds(rail.removeFromTop(19).withSizeKeepingCentre(19, 19));
        rail.removeFromTop(4);
        collapseButton.setBounds(rail.removeFromTop(19).withSizeKeepingCentre(19, 19));

        searchBox.setBounds({});
        rowsList.setBounds({});
        for (auto* chip : sectionChips)
            chip->setBounds({});

        return;
    }

    auto header = area.removeFromTop(headerHeight).reduced(6, 0);
    header.removeFromLeft(64);
    collapseButton.setBounds(header.removeFromRight(18).withSizeKeepingCentre(18, 18));
    header.removeFromRight(3);
    minimizeButton.setBounds(header.removeFromRight(18).withSizeKeepingCentre(18, 18));
    header.removeFromRight(3);
    dockButton.setBounds(header.removeFromRight(18).withSizeKeepingCentre(18, 18));

    auto searchArea = area.removeFromTop(searchHeight + 12).reduced(7, 6);
    searchBox.setBounds(searchArea.withTrimmedLeft(18).reduced(0, 4));

    // Section chips wrap onto extra rows when the panel is narrow.
    const auto chipGap = 3;
    const auto availableWidth = juce::jmax(60, area.getWidth() - 14);

    int rowsNeeded = 1;
    int measuredWidth = 0;
    for (auto* chip : sectionChips)
    {
        const auto width = chip->getPreferredWidth();
        if (measuredWidth > 0 && measuredWidth + width > availableWidth)
        {
            ++rowsNeeded;
            measuredWidth = 0;
        }

        measuredWidth += width + chipGap;
    }

    auto chipArea = area.removeFromTop(rowsNeeded * chipHeight + (rowsNeeded - 1) * chipGap).reduced(7, 0);
    auto chipX = chipArea.getX();
    auto chipY = chipArea.getY();

    for (auto* chip : sectionChips)
    {
        const auto width = juce::jmin(chip->getPreferredWidth(), chipArea.getWidth());

        if (chipX > chipArea.getX() && chipX + width > chipArea.getRight())
        {
            chipY += chipHeight + chipGap;
            chipX = chipArea.getX();
        }

        chip->setBounds(chipX, chipY, width, chipHeight);
        chipX += width + chipGap;
    }

    area.removeFromTop(6);

    area.removeFromBottom(footerHeight);
    rowsList.setBounds(area.reduced(5, 0));
}

void BrowserPanel::setCollapsed(bool shouldCollapse)
{
    collapsed = shouldCollapse;
    refreshControls();
    resized();
    repaint();
}

void BrowserPanel::setMinimized(bool shouldMinimize)
{
    minimized = shouldMinimize;
    refreshControls();
    resized();
    repaint();
}

void BrowserPanel::setDockPosition(DockPosition position)
{
    dockPosition = position;
    refreshControls();
}

void BrowserPanel::setCollapseToggleCallback(std::function<void()> callback)
{
    collapseToggleCallback = std::move(callback);
}

void BrowserPanel::setMinimizeToggleCallback(std::function<void()> callback)
{
    minimizeToggleCallback = std::move(callback);
}

void BrowserPanel::setDockCycleCallback(std::function<void()> callback)
{
    dockCycleCallback = std::move(callback);
}

void BrowserPanel::setFileActivatedCallback(std::function<void(const juce::File&)> callback)
{
    fileActivatedCallback = std::move(callback);
}

void BrowserPanel::setVst3Plugins(const juce::Array<juce::PluginDescription>& plugins)
{
    vst3PluginNames.clear();
    vst3PluginMakers.clear();

    for (const auto& plugin : plugins)
    {
        if (plugin.pluginFormatName != "VST3")
            continue;

        vst3PluginNames.add(plugin.name);
        vst3PluginMakers.add(plugin.manufacturerName);
    }

    if (selectedSectionIndex == sections.size() - 1)
        rebuildRows();

    repaint();
}

void BrowserPanel::setScanProgress(bool isScanning, int scanned, int total, const juce::String& currentItem)
{
    scanning = isScanning;
    scanScanned = scanned;
    scanTotal = total;
    scanCurrentItem = currentItem;
    repaint();
}

void BrowserPanel::refreshContent()
{
    rebuildRows();
}

juce::String BrowserPanel::getSelectedSection() const
{
    return sections[juce::jlimit(0, sections.size() - 1, selectedSectionIndex)];
}

void BrowserPanel::buttonClicked(juce::Button* button)
{
    if (button == &collapseButton)
    {
        if (collapseToggleCallback)
            collapseToggleCallback();
        return;
    }

    if (button == &minimizeButton)
    {
        if (minimizeToggleCallback)
            minimizeToggleCallback();
        return;
    }

    if (button == &dockButton)
    {
        if (dockCycleCallback)
            dockCycleCallback();
        return;
    }

    for (int i = 0; i < sectionChips.size(); ++i)
        if (button == sectionChips[i])
            selectSection(i);
}

void BrowserPanel::textEditorTextChanged(juce::TextEditor& editor)
{
    juce::ignoreUnused(editor);
    rebuildRows();
}

void BrowserPanel::selectSection(int index)
{
    selectedSectionIndex = juce::jlimit(0, sections.size() - 1, index);

    for (int i = 0; i < sectionChips.size(); ++i)
        sectionChips[i]->setToggleState(i == selectedSectionIndex, juce::dontSendNotification);

    rebuildRows();
}

void BrowserPanel::rebuildRows()
{
    rows.clear();

    const auto root = FileUtils::getDefaultProjectRoot();

    switch (selectedSectionIndex)
    {
        case 0: appendFolder(root, "*.djrs"); break;
        case 1: appendFolder(root.getChildFile("Samples"), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"); break;
        case 2: appendFolder(root.getChildFile("Recordings"), "*.wav;*.flac"); break;
        case 3: appendFolder(root.getChildFile("Presets"), "*.djrp;*.xml"); break;

        default:
        {
            for (int i = 0; i < vst3PluginNames.size(); ++i)
                rows.push_back({ vst3PluginNames[i], vst3PluginMakers[i], false, {}, Theme::purple() });
            break;
        }
    }

    const auto filter = searchBox.getText().trim();
    if (filter.isNotEmpty())
    {
        std::vector<Row> filtered;
        for (const auto& row : rows)
            if (! row.isGroup && row.name.containsIgnoreCase(filter))
                filtered.push_back(row);

        rows = std::move(filtered);
    }

    rowsList.updateContent();
    rowsList.repaint();
}

void BrowserPanel::appendFolder(const juce::File& folder, const juce::String& wildcard)
{
    if (! folder.isDirectory())
    {
        rows.push_back({ folder.getFileName() + " belum ada", "", true, {}, Theme::accent() });
        return;
    }

    const auto addFiles = [this, &wildcard] (const juce::File& directory)
    {
        auto files = directory.findChildFiles(juce::File::findFiles, false, wildcard);
        files.sort();

        for (const auto& file : files)
            rows.push_back({ file.getFileName(), describeFile(file), false, file, Theme::outlineStrong().brighter(0.2f) });
    };

    auto subFolders = folder.findChildFiles(juce::File::findDirectories, false);
    subFolders.sort();

    addFiles(folder);

    for (const auto& subFolder : subFolders)
    {
        rows.push_back({ subFolder.getFileName(), "", true, subFolder, Theme::accent() });
        addFiles(subFolder);
    }

    if (rows.empty())
        rows.push_back({ "Folder kosong", "", true, {}, Theme::accent() });
}

void BrowserPanel::refreshControls()
{
    collapseButton.setIcon(collapsed ? Icon::chevronRight : Icon::chevronLeft);
    collapseButton.setTooltip(collapsed ? "Expand browser" : "Collapse browser");
    minimizeButton.setIcon(minimized ? Icon::restore : Icon::minimise);
    minimizeButton.setTooltip(minimized ? "Restore browser" : "Minimize browser");

    auto dockIcon = Icon::dockLeft;
    juce::String dockTooltip = "Browser di kiri";

    if (dockPosition == DockPosition::right)
    {
        dockIcon = Icon::dockRight;
        dockTooltip = "Browser di kanan";
    }
    else if (dockPosition == DockPosition::bottom)
    {
        dockIcon = Icon::dockBottom;
        dockTooltip = "Browser di bawah";
    }

    dockButton.setIcon(dockIcon);
    dockButton.setTooltip(dockTooltip);

    searchBox.setVisible(! collapsed);
    rowsList.setVisible(! collapsed);

    for (auto* chip : sectionChips)
        chip->setVisible(! collapsed);
}

juce::String BrowserPanel::describeFile(const juce::File& file)
{
    const auto age = juce::Time::getCurrentTime() - file.getLastModificationTime();

    if (age.inMinutes() < 60.0)
        return juce::String(juce::jmax(1, static_cast<int>(age.inMinutes()))) + " m";

    if (age.inHours() < 24.0)
        return juce::String(static_cast<int>(age.inHours())) + " j";

    if (age.inDays() < 30.0)
        return juce::String(static_cast<int>(age.inDays())) + " h";

    return juce::File::descriptionOfSizeInBytes(file.getSize());
}

} // namespace djr
