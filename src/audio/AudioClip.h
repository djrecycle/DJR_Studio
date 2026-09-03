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

    /** Builds a clip over audio that is already in memory, for the sample
        editor's capture: there is no file behind it, and there never will be
        until someone exports it.

        The samples are copied, because the caller's buffer keeps being written
        to. Returns nullptr when there is nothing to copy.
    */
    static std::unique_ptr<AudioClip> createFromBuffer(const juce::String& name,
                                                       const juce::AudioBuffer<float>& source,
                                                       int numSamples,
                                                       double sampleRate);

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

    /** How a warped clip follows the tempo. */
    enum class WarpMode
    {
        /** Plays the source faster or slower. Cheap, and the pitch moves with
            the tempo - right for a one-shot, wrong for a melodic loop.
        */
        resample,
        /** Keeps the pitch where it was. Costs a stretched copy of the audio,
            rebuilt whenever the tempo changes.
        */
        stretch
    };

    void setWarpMode(WarpMode mode) noexcept;
    WarpMode getWarpMode() const noexcept;

    /** Builds the stretched copy this clip needs at `tempoBpm`, if it needs one.

        Message thread only, and not cheap: a few-second clip is instant, a long
        take takes about a second. Does nothing when the clip is not warped, not
        in stretch mode, or already holds the copy for this tempo.
    */
    void prepareWarp(double tempoBpm);
    /** True when the prepared copy for `tempoBpm` - and for the pitch the clip
        is set to - is ready to play.
    */
    bool isWarpPrepared(double tempoBpm) const noexcept;

    /** When warped the clip keeps a fixed length in beats and follows the tempo.
        Off, it plays at its own rate.
    */
    void setWarpEnabled(bool shouldWarp) noexcept;
    bool isWarpEnabled() const noexcept;
    void setOriginalTempo(double tempoBpm) noexcept;
    double getOriginalTempo() const noexcept;

    /** How much of the timeline this clip covers at the given tempo. */
    double getLengthBeats(double tempoBpm) const noexcept;

    /** Semitones the clip plays away from what was recorded.

        Unlike the channel's pitch, there are no notes here to rewrite: this is
        audio that has already been played. So it is done the only way audio
        can be pitched - read faster, then stretched back to the length it had -
        using the same two pieces warp already uses. The work happens once, on
        the message thread, like the warped copy; playback only reads it.

        The clip keeps its place and its length on the timeline: only the pitch
        moves.
    */
    void setPitchSemitones(int semitones) noexcept;
    int getPitchSemitones() const noexcept;
    /** Widest the knob goes either way. Past an octave the stretcher smears
        more than the pitch is worth.
    */
    static constexpr int maxPitchSemitones = 12;

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

    // Destructive edits ------------------------------------------------------
    /** An edit that rewrites the audio itself rather than how it is played.

        Gain, fades and warp are settings: they are applied while playing and
        cost nothing to change. These are not - they replace this clip's copy of
        the samples. The file on disk is never touched, and the edits are
        written to the project so reopening rebuilds them from the original.
    */
    enum class SampleEdit
    {
        normalise,  ///< lifts the loudest sample in the region to full scale
        reverse     ///< plays the region backwards
    };

    /** One edit and the part of the source it was applied to, in source
        seconds, so a project can replay it exactly where it happened.
    */
    struct SampleEditRecord
    {
        SampleEdit edit = SampleEdit::normalise;
        double offsetSeconds = 0.0;
        double lengthSeconds = 0.0;
    };

    /** Applies `edit` to the part of the source this clip plays.

        Message thread only, and it copies the audio: clips made by slicing or
        duplicating share their samples, and editing in place would rewrite the
        siblings too. Returns false when the edit would change nothing, so a
        no-op does not land on the undo stack.
    */
    bool applySampleEdit(SampleEdit edit);
    /** Whether applySampleEdit would change anything - normalising audio that
        already reaches full scale does not. Asked before the undo snapshot is
        taken, so a no-op never lands on the stack as a step that undoes nothing.
    */
    bool canApplySampleEdit(SampleEdit edit) const;
    /** What has been done to this clip's audio, oldest first. */
    std::vector<SampleEditRecord> getSampleEdits() const;
    bool hasSampleEdits() const noexcept;

    // Drawing at sample level ------------------------------------------------
    /** How many samples the decoded audio holds. */
    int getNumSourceSamples() const noexcept;
    /** The rate the audio was decoded to, which is the device rate. */
    double getClipSampleRate() const noexcept;
    /** How many channels the decoded audio holds: one or two. */
    int getNumSourceChannels() const noexcept;
    /** Lowest and highest sample in `channel` over a run of source samples.
        Zoomed all the way in that run is one sample, and the two answers are
        the same number - which is the point: it is the sample.

        Message thread only. False when the run falls outside the audio.
    */
    bool getSampleRange(int channel, int firstSample, int numSamples, float& minOut, float& maxOut) const;

    /** Copies a run of source samples out, for writing to a file.

        Message thread only, and it allocates: this is for saving, not for
        playing. False when the run falls outside the audio.
    */
    bool copySamples(int firstSample, int numSamples, juce::AudioBuffer<float>& destination) const;
    /** The part of the source this clip plays, in samples. What an edit touches
        and what an export writes, so the three never disagree.
    */
    void getPlayedRegion(int& firstSampleOut, int& numSamplesOut) const;

    /** Writes the played region to `file`, picking the format from its
        extension and falling back to wav when nothing can write it.

        The samples as they stand, which is what the sample editor draws. Gain
        and the fades are deliberately not written in: those are playback
        settings, and a file that does not match the picture it was exported
        from is a file nobody can reason about.

        Returns the file actually written - the extension may have changed - or
        an empty File, with `errorOut` saying why.
    */
    juce::File exportPlayedRegion(const juce::File& file,
                                  juce::AudioFormatManager& formats,
                                  juce::String& errorOut) const;

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

    /** Whether a buffer the audio thread checked out is still waiting for the
        message thread to actually release it. Exposed for tests: there is no
        other way to see this deferred-release handoff from outside.
    */
    bool hasPendingRelease() const noexcept;

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
    /** The audio half of an edit, without the bookkeeping: no record kept and
        no fades swapped. Reopening a project replays through here, because the
        record and the swapped fades were both already saved.
    */
    bool editSamples(SampleEdit edit, double offsetSeconds, double lengthSeconds);
    /** Re-applies the fade limits after the clip's length has changed. */
    void clampFadesToLength() noexcept;
    double getPlaybackRate(double tempoBpm) const noexcept;
    /** The pitch as a speed: an octave up is twice as fast. */
    double getPitchRatio() const noexcept;
    /** Whether anything needs a copy built ahead of playback. Stretch-warping
        needs one, and so does any pitch other than none.
    */
    bool needsPreparedCopy() const noexcept;
    /** What the copy is stretched by. Above one it comes out shorter. */
    double getPreparedFactor(double tempoBpm) const noexcept;

    juce::String name;
    juce::File sourceFile;
    /** Guards the samples pointer. The audio never changes under the audio
        thread's feet - a destructive edit builds a new buffer and swaps the
        pointer - but the swap itself has to be seen whole.
    */
    mutable juce::SpinLock sampleLock;
    // Shared so slicing and duplicating a clip costs nothing.
    std::shared_ptr<const juce::AudioBuffer<float>> samples;

    /** Where the audio thread parks a buffer it was using instead of letting
        the shared_ptr's destructor - and the free() that might be inside it -
        run there: a destructive edit can drop the message thread's own
        reference at any time, and if the audio thread's copy turns out to be
        the last one, its release has no business happening on that thread.
        Only sweepGarbage(), called from the message thread right before it
        swaps in a new buffer, actually clears these.
    */
    mutable juce::SpinLock garbageLock;
    mutable std::shared_ptr<const juce::AudioBuffer<float>> garbage[4];
    /** Audio thread only. */
    void deferRelease(std::shared_ptr<const juce::AudioBuffer<float>> buffer) const;
    /** Message thread only. */
    void sweepGarbage() const;

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
    std::atomic<WarpMode> warpMode { WarpMode::resample };
    std::atomic<int> pitchSemitones { 0 };
    /** Guarded by sampleLock, like the audio it describes. */
    std::vector<SampleEditRecord> sampleEdits;

    /** The pitch-preserved copy and the tempo it was built for. Guarded like
        the rest of this codebase guards audio-thread reads: a try-lock, and a
        lost race costs one block of the un-stretched path.
    */
    mutable juce::SpinLock stretchLock;
    std::shared_ptr<const juce::AudioBuffer<float>> stretched;
    double stretchedForTempo = 0.0;
    /** The factor the copy was built with, which is what maps a position in
        the source onto a position in the copy.
    */
    double stretchedForFactor = 1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioClip)
};

} // namespace djr
