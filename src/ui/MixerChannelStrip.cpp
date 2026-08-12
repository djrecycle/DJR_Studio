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

    // Level readout ----------------------------------------------------------
    const auto level = getLevel();
    const auto db = level <= 0.0001f ? juce::String("-inf")
                                     : juce::String(juce::Decibels::gainToDecibels(level), 1);

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(9.5f));
    g.drawText(db, bounds.withTrimmedBottom(3).removeFromBottom(10), juce::Justification::centred, false);
}

void MixerChannelStrip::setSelected(bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    repaint();
}

void MixerChannelStrip::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    // Touching a strip anywhere makes it the session's current track.
    if (! isMaster() && onSelected)
        onSelected();

    if (! isMaster() && track != nullptr)
    {
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
    const auto fixed = stripPadding * 2 + nameRowHeight + rowGap + panRowHeight + rowGap
                     + rowGap + buttonRowHeight + dbRowHeight;

    return juce::jmax(faderRowHeight, getHeight() - fixed);
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

juce::Rectangle<int> MixerChannelStrip::getPanKnobArea() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap);
    return area.removeFromTop(panRowHeight).removeFromLeft(19);
}

juce::Rectangle<int> MixerChannelStrip::getButtonRow() const
{
    auto area = getLocalBounds().reduced(stripPadding);
    area.removeFromTop(nameRowHeight + rowGap + panRowHeight + rowGap + getFaderRowHeight() + rowGap);
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
    if (track != nullptr)
        return track->getVolume();

    return masterBus != nullptr ? masterBus->getGain() : 0.0f;
}

void MixerChannelStrip::setLevel(float newLevel)
{
    if (track != nullptr)
        track->setVolume(newLevel);
    else if (masterBus != nullptr)
        masterBus->setGain(newLevel);
}

float MixerChannelStrip::getPan() const
{
    return track != nullptr ? track->getPan() : 0.0f;
}

juce::String MixerChannelStrip::getDisplayName() const
{
    return track != nullptr ? track->getName() : juce::String("MASTER");
}

} // namespace djr
