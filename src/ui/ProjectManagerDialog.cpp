#include "ProjectManagerDialog.h"

#include "Theme.h"
#include "utils/FileUtils.h"

namespace djr
{

namespace
{
    constexpr int cardWidth = 720;
    constexpr int cardHeight = 420;
    constexpr int dialogHeaderHeight = 52;
    constexpr int tabRowHeight = 28;
    constexpr int entryHeight = 52;
    constexpr int entryGap = 6;
    constexpr int contentPadding = 18;

    juce::String relativeTime(const juce::Time& time)
    {
        const auto age = juce::Time::getCurrentTime() - time;

        if (age.inMinutes() < 1.0)  return TRANS("just now");
        if (age.inMinutes() < 60.0) return juce::String(static_cast<int>(age.inMinutes())) + " menit lalu";
        if (age.inHours() < 24.0)   return juce::String(static_cast<int>(age.inHours())) + " jam lalu";
        if (age.inDays() < 30.0)    return juce::String(static_cast<int>(age.inDays())) + " hari lalu";

        return time.toString(true, false);
    }
}

ProjectManagerDialog::ProjectManagerDialog()
{
    closeButton.setDangerHover(true);
    closeButton.setCornerSize(6.0f);
    closeButton.addListener(this);
    addAndMakeVisible(closeButton);

    for (auto* chip : { &recentChip, &templatesChip, &backupsChip })
    {
        chip->setRadioGroupId(0xD3C);
        chip->addListener(this);
        addAndMakeVisible(chip);
    }

    recentChip.setToggleState(true, juce::dontSendNotification);

    newProjectButton.setFillColour(Theme::green());
    newProjectButton.addListener(this);
    addAndMakeVisible(newProjectButton);

    browseButton.addListener(this);
    addAndMakeVisible(browseButton);
}

ProjectManagerDialog::~ProjectManagerDialog()
{
    closeButton.removeListener(this);
    newProjectButton.removeListener(this);
    browseButton.removeListener(this);

    for (auto* chip : { &recentChip, &templatesChip, &backupsChip })
        chip->removeListener(this);
}

void ProjectManagerDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromString("b807080b"));

    const auto card = getCardBounds();

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(card.toFloat().translated(0.0f, 12.0f).expanded(6.0f), 18.0f);

    Theme::drawCard(g, card, Theme::panel(), Theme::outlineStrong(), 14.0f);

    auto header = card.withHeight(dialogHeaderHeight);
    g.setColour(Theme::outline());
    g.fillRect(header.withHeight(1).withY(header.getBottom() - 1));

    auto headerContent = header.reduced(contentPadding, 0);
    g.setColour(Theme::accent());
    Icons::draw(g, Icon::folder, headerContent.removeFromLeft(16).toFloat().withSizeKeepingCentre(16.0f, 16.0f), 1.6f);
    headerContent.removeFromLeft(10);
    g.setColour(Theme::text());
    g.setFont(Theme::ui(15.0f, true));
    g.drawText("Project Manager", headerContent, juce::Justification::centredLeft, false);

    if (selectedTab != 0)
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(13.0f));
        g.drawText(selectedTab == 1 ? TRANS("No templates yet.") : TRANS("No automatic backups yet."),
                   card.withTrimmedTop(dialogHeaderHeight + tabRowHeight + 30),
                   juce::Justification::centredTop,
                   false);
        return;
    }

    if (entries.empty())
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(13.0f));
        g.drawText(TRANS("No projects in ") + FileUtils::getDefaultProjectRoot().getFullPathName(),
                   card.withTrimmedTop(dialogHeaderHeight + tabRowHeight + 30),
                   juce::Justification::centredTop,
                   true);
        return;
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        const auto row = getEntryBounds(i);
        if (row.getBottom() > card.getBottom() - contentPadding)
            break;

        const auto& entry = entries[static_cast<size_t>(i)];
        const auto hovered = row.contains(getMouseXYRelative());

        Theme::drawCard(g, row, Theme::panelAlt(), hovered ? Theme::accent() : Theme::divider(), 9.0f);

        auto content = row.reduced(12, 0);
        auto badge = content.removeFromLeft(34).withSizeKeepingCentre(34, 34);
        g.setColour(Theme::inset());
        g.fillRoundedRectangle(badge.toFloat(), 8.0f);
        g.setColour(entry.colour);
        g.setFont(Theme::mono(10.0f));
        g.drawText(entry.tempo, badge, juce::Justification::centred, false);
        content.removeFromLeft(12);

        const auto whenWidth = Theme::textWidth(Theme::ui(11.5f), entry.when) + 8;
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(11.5f));
        g.drawText(entry.when, content.removeFromRight(whenWidth), juce::Justification::centredRight, false);

        g.setColour(Theme::text());
        g.setFont(Theme::ui(13.0f, true));
        g.drawText(entry.name, content.removeFromTop(entryHeight / 2).withTrimmedTop(6),
                   juce::Justification::bottomLeft, true);
        g.setColour(Theme::mutedText());
        g.setFont(Theme::mono(10.5f));
        g.drawText(entry.meta, content, juce::Justification::topLeft, true);
    }
}

