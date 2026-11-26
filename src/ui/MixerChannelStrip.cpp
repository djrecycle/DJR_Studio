#include "MixerChannelStrip.h"
#include "ChannelEqEditor.h"
#include "PanelWindow.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int stripPadding = 5;
    constexpr int nameRowHeight = 13;
    constexpr int panRowHeight = 19;
    constexpr int faderRowHeight = 40;
    constexpr int buttonRowHeight = 15;
    constexpr int faderColumnWidth = 32;
    constexpr int faderHandleHeight = 9;
    constexpr int rowGap = 4;
    constexpr int dbRowHeight = 13;
    constexpr int routingRowHeight = 13;
    constexpr float maxLevel = 1.5f;
    /** One line per send, name and level: tall enough for 8.5 pt text. */
    constexpr int sendSlotHeight = 14;
    constexpr int sendSlotGap = 2;
    /** Pixels of drag for silence to full; the strip is only ~60 px wide, so a
        drag is relative and may leave it, rather than an absolute position.
    */
    constexpr int sendDragRange = 150;
    constexpr float minWheelDb = -60.0f;

    juce::String describeSendLevel(float level)
    {
        return level <= 0.0001f ? juce::String("-inf")
                                : juce::String(juce::Decibels::gainToDecibels(level), 1);
    }
}

float MixerChannelStrip::sendFillRatio(float level) noexcept
{
    return juce::jlimit(0.0f, 1.0f, level / maxSendLevel);
}

float MixerChannelStrip::sendLevelAfterDrag(float startLevel, int deltaPixels, bool fine) noexcept
{
    const auto range = static_cast<float>(fine ? sendDragRange * 4 : sendDragRange);
    return juce::jlimit(0.0f, maxSendLevel, startLevel + maxSendLevel * static_cast<float>(deltaPixels) / range);
}

float MixerChannelStrip::sendLevelAfterWheel(float level, float wheelDelta) noexcept
{
    if (wheelDelta == 0.0f)
        return level;

    // In dB, so a notch sounds the same at -30 as at -3; from silence the first
    // notch up lands at -60 rather than staying at -inf for ever.
    const auto db = level <= 0.0001f ? minWheelDb : juce::Decibels::gainToDecibels(level);
    const auto next = db + (wheelDelta > 0.0f ? 0.5f : -0.5f);

    if (next <= minWheelDb)
        return 0.0f;

    return juce::jlimit(0.0f, maxSendLevel, juce::Decibels::decibelsToGain(next));
}

std::vector<int> MixerChannelStrip::insertRowsFor(int pluginCount)
{
    std::vector<int> rows;
    pluginCount = juce::jmax(0, pluginCount);

    // A long chain folds rather than squeezing the fader out of the strip.
    const auto shown = pluginCount > maxVisibleInserts ? maxVisibleInserts - 1 : pluginCount;

    for (int i = 0; i < shown; ++i)
        rows.push_back(i);

    if (shown < pluginCount)
        rows.push_back(moreInsertsRow);

    rows.push_back(addInsertRow);
    return rows;
}

MixerChannelStrip::MixerChannelStrip(Track& trackToUse, int colourIndex)
    : track(&trackToUse), colour(Theme::trackColour(colourIndex))
{
    startTimerHz(24);
}

MixerChannelStrip::MixerChannelStrip(MasterBus& masterBusToUse)
    : masterBus(&masterBusToUse), colour(Theme::text())
{
    startTimerHz(24);
}

MixerChannelStrip::~MixerChannelStrip() = default;

bool MixerChannelStrip::isMaster() const noexcept
{
    return masterBus != nullptr;
}

