#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** One appearance of a pattern on the song timeline.

    Trimming the left edge does not throw notes away: it moves `sourceOffsetBeats`
    so the placement starts further into the pattern, the same way a DAW reveals
    a later part of a clip.
*/
struct PatternPlacement
{
    int patternIndex = 0;
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    double sourceOffsetBeats = 0.0;
    bool muted = false;

    /** Shortest a placement may be trimmed to. */
    static constexpr double minimumLengthBeats = 0.25;

    double getEndBeat() const noexcept { return startBeat + lengthBeats; }

    /** Drags the left edge, keeping the notes underneath where they are. */
    void trimStart(double newStartBeat) noexcept
    {
        const auto limit = startBeat + lengthBeats - minimumLengthBeats;
        const auto clamped = juce::jlimit(juce::jmax(0.0, startBeat - sourceOffsetBeats), limit, newStartBeat);
        const auto delta = clamped - startBeat;

        startBeat = clamped;
        sourceOffsetBeats = juce::jmax(0.0, sourceOffsetBeats + delta);
        lengthBeats = juce::jmax(minimumLengthBeats, lengthBeats - delta);
    }

    /** Drags the right edge. */
    void trimEnd(double newEndBeat) noexcept
    {
        lengthBeats = juce::jmax(minimumLengthBeats, newEndBeat - startBeat);
    }

    /** Whether splitAt would accept this cut, without performing it. The slice
        tool asks this before drawing its preview line.
    */
    bool canSplitAt(double beat) const noexcept
    {
        return beat > startBeat + minimumLengthBeats && beat < getEndBeat() - minimumLengthBeats;
    }

    /** Splits at `beat`, returning the right hand piece. */
    bool splitAt(double beat, PatternPlacement& rightOut) noexcept
    {
        if (! canSplitAt(beat))
            return false;

        rightOut = *this;
        rightOut.sourceOffsetBeats += beat - startBeat;
        rightOut.lengthBeats = getEndBeat() - beat;
        rightOut.startBeat = beat;

        lengthBeats = beat - startBeat;
        return true;
    }

    juce::var toVar() const
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("pattern", patternIndex);
        object->setProperty("startBeat", startBeat);
        object->setProperty("lengthBeats", lengthBeats);
        object->setProperty("sourceOffset", sourceOffsetBeats);
        object->setProperty("muted", muted);
        return object;
    }

    static PatternPlacement fromVar(const juce::var& value)
    {
        PatternPlacement placement;

        if (auto* object = value.getDynamicObject())
        {
            placement.patternIndex = static_cast<int>(object->getProperty("pattern"));
            placement.startBeat = static_cast<double>(object->getProperty("startBeat"));
            placement.lengthBeats = static_cast<double>(object->getProperty("lengthBeats"));
            placement.sourceOffsetBeats = static_cast<double>(object->getProperty("sourceOffset"));
            placement.muted = static_cast<bool>(object->getProperty("muted"));
        }

        return placement;
    }
};

} // namespace djr
