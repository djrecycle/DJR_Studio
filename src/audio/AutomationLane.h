#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace djr
{

/** What an automation lane drives.

    Values travel through the lane normalised to 0..1 so one curve model can
    aim at anything; the target owns the mapping back to the parameter's own
    range. `label` is captured when the lane is made rather than asked of the
    plugin every time, so a project whose plugin is missing still shows what
    the lane was for instead of an empty header.
*/
struct AutomationTarget
{
    enum class Kind
    {
        trackVolume,
        trackPan,
        pluginParameter
    };

    /** Plugin slot meaning "the instrument", which sits outside the insert chain. */
    static constexpr int instrumentSlot = -1;

    Kind kind = Kind::trackVolume;
    /** instrumentSlot for the instrument, otherwise the insert index. */
    int pluginSlot = instrumentSlot;
    int parameterIndex = 0;
    juce::String label;

    /** Track volume runs 0..2, pan -1..1, plugin parameters are already 0..1. */
    double toParameterValue(double normalised) const noexcept;
    double fromParameterValue(double value) const noexcept;
    /** The value as a person reads it, for the lane's readout. */
    juce::String describeValue(double normalised) const;
    /** Short name for the lane header when no label was captured. */
    juce::String describe() const;

    bool aimsAtSameParameter(const AutomationTarget& other) const noexcept;

    juce::var toVar() const;
    static AutomationTarget fromVar(const juce::var& value);
};

/** One breakpoint. `curve` bends the segment *ending* here: positive holds the
    previous value longer, negative races towards this one. Zero is a straight
    line, which is what every point starts as.
*/
struct AutomationPoint
{
    double beat = 0.0;
    double value = 0.5;
    double curve = 0.0;
};

/** Plain copy of a lane, for undo snapshots and project files.

    The lane itself owns a spin lock and so cannot be copied; everything worth
    restoring lives in here instead.
*/
struct AutomationLaneState
{
    AutomationTarget target;
    std::vector<AutomationPoint> points;
    bool enabled = true;
    /** Playlist lane height in pixels. Zero means the default height. */
    int laneHeight = 0;
};

/** A curve over song time driving one parameter.

    Points are kept sorted by beat and guarded by a spin lock, the same deal the
    rest of the engine makes: the message thread locks, the audio thread only
    ever try-locks. A failed try-lock costs one block of the previous value,
    which is the same trade Track already makes for its plugin chain.
*/
class AutomationLane
{
public:
    explicit AutomationLane(AutomationTarget targetToDrive);

    /** More than this and a curve is no longer something a person drew. */
    static constexpr int maxPoints = 1024;

    const AutomationTarget& getTarget() const noexcept;

    /** A bypassed lane keeps its curve but stops writing to the parameter. */
    void setEnabled(bool shouldBeEnabled) noexcept;
    bool isEnabled() const noexcept;

    /** Playlist lane height; zero asks the view for its default. */
    void setLaneHeight(int height) noexcept;
    int getLaneHeight() const noexcept;

    // Editing (message thread) ----------------------------------------------
    /** Adds a point, replacing any that already sits on that exact beat.
        Returns the index it ended up at, or -1 when the lane is full.
    */
    int addPoint(double beat, double value);
    /** Moves a point and re-sorts. Returns where it ended up, or -1. */
    int movePoint(int index, double beat, double value);
    bool removePoint(int index);
    bool setPointCurve(int index, double curve);
    void clearPoints();
    std::vector<AutomationPoint> getPoints() const;
    void setPoints(std::vector<AutomationPoint> newPoints);

    /** Cheap enough for the audio thread: no lock, no allocation. */
    bool isEmpty() const noexcept;
    int getNumPoints() const noexcept;

    /** Value at `beat`, for the audio thread. Answers false and leaves
        `valueOut` alone when the lane is empty, bypassed, or momentarily locked
        by an edit - all three mean "leave the parameter as it is".
    */
    bool getValueAt(double beat, double& valueOut) const noexcept;
    /** Samples the curve at `count` evenly spaced beats from `startBeat` to
        `endBeat` inclusive, under a single try-lock.

        Automation is applied in sub-blocks so a steep curve is followed rather
        than approximated by one straight line per buffer; asking for each point
        separately would take the lock once per sub-block, which is exactly the
        contention this avoids.
    */
    bool sampleRange(double startBeat, double endBeat, double* valuesOut, int count) const noexcept;

    /** Same curve, but it always answers. For drawing and for the tests. */
    double getValueAtBeat(double beat) const;

    /** Copies the points into `destination`, reusing whatever capacity it
        already has. The views keep one snapshot per frame this way instead of
        allocating a fresh vector every time they need to look at the curve.
    */
    void copyPointsInto(std::vector<AutomationPoint>& destination) const;

    /** The curve read straight from a snapshot - no lock, no lane needed. This
        is what drawing uses, so painting never contends with the audio thread.
    */
    static double valueAt(const std::vector<AutomationPoint>& points, double beat) noexcept;

    /** Beat of the last point, so the view knows how far the curve reaches. */
    double getLastBeat() const;

    AutomationLaneState captureState() const;
    void applyState(const AutomationLaneState& state);

    juce::var toVar() const;
    static std::unique_ptr<AutomationLane> fromVar(const juce::var& value);

    /** Interpolates between two points, honouring `to`'s curve. Exposed so the
        view can draw exactly what the audio thread will play.
    */
    static double interpolate(const AutomationPoint& from, const AutomationPoint& to, double beat) noexcept;

private:
    /** The curve read, assuming the caller holds the lock. */
    double evaluate(double beat) const noexcept;
    void sortAndPublish();

    AutomationTarget target;
    mutable juce::SpinLock pointLock;
    std::vector<AutomationPoint> points;
    /** Mirrors points.size() so the audio thread can skip an empty lane without
        touching the lock at all.
    */
    std::atomic<int> pointCount { 0 };
    std::atomic<bool> enabled { true };
    std::atomic<int> laneHeight { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutomationLane)
};

} // namespace djr
