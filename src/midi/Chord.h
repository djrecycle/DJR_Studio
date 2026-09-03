#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace djr
{

/** The chord shapes the piano roll's chord stamp can write. Intervals are in
    semitones from whichever pitch was clicked, so the shape follows the
    click rather than any fixed key.
*/
enum class ChordType
{
    major = 0,
    minor,
    diminished,
    augmented,
    sus2,
    sus4,
    major7,
    minor7,
    dominant7,
    diminished7,
    halfDiminished7,
    minorMajor7,
    count
};

namespace Chord
{
    /** Semitones above the clicked pitch, root first. */
    const std::vector<int>& intervalsFor(ChordType type);
    /** Full display name, for the chord-type picker menu. */
    juce::String nameFor(ChordType type);
    /** Three or four letters, for the badge - "Half-diminished 7th" does not
        fit in a corner the size of a piano key.
    */
    juce::String shortNameFor(ChordType type);
}

} // namespace djr
