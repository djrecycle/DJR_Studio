#pragma once

#include "PianoRollView.h"

#include "midi/PianoRollModel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Velocity bars underneath the piano roll. Shares the roll's horizontal
    geometry so bars line up with their notes.
*/
class VelocityLane final : public juce::Component,
                           private juce::ChangeListener
{
public:
    VelocityLane(PianoRollModel& model, const PianoRollView& pianoRoll);
    ~VelocityLane() override;

    /** Brackets a drag: true on mouse down, false on mouse up, so a whole drag
        collapses into one undo step.
    */
    std::function<void(bool)> onEditGesture;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    int barAtPosition(juce::Point<int> position) const;
    juce::Rectangle<int> barBounds(const MidiNote& note) const;
    void applyVelocityFromY(int noteIndex, int y);

    PianoRollModel& model;
    const PianoRollView& pianoRoll;
    int draggedNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityLane)
};

} // namespace djr
