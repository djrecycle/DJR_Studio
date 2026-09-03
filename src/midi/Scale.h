#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace djr
{

/** Which scale a key highlights. chromatic is the "off" state: every pitch
    counts as in scale, so the roll draws exactly as it always has.
*/
enum class ScaleType
{
    chromatic = 0,
    major,
    naturalMinor,
    harmonicMinor,
    melodicMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    majorPentatonic,
    minorPentatonic,
    blues,
    count
};

/** A root pitch class plus a scale, used to tell the piano roll which rows
    are in key. Cheap to copy and compare, so views and session state can
    each just hold one by value.
*/
class Scale
{
public:
    Scale() noexcept { rebuild(); }

    /** `rootPitchClass` need not already be in 0..11 - it is wrapped. */
    Scale(int rootPitchClass, ScaleType scaleType) noexcept
        : root(wrap(rootPitchClass)), type(scaleType)
    {
        rebuild();
    }

    int getRoot() const noexcept { return root; }
    ScaleType getType() const noexcept { return type; }

    /** True for every pitch when chromatic - there is nothing to highlight
        against, so nothing should ever be drawn as "out of scale".
    */
    bool isInScale(int pitch) const noexcept
    {
        return type == ScaleType::chromatic || inScale[static_cast<size_t>(pitchClassOf(pitch))];
    }

    /** True when `pitch` is the key's root, in any octave. Meaningless while
        chromatic, so always false then - there is no root to speak of.
    */
    bool isRoot(int pitch) const noexcept
    {
        return type != ScaleType::chromatic && pitchClassOf(pitch) == root;
    }

    bool operator==(const Scale& other) const noexcept
    {
        return root == other.root && type == other.type;
    }
    bool operator!=(const Scale& other) const noexcept { return ! (*this == other); }

    static int pitchClassOf(int pitch) noexcept { return wrap(pitch); }

    /** The interval pattern (semitones from the root) that defines each type. */
    static const std::vector<int>& intervalsFor(ScaleType scaleType);
    /** Full display name, for the scale-type picker menu. */
    static juce::String nameFor(ScaleType scaleType);
    /** Three or four letters, for the badge - "Natural minor" does not fit in
        a corner the size of a piano key.
    */
    static juce::String shortNameFor(ScaleType scaleType);
    /** Display name for a pitch class (0 = C .. 11 = B), for the root picker. */
    static juce::String pitchClassName(int pitchClass);

private:
    static int wrap(int value) noexcept { return ((value % 12) + 12) % 12; }

    void rebuild() noexcept
    {
        inScale.fill(false);

        for (const auto interval : intervalsFor(type))
            inScale[static_cast<size_t>(wrap(root + interval))] = true;
    }

    int root = 0;
    ScaleType type = ScaleType::chromatic;
    std::array<bool, 12> inScale {};
};

} // namespace djr
