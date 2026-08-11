#include "InsertChainPanel.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int headerHeight = Metrics::panelToolbarHeight;
    constexpr int captionHeight = 15;
    constexpr int rowHeight = 22;
    constexpr int rowGap = 3;
    constexpr int contentPadding = 7;
}

InsertChainPanel::InsertChainPanel(Mixer& mixerToUse)
    : mixer(mixerToUse)
{
}

void InsertChainPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(Theme::panel());

    const auto* track = mixer.getTrack(selectedTrack);
    const auto colour = Theme::trackColour(selectedTrack);

    auto header = bounds.withHeight(headerHeight);
    g.setColour(Theme::panelHeader());
    g.fillRect(header);
    g.setColour(Theme::outline());
    g.fillRect(header.withHeight(1).withY(header.getBottom() - 1));

    auto headerContent = header.reduced(contentPadding, 0);
    auto chip = headerContent.removeFromLeft(3).withSizeKeepingCentre(3, 12);
    g.setColour(colour);
    g.fillRoundedRectangle(chip.toFloat(), 1.5f);
    headerContent.removeFromLeft(6);

    const auto trackName = track != nullptr ? track->getName() : juce::String("Tidak ada track");
    const auto nameFont = Theme::ui(12.0f, true);
    const auto nameWidth = Theme::textWidth(nameFont, trackName);

    g.setColour(Theme::text());
    g.setFont(nameFont);
    g.drawText(trackName, headerContent.removeFromLeft(nameWidth), juce::Justification::centredLeft, false);
    headerContent.removeFromLeft(7);

    if (track != nullptr)
    {
        const auto kind = track->getKind() == TrackKind::midi
            ? "MIDI - CH " + juce::String(selectedTrack + 1)
            : juce::String("AUDIO");

        g.setColour(Theme::faintText());
        g.setFont(Theme::mono(9.5f));
        g.drawText(kind, headerContent, juce::Justification::centredLeft, true);
    }

    if (track == nullptr)
        return;

    // Instrument slot -------------------------------------------------------
    Theme::drawCaption(g,
                       bounds.withTrimmedTop(headerHeight).withHeight(captionHeight).reduced(contentPadding, 0),
                       "Instrument");

    {
        const auto row = getInstrumentRowBounds();
        const auto instrumentName = track->getInstrumentName();
        const auto empty = instrumentName.isEmpty();

        if (! empty)
        {
            g.setColour(Theme::purple().withAlpha(0.16f));
            g.fillRoundedRectangle(row.toFloat(), 4.0f);
        }

        juce::Path outline;
        outline.addRoundedRectangle(row.toFloat().reduced(0.5f), 4.0f);
        juce::Path dashed;
        const float dashes[] = { 3.0f, 2.5f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(empty ? Theme::outlineStrong().withAlpha(0.8f) : Theme::purple());
        g.fillPath(dashed);

        auto content = row.reduced(7, 0);
        auto dot = content.removeFromLeft(5).withSizeKeepingCentre(5, 5);
        g.setColour(empty ? Theme::outlineStrong().brighter(0.2f) : Theme::purple());
        g.fillEllipse(dot.toFloat());
        content.removeFromLeft(6);

        if (! empty)
        {
            g.setColour(Theme::faintText());
            g.setFont(Theme::mono(9.5f));
            g.drawText("SYNTH", content.removeFromRight(38), juce::Justification::centredRight, false);
        }

        g.setColour(empty ? Theme::faintText() : Theme::text());
        g.setFont(Theme::ui(11.5f));
        g.drawText(empty ? "Belum ada instrument" : instrumentName,
                   content, juce::Justification::centredLeft, true);
    }

    Theme::drawCaption(g,
                       bounds.withTrimmedTop(headerHeight + captionHeight + rowHeight + rowGap * 2)
                             .withHeight(captionHeight)
                             .reduced(contentPadding, 0),
                       "Insert chain");

    const auto pluginNames = track->getPluginNames();

    for (int i = 0; i < getNumRows(); ++i)
    {
        const auto row = getRowBounds(i);
        if (row.getBottom() > bounds.getBottom() - 6)
            break;

        const auto isDropSlot = i >= pluginNames.size();

        g.setColour(isDropSlot ? juce::Colours::transparentBlack : Theme::panelAlt());
        if (! isDropSlot)
            g.fillRoundedRectangle(row.toFloat(), 4.0f);

        juce::Path outline;
        outline.addRoundedRectangle(row.toFloat().reduced(0.5f), 4.0f);

        juce::Path dashed;
        const float dashes[] = { 3.0f, 2.5f };
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);

        g.setColour(isDropSlot ? Theme::outlineStrong().withAlpha(0.8f) : Theme::outlineStrong());
        g.fillPath(dashed);

        auto content = row.reduced(7, 0);
        auto dot = content.removeFromLeft(5).withSizeKeepingCentre(5, 5);
        g.setColour(isDropSlot ? Theme::outlineStrong().brighter(0.2f) : Theme::trackColour(i));
        g.fillEllipse(dot.toFloat());
        content.removeFromLeft(6);

        if (! isDropSlot)
        {
            g.setColour(Theme::faintText());
            g.setFont(Theme::mono(9.5f));
            g.drawText("VST3", content.removeFromRight(30), juce::Justification::centredRight, false);
        }

        g.setColour(isDropSlot ? Theme::faintText() : Theme::text());
        g.setFont(Theme::ui(11.5f));
        g.drawText(isDropSlot ? "Klik untuk load plugin terpilih" : pluginNames[i],
                   content,
                   juce::Justification::centredLeft,
                   true);
    }
}

