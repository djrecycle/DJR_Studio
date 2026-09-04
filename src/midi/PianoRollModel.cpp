#include "PianoRollModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace djr
{

void PianoRollModel::setTargetClip(MidiClip* clip)
{
    if (targetClip == clip)
        return;

    targetClip = clip;
    sendChangeMessage();
}

MidiClip* PianoRollModel::getTargetClip() const noexcept
{
    return targetClip;
}

bool PianoRollModel::hasTargetClip() const noexcept
{
    return targetClip != nullptr;
}

void PianoRollModel::addNote(int pitch, double startBeat, double lengthBeats, float velocity)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    targetClip->addNote({ pitch, velocity, snapToGrid(startBeat), lengthBeats });
    sendChangeMessage();
}

void PianoRollModel::deleteNoteAt(int index)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    targetClip->removeNoteAt(index);
    sendChangeMessage();
}

void PianoRollModel::dragNote(int index, double startBeat, int pitch)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    targetClip->moveNote(index, snapToGrid(startBeat), pitch);
    sendChangeMessage();
}

void PianoRollModel::setNoteVelocity(int index, float velocity)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    auto notes = targetClip->getNotesSnapshot();
    if (! juce::isPositiveAndBelow(index, notes.size()))
        return;

    notes.getReference(index).velocity = juce::jlimit(0.0f, 1.0f, velocity);
    targetClip->setNotes(notes);
    sendChangeMessage();
}

void PianoRollModel::setNoteLength(int index, double lengthBeats)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    auto notes = targetClip->getNotesSnapshot();
    if (! juce::isPositiveAndBelow(index, notes.size()))
        return;

    // With snapping off a note can be any length, down to a hair.
    const auto shortest = snapBeats > 0.0 ? snapBeats : 0.03125;
    notes.getReference(index).lengthBeats = juce::jmax(shortest, snapToGrid(lengthBeats));
    targetClip->setNotes(notes);
    sendChangeMessage();
}

void PianoRollModel::strumNotes(const juce::Array<int>& indices, double strumStepBeats)
{
    if (targetClip == nullptr || indices.size() < 2)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    auto notes = targetClip->getNotesSnapshot();

    auto ordered = indices;
    std::sort(ordered.begin(), ordered.end(), [&notes] (int a, int b)
    {
        return notes[a].pitch < notes[b].pitch;
    });

    // Fanned out from whichever of them starts earliest, so strumming a
    // chord that is not already perfectly stacked spreads forward from
    // where it begins instead of dragging the whole thing later.
    auto anchorBeat = std::numeric_limits<double>::max();
    for (const auto index : ordered)
        if (juce::isPositiveAndBelow(index, notes.size()))
            anchorBeat = juce::jmin(anchorBeat, notes[index].startBeat);

    for (int i = 0; i < ordered.size(); ++i)
    {
        const auto index = ordered[i];
        if (juce::isPositiveAndBelow(index, notes.size()))
            notes.getReference(index).startBeat = anchorBeat + i * strumStepBeats;
    }

    targetClip->setNotes(notes);
    sendChangeMessage();
}

void PianoRollModel::setNotes(const juce::Array<MidiNote>& notes)
{
    if (targetClip == nullptr)
        return;

    if (onBeforeEdit)
        onBeforeEdit();

    targetClip->setNotes(notes);
    sendChangeMessage();
}

double PianoRollModel::snapToGrid(double beats) const noexcept
{
    // Zero is the "(none)" snap entry: leave the value exactly where it is.
    if (snapBeats <= 0.0)
        return beats;

    return std::round(beats / snapBeats) * snapBeats;
}

void PianoRollModel::setSnapBeats(double beats)
{
    const auto clamped = beats <= 0.0 ? 0.0 : juce::jlimit(0.015625, 4.0, beats);

    if (std::abs(snapBeats - clamped) < 1.0e-9)
        return;

    // The piano roll draws its grid from this, so listeners have to redraw.
    snapBeats = clamped;
    sendChangeMessage();
}

double PianoRollModel::getSnapBeats() const noexcept
{
    return snapBeats;
}

juce::Array<MidiNote> PianoRollModel::getNotes() const
{
    return targetClip != nullptr ? targetClip->getNotesSnapshot() : juce::Array<MidiNote>();
}

void PianoRollModel::notifyClipChanged()
{
    sendChangeMessage();
}

} // namespace djr