void MixerChannelStrip::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    const auto border = selected ? colour
                                 : (isMaster() ? Theme::outlineStrong() : Theme::outline());

    Theme::drawCard(g, bounds,
                    isMaster() ? Theme::accent().withAlpha(0.08f)
                               : (selected ? colour.withAlpha(0.10f) : Theme::panelHeader()),
                    border,
                    5.0f);

    auto area = bounds.reduced(stripPadding);

    // Name -------------------------------------------------------------------
    auto nameRow = area.removeFromTop(nameRowHeight);
    auto chip = nameRow.removeFromLeft(3).withSizeKeepingCentre(3, 9);
    g.setColour(colour);
    g.fillRoundedRectangle(chip.toFloat(), 1.5f);
    nameRow.removeFromLeft(4);
    g.setColour(Theme::text());
    g.setFont(Theme::ui(11.0f, true));
    g.drawText(getDisplayName(), nameRow, juce::Justification::centredLeft, true);

    // Pan --------------------------------------------------------------------
    const auto knob = getPanKnobArea();
    g.setColour(Theme::inset());
    g.fillEllipse(knob.toFloat());
    g.setColour(Theme::outlineStrong());
    g.drawEllipse(knob.toFloat().reduced(0.5f), 1.0f);

    const auto panValue = getPan();
    const auto angle = panValue * juce::degreesToRadians(135.0f);
    juce::Path pointer;
    pointer.addRoundedRectangle(knob.getCentreX() - 1.0f, knob.getY() + 2.0f, 2.0f, 6.0f, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle, knob.getCentreX(), knob.getCentreY()));
    g.setColour(isMaster() ? Theme::outlineStrong() : colour);
    g.fillPath(pointer);

    auto panText = juce::Rectangle<int>(knob.getRight() + 5, knob.getY(), area.getRight() - knob.getRight() - 5, knob.getHeight());
    Theme::drawCaption(g, panText.removeFromTop(9), "Pan");
    g.setColour(Theme::textSoft());
    g.setFont(Theme::mono(9.5f));
    g.drawText(isMaster() ? juce::String("--")
                          : (std::abs(panValue) < 0.01f
                                 ? juce::String("C")
                                 : (panValue < 0.0f ? "L" : "R") + juce::String(juce::roundToInt(std::abs(panValue) * 100.0f))),
               panText, juce::Justification::topLeft, false);

    // Fader + meters ---------------------------------------------------------
    const auto faderColumn = getFaderColumn();
    g.setColour(Theme::meterTrack());
    g.fillRoundedRectangle(juce::Rectangle<int>(faderColumn.getX() + 14, faderColumn.getY(), 3, faderColumn.getHeight()).toFloat(), 1.5f);

    const auto handle = getFaderHandle();
    g.setGradientFill(juce::ColourGradient(juce::Colour::fromString("ff3c4759"), 0.0f, static_cast<float>(handle.getY()),
                                           juce::Colour::fromString("ff242c3d"), 0.0f, static_cast<float>(handle.getBottom()), false));
    g.fillRoundedRectangle(handle.toFloat(), 2.0f);
    g.setColour(juce::Colour::fromString("ff4a566d"));
    g.drawRoundedRectangle(handle.toFloat().reduced(0.5f), 2.0f, 1.0f);
    g.setColour(colour);
    g.fillRect(handle.getX() + 2, handle.getCentreY() - 1, handle.getWidth() - 4, 2);

    // A fader the automation is holding says so, otherwise it just looks like a
    // control that refuses to move.
    if (track != nullptr && track->isVolumeAutomated())
    {
        g.setColour(Theme::amber());
        g.setFont(Theme::mono(8.0f, true));
        g.drawText("A", handle.getX() - 9, handle.getY(), 8, handle.getHeight(),
                   juce::Justification::centred, false);
    }

    auto meters = getMeterArea();
    const auto meterWidth = (meters.getWidth() - 2) / 2;
    Theme::drawLevelMeter(g, meters.removeFromLeft(meterWidth).toFloat(), smoothedLeft, true);
    meters.removeFromLeft(2);
    Theme::drawLevelMeter(g, meters.removeFromLeft(meterWidth).toFloat(), smoothedRight, true);

    // Buttons ----------------------------------------------------------------
    const auto drawButton = [&g] (juce::Rectangle<int> area2, const juce::String& label, bool on, juce::Colour onColour, bool dot)
    {
        g.setColour(on ? onColour : Theme::control());
        g.fillRoundedRectangle(area2.toFloat(), 3.0f);

        if (dot)
        {
            g.setColour(on ? Theme::windowBackground() : Theme::faintText());
            g.fillEllipse(area2.toFloat().withSizeKeepingCentre(5.0f, 5.0f));
            return;
        }

        g.setColour(on ? Theme::windowBackground() : Theme::mutedText());
        g.setFont(Theme::ui(10.5f, true));
        g.drawText(label, area2, juce::Justification::centred, false);
    };

    if (! isMaster() && track != nullptr)
    {
        drawButton(getMuteBounds(), "M", track->isMuted(), Theme::pink(), false);
        drawButton(getSoloBounds(), "S", track->isSoloed(), Theme::amber(), false);
        drawButton(getArmBounds(), "R", track->isRecordArmed(), Theme::pink(), true);
    }

    // Channel EQ -------------------------------------------------------------
    // One row: an on/off dot, "EQ" and the response in miniature, so a
    // channel that is being EQ'd says so at a glance.
    if (const auto eqRow = getEqRowBounds(); ! eqRow.isEmpty())
    {
        const auto settings = track->getChannelEq().getSettings();
        const auto hovered = eqRow.contains(hoverPosition);

        g.setColour(Theme::inset().brighter(hovered ? 0.15f : 0.0f));
        g.fillRoundedRectangle(eqRow.toFloat(), 2.5f);

        const auto dot = getBypassDotArea(eqRow).toFloat().withSizeKeepingCentre(5.0f, 5.0f);
        g.setColour(settings.enabled ? colour : Theme::faintText());

        if (settings.enabled)
            g.fillEllipse(dot);
        else
            g.drawEllipse(dot, 1.0f);

        auto content = eqRow.withTrimmedLeft(12);
        g.setFont(Theme::mono(8.0f, settings.enabled));
        g.setColour(settings.enabled ? Theme::textSoft() : Theme::faintText());
        g.drawText("EQ", content.removeFromLeft(14), juce::Justification::centredLeft, false);

        ChannelEqEditor::drawCurve(g, content.reduced(2, 2).toFloat(), settings,
                                   track->getPreparedSampleRate(), colour, false);
    }

    // Inserts ----------------------------------------------------------------
    // Above the fader, in the order the signal meets them: plugin, then fader,
    // then sends. A dot per insert says on (track colour) or bypassed.
    if (track != nullptr && ! isMaster())
    {
        const auto rows = getInsertRows();
        const auto names = track->getPluginNames();

        for (size_t row = 0; row < rows.size(); ++row)
        {
            const auto kind = rows[row];
            const auto area = getInsertRowBounds(static_cast<int>(row));
            const auto hovered = area.contains(hoverPosition);

            if (kind == addInsertRow || kind == moreInsertsRow)
            {
                g.setColour(Theme::control().withAlpha(hovered ? 0.75f : 0.45f));
                g.fillRoundedRectangle(area.toFloat(), 2.5f);
                g.setColour(Theme::outline().withAlpha(0.8f));
                g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.5f, 1.0f);
                g.setColour(hovered ? Theme::text() : Theme::mutedText());
                g.setFont(Theme::mono(8.5f));
                g.drawText(kind == addInsertRow ? juce::String("+ FX")
                                                : "... " + juce::String(track->getPluginCount() - (maxVisibleInserts - 1)) + TRANS(" more"),
                           area, juce::Justification::centred, false);
                continue;
            }

            const auto bypassed = track->isPluginBypassed(kind);

            g.setColour(Theme::inset().brighter(hovered ? 0.15f : 0.0f));
            g.fillRoundedRectangle(area.toFloat(), 2.5f);

            const auto dot = getBypassDotArea(area).toFloat().withSizeKeepingCentre(5.0f, 5.0f);
            g.setColour(bypassed ? Theme::faintText() : colour);

            if (bypassed)
                g.drawEllipse(dot, 1.0f);
            else
                g.fillEllipse(dot);

            g.setFont(Theme::mono(8.0f, ! bypassed));
            g.setColour(bypassed ? Theme::faintText() : Theme::textSoft());
            g.drawText(kind < names.size() ? names[kind] : juce::String("?"),
                       area.withTrimmedLeft(12).withTrimmedRight(2), juce::Justification::centredLeft, true);
        }
    }

    // Sends ------------------------------------------------------------------
    // Always there on a channel, so a send is something you see and reach for
    // rather than something buried in the routing menu: one line per send with
    // its bus and level, then "+ Send" while a slot is free.
    if (track != nullptr && mixer != nullptr)
    {
        const auto rows = getSendRows();

        for (size_t row = 0; row < rows.size(); ++row)
        {
            const auto slot = rows[row];
            const auto area = getSendRowBounds(static_cast<int>(row));
            const auto send = track->getSend(slot);
            const auto hovered = area.contains(hoverPosition);

            if (send.destination < 0)
            {
                g.setColour(Theme::control().withAlpha(hovered ? 0.75f : 0.45f));
                g.fillRoundedRectangle(area.toFloat(), 2.5f);
                g.setColour(Theme::outline().withAlpha(0.8f));
                g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 2.5f, 1.0f);
                g.setColour(hovered ? Theme::text() : Theme::mutedText());
                g.setFont(Theme::mono(8.5f));
                g.drawText("+ Send", area, juce::Justification::centred, false);
                continue;
            }

            // Pre-fader reads amber, post-fader takes the track colour: the one
            // thing you cannot afford to misread about a send is which it is.
            const auto sendColour = send.preFader ? Theme::amber() : colour;

            g.setColour(Theme::inset());
            g.fillRoundedRectangle(area.toFloat(), 2.5f);

            auto filled = area.reduced(1);
            filled.setWidth(juce::roundToInt(sendFillRatio(send.level) * static_cast<float>(filled.getWidth())));
            g.setColour(sendColour.withAlpha(draggingSend == slot || hovered ? 0.55f : 0.38f));
            g.fillRoundedRectangle(filled.toFloat(), 2.0f);

            auto text = area.reduced(3, 0);
            const auto levelText = describeSendLevel(send.level);
            g.setFont(Theme::mono(8.0f));
            g.setColour(Theme::text());
            const auto levelWidth = Theme::textWidth(Theme::mono(8.0f), levelText);
            g.drawText(levelText, text.removeFromRight(levelWidth), juce::Justification::centredRight, false);
            text.removeFromRight(2);

            juce::String name = "?";

            if (const auto* destination = mixer->getTrack(send.destination))
                name = destination->getName();

            g.setFont(Theme::mono(8.0f, true));
            g.setColour(send.preFader ? Theme::amber() : Theme::textSoft());
            g.drawText(name, text, juce::Justification::centredLeft, true);
        }
    }

    // Output Routing Badge ---------------------------------------------------
    if (! isMaster() && track != nullptr)
    {
        const auto routingBounds = getRoutingBounds();
        if (! routingBounds.isEmpty())
        {
            const auto dest = track->getOutputDestination();
            const auto isBusRoute = (dest != Track::masterDestination);
            const auto badgeColour = isBusRoute ? Theme::amber() : Theme::control();

            g.setColour(badgeColour.withAlpha(isBusRoute ? 0.35f : 0.22f));
            g.fillRoundedRectangle(routingBounds.toFloat(), 2.5f);

            g.setColour(isBusRoute ? Theme::amber() : Theme::mutedText());
            g.setFont(Theme::mono(8.5f, true));
            g.drawText("Out: " + getOutputDestinationName(), routingBounds, juce::Justification::centred, true);
        }
    }

    // Level readout ----------------------------------------------------------
    const auto level = getLevel();
    const auto db = level <= 0.0001f ? juce::String("-inf")
                                     : juce::String(juce::Decibels::gainToDecibels(level), 1);

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(9.5f));
    g.drawText(db, bounds.withTrimmedBottom(3).removeFromBottom(10), juce::Justification::centred, false);
}

