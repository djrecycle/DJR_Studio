#include "MidiClip.h"

#include <cmath>

namespace djr
{

void MidiClip::addNote(MidiNote note)
{
    note.pitch = juce::jlimit(0, 127, note.pitch);
    note.velocity = juce::jlimit(0.0f, 1.0f, note.velocity);
    note.startBeat = juce::jmax(0.0, note.startBeat);
    note.lengthBeats = juce::jmax(0.0625, note.lengthBeats);

    const juce::ScopedLock scoped(lock);
    notes.add(note);
}

void MidiClip::removeNoteAt(int index)
{
    const juce::ScopedLock scoped(lock);
    if (juce::isPositiveAndBelow(index, notes.size()))
        notes.remove(index);
}

void MidiClip::moveNote(int index, double newStartBeat, int newPitch)
{
    const juce::ScopedLock scoped(lock);
    if (! juce::isPositiveAndBelow(index, notes.size()))
        return;

    auto& note = notes.getReference(index);
    note.startBeat = juce::jmax(0.0, newStartBeat);
    note.pitch = juce::jlimit(0, 127, newPitch);
}

void MidiClip::quantize(double gridBeats)
{
    if (gridBeats <= 0.0)
        return;

    const juce::ScopedLock scoped(lock);
    for (auto& note : notes)
        note.startBeat = std::round(note.startBeat / gridBeats) * gridBeats;
}

void MidiClip::clear()
{
    const juce::ScopedLock scoped(lock);
    notes.clear();
}

void MidiClip::setNotes(const juce::Array<MidiNote>& newNotes)
{
    const juce::ScopedLock scoped(lock);
    notes = newNotes;
}

juce::Array<MidiNote> MidiClip::getNotesSnapshot() const
{
    const juce::ScopedLock scoped(lock);
    return notes;
}

int MidiClip::getNumNotes() const
{
    const juce::ScopedLock scoped(lock);
    return notes.size();
}

juce::var MidiClip::toVar() const
{
    juce::Array<juce::var> array;
    const juce::ScopedLock scoped(lock);
    for (const auto& note : notes)
        array.add(note.toVar());
    return array;
}

void MidiClip::fromVar(const juce::var& value)
{
    const juce::ScopedLock scoped(lock);
    notes.clear();

    if (auto* array = value.getArray())
        for (const auto& entry : *array)
            notes.add(MidiNote::fromVar(entry));
}

} // namespace djr
