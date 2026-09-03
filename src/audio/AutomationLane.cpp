#include "AutomationLane.h"

#include <algorithm>
#include <cmath>

namespace djr
{

namespace
{
    /** Two points closer than this are the same point as far as editing goes. */
    constexpr double beatEpsilon = 1.0e-6;

    double clampNormalised(double value) noexcept
    {
        return juce::jlimit(0.0, 1.0, value);
    }
}

// AutomationTarget ----------------------------------------------------------

double AutomationTarget::toParameterValue(double normalised) const noexcept
{
    const auto clamped = clampNormalised(normalised);

    switch (kind)
    {
        case Kind::trackVolume:     return clamped * 2.0;
        case Kind::trackPan:        return clamped * 2.0 - 1.0;
        case Kind::pluginParameter: return clamped;
    }

    return clamped;
}

double AutomationTarget::fromParameterValue(double value) const noexcept
{
    switch (kind)
    {
        case Kind::trackVolume:     return clampNormalised(value * 0.5);
        case Kind::trackPan:        return clampNormalised((value + 1.0) * 0.5);
        case Kind::pluginParameter: return clampNormalised(value);
    }

    return clampNormalised(value);
}

juce::String AutomationTarget::describeValue(double normalised) const
{
    const auto value = toParameterValue(normalised);

    switch (kind)
    {
        case Kind::trackVolume:
            // Silence has no decibel value to print, so say so instead of -inf.
            return value <= 1.0e-4 ? juce::String("-inf dB")
                                   : juce::String(20.0 * std::log10(value), 1) + " dB";

        case Kind::trackPan:
            if (std::abs(value) < 0.005)
                return "C";

            return (value < 0.0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(value) * 100.0));

        case Kind::pluginParameter:
            break;
    }

    return juce::String(value, 3);
}

juce::String AutomationTarget::describe() const
{
    if (label.isNotEmpty())
        return label;

    switch (kind)
    {
        case Kind::trackVolume: return "Volume";
        case Kind::trackPan:    return "Pan";
        case Kind::pluginParameter:
            return (pluginSlot == instrumentSlot ? juce::String("Inst") : "FX " + juce::String(pluginSlot + 1))
                 + " p" + juce::String(parameterIndex + 1);
    }

    return "Automation";
}

bool AutomationTarget::aimsAtSameParameter(const AutomationTarget& other) const noexcept
{
    if (kind != other.kind)
        return false;

    if (kind != Kind::pluginParameter)
        return true;

    return pluginSlot == other.pluginSlot && parameterIndex == other.parameterIndex;
}

juce::var AutomationTarget::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("kind", static_cast<int>(kind));
    object->setProperty("pluginSlot", pluginSlot);
    object->setProperty("parameterIndex", parameterIndex);
    object->setProperty("label", label);
    return object;
}

AutomationTarget AutomationTarget::fromVar(const juce::var& value)
{
    AutomationTarget target;

    if (auto* object = value.getDynamicObject())
    {
        const auto kindValue = static_cast<int>(object->getProperty("kind"));
        target.kind = juce::isPositiveAndBelow(kindValue, 3) ? static_cast<Kind>(kindValue)
                                                             : Kind::trackVolume;
        target.pluginSlot = static_cast<int>(object->getProperty("pluginSlot"));
        target.parameterIndex = juce::jmax(0, static_cast<int>(object->getProperty("parameterIndex")));
        target.label = object->getProperty("label").toString();
    }

    return target;
}

// AutomationLane ------------------------------------------------------------

AutomationLane::AutomationLane(AutomationTarget targetToDrive)
    : target(std::move(targetToDrive))
{
    points.reserve(64);
}

const AutomationTarget& AutomationLane::getTarget() const noexcept
{
    return target;
}

void AutomationLane::setEnabled(bool shouldBeEnabled) noexcept
{
    enabled.store(shouldBeEnabled, std::memory_order_release);
}

bool AutomationLane::isEnabled() const noexcept
{
    return enabled.load(std::memory_order_acquire);
}

void AutomationLane::setLaneHeight(int height) noexcept
{
    laneHeight.store(juce::jmax(0, height), std::memory_order_release);
}

int AutomationLane::getLaneHeight() const noexcept
{
    return laneHeight.load(std::memory_order_acquire);
}