void MixerChannelStrip::mouseMove(const juce::MouseEvent& event)
{
    const auto sendBefore = getSendRowAt(hoverPosition);
    const auto insertBefore = getInsertRowAt(hoverPosition);
    const auto eqBefore = getEqRowBounds().contains(hoverPosition);
    hoverPosition = event.getPosition();

    if (getSendRowAt(hoverPosition) != sendBefore || getInsertRowAt(hoverPosition) != insertBefore
        || getEqRowBounds().contains(hoverPosition) != eqBefore)
        repaint();
}

void MixerChannelStrip::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (getSendRowAt(hoverPosition) >= 0 || getInsertRowAt(hoverPosition) >= 0
        || getEqRowBounds().contains(hoverPosition))
        repaint();

    hoverPosition = { -1, -1 };
}

void MixerChannelStrip::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (const auto row = getSendRowAt(event.getPosition()); row >= 0 && track != nullptr)
    {
        const auto slot = getSendRows()[(size_t) row];
        auto send = track->getSend(slot);

        if (send.destination >= 0)
        {
            send.level = sendLevelAfterWheel(send.level, wheel.deltaY);
            applySend(slot, send);
            return;
        }
    }

    // Not on a send: let the mixer's viewport scroll the strips.
    juce::Component::mouseWheelMove(event, wheel);
}

juce::String MixerChannelStrip::getTooltip()
{
    if (getEqRowBounds().contains(hoverPosition))
        return TRANS("Channel EQ - click: edit - dot: on/off - right-click: reset");

    if (const auto row = getInsertRowAt(hoverPosition); row >= 0 && track != nullptr)
    {
        const auto kind = getInsertRows()[(size_t) row];

        if (kind == addInsertRow)
            return TRANS("Add an effect plugin to this channel");

        if (kind == moreInsertsRow)
            return TRANS("Show the whole insert chain");

        return TRANS("Click: open - dot: on/off (bypass) - right-click: move, remove");
    }

    if (const auto row = getSendRowAt(hoverPosition); row >= 0 && track != nullptr)
    {
        if (track->getSend(getSendRows()[(size_t) row]).destination < 0)
            return TRANS("Send this channel to a bus as well (reverb, delay, parallel compression)");

        return TRANS("Drag or scroll: send level (Shift = fine) - double-click: 0 dB - right-click: bus, pre/post fader, remove");
    }

    return {};
}

