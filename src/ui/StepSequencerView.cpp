#include "StepSequencerView.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int padSize = 26;
    constexpr int padGap = 4;
    constexpr int cardWidth = 104;
    constexpr int contentPaddingX = 8;
    constexpr int contentPaddingY = 7;
    constexpr int numbersHeight = 14;
    constexpr int laneGap = 5;
    constexpr double stepBeats = 0.25;
}

StepSequencerView::StepSequencerView(PianoRollModel& modelToUse, Transport& transportToUse)
    : model(modelToUse), transport(transportToUse)
{
    model.addChangeListener(this);
    startTimerHz(30);
}

StepSequencerView::~StepSequencerView()
{
    model.removeChangeListener(this);
}

void StepSequencerView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint();
}

double StepSequencerView::getPageStartBeat() const noexcept
{
    return page * getNumSteps() * stepBeats;
}

bool StepSequencerView::isStepOn(const juce::Array<MidiNote>& notes, int lane, int step) const
{
    if (! juce::isPositiveAndBelow(lane, static_cast<int>(lanes.size())))
        return false;

    const auto pitch = lanes[static_cast<size_t>(lane)].pitch;
    const auto stepStart = getPageStartBeat() + static_cast<double>(step) * stepBeats;

    for (const auto& note : notes)
        if (note.pitch == pitch && std::abs(note.startBeat - stepStart) < stepBeats * 0.5)
            return true;

    return false;
}

int StepSequencerView::getNumSteps() const
{
    // A page is one bar wide; sixteenth note steps, so 4/4 gives sixteen pads.
    return juce::jmax(1, juce::roundToInt(transport.getBeatsPerBar() / stepBeats));
}

int StepSequencerView::getNumPages() const
{
    const auto beatsPerPage = getNumSteps() * stepBeats;
    return juce::jmax(1, static_cast<int>(std::ceil(patternLengthBeats / beatsPerPage - 1.0e-9)));
}

void StepSequencerView::setPatternLengthBeats(double beats)
{
    const auto clamped = juce::jmax(1.0, beats);

    if (std::abs(patternLengthBeats - clamped) < 1.0e-9)
        return;

    patternLengthBeats = clamped;
    setPage(juce::jmin(page, getNumPages() - 1));
    repaint();
}

void StepSequencerView::setFollowPlayhead(bool shouldFollow)
{
    followPlayhead = shouldFollow;
}

void StepSequencerView::setPage(int newPage)
{
    const auto clamped = juce::jlimit(0, getNumPages() - 1, newPage);

    if (clamped == page)
        return;

    page = clamped;
    repaint();
}

void StepSequencerView::toggleStep(int lane, int step)
{
    if (! model.hasTargetClip() || ! juce::isPositiveAndBelow(lane, static_cast<int>(lanes.size())))
        return;

    const auto pitch = lanes[static_cast<size_t>(lane)].pitch;
    const auto stepStart = getPageStartBeat() + static_cast<double>(step) * stepBeats;

    auto notes = model.getNotes();

    for (int i = notes.size(); --i >= 0;)
    {
        const auto& note = notes.getReference(i);

        if (note.pitch == pitch && std::abs(note.startBeat - stepStart) < stepBeats * 0.5)
        {
            notes.remove(i);
            model.setNotes(notes);
            return;
        }
    }

    notes.add({ pitch, 0.9f, stepStart, stepBeats });
    model.setNotes(notes);
}

