#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** The entries of FL's snap menu, in menu order.

    Line, Cell and none have no fixed length: Line follows whatever grid the view
    is currently drawing, Cell means one playlist bar or one piano roll step, and
    none switches snapping off. Everything else is a plain beat value.

    This is an enum rather than a beat number because two entries can share a
    length - Step and 1/4 beat are both a quarter of a beat - and the menu still
    has to show a tick against the one that was actually chosen.
*/
enum class SnapUnit
{
    line,
    cell,
    none,
    sixthStep,
    quarterStep,
    thirdStep,
    halfStep,
    step,
    sixthBeat,
    quarterBeat,
    thirdBeat,
    halfBeat,
    beat,
    bar
};

/** One step of the step sequencer: a sixteenth note, a quarter of a beat. */
constexpr double stepBeats = 0.25;

inline const SnapUnit* getSnapMenuOrder(int& countOut) noexcept
{
    static const SnapUnit order[] = {
        SnapUnit::line, SnapUnit::cell, SnapUnit::none,
        SnapUnit::sixthStep, SnapUnit::quarterStep, SnapUnit::thirdStep,
        SnapUnit::halfStep, SnapUnit::step,
        SnapUnit::sixthBeat, SnapUnit::quarterBeat, SnapUnit::thirdBeat,
        SnapUnit::halfBeat, SnapUnit::beat, SnapUnit::bar
    };

    countOut = static_cast<int>(std::size(order));
    return order;
}

inline juce::String getSnapUnitLabel(SnapUnit unit)
{
    switch (unit)
    {
        case SnapUnit::line:        return "Line";
        case SnapUnit::cell:        return "Cell";
        case SnapUnit::none:        return "(none)";
        case SnapUnit::sixthStep:   return "1/6 step";
        case SnapUnit::quarterStep: return "1/4 step";
        case SnapUnit::thirdStep:   return "1/3 step";
        case SnapUnit::halfStep:    return "1/2 step";
        case SnapUnit::step:        return "Step";
        case SnapUnit::sixthBeat:   return "1/6 beat";
        case SnapUnit::quarterBeat: return "1/4 beat";
        case SnapUnit::thirdBeat:   return "1/3 beat";
        case SnapUnit::halfBeat:    return "1/2 beat";
        case SnapUnit::beat:        return "Beat";
        case SnapUnit::bar:         return "Bar";
    }

    return "Step";
}

/** Length in beats, or 0 for the entries a view has to resolve for itself. */
inline double getSnapUnitBeats(SnapUnit unit) noexcept
{
    switch (unit)
    {
        case SnapUnit::sixthStep:   return stepBeats / 6.0;
        case SnapUnit::quarterStep: return stepBeats / 4.0;
        case SnapUnit::thirdStep:   return stepBeats / 3.0;
        case SnapUnit::halfStep:    return stepBeats / 2.0;
        case SnapUnit::step:        return stepBeats;
        case SnapUnit::sixthBeat:   return 1.0 / 6.0;
        case SnapUnit::quarterBeat: return 0.25;
        case SnapUnit::thirdBeat:   return 1.0 / 3.0;
        case SnapUnit::halfBeat:    return 0.5;
        case SnapUnit::beat:        return 1.0;
        case SnapUnit::bar:         return 4.0;

        case SnapUnit::line:
        case SnapUnit::cell:
        case SnapUnit::none:
        default:
            return 0.0;
    }
}

} // namespace djr