void MixerChannelStrip::setMixer(Mixer* mixerToRouteThrough, int indexInMixer) noexcept
{
    mixer = mixerToRouteThrough;
    trackIndex = indexInMixer;
}

void MixerChannelStrip::setDeviceInputCount(int count) noexcept
{
    deviceInputCount = juce::jmax(0, count);
}

std::vector<int> MixerChannelStrip::getAssignedSends() const
{
    std::vector<int> assigned;

    if (track == nullptr)
        return assigned;

    for (int slot = 0; slot < Track::maxSends; ++slot)
        if (track->getSend(slot).destination >= 0)
            assigned.push_back(slot);

    return assigned;
}

juce::Rectangle<int> MixerChannelStrip::getEqRowBounds() const
{
    if (track == nullptr || isMaster())
        return {};

    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap);
    return area.withHeight(sendSlotHeight);
}

void MixerChannelStrip::setEqEnabled(bool shouldBeEnabled)
{
    if (track == nullptr)
        return;

    auto settings = track->getChannelEq().getSettings();
    settings.enabled = shouldBeEnabled;
    track->getChannelEq().setSettings(settings);

    if (onEqChanged)
        onEqChanged();

    repaint();
}

void MixerChannelStrip::showEqEditor()
{
    if (track == nullptr)
        return;

    auto editor = std::make_unique<ChannelEqEditor>(track->getChannelEq(), track->getName(), colour,
                                                    track->getPreparedSampleRate());

    // The strip may be rebuilt while the call-out is open (a track added or
    // removed elsewhere), so the editor reaches back through a SafePointer.
    editor->onChange = [safeThis = juce::Component::SafePointer<MixerChannelStrip>(this)]
    {
        if (safeThis == nullptr)
            return;

        if (safeThis->onEqChanged)
            safeThis->onEqChanged();

        safeThis->repaint();
    };

    auto* top = getTopLevelComponent();
    const auto area = top != nullptr ? top->getLocalArea(this, getEqRowBounds()) : getEqRowBounds();
    juce::CallOutBox::launchAsynchronously(std::move(editor), area, top);
}

void MixerChannelStrip::showEqMenu()
{
    if (track == nullptr)
        return;

    const auto settings = track->getChannelEq().getSettings();

    juce::PopupMenu menu;
    menu.addSectionHeader("EQ - " + track->getName());
    menu.addItem(1, TRANS("Edit EQ..."));
    menu.addItem(2, TRANS("EQ on"), true, settings.enabled);
    menu.addItem(3, TRANS("Reset to flat"), settings.lowDb != 0.0f || settings.midDb != 0.0f || settings.highDb != 0.0f
                                                || settings.midHz != ChannelEq::defaultMidHz);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(160)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (track == nullptr || result == 0)
                return;

            if (result == 1)
            {
                showEqEditor();
                return;
            }

            if (result == 2)
            {
                setEqEnabled(! track->getChannelEq().getSettings().enabled);
                return;
            }

            if (result == 3)
            {
                ChannelEq::Settings flat;
                flat.enabled = track->getChannelEq().getSettings().enabled;
                track->getChannelEq().setSettings(flat);

                if (onEqChanged)
                    onEqChanged();

                repaint();
            }
        });
}

std::vector<int> MixerChannelStrip::getInsertRows() const
{
    if (track == nullptr || isMaster())
        return {};

    return insertRowsFor(track->getPluginCount());
}

int MixerChannelStrip::getInsertSectionHeight() const
{
    const auto count = static_cast<int>(getInsertRows().size());
    return count == 0 ? 0 : count * sendSlotHeight + (count - 1) * sendSlotGap;
}

int MixerChannelStrip::getHeightAboveFader() const
{
    auto height = nameRowHeight + rowGap + panRowHeight + rowGap;

    if (! getEqRowBounds().isEmpty())
        height += sendSlotHeight + rowGap;

    if (const auto inserts = getInsertSectionHeight(); inserts > 0)
        height += inserts + rowGap;

    return height;
}

juce::Rectangle<int> MixerChannelStrip::getInsertRowBounds(int rowIndex) const
{
    if (! juce::isPositiveAndBelow(rowIndex, static_cast<int>(getInsertRows().size())))
        return {};

    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap);

    if (! getEqRowBounds().isEmpty())
        area.removeFromTop(sendSlotHeight + rowGap);

    return area.withHeight(sendSlotHeight).translated(0, rowIndex * (sendSlotHeight + sendSlotGap));
}

int MixerChannelStrip::getInsertRowAt(juce::Point<int> position) const
{
    const auto count = static_cast<int>(getInsertRows().size());

    for (int row = 0; row < count; ++row)
        if (getInsertRowBounds(row).contains(position))
            return row;

    return -1;
}

juce::Rectangle<int> MixerChannelStrip::getBypassDotArea(juce::Rectangle<int> row)
{
    return row.withWidth(12);
}

void MixerChannelStrip::notifyInsertsChanged()
{
    if (onInsertsChanged)
        onInsertsChanged();

    repaint();
}