void StepSequencerView::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panelDeep());

    const auto notes = model.getNotes();
    const auto hasClip = model.hasTargetClip();
    const auto currentStep = transport.isPlaying() ? getCurrentStep() : -1;

    if (! hasClip)
    {
        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(12.5f));
        g.drawText("Pilih track MIDI untuk memakai step sequencer",
                   getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    // Bar navigation, so the grid can reach the whole timeline ---------------
    const auto drawBarButton = [&g] (juce::Rectangle<int> area, const juce::String& glyph, bool enabled)
    {
        g.setColour(enabled ? Theme::control() : Theme::control().withAlpha(0.4f));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(enabled ? Theme::text() : Theme::faintText());
        g.setFont(Theme::mono(10.0f));
        g.drawText(glyph, area, juce::Justification::centred, false);
    };

    drawBarButton(getPreviousBarBounds(), "<", page > 0);
    drawBarButton(getNextBarBounds(), ">", page + 1 < getNumPages());

    g.setColour(Theme::mutedText());
    g.setFont(Theme::mono(10.0f));
    g.drawText("BAR " + juce::String(page + 1) + "/" + juce::String(getNumPages()),
               juce::Rectangle<int>(contentPaddingX + 45, contentPaddingY, 70, numbersHeight - 3),
               juce::Justification::centredLeft,
               false);

    // Step numbers -----------------------------------------------------------
    for (int step = 0; step < getNumSteps(); ++step)
    {
        const auto pad = getPadBounds(0, step);
        g.setColour(step % 4 == 0 ? Theme::text() : Theme::faintText());
        g.setFont(Theme::mono(10.0f));
        g.drawText(juce::String(page * getNumSteps() + step + 1),
                   pad.withY(contentPaddingY).withHeight(numbersHeight - 6),
                   juce::Justification::centred,
                   false);
    }

    for (int lane = 0; lane < static_cast<int>(lanes.size()); ++lane)
    {
        const auto colour = Theme::trackColour(lanes[static_cast<size_t>(lane)].colourIndex);
        const auto card = getLaneCardBounds(lane);

        Theme::drawCard(g, card, Theme::panelHeader(), Theme::divider(), 4.0f);

        auto cardContent = card.reduced(7, 0);
        auto chip = cardContent.removeFromLeft(4).withSizeKeepingCentre(4, 13);
        g.setColour(colour);
        g.fillRoundedRectangle(chip.toFloat(), 1.5f);
        cardContent.removeFromLeft(6);

        g.setColour(Theme::text());
        g.setFont(Theme::ui(11.5f, true));
        g.drawText(lanes[static_cast<size_t>(lane)].name,
                   cardContent.removeFromTop(card.getHeight() / 2).withTrimmedTop(2),
                   juce::Justification::bottomLeft, true);
        g.setColour(Theme::faintText());
        g.setFont(Theme::mono(9.0f));
        g.drawText(lanes[static_cast<size_t>(lane)].note, cardContent, juce::Justification::topLeft, true);

        for (int step = 0; step < getNumSteps(); ++step)
        {
            const auto pad = getPadBounds(lane, step);
            const auto on = isStepOn(notes, lane, step);
            const auto isDownbeat = step % 4 == 0;

            auto background = on ? colour
                                 : (isDownbeat ? juce::Colour::fromString("ff1c2231")
                                               : juce::Colour::fromString("ff161b25"));

            if (step == currentStep)
                background = background.brighter(on ? 0.25f : 0.35f);

            g.setColour(background);
            g.fillRoundedRectangle(pad.toFloat(), 4.0f);
            g.setColour(on ? colour : (step == currentStep ? Theme::accent() : Theme::divider()));
            g.drawRoundedRectangle(pad.toFloat().reduced(0.5f), 4.0f, 1.0f);

            const auto accent = on ? Theme::windowBackground().withAlpha(0.55f)
                                   : (isDownbeat ? Theme::gridBar() : juce::Colours::transparentBlack);

            if (! accent.isTransparent())
            {
                g.setColour(accent);
                g.fillRoundedRectangle(juce::Rectangle<int>(pad.getCentreX() - 5, pad.getBottom() - 5, 10, 2).toFloat(), 1.0f);
            }
        }
    }
}

void StepSequencerView::mouseDown(const juce::MouseEvent& event)
{
    if (getPreviousBarBounds().contains(event.getPosition()))
    {
        setPage(page - 1);
        return;
    }

    if (getNextBarBounds().contains(event.getPosition()))
    {
        setPage(page + 1);
        return;
    }

    for (int lane = 0; lane < static_cast<int>(lanes.size()); ++lane)
    {
        for (int step = 0; step < getNumSteps(); ++step)
        {
            if (! getPadBounds(lane, step).contains(event.getPosition()))
                continue;

            toggleStep(lane, step);
            repaint();
            return;
        }
    }
}

void StepSequencerView::timerCallback()
{
    // Page along with playback so the lit step is always the one you hear.
    if (followPlayhead && transport.isPlaying())
    {
        const auto beatsPerPage = getNumSteps() * stepBeats;
        const auto wrapped = std::fmod(juce::jmax(0.0, transport.getPositionBeats()), patternLengthBeats);
        const auto playingPage = juce::jlimit(0, getNumPages() - 1,
                                              static_cast<int>(std::floor(wrapped / beatsPerPage)));

        if (playingPage != page)
        {
            page = playingPage;
            repaint();
        }
    }

    const auto step = transport.isPlaying() ? getCurrentStep() : -1;

    if (step == lastPaintedStep)
        return;

    lastPaintedStep = step;
    repaint();
}

int StepSequencerView::getPadSize() const
{
    const auto lanes4 = static_cast<int>(lanes.size());
    const auto available = getHeight() - contentPaddingY * 2 - numbersHeight - (lanes4 - 1) * laneGap;
    return juce::jlimit(14, padSize, available / juce::jmax(1, lanes4));
}

int StepSequencerView::getLaneGap() const
{
    return getPadSize() < padSize ? 3 : laneGap;
}

juce::Rectangle<int> StepSequencerView::getPadBounds(int lane, int step) const
{
    const auto size = getPadSize();
    const auto x = contentPaddingX + cardWidth + padGap + step * (size + padGap);
    const auto y = contentPaddingY + numbersHeight + lane * (size + getLaneGap());
    return { x, y, size, size };
}

juce::Rectangle<int> StepSequencerView::getLaneCardBounds(int lane) const
{
    const auto size = getPadSize();
    const auto y = contentPaddingY + numbersHeight + lane * (size + getLaneGap());
    return { contentPaddingX, y, cardWidth, size };
}

int StepSequencerView::getCurrentStep() const
{
    // Pattern mode loops, so the lit step follows the wrapped position.
    const auto wrapped = std::fmod(juce::jmax(0.0, transport.getPositionBeats()), patternLengthBeats);
    const auto stepsFromOrigin = static_cast<int>(std::floor(wrapped / stepBeats));
    const auto relative = stepsFromOrigin - page * getNumSteps();

    return juce::isPositiveAndBelow(relative, getNumSteps()) ? relative : -1;
}

juce::Rectangle<int> StepSequencerView::getPreviousBarBounds() const
{
    return { contentPaddingX, contentPaddingY, 18, numbersHeight - 3 };
}

juce::Rectangle<int> StepSequencerView::getNextBarBounds() const
{
    return { contentPaddingX + 21, contentPaddingY, 18, numbersHeight - 3 };
}

} // namespace djr
