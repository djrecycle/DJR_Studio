#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

struct MidiNote
{
    int pitch = 60;
    float velocity = 0.8f;
    double startBeat = 0.0;
    double lengthBeats = 1.0;
    /** A muted note stays visible and editable but is never played. */
    bool muted = false;
    /** Notes written by one chord stamp share this id, so dragging or
        resizing any one of them carries the rest along. -1 means the note
        stands alone.
    */
    int chordGroupId = -1;

    juce::var toVar() const
    {
        auto object = new juce::DynamicObject();
        object->setProperty("pitch", pitch);
        object->setProperty("velocity", velocity);
        object->setProperty("startBeat", startBeat);
        object->setProperty("lengthBeats", lengthBeats);
        object->setProperty("muted", muted);
        object->setProperty("chordGroupId", chordGroupId);
        return object;
    }

    static MidiNote fromVar(const juce::var& value)
    {
        MidiNote note;
        if (auto* object = value.getDynamicObject())
        {
            note.pitch = static_cast<int>(object->getProperty("pitch"));
            note.velocity = static_cast<float>(static_cast<double>(object->getProperty("velocity")));
            note.startBeat = static_cast<double>(object->getProperty("startBeat"));
            note.lengthBeats = static_cast<double>(object->getProperty("lengthBeats"));
            note.muted = static_cast<bool>(object->getProperty("muted"));
            note.chordGroupId = object->hasProperty("chordGroupId")
                                     ? static_cast<int>(object->getProperty("chordGroupId"))
                                     : -1;
        }
        return note;
    }
};

} // namespace djr