void MixerChannelStrip::showInsertMenu(int insertIndex)
{
    if (track == nullptr || ! juce::isPositiveAndBelow(insertIndex, track->getPluginCount()))
        return;

    const auto count = track->getPluginCount();
    const auto names = track->getPluginNames();

    juce::PopupMenu menu;
    menu.addSectionHeader(insertIndex < names.size() ? names[insertIndex] : juce::String("Insert"));
    menu.addItem(1, TRANS("Open plugin editor"));
    menu.addItem(2, TRANS("Bypass"), true, track->isPluginBypassed(insertIndex));
    menu.addSeparator();
    menu.addItem(3, TRANS("Move up"), insertIndex > 0);
    menu.addItem(4, TRANS("Move down"), insertIndex < count - 1);
    menu.addSeparator();
    menu.addItem(5, TRANS("Remove"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(170)
                           .withStandardItemHeight(21),
        [this, insertIndex] (int result)
        {
            if (result == 0 || track == nullptr || ! juce::isPositiveAndBelow(insertIndex, track->getPluginCount()))
                return;

            if (result == 1)
            {
                if (onOpenInsert)
                    onOpenInsert(insertIndex);

                return;
            }

            if (result == 2)
                track->setPluginBypassed(insertIndex, ! track->isPluginBypassed(insertIndex));
            else if (result == 3)
                track->movePlugin(insertIndex, insertIndex - 1);
            else if (result == 4)
                track->movePlugin(insertIndex, insertIndex + 1);
            else if (result == 5)
                track->removePlugin(insertIndex);

            notifyInsertsChanged();
        });
}

std::vector<int> MixerChannelStrip::getSendRows() const
{
    auto rows = getAssignedSends();

    if (track == nullptr || mixer == nullptr || isMaster())
        return {};

    for (int slot = 0; slot < Track::maxSends; ++slot)
    {
        if (track->getSend(slot).destination < 0)
        {
            rows.push_back(slot);
            break;
        }
    }

    return rows;
}

int MixerChannelStrip::getSendSectionHeight() const
{
    const auto count = static_cast<int>(getSendRows().size());
    return count == 0 ? 0 : count * sendSlotHeight + (count - 1) * sendSlotGap;
}

juce::Rectangle<int> MixerChannelStrip::getSendRowBounds(int rowIndex) const
{
    if (! juce::isPositiveAndBelow(rowIndex, static_cast<int>(getSendRows().size())))
        return {};

    return getRowsBelowFader().withHeight(sendSlotHeight)
                              .translated(0, rowIndex * (sendSlotHeight + sendSlotGap));
}

int MixerChannelStrip::getSendRowAt(juce::Point<int> position) const
{
    const auto count = static_cast<int>(getSendRows().size());

    for (int row = 0; row < count; ++row)
        if (getSendRowBounds(row).contains(position))
            return row;

    return -1;
}

void MixerChannelStrip::applySend(int slot, const TrackSend& send)
{
    if (mixer == nullptr)
        return;

    mixer->setTrackSend(trackIndex, slot, send);

    if (onRoutingChanged)
        onRoutingChanged();

    repaint();
}

void MixerChannelStrip::showSendMenu(int slot)
{
    if (track == nullptr || mixer == nullptr)
        return;

    const auto send = track->getSend(slot);

    juce::PopupMenu destinations;

    for (int i = 0; i < mixer->getNumTracks(); ++i)
        if (const auto* candidate = mixer->getTrack(i); candidate != nullptr && candidate->getKind() == TrackKind::bus)
            destinations.addItem(100 + i, candidate->getName(), mixer->canRoute(trackIndex, i), send.destination == i);

    juce::PopupMenu menu;
    menu.addSectionHeader("Send " + juce::String(slot + 1) + " - " + describeSendLevel(send.level) + " dB");
    menu.addSubMenu(TRANS("Send to"), destinations);
    menu.addItem(1, TRANS("Pre-fader (ignores the fader)"), true, send.preFader);
    menu.addItem(2, TRANS("Reset to 0 dB"), send.level != 1.0f);
    menu.addSeparator();
    menu.addItem(3, TRANS("Remove send"));

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this, slot] (int result)
        {
            if (result == 0 || track == nullptr)
                return;

            auto updated = track->getSend(slot);

            if (result == 1)
                updated.preFader = ! updated.preFader;
            else if (result == 2)
                updated.level = 1.0f;
            else if (result == 3)
                updated = {};
            else if (result >= 100)
                updated.destination = result - 100;

            applySend(slot, updated);
        });
}

void MixerChannelStrip::showAddSendMenu(int slot)
{
    if (track == nullptr || mixer == nullptr)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("Send to"));
    auto anyBus = false;

    for (int i = 0; i < mixer->getNumTracks(); ++i)
    {
        if (const auto* candidate = mixer->getTrack(i); candidate != nullptr && candidate->getKind() == TrackKind::bus)
        {
            anyBus = true;
            menu.addItem(100 + i, candidate->getName(), mixer->canRoute(trackIndex, i));
        }
    }

    if (! anyBus)
        menu.addItem(99, TRANS("No buses yet"), false);

    menu.addSeparator();
    menu.addItem(1, TRANS("New bus"), onCreateBusForSend != nullptr);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(170)
                           .withStandardItemHeight(21),
        [this, slot] (int result)
        {
            if (result == 0 || track == nullptr)
                return;

            if (result == 1)
            {
                // The owner rebuilds every strip after adding the bus - this one
                // included - so nothing here may touch `this` afterwards.
                if (auto callback = onCreateBusForSend)
                    callback(slot);

                return;
            }

            // A new send starts at unity so it is heard straight away.
            TrackSend send;
            send.destination = result - 100;
            send.level = 1.0f;
            applySend(slot, send);
        });
}

