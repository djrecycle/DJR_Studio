#include "SessionState.h"

#include <cmath>

namespace djr
{

void SessionState::setSelectedTrack(int trackIndex)
{
    const auto clamped = juce::jmax(0, trackIndex);

    if (clamped == selectedTrack)
        return;

    selectedTrack = clamped;
    sendChangeMessage();
}

int SessionState::getSelectedTrack() const noexcept
{
    return selectedTrack;
}

void SessionState::setActivePattern(int patternIndex)
{
    const auto clamped = juce::jlimit(0, 15, patternIndex);

    if (clamped == activePattern)
        return;

    activePattern = clamped;
    sendChangeMessage();
}

int SessionState::getActivePattern() const noexcept
{
    return activePattern;
}

void SessionState::setPatternLengthBeats(int patternIndex, double beats)
{
    if (! juce::isPositiveAndBelow(patternIndex, maxPatterns))
        return;

    // Zero is the "follow the notes" setting, anything else is a fixed length.
    const auto clamped = beats <= 0.0 ? 0.0 : juce::jlimit(1.0, 256.0, beats);

    if (std::abs(patternLengths[static_cast<size_t>(patternIndex)] - clamped) < 1.0e-9)
        return;

    patternLengths[static_cast<size_t>(patternIndex)] = clamped;
    sendChangeMessage();
}

double SessionState::getPatternLengthBeats(int patternIndex) const
{
    return juce::isPositiveAndBelow(patternIndex, maxPatterns)
        ? patternLengths[static_cast<size_t>(patternIndex)]
        : 0.0;
}

void SessionState::setPatternName(int patternIndex, const juce::String& name)
{
    if (! juce::isPositiveAndBelow(patternIndex, maxPatterns))
        return;

    const auto trimmed = name.trim();

    if (patternNames[static_cast<size_t>(patternIndex)] == trimmed)
        return;

    patternNames[static_cast<size_t>(patternIndex)] = trimmed;
    sendChangeMessage();
}

juce::String SessionState::getPatternName(int patternIndex) const
{
    const auto custom = getCustomPatternName(patternIndex);

    return custom.isNotEmpty() ? custom : "PAT " + juce::String(patternIndex + 1);
}

juce::String SessionState::getCustomPatternName(int patternIndex) const
{
    return juce::isPositiveAndBelow(patternIndex, maxPatterns)
        ? patternNames[static_cast<size_t>(patternIndex)]
        : juce::String();
}

void SessionState::setSongMode(bool shouldPlaySong)
{
    if (songMode == shouldPlaySong)
        return;

    songMode = shouldPlaySong;
    sendChangeMessage();
}

bool SessionState::isSongMode() const noexcept
{
    return songMode;
}

void SessionState::setScale(const Scale& newScale)
{
    if (scale == newScale)
        return;

    scale = newScale;
    sendChangeMessage();
}

const Scale& SessionState::getScale() const noexcept
{
    return scale;
}

void SessionState::refresh()
{
    sendChangeMessage();
}

} // namespace djr
