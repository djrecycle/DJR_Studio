#include "VelocityLane.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int barWidth = 7;
    constexpr int verticalPadding = 5;
}

VelocityLane::VelocityLane(PianoRollModel& modelToUse, const PianoRollView& pianoRollToUse)
    : model(modelToUse), pianoRoll(pianoRollToUse)
{
    model.addChangeListener(this);
}

VelocityLane::~VelocityLane()
{
    model.removeChangeListener(this);
}

void VelocityLane::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto keyboardWidth = pianoRoll.getKeyboardWidth();

    g.fillAll(Theme::inset());
    g.setColour(Theme::divider());
    g.fillRect(bounds.withHeight(1));
    g.fillRect(keyboardWidth - 1, 0, 1, bounds.getHeight());

    // Header column ----------------------------------------------------------
    auto header = bounds.withWidth(keyboardWidth).reduced(5, 4);
    Theme::drawCaption(g, header.removeFromTop(10), "Vel");
    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(9.5f));
    g.drawText("127", header.removeFromTop(11), juce::Justification::topLeft, false);
    g.drawText("0", header.removeFromBottom(11), juce::Justification::bottomLeft, false);

    // Grid -------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.withTrimmedLeft(keyboardWidth));

        const auto pixelsPerBeat = pianoRoll.getPixelsPerBeat();
        const auto scrollBeats = pianoRoll.getScrollBeats();
        const auto lastBeat = scrollBeats + (bounds.getWidth() - keyboardWidth) / pixelsPerBeat + 1.0;

        g.setColour(juce::Colour::fromString("ff1c2431"));

        for (auto beat = std::floor(scrollBeats / pianoRoll.getBeatsPerBar()) * pianoRoll.getBeatsPerBar(); beat <= lastBeat; beat += pianoRoll.getBeatsPerBar())
            g.fillRect(keyboardWidth + juce::roundToInt((beat - scrollBeats) * pixelsPerBeat), 0, 1, bounds.getHeight());

        for (int y = 0; y < bounds.getHeight(); y += 13)
            g.fillRect(keyboardWidth, y, bounds.getWidth() - keyboardWidth, 1);
    }

    // Bars -------------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.withTrimmedLeft(keyboardWidth));

        const auto notes = model.getNotes();
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto bar = barBounds(notes[i]);
            if (bar.getRight() < keyboardWidth || bar.getX() > bounds.getRight())
                continue;

            g.setColour(PianoRollView::velocityColour(notes[i].velocity)
                            .withMultipliedBrightness(i == draggedNote ? 1.35f : 1.0f));
            g.fillRoundedRectangle(bar.toFloat(), 1.5f);

            g.setColour(Theme::text());
            g.fillRoundedRectangle(juce::Rectangle<int>(bar.getX() - 1, bar.getY() - 2, barWidth + 2, 2).toFloat(), 1.0f);
        }

        if (notes.isEmpty())
        {
            g.setColour(Theme::faintText());
            g.setFont(Theme::ui(11.5f));
            g.drawText(TRANS("No notes yet"), bounds.withTrimmedLeft(keyboardWidth), juce::Justification::centred, false);
        }
    }
}

void VelocityLane::mouseDown(const juce::MouseEvent& event)
{
    if (onEditGesture)
        onEditGesture(true);

    draggedNote = barAtPosition(event.getPosition());

    if (draggedNote >= 0)
        applyVelocityFromY(draggedNote, event.y);
}

void VelocityLane::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedNote >= 0)
        applyVelocityFromY(draggedNote, event.y);
}

void VelocityLane::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (onEditGesture)
        onEditGesture(false);

    draggedNote = -1;
    repaint();
}

void VelocityLane::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint();
}

int VelocityLane::barAtPosition(juce::Point<int> position) const
{
    const auto notes = model.getNotes();

    for (int i = notes.size(); --i >= 0;)
        if (barBounds(notes[i]).expanded(3, 6).contains(position))
            return i;

    return -1;
}

juce::Rectangle<int> VelocityLane::barBounds(const MidiNote& note) const
{
    const auto keyboardWidth = pianoRoll.getKeyboardWidth();
    const auto x = keyboardWidth + juce::roundToInt((note.startBeat - pianoRoll.getScrollBeats()) * pianoRoll.getPixelsPerBeat()) + 2;
    const auto usableHeight = juce::jmax(1, getHeight() - verticalPadding);
    const auto height = juce::roundToInt(juce::jlimit(0.0f, 1.0f, note.velocity) * static_cast<float>(usableHeight));

    return { x, getHeight() - height, barWidth, height };
}

void VelocityLane::applyVelocityFromY(int noteIndex, int y)
{
    const auto usableHeight = juce::jmax(1, getHeight() - verticalPadding);
    const auto velocity = 1.0f - static_cast<float>(y - verticalPadding) / static_cast<float>(usableHeight);
    model.setNoteVelocity(noteIndex, juce::jlimit(0.0f, 1.0f, velocity));
}

} // namespace djr
