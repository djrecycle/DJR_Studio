#pragma once

#include "audio/Transport.h"
#include "midi/PianoRollModel.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace djr
{

/** 16 step drum grid. The pads are a second view onto the same clip the piano
    roll edits, so toggling one writes a real note and is heard immediately.
*/
class StepSequencerView final : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer
{
public:
    StepSequencerView(PianoRollModel& model, Transport& transport);
    ~StepSequencerView() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    struct Lane
    {
        juce::String name;
        juce::String note;
        int pitch = 36;
        int colourIndex = 0;
    };

public:
    /** Bars the pattern loops for, so the grid never pages past its end. */
    void setPatternLengthBeats(double beats);
    /** Follow the playhead while the transport rolls, like FL's step view. */
    void setFollowPlayhead(bool shouldFollow);

private:
    /** Steps in one page. A page is one bar, so this follows the time
        signature: sixteen in 4/4, twelve in 3/4.
    */
    int getNumSteps() const;
    int getNumPages() const;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    int getPadSize() const;
    int getLaneGap() const;
    juce::Rectangle<int> getPadBounds(int lane, int step) const;
    juce::Rectangle<int> getLaneCardBounds(int lane) const;
    juce::Rectangle<int> getPreviousBarBounds() const;
    juce::Rectangle<int> getNextBarBounds() const;
    int getCurrentStep() const;
    /** Beat the first pad of the visible bar sits on. */
    double getPageStartBeat() const noexcept;
    bool isStepOn(const juce::Array<MidiNote>& notes, int lane, int step) const;
    void toggleStep(int lane, int step);
    void setPage(int newPage);

    PianoRollModel& model;
    Transport& transport;

    std::array<Lane, 4> lanes { {
        { "Kick",  "C1 - note 36",  36, 0 },
        { "Snare", "D1 - note 38",  38, 1 },
        { "Hat",   "F#1 - note 42", 42, 2 },
        { "Clap",  "D#1 - note 39", 39, 3 }
    } };

    int lastPaintedStep = -1;
    int page = 0;
    double patternLengthBeats = 4.0;
    bool followPlayhead = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StepSequencerView)
};

} // namespace djr
