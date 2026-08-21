#include "MixerChannelStrip.h"

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
    constexpr float maxLevel = 1.5f;
    /** The send row only exists when something is assigned, so an unrouted
        strip looks exactly as it always did.
    */
    constexpr int sendRowHeight = 15;
    constexpr int sendBarGap = 2;
    constexpr float maxSendLevel = 1.5f;
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

    // Sends ------------------------------------------------------------------
    // Only drawn when something is assigned, so a strip with no sends looks
    // exactly as it did before routing existed.
    if (track != nullptr && mixer != nullptr)
    {
        for (const auto slot : getAssignedSends())
        {
            const auto bar = getSendBarBounds(slot);

            if (bar.isEmpty())
                continue;

            const auto send = track->getSend(slot);
            // Pre-fader reads amber, post-fader takes the track colour: the one
            // thing you cannot afford to misread about a send is which it is.
            const auto sendColour = send.preFader ? Theme::amber() : colour;

            g.setColour(Theme::inset());
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);

            const auto ratio = juce::jlimit(0.0f, 1.0f, send.level / maxSendLevel);
            auto filled = bar.reduced(1);
            filled = filled.withHeight(juce::roundToInt(ratio * filled.getHeight()))
                           .withBottomY(bar.getBottom() - 1);

            g.setColour(sendColour.withAlpha(0.85f));
            g.fillRoundedRectangle(filled.toFloat(), 1.5f);

            g.setColour(Theme::windowBackground().withAlpha(0.85f));
            g.setFont(Theme::mono(8.0f, true));
            g.drawText(juce::String(slot + 1), bar, juce::Justification::centred, false);
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

juce::Rectangle<int> MixerChannelStrip::getSendRowBounds() const
{
    if (getAssignedSends().empty())
        return {};

    return getRowsBelowFader().removeFromTop(sendRowHeight);
}

juce::Rectangle<int> MixerChannelStrip::getSendBarBounds(int slot) const
{
    const auto assigned = getAssignedSends();
    const auto row = getSendRowBounds();

    if (assigned.empty() || row.isEmpty())
        return {};

    const auto position = std::find(assigned.begin(), assigned.end(), slot);

    if (position == assigned.end())
        return {};

    const auto count = static_cast<int>(assigned.size());
    const auto index = static_cast<int>(std::distance(assigned.begin(), position));
    const auto width = (row.getWidth() - sendBarGap * (count - 1)) / count;

    return { row.getX() + index * (width + sendBarGap), row.getY(), width, row.getHeight() };
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

            // Anywhere else on the strip is where the audio goes.
            showRoutingMenu();
            return;
        }

        // Sends are dragged like little faders of their own.
        for (const auto slot : getAssignedSends())
        {
            if (! getSendBarBounds(slot).contains(position))
                continue;

            draggingSend = slot;
            auto send = track->getSend(slot);
            const auto bar = getSendBarBounds(slot);
            const auto ratio = 1.0f - static_cast<float>(position.y - bar.getY())
                                          / static_cast<float>(juce::jmax(1, bar.getHeight()));
            send.level = juce::jlimit(0.0f, 1.0f, ratio) * maxSendLevel;

            if (mixer != nullptr)
                mixer->setTrackSend(trackIndex, slot, send);

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
        const auto bar = getSendBarBounds(draggingSend);

        if (! bar.isEmpty())
        {
            auto send = track->getSend(draggingSend);
            const auto ratio = 1.0f - static_cast<float>(event.getPosition().y - bar.getY())
                                          / static_cast<float>(juce::jmax(1, bar.getHeight()));
            send.level = juce::jlimit(0.0f, 1.0f, ratio) * maxSendLevel;
            mixer->setTrackSend(trackIndex, draggingSend, send);
        }

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
    const auto decay = 0.18f;
    const auto left = track != nullptr ? track->getPeakLevel(0) : masterBus->getPeakLevel(0);
    const auto right = track != nullptr ? track->getPeakLevel(1) : masterBus->getPeakLevel(1);

    smoothedLeft = juce::jmax(left, smoothedLeft - decay);
    smoothedRight = juce::jmax(right, smoothedRight - decay);
    repaint();
}

int MixerChannelStrip::getFaderRowHeight() const
{
    // Every row below the fader, in the order they are stacked. The send row
    // used to be measured from the bottom while the buttons were measured from
    // the top, and neither knew about the other: assigning a send put the send
    // bar straight on top of the M and S buttons.
    auto fixed = stripPadding * 2
               + nameRowHeight + rowGap
               + panRowHeight + rowGap
               + rowGap
               + buttonRowHeight + rowGap
               + dbRowHeight;

    if (! getAssignedSends().empty())
        fixed += sendRowHeight + rowGap;

    return juce::jmax(faderRowHeight, getHeight() - fixed);
}

juce::Rectangle<int> MixerChannelStrip::getRowsBelowFader() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap
                           + getFaderRowHeight() + rowGap);
    return area;
}

juce::Rectangle<int> MixerChannelStrip::getFaderColumn() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap);
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
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap);
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

    if (! getAssignedSends().empty())
        area.removeFromTop(sendRowHeight + rowGap);

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

} // namespace djr
