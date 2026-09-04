#pragma once

#include "midi/Scale.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <array>

namespace djr
{

/** The one place that knows which track the user is working on.

    Every panel both listens to this and writes to it, so clicking a mixer
    strip, a playlist row or a plugin slot moves the whole session together.
*/
class SessionState final : public juce::ChangeBroadcaster
{
public:
    void setSelectedTrack(int trackIndex);
    int getSelectedTrack() const noexcept;

    /** Which pattern every MIDI track edits and, in pattern mode, plays. */
    void setActivePattern(int patternIndex);
    int getActivePattern() const noexcept;

    /** Length a pattern loops for, in beats. Zero means follow its content. */
    void setPatternLengthBeats(int patternIndex, double beats);
    double getPatternLengthBeats(int patternIndex) const;
    static constexpr int maxPatterns = 16;

    /** A pattern's own name. Empty means it has never been renamed, and callers
        fall back to getPatternName() which builds the "PAT n" default.
    */
    void setPatternName(int patternIndex, const juce::String& name);
    juce::String getPatternName(int patternIndex) const;
    /** The stored name only, empty when the pattern still uses its default. */
    juce::String getCustomPatternName(int patternIndex) const;

    /** Song mode plays the placements on the timeline. */
    void setSongMode(bool shouldPlaySong);
    bool isSongMode() const noexcept;

    /** The piano roll's scale highlight. Chromatic (the default) means off. */
    void setScale(const Scale& newScale);
    const Scale& getScale() const noexcept;

    /** Re-announces the current selection, e.g. after tracks are added. */
    void refresh();

private:
    int selectedTrack = 0;
    int activePattern = 0;
    bool songMode = false;
    Scale scale;
    std::array<double, maxPatterns> patternLengths {};
    std::array<juce::String, maxPatterns> patternNames {};
};

} // namespace djr