void InsertChainPanel::mouseDown(const juce::MouseEvent& event)
{
    auto* track = mixer.getTrack(selectedTrack);
    if (track == nullptr)
        return;

    if (getInstrumentRowBounds().contains(event.getPosition()))
    {
        if (! track->hasInstrument())
        {
            if (loadSelectedPluginCallback)
                loadSelectedPluginCallback(selectedTrack);

            return;
        }

        if (event.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Buka editor instrument");
            menu.addItem(2, "Lepas instrument");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withStandardItemHeight(21),
                [this] (int result)
                {
                    auto* selected = mixer.getTrack(selectedTrack);
                    if (selected == nullptr)
                        return;

                    if (result == 1 && openSlotCallback)
                        openSlotCallback(selectedTrack, -1);
                    else if (result == 2)
                        selected->clearInstrument();

                    repaint();
                });

            return;
        }

        if (openSlotCallback)
            openSlotCallback(selectedTrack, -1);

        return;
    }

    const auto pluginCount = track->getPluginNames().size();

    for (int i = 0; i < getNumRows(); ++i)
    {
        if (! getRowBounds(i).contains(event.getPosition()))
            continue;

        if (i >= pluginCount)
        {
            if (loadSelectedPluginCallback)
                loadSelectedPluginCallback(selectedTrack);

            return;
        }

        if (event.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Buka editor plugin");
            menu.addItem(2, "Hapus semua insert");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withStandardItemHeight(21),
                [this, i] (int result)
                {
                    if (result == 1 && openSlotCallback)
                        openSlotCallback(selectedTrack, i);
                    else if (result == 2)
                    {
                        if (auto* selected = mixer.getTrack(selectedTrack))
                            selected->clearPlugins();

                        repaint();
                    }
                });

            return;
        }

        if (openSlotCallback)
            openSlotCallback(selectedTrack, i);

        return;
    }
}

void InsertChainPanel::setOpenSlotCallback(std::function<void(int, int)> callback)
{
    openSlotCallback = std::move(callback);
}

void InsertChainPanel::setSelectedTrack(int trackIndex)
{
    selectedTrack = juce::jlimit(0, juce::jmax(0, mixer.getNumTracks() - 1), trackIndex);
    repaint();
}

int InsertChainPanel::getSelectedTrack() const noexcept
{
    return selectedTrack;
}

void InsertChainPanel::setLoadSelectedPluginCallback(std::function<void(int)> callback)
{
    loadSelectedPluginCallback = std::move(callback);
}

void InsertChainPanel::setOpenEditorCallback(std::function<void(int)> callback)
{
    openEditorCallback = std::move(callback);
}

void InsertChainPanel::refresh()
{
    repaint();
}

juce::Rectangle<int> InsertChainPanel::getInstrumentRowBounds() const
{
    return { contentPadding, headerHeight + captionHeight, getWidth() - contentPadding * 2, rowHeight };
}

juce::Rectangle<int> InsertChainPanel::getRowBounds(int index) const
{
    // Sits below the instrument slot and its own caption.
    const auto top = headerHeight + captionHeight + rowHeight + rowGap * 2 + captionHeight
                   + index * (rowHeight + rowGap);

    return juce::Rectangle<int>(contentPadding, top, getWidth() - contentPadding * 2, rowHeight);
}

int InsertChainPanel::getNumRows() const
{
    const auto* track = mixer.getTrack(selectedTrack);
    return (track != nullptr ? track->getPluginNames().size() : 0) + 1;
}

} // namespace djr