int AutomationLane::addPoint(double beat, double value)
{
    const auto clampedBeat = juce::jmax(0.0, beat);
    const auto clampedValue = clampNormalised(value);

    const juce::SpinLock::ScopedLockType scoped(pointLock);

    // Dropping a point on one that is already there means "move that one", not
    // "stack two on the same beat" - two points at one beat make the curve
    // depend on vector order, which the user cannot see.
    for (auto& point : points)
    {
        if (std::abs(point.beat - clampedBeat) < beatEpsilon)
        {
            point.value = clampedValue;
            sortAndPublish();
            return static_cast<int>(std::distance(points.data(), &point));
        }
    }

    if (static_cast<int>(points.size()) >= maxPoints)
        return -1;

    points.push_back({ clampedBeat, clampedValue, 0.0 });
    sortAndPublish();

    for (int i = 0; i < static_cast<int>(points.size()); ++i)
        if (std::abs(points[static_cast<size_t>(i)].beat - clampedBeat) < beatEpsilon)
            return i;

    return -1;
}

int AutomationLane::movePoint(int index, double beat, double value)
{
    const auto clampedBeat = juce::jmax(0.0, beat);
    const auto clampedValue = clampNormalised(value);

    const juce::SpinLock::ScopedLockType scoped(pointLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(points.size())))
        return -1;

    // Dragging past a neighbour re-orders the vector, so the point is tracked by
    // identity rather than by index: the caller gets its new index back.
    points[static_cast<size_t>(index)].beat = clampedBeat;
    points[static_cast<size_t>(index)].value = clampedValue;

    const auto moved = points[static_cast<size_t>(index)];
    sortAndPublish();

    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const auto& point = points[static_cast<size_t>(i)];

        if (std::abs(point.beat - moved.beat) < beatEpsilon && std::abs(point.value - moved.value) < 1.0e-9)
            return i;
    }

    return -1;
}

bool AutomationLane::removePoint(int index)
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(points.size())))
        return false;

    points.erase(points.begin() + index);
    pointCount.store(static_cast<int>(points.size()), std::memory_order_release);
    return true;
}

bool AutomationLane::setPointCurve(int index, double curve)
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(points.size())))
        return false;

    points[static_cast<size_t>(index)].curve = juce::jlimit(-1.0, 1.0, curve);
    return true;
}

void AutomationLane::clearPoints()
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);
    points.clear();
    pointCount.store(0, std::memory_order_release);
}

std::vector<AutomationPoint> AutomationLane::getPoints() const
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);
    return points;
}

void AutomationLane::setPoints(std::vector<AutomationPoint> newPoints)
{
    if (static_cast<int>(newPoints.size()) > maxPoints)
        newPoints.resize(static_cast<size_t>(maxPoints));

    const juce::SpinLock::ScopedLockType scoped(pointLock);
    points = std::move(newPoints);
    sortAndPublish();
}

bool AutomationLane::isEmpty() const noexcept
{
    return getNumPoints() == 0;
}

int AutomationLane::getNumPoints() const noexcept
{
    return pointCount.load(std::memory_order_acquire);
}

bool AutomationLane::getValueAt(double beat, double& valueOut) const noexcept
{
    if (isEmpty() || ! isEnabled())
        return false;

    const juce::SpinLock::ScopedTryLockType scoped(pointLock);

    if (! scoped.isLocked() || points.empty())
        return false;

    valueOut = evaluate(beat);
    return true;
}

bool AutomationLane::sampleRange(double startBeat, double endBeat, double* valuesOut, int count) const noexcept
{
    if (valuesOut == nullptr || count <= 0 || isEmpty() || ! isEnabled())
        return false;

    const juce::SpinLock::ScopedTryLockType scoped(pointLock);

    if (! scoped.isLocked() || points.empty())
        return false;

    if (count == 1)
    {
        valuesOut[0] = evaluate(startBeat);
        return true;
    }

    const auto step = (endBeat - startBeat) / (count - 1);

    for (int i = 0; i < count; ++i)
        valuesOut[i] = evaluate(startBeat + step * i);

    return true;
}

double AutomationLane::getValueAtBeat(double beat) const
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);
    return points.empty() ? 0.0 : evaluate(beat);
}

void AutomationLane::copyPointsInto(std::vector<AutomationPoint>& destination) const
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);
    destination.assign(points.begin(), points.end());
}

