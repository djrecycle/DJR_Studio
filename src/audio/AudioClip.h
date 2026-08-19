#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>
#include <vector>

namespace djr
{

/** A piece of audio placed on the timeline.

    The samples are decoded and resampled to the device rate once, up front, so
    playback never touches the disk or allocates on the audio thread. That costs
    memory - roughly 350 kB per stereo second at 44.1 kHz - which is the right
    trade for takes and one-shots, less so for hour-long files.

    Placement, trim and warp are atomics because the playlist edits them from
    the message thread while the audio thread is reading.
*/
class AudioClip
{
public:
    /** Reads `file` and resamples it to `targetSampleRate`. Returns nullptr on failure. */
    static std::unique_ptr<AudioClip> createFromFile(const juce::File& file,
                                                     double targetSampleRate,
                                                     juce::AudioFormatManager& formats,
                                                     juce::String& errorOut);

    const juce::String& getName() const noexcept;
    const juce::File& getFile() const noexcept;

    double getStartBeat() const noexcept;
    void setStartBeat(double beat) noexcept;

    /** Seconds of source skipped at the head, moved by trimming the left edge. */
    double getSourceOffsetSeconds() const noexcept;
    /** How much source plays, in seconds; shortened by trimming either edge. */
    double getPlayLengthSeconds() const noexcept;
    /** Whole decoded duration, the ceiling for trimming. */
    double getSourceLengthSeconds() const noexcept;

    /** Slides which part of the source plays, leaving the clip where it is. */
    void setSourceOffsetSeconds(double seconds) noexcept;

    /** Drags the left edge: keeps the audio still and moves where it starts. */
    void trimStart(double newStartBeat, double tempoBpm) noexcept;
    /** Drags the right edge. */
    void trimEnd(double newEndBeat, double tempoBpm) noexcept;

    /** When warped the clip keeps a fixed length in beats and follows the tempo
        by resampling, so its pitch moves with it. Off, it plays at its own rate.
    */
    void setWarpEnabled(bool shouldWarp) noexcept;
    bool isWarpEnabled() const noexcept;
    void setOriginalTempo(double tempoBpm) noexcept;
    double getOriginalTempo() const noexcept;

    /** How much of the timeline this clip covers at the given tempo. */
    double getLengthBeats(double tempoBpm) const noexcept;

    float getGain() const noexcept;
    void setGain(float newGain) noexcept;

    /** Gain ramps at the clip's edges, in source seconds - the same unit as
        the trim, so one clip measures its length one way only.

        A recorded take almost always starts and ends mid-waveform, and cutting
        straight to it clicks. These are applied while playing rather than
        written into the samples, so they cost nothing to change and the
        recording on disk is never touched.
    */
    double getFadeInSeconds() const noexcept;
    void setFadeInSeconds(double seconds) noexcept;
    double getFadeOutSeconds() const noexcept;
    void setFadeOutSeconds(double seconds) noexcept;
    /** A fade long enough to swallow a click, for the menu's default. */
    static constexpr double defaultFadeSeconds = 0.01;
    void setMuted(bool shouldMute) noexcept;
    bool isMuted() const noexcept;

    /** A second clip over the same samples - no reload, no extra memory. */
    std::unique_ptr<AudioClip> duplicate() const;
    /** Splits at `beat`, returning the right hand piece. Null if the cut misses. */
    std::unique_ptr<AudioClip> splitAt(double beat, double tempoBpm);
    /** Whether splitAt would accept this cut. The slice tool asks before it
        draws its preview line, so the rule lives in one place.
    */
    bool canSplitAt(double beat, double tempoBpm) const;

    /** Adds this clip's contribution for one block. */
    void addToBuffer(juce::AudioBuffer<float>& destination,
                     double blockStartBeat,
                     double tempoBpm,
                     double sampleRate) const;

    /** Normalised peaks for drawing, one per bucket, over the whole source. */
    const std::vector<float>& getPeaks() const noexcept;
    /** Fraction of the source the clip currently shows, for drawing the trim. */
    double getTrimStartFraction() const noexcept;
    double getTrimEndFraction() const noexcept;

    /** Everything the project file needs to rebuild this clip. */
    juce::var toVar() const;
    /** Applies saved placement, trim and warp to a freshly loaded clip. */
    void applyStateFromVar(const juce::var& value);
    static juce::File getFileFromVar(const juce::var& value);

    /** Shortest a clip may be trimmed to, in seconds. */
    static constexpr double minimumLengthSeconds = 0.02;

private:
    AudioClip() = default;
    void buildPeaks();
    /** Re-applies the fade limits after the clip's length has changed. */
    void clampFadesToLength() noexcept;
    double getPlaybackRate(double tempoBpm) const noexcept;

    juce::String name;
    juce::File sourceFile;
    // Shared so slicing and duplicating a clip costs nothing.
    std::shared_ptr<const juce::AudioBuffer<float>> samples;
    std::shared_ptr<const std::vector<float>> peaks;
    double clipSampleRate = 44100.0;
    double sourceLengthSeconds = 0.0;

    std::atomic<double> startBeat { 0.0 };
    std::atomic<double> sourceOffsetSeconds { 0.0 };
    std::atomic<double> playLengthSeconds { 0.0 };
    std::atomic<double> originalTempo { 120.0 };
    std::atomic<bool> warpEnabled { true };
    std::atomic<bool> muted { false };
    std::atomic<float> gain { 1.0f };
    std::atomic<double> fadeInSeconds { 0.0 };
    std::atomic<double> fadeOutSeconds { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioClip)
};

} // namespace djr