void ProjectManagerDialog::resized()
{
    const auto card = getCardBounds();

    auto header = card.withHeight(dialogHeaderHeight).reduced(contentPadding, 0);
    closeButton.setBounds(header.removeFromRight(26).withSizeKeepingCentre(26, 26));

    auto tabs = card.withTrimmedTop(dialogHeaderHeight + 14).withHeight(tabRowHeight).reduced(contentPadding, 0);
    recentChip.setBounds(tabs.removeFromLeft(recentChip.getPreferredWidth() + 8));
    tabs.removeFromLeft(8);
    templatesChip.setBounds(tabs.removeFromLeft(templatesChip.getPreferredWidth() + 8));
    tabs.removeFromLeft(8);
    backupsChip.setBounds(tabs.removeFromLeft(backupsChip.getPreferredWidth() + 8));

    newProjectButton.setBounds(tabs.removeFromRight(newProjectButton.getPreferredWidth()));
    tabs.removeFromRight(8);
    browseButton.setBounds(tabs.removeFromRight(browseButton.getPreferredWidth()));
}

void ProjectManagerDialog::mouseDown(const juce::MouseEvent& event)
{
    const auto card = getCardBounds();

    if (! card.contains(event.getPosition()))
    {
        if (closeCallback)
            closeCallback();

        return;
    }

    if (selectedTab != 0)
        return;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        if (! getEntryBounds(i).contains(event.getPosition()))
            continue;

        if (openProjectCallback)
            openProjectCallback(entries[static_cast<size_t>(i)].file);

        return;
    }
}

void ProjectManagerDialog::refresh()
{
    entries.clear();

    const auto root = FileUtils::getDefaultProjectRoot();
    auto files = root.findChildFiles(juce::File::findFiles, true, "*.djrs");

    std::sort(files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });

    int index = 0;
    for (const auto& file : files)
    {
        if (index >= 10)
            break;

        Entry entry;
        entry.file = file;
        entry.name = file.getFileNameWithoutExtension();
        entry.when = relativeTime(file.getLastModificationTime());
        entry.colour = Theme::trackColour(index);
        entry.tempo = "--";

        const auto parsed = juce::JSON::parse(file.loadFileAsString());
        if (auto* object = parsed.getDynamicObject())
        {
            const auto tempo = object->getProperty("tempo");
            if (! tempo.isVoid())
                entry.tempo = juce::String(juce::roundToInt(static_cast<double>(tempo)));

            const auto tracks = object->getProperty("tracks");
            const auto trackCount = tracks.isArray() ? tracks.getArray()->size() : 0;
            entry.meta = juce::String(trackCount) + " track - " + entry.tempo + " BPM - "
                       + juce::File::descriptionOfSizeInBytes(file.getSize());
        }
        else
        {
            entry.meta = juce::File::descriptionOfSizeInBytes(file.getSize());
        }

        entries.push_back(entry);
        ++index;
    }

    repaint();
}

void ProjectManagerDialog::setCloseCallback(std::function<void()> callback)
{
    closeCallback = std::move(callback);
}

void ProjectManagerDialog::setOpenProjectCallback(std::function<void(const juce::File&)> callback)
{
    openProjectCallback = std::move(callback);
}

void ProjectManagerDialog::setNewProjectCallback(std::function<void()> callback)
{
    newProjectCallback = std::move(callback);
}

void ProjectManagerDialog::setBrowseCallback(std::function<void()> callback)
{
    browseCallback = std::move(callback);
}

void ProjectManagerDialog::buttonClicked(juce::Button* button)
{
    if (button == &closeButton)
    {
        if (closeCallback)
            closeCallback();
    }
    else if (button == &newProjectButton)
    {
        if (newProjectCallback)
            newProjectCallback();
    }
    else if (button == &browseButton)
    {
        if (browseCallback)
            browseCallback();
    }
    else if (button == &recentChip)
    {
        selectedTab = 0;
        repaint();
    }
    else if (button == &templatesChip)
    {
        selectedTab = 1;
        repaint();
    }
    else if (button == &backupsChip)
    {
        selectedTab = 2;
        repaint();
    }
}

juce::Rectangle<int> ProjectManagerDialog::getCardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(juce::jmin(cardWidth, getWidth() - 40),
                                                  juce::jmin(cardHeight, getHeight() - 40));
}

juce::Rectangle<int> ProjectManagerDialog::getEntryBounds(int index) const
{
    const auto card = getCardBounds();
    const auto top = card.getY() + dialogHeaderHeight + 14 + tabRowHeight + 12 + index * (entryHeight + entryGap);

    return { card.getX() + contentPadding, top, card.getWidth() - contentPadding * 2, entryHeight };
}

} // namespace djr