void MixerChannelStrip::showRoutingMenu()
{
    if (track == nullptr || mixer == nullptr)
        return;

    // Built from the mixer, so a destination that would feed back is greyed out
    // rather than offered and then refused.
    const auto addDestinations = [this] (juce::PopupMenu& menu, int baseId, int current, bool includeMaster)
    {
        if (includeMaster)
            menu.addItem(baseId, "Master", true, current == Track::masterDestination);
        else
            menu.addItem(baseId, TRANS("Off"), true, current < 0);

        for (int i = 0; i < mixer->getNumTracks(); ++i)
        {
            const auto* candidate = mixer->getTrack(i);

            if (candidate == nullptr || candidate->getKind() != TrackKind::bus)
                continue;

            menu.addItem(baseId + 1 + i,
                         candidate->getName(),
                         mixer->canRoute(trackIndex, i),
                         current == i);
        }
    };

    juce::PopupMenu outputMenu;
    addDestinations(outputMenu, 1000, track->getOutputDestination(), true);

    juce::PopupMenu inputMenu;
    const auto currentInput = track->getInputChannel();
    const auto currentStereo = track->isInputStereo();

    inputMenu.addItem(3000, TRANS("None"), true, currentInput < 0);

    for (int channel = 0; channel < deviceInputCount; ++channel)
        inputMenu.addItem(3001 + channel,
                          "In " + juce::String(channel + 1),
                          true,
                          currentInput == channel && ! currentStereo);

    // Stereo takes a pair, so the last channel on its own cannot start one.
    for (int channel = 0; channel + 1 < deviceInputCount; ++channel)
        inputMenu.addItem(3100 + channel,
                          "In " + juce::String(channel + 1) + " + " + juce::String(channel + 2),
                          true,
                          currentInput == channel && currentStereo);

    if (deviceInputCount <= 0)
        inputMenu.addItem(3999, TRANS("No audio inputs on this device"), false, false);

    juce::PopupMenu menu;
    menu.addSectionHeader(track->getName());
    menu.addSubMenu(TRANS("Input"), inputMenu);
    menu.addSubMenu("Output", outputMenu);
    menu.addSeparator();

    for (int slot = 0; slot < Track::maxSends; ++slot)
    {
        const auto send = track->getSend(slot);

        juce::PopupMenu sendMenu;
        addDestinations(sendMenu, 2000 + slot * 100, send.destination, false);
        sendMenu.addSeparator();
        sendMenu.addItem(2000 + slot * 100 + 99, "Pre-fader", send.destination >= 0, send.preFader);

        auto label = "Send " + juce::String(slot + 1);

        if (send.destination >= 0)
            if (const auto* destination = mixer->getTrack(send.destination))
                label += ": " + destination->getName() + (send.preFader ? " (pre)" : "");

        menu.addSubMenu(label, sendMenu);
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (result == 0 || track == nullptr || mixer == nullptr)
                return;

            if (result >= 3000)
            {
                if (result == 3000)
                {
                    track->setInputChannel(Track::noInput);
                }
                else if (result >= 3100 && result < 3999)
                {
                    track->setInputChannel(result - 3100);
                    track->setInputStereo(true);
                }
                else if (result < 3100)
                {
                    track->setInputChannel(result - 3001);
                    track->setInputStereo(false);
                }
            }
            else if (result >= 1000 && result < 2000)
            {
                mixer->setTrackOutput(trackIndex, result - 1000 - 1);
            }
            else if (result >= 2000)
            {
                const auto slot = (result - 2000) / 100;
                const auto choice = (result - 2000) % 100;
                auto send = track->getSend(slot);

                if (choice == 99)
                {
                    send.preFader = ! send.preFader;
                }
                else
                {
                    send.destination = choice - 1;

                    // A send that has just been pointed somewhere starts at
                    // unity rather than at zero, so it is audible immediately.
                    if (send.destination >= 0 && send.level <= 0.0f)
                        send.level = 1.0f;
                }

                mixer->setTrackSend(trackIndex, slot, send);
            }

            if (onRoutingChanged)
                onRoutingChanged();

            repaint();
        });
}

void MixerChannelStrip::showAutomationMenu(bool forPan)
{
    if (track == nullptr)
        return;

    AutomationTarget target;
    target.kind = forPan ? AutomationTarget::Kind::trackPan : AutomationTarget::Kind::trackVolume;
    target.label = forPan ? "Pan" : "Volume";

    const auto existing = track->findAutomationLane(target);
    const auto current = forPan ? track->getPan() : track->getVolume();

    juce::PopupMenu menu;
    menu.addSectionHeader(track->getName() + " - " + target.label);
    menu.addItem(1, TRANS("Create automation clip"), existing < 0);
    menu.addItem(2, TRANS("Remove automation"), existing >= 0);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withMousePosition()
                           .withMinimumWidth(180)
                           .withStandardItemHeight(21),
        [this, target, current, existing] (int result)
        {
            if (track == nullptr || result == 0)
                return;

            if (result == 1)
            {
                if (auto* lane = track->addAutomationLane(target))
                {
                    // Seeded at the value the control has now, so creating a
                    // clip never changes the sound by itself.
                    if (lane->isEmpty())
                        lane->addPoint(0.0, target.fromParameterValue(current));
                }
            }
            else if (result == 2)
            {
                track->removeAutomationLane(existing);
            }

            if (onAutomationChanged)
                onAutomationChanged();

            repaint();
        });
}

void MixerChannelStrip::setSelected(bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void MixerChannelStrip::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    // Nothing used to clear these, so panning once left every later drag on the
    // strip moving the pan instead of whatever was actually grabbed.
    draggingFader = false;
    draggingPan = false;
    draggingSend = -1;
}

