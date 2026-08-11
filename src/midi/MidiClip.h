#pragma once

#include "MidiNote.h"

#include <juce_core/juce_core.h>

namespace djr
{

class MidiClip
{
public:
    void addNote(MidiNote note);
    void removeNoteAt(int index);
    void moveNote(int index, double newStartBeat, int newPitch);
    void quantize(double gridBeats);
    void clear();
    void setNotes(const juce::Array<MidiNote>& newNotes);

    juce::Array<MidiNote> getNotesSnapshot() const;
    int getNumNotes() const;

    juce::var toVar() const;
    void fromVar(const juce::var& value);

private:
    mutable juce::CriticalSection lock;
    juce::Array<MidiNote> notes;
};

} // namespace djr
