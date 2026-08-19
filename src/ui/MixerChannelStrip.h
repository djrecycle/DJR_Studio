#pragma once

#include "audio/MasterBus.h"
#include "audio/Mixer.h"
#include "audio/Track.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** One mixer channel: pan knob, fader, stereo meters, mute/solo/arm and the
    level readout. Also renders the master bus when constructed with one.
*/
class MixerChannelStrip final : public juce::Component,
                                private juce::Timer
{
public:
    MixerChannelStrip(Track& track, int colourIndex);
    MixerChannelStrip(MasterBus& masterBus);
    ~MixerChannelStrip() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    bool isMaster() const noexcept;

    /** Highlights this strip as the one the rest of the session is following. */
    void setSelected(bool shouldBeSelected);

    /** Fired when the strip is clicked anywhere, so the owner can select it. */
    std::function<void()> onSelected;
    /** Fired after a lane is created or removed from this strip, so the playlist
        can grow the lane and the project can be marked unsaved.
    */
    std::function<void()> onAutomationChanged;

    /** Fired after this strip changes where its audio goes, so the owner can
        re-read the routing everywhere it is shown.
    */
    std::function<void()> onRoutingChanged;

    /** The strip needs the mixer to change routing: only the mixer can see the
        whole graph and refuse a route that would feed back.
    */
    void setMixer(Mixer* mixerToRouteThrough, int trackIndex) noexcept;
    /** How many inputs the audio device has, so the input menu only ever offers
        sockets that actually exist.
    */
    void setDeviceInputCount(int count) noexcept;

private:
    void timerCallback() override;
    /** FL's "create automation clip", on the fader and on the pan knob. */
    void showAutomationMenu(bool forPan);
    /** Where this track's audio goes, and what its sends are doing. */
    void showRoutingMenu();
    /** The row of send level bars, empty when nothing is assigned. */
    juce::Rectangle<int> getSendRowBounds() const;
    juce::Rectangle<int> getSendBarBounds(int slot) const;
    /** Slots that actually point somewhere, in slot order. */
    std::vector<int> getAssignedSends() const;

    /** The fader/meter block absorbs whatever height the strip has spare. */
    int getFaderRowHeight() const;
    juce::Rectangle<int> getFaderColumn() const;
    juce::Rectangle<int> getFaderHandle() const;
    juce::Rectangle<int> getMeterArea() const;
    juce::Rectangle<int> getPanKnobArea() const;
    /** Everything under the fader, in one place, so the send row and the button
        row cannot be measured from opposite ends and land on each other.
    */
    juce::Rectangle<int> getRowsBelowFader() const;
    juce::Rectangle<int> getButtonRow() const;
    juce::Rectangle<int> getMuteBounds() const;
    juce::Rectangle<int> getSoloBounds() const;
    juce::Rectangle<int> getArmBounds() const;

    float getLevel() const;
    void setLevel(float newLevel);
    float getPan() const;
    juce::String getDisplayName() const;

    Track* track = nullptr;
    MasterBus* masterBus = nullptr;
    juce::Colour colour;

    float smoothedLeft = 0.0f;
    float smoothedRight = 0.0f;
    bool draggingFader = false;
    bool draggingPan = false;
    bool selected = false;
    float panDragStart = 0.0f;
    /** Set when the strip belongs to a mixer track, so routing can be changed. */
    Mixer* mixer = nullptr;
    int deviceInputCount = 0;
    int trackIndex = -1;
    /** Send slot being dragged, or -1. */
    int draggingSend = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannelStrip)
};

} // namespace djr