void MixerChannelStrip::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    // A fresh press owns the strip: whatever the last gesture grabbed, it is
    // finished with now.
    draggingFader = false;
    draggingPan = false;
    draggingSend = -1;

    // Touching a strip anywhere makes it the session's current track.
    if (! isMaster() && onSelected)
        onSelected();

    if (! isMaster() && track != nullptr)
    {
        // Right clicking a control is where FL puts "create automation clip",
        // and it is the shortest route from hearing a level to automating it.
        if (event.mods.isRightButtonDown())
        {
            if (getPanKnobArea().expanded(3).contains(position))
            {
                showAutomationMenu(true);
                return;
            }

            if (getFaderColumn().expanded(4, 6).contains(position))
            {
                showAutomationMenu(false);
                return;
            }

            if (getEqRowBounds().contains(position))
            {
                showEqMenu();
                return;
            }

            if (const auto row = getInsertRowAt(position); row >= 0)
            {
                const auto kind = getInsertRows()[(size_t) row];

                if (kind >= 0)
                    showInsertMenu(kind);
                else if (kind == addInsertRow && onAddInsert)
                    onAddInsert();
                else if (kind == moreInsertsRow && onShowInsertChain)
                    onShowInsertChain();

                return;
            }

            if (const auto row = getSendRowAt(position); row >= 0)
            {
                const auto slot = getSendRows()[(size_t) row];

                if (track->getSend(slot).destination >= 0)
                    showSendMenu(slot);
                else
                    showAddSendMenu(slot);

                return;
            }

            // Anywhere else on the strip is where the audio goes.
            showRoutingMenu();
            return;
        }

        // Clicking the routing badge opens the routing menu immediately.
        if (getRoutingBounds().expanded(2, 2).contains(position))
        {
            showRoutingMenu();
            return;
        }

        // The EQ row: the dot switches it on and off, anywhere else opens it.
        if (const auto eqRow = getEqRowBounds(); eqRow.contains(position))
        {
            if (getBypassDotArea(eqRow).contains(position))
                setEqEnabled(! track->getChannelEq().getSettings().enabled);
            else
                showEqEditor();

            return;
        }

        // Inserts: the dot switches one on and off, the name opens it.
        if (const auto row = getInsertRowAt(position); row >= 0)
        {
            const auto kind = getInsertRows()[(size_t) row];

            if (kind == addInsertRow)
            {
                if (onAddInsert)
                    onAddInsert();
            }
            else if (kind == moreInsertsRow)
            {
                if (onShowInsertChain)
                    onShowInsertChain();
            }
            else if (getBypassDotArea(getInsertRowBounds(row)).contains(position))
            {
                track->setPluginBypassed(kind, ! track->isPluginBypassed(kind));
                notifyInsertsChanged();
            }
            else if (onOpenInsert)
            {
                onOpenInsert(kind);
            }

            return;
        }

        // A send is dragged like a small fader of its own - relative, since
        // the row is far narrower than the travel it needs. "+ Send" opens the
        // list of buses instead.
        if (const auto row = getSendRowAt(position); row >= 0)
        {
            const auto slot = getSendRows()[(size_t) row];

            if (track->getSend(slot).destination < 0)
            {
                showAddSendMenu(slot);
                return;
            }

            draggingSend = slot;
            sendDragStartLevel = track->getSend(slot).level;
            repaint();
            return;
        }

        if (getMuteBounds().contains(position))
        {
            track->setMuted(! track->isMuted());
            repaint();
            return;
        }

        if (getSoloBounds().contains(position))
        {
            track->setSoloed(! track->isSoloed());
            repaint();
            return;
        }

        if (getArmBounds().contains(position))
        {
            track->setRecordArmed(! track->isRecordArmed());
            repaint();
            return;
        }

        if (getPanKnobArea().expanded(3).contains(position))
        {
            draggingPan = true;
            panDragStart = track->getPan();
            return;
        }
    }

    if (getFaderColumn().expanded(4, 6).contains(position))
    {
        draggingFader = true;
        const auto ratio = 1.0f - static_cast<float>(position.y - getFaderColumn().getY())
                                      / static_cast<float>(juce::jmax(1, getFaderColumn().getHeight()));
        setLevel(juce::jlimit(0.0f, 1.0f, ratio) * maxLevel);
        repaint();
    }
}

void MixerChannelStrip::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingSend >= 0 && track != nullptr && mixer != nullptr)
    {
        // Right or up raises it, like every other control on the strip.
        const auto delta = event.getDistanceFromDragStartX() - event.getDistanceFromDragStartY();
        auto send = track->getSend(draggingSend);
        send.level = sendLevelAfterDrag(sendDragStartLevel, delta, event.mods.isShiftDown());
        mixer->setTrackSend(trackIndex, draggingSend, send);
        repaint();
        return;
    }

    if (draggingPan && track != nullptr)
    {
        track->setPan(juce::jlimit(-1.0f, 1.0f, panDragStart - event.getDistanceFromDragStartY() * 0.01f));
        repaint();
        return;
    }

    if (! draggingFader)
        return;

    const auto column = getFaderColumn();
    const auto ratio = 1.0f - static_cast<float>(event.getPosition().y - column.getY())
                                  / static_cast<float>(juce::jmax(1, column.getHeight()));
    setLevel(juce::jlimit(0.0f, 1.0f, ratio) * maxLevel);
    repaint();
}

void MixerChannelStrip::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (const auto row = getSendRowAt(event.getPosition()); row >= 0 && track != nullptr)
    {
        const auto slot = getSendRows()[(size_t) row];
        auto send = track->getSend(slot);

        if (send.destination >= 0)
        {
            send.level = 1.0f;
            applySend(slot, send);
        }

        return;
    }

    // The name is the channel, so double clicking it opens the channel window -
    // whatever the channel holds, including nothing but the preview synth.
    if (getNameRowBounds().contains(event.getPosition()) && track != nullptr)
    {
        if (onOpenChannel)
            onOpenChannel();

        return;
    }

    if (getPanKnobArea().expanded(3).contains(event.getPosition()) && track != nullptr)
    {
        track->setPan(0.0f);
        repaint();
        return;
    }

    if (getFaderColumn().expanded(4, 6).contains(event.getPosition()))
    {
        setLevel(isMaster() ? 0.85f : 0.8f);
        repaint();
    }
}

void MixerChannelStrip::timerCallback()
{
    // Smoothed where it is drawn, not where it is measured: the fall has to
    // look even on the meter, and a straight line in amplitude is a cliff in dB.
    const auto decay = 0.18f;
    const auto left = Theme::meterPosition(track != nullptr ? track->getPeakLevel(0)
                                                            : masterBus->getPeakLevel(0));
    const auto right = Theme::meterPosition(track != nullptr ? track->getPeakLevel(1)
                                                             : masterBus->getPeakLevel(1));

    const auto newLeft = juce::jmax(left, smoothedLeft - decay);
    const auto newRight = juce::jmax(right, smoothedRight - decay);

    // Under a maximised playlist the strip is not seen at all, yet every
    // repaint here would still make JUCE redraw the playlist over it.
    if (isCoveredByPanelAbove(*this))
    {
        smoothedLeft = newLeft;
        smoothedRight = newRight;
        return;
    }

    // Fader, pan and buttons can be moved from elsewhere - automation during
    // playback, M/S in the playlist - so they are checked too, but only the
    // meters animate: a silent strip asks for no repaint at all.
    auto stateKey = getDisplayName() + "|" + juce::String(getLevel(), 4) + "|" + juce::String(getPan(), 3);

    if (track != nullptr)
        stateKey << "|" << (int) track->isMuted() << (int) track->isSoloed() << (int) track->isRecordArmed()
                 << (int) track->isVolumeAutomated() << "|" << getOutputDestinationName();

    if (track != nullptr)
    {
        stateKey << "|" << track->getPluginNames().joinIntoString(",");

        const auto eq = track->getChannelEq().getSettings();
        stateKey << "|eq" << (int) eq.enabled << ":" << eq.lowDb << ":" << eq.midDb << ":" << eq.midHz << ":" << eq.highDb;

        for (int i = 0; i < track->getPluginCount(); ++i)
            stateKey << (int) track->isPluginBypassed(i);
    }

    if (track != nullptr)
        for (int slot = 0; slot < Track::maxSends; ++slot)
        {
            const auto send = track->getSend(slot);
            stateKey << "|" << send.destination << ":" << juce::String(send.level, 3) << ":" << (int) send.preFader;
        }

    const auto metersMoved = std::abs(newLeft - smoothedLeft) > 0.002f || std::abs(newRight - smoothedRight) > 0.002f;

    smoothedLeft = newLeft;
    smoothedRight = newRight;

    if (stateKey != lastStateKey)
    {
        lastStateKey = stateKey;
        repaint();
    }
    else if (metersMoved)
    {
        repaint(getMeterArea());
    }
}