double AutomationLane::getLastBeat() const
{
    const juce::SpinLock::ScopedLockType scoped(pointLock);
    return points.empty() ? 0.0 : points.back().beat;
}

AutomationLaneState AutomationLane::captureState() const
{
    AutomationLaneState state;
    state.target = target;
    state.points = getPoints();
    state.enabled = isEnabled();
    state.laneHeight = getLaneHeight();
    return state;
}

void AutomationLane::applyState(const AutomationLaneState& state)
{
    target = state.target;
    setEnabled(state.enabled);
    setLaneHeight(state.laneHeight);
    setPoints(state.points);
}

juce::var AutomationLane::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("target", target.toVar());
    object->setProperty("enabled", isEnabled());
    object->setProperty("laneHeight", getLaneHeight());

    juce::Array<juce::var> pointArray;

    for (const auto& point : getPoints())
    {
        auto* pointObject = new juce::DynamicObject();
        pointObject->setProperty("beat", point.beat);
        pointObject->setProperty("value", point.value);
        pointObject->setProperty("curve", point.curve);
        pointArray.add(pointObject);
    }

    object->setProperty("points", pointArray);
    return object;
}

std::unique_ptr<AutomationLane> AutomationLane::fromVar(const juce::var& value)
{
    auto* object = value.getDynamicObject();

    if (object == nullptr)
        return nullptr;

    auto lane = std::make_unique<AutomationLane>(AutomationTarget::fromVar(object->getProperty("target")));

    // Older files have no "enabled" flag; a lane that was saved was in use.
    const auto enabledValue = object->getProperty("enabled");
    lane->setEnabled(enabledValue.isVoid() ? true : static_cast<bool>(enabledValue));
    lane->setLaneHeight(static_cast<int>(object->getProperty("laneHeight")));

    std::vector<AutomationPoint> restored;

    if (auto* pointArray = object->getProperty("points").getArray())
    {
        restored.reserve(static_cast<size_t>(pointArray->size()));

        for (const auto& entry : *pointArray)
        {
            if (auto* pointObject = entry.getDynamicObject())
                restored.push_back({ static_cast<double>(pointObject->getProperty("beat")),
                                     clampNormalised(static_cast<double>(pointObject->getProperty("value"))),
                                     juce::jlimit(-1.0, 1.0, static_cast<double>(pointObject->getProperty("curve"))) });
        }
    }

    lane->setPoints(std::move(restored));
    return lane;
}

double AutomationLane::interpolate(const AutomationPoint& from, const AutomationPoint& to, double beat) noexcept
{
    const auto span = to.beat - from.beat;

    if (span <= beatEpsilon)
        return to.value;

    const auto t = juce::jlimit(0.0, 1.0, (beat - from.beat) / span);

    // Tension as an exponent: 0 is a straight line, +1 hangs on to the previous
    // value, -1 jumps early. Staying with one monotonic curve means dragging a
    // handle can never make the line double back on itself.
    const auto shaped = std::abs(to.curve) < 1.0e-6
        ? t
        : std::pow(t, std::pow(2.0, juce::jlimit(-1.0, 1.0, to.curve) * 2.0));

    return from.value + (to.value - from.value) * shaped;
}

double AutomationLane::valueAt(const std::vector<AutomationPoint>& points, double beat) noexcept
{
    if (points.empty())
        return 0.0;

    // Outside the drawn range the curve holds, the way a fader left alone does.
    if (beat <= points.front().beat)
        return points.front().value;

    if (beat >= points.back().beat)
        return points.back().value;

    // Points are sorted, so the segment is one binary search away even when a
    // long song has hundreds of them.
    const auto next = std::upper_bound(points.begin(), points.end(), beat,
                                       [] (double wanted, const AutomationPoint& point)
                                       {
                                           return wanted < point.beat;
                                       });

    if (next == points.begin())
        return points.front().value;

    if (next == points.end())
        return points.back().value;

    return interpolate(*(next - 1), *next, beat);
}

double AutomationLane::evaluate(double beat) const noexcept
{
    return valueAt(points, beat);
}

void AutomationLane::sortAndPublish()
{
    std::stable_sort(points.begin(), points.end(),
                     [] (const AutomationPoint& a, const AutomationPoint& b) { return a.beat < b.beat; });

    pointCount.store(static_cast<int>(points.size()), std::memory_order_release);
}

} // namespace djr