int MixerChannelStrip::getMinimumHeight() const
{
    return getFixedRowsHeight() + minimumFaderHeight;
}

int MixerChannelStrip::getFaderRowHeight() const
{
    return juce::jmax(faderRowHeight, getHeight() - getFixedRowsHeight());
}

int MixerChannelStrip::getFixedRowsHeight() const
{
    // Every row below the fader, in the order they are stacked. The send row
    // used to be measured from the bottom while the buttons were measured from
    // the top, and neither knew about the other: assigning a send put the send
    // bar straight on top of the M and S buttons.
    auto fixed = stripPadding * 2
               + getHeightAboveFader()
               + rowGap
               + buttonRowHeight + rowGap
               + dbRowHeight;

    if (! isMaster() && track != nullptr)
        fixed += routingRowHeight + rowGap;

    if (const auto sends = getSendSectionHeight(); sends > 0)
        fixed += sends + rowGap;

    return fixed;
}

juce::Rectangle<int> MixerChannelStrip::getRowsBelowFader() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(getHeightAboveFader()
                           + getFaderRowHeight() + rowGap);
    return area;
}

juce::Rectangle<int> MixerChannelStrip::getFaderColumn() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(getHeightAboveFader());
    return area.removeFromTop(getFaderRowHeight()).withWidth(faderColumnWidth);
}

juce::Rectangle<int> MixerChannelStrip::getFaderHandle() const
{
    const auto column = getFaderColumn();
    const auto ratio = juce::jlimit(0.0f, 1.0f, getLevel() / maxLevel);
    const auto top = column.getY() + juce::roundToInt((1.0f - ratio) * static_cast<float>(column.getHeight() - faderHandleHeight));
    return { column.getX() + 6, top, 19, faderHandleHeight };
}

juce::Rectangle<int> MixerChannelStrip::getMeterArea() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(getHeightAboveFader());
    auto row = area.removeFromTop(getFaderRowHeight());
    row.removeFromLeft(faderColumnWidth + 4);
    return row;
}

juce::Rectangle<int> MixerChannelStrip::getNameRowBounds() const
{
    return getLocalBounds().reduced(stripPadding).removeFromTop(nameRowHeight);
}

juce::Rectangle<int> MixerChannelStrip::getPanKnobArea() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap);
    return area.removeFromTop(panRowHeight).removeFromLeft(19);
}

juce::Rectangle<int> MixerChannelStrip::getButtonRow() const
{
    auto area = getRowsBelowFader();

    if (const auto sends = getSendSectionHeight(); sends > 0)
        area.removeFromTop(sends + rowGap);

    return area.removeFromTop(buttonRowHeight);
}

juce::Rectangle<int> MixerChannelStrip::getMuteBounds() const
{
    auto row = getButtonRow();
    return row.removeFromLeft((row.getWidth() - 6) / 3);
}

juce::Rectangle<int> MixerChannelStrip::getSoloBounds() const
{
    auto row = getButtonRow();
    const auto width = (row.getWidth() - 6) / 3;
    row.removeFromLeft(width + 3);
    return row.removeFromLeft(width);
}

juce::Rectangle<int> MixerChannelStrip::getArmBounds() const
{
    auto row = getButtonRow();
    const auto width = (row.getWidth() - 6) / 3;
    row.removeFromLeft((width + 3) * 2);
    return row.removeFromLeft(width);
}

float MixerChannelStrip::getLevel() const
{
    // The automated value, so the fader shows what is actually being applied
    // rather than the number the automation is overriding.
    if (track != nullptr)
        return track->getEffectiveVolume();

    return masterBus != nullptr ? masterBus->getGain() : 0.0f;
}

void MixerChannelStrip::setLevel(float newLevel)
{
    // An automated fader still moves when dragged, the way FL's does: the curve
    // takes it straight back on the next block, so it visibly springs back.
    // That reads as "something else owns this", where a control that refuses to
    // move at all just reads as broken.
    if (track != nullptr)
        track->setVolume(newLevel);
    else if (masterBus != nullptr)
        masterBus->setGain(newLevel);
}

float MixerChannelStrip::getPan() const
{
    return track != nullptr ? track->getEffectivePan() : 0.0f;
}

juce::String MixerChannelStrip::getDisplayName() const
{
    return track != nullptr ? track->getName() : juce::String("MASTER");
}

juce::Rectangle<int> MixerChannelStrip::getRoutingBounds() const
{
    if (isMaster() || track == nullptr)
        return {};

    auto area = getRowsBelowFader();

    if (const auto sends = getSendSectionHeight(); sends > 0)
        area.removeFromTop(sends + rowGap);

    area.removeFromTop(buttonRowHeight + rowGap);
    return area.removeFromTop(routingRowHeight);
}

juce::String MixerChannelStrip::getOutputDestinationName() const
{
    if (isMaster() || track == nullptr)
        return "OUT";

    const auto dest = track->getOutputDestination();
    if (dest == Track::masterDestination)
        return "Master";

    if (dest < 0)
        return "Off";

    if (mixer != nullptr && juce::isPositiveAndBelow(dest, mixer->getNumTracks()))
    {
        if (const auto* bus = mixer->getTrack(dest))
            return bus->getName();
    }

    return "Master";
}

} // namespace djr
