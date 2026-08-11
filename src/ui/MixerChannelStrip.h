#pragma once

#include "audio/MasterBus.h"
#include "audio/Track.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

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
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    bool isMaster() const noexcept;

    /** Highlights this strip as the one the rest of the session is following. */
    void setSelected(bool shouldBeSelected);

    /** Fired when the strip is clicked anywhere, so the owner can select it. */
    std::function<void()> onSelected;

private:
    void timerCallback() override;

    /** The fader/meter block absorbs whatever height the strip has spare. */
    int getFaderRowHeight() const;
    juce::Rectangle<int> getFaderColumn() const;
    juce::Rectangle<int> getFaderHandle() const;
    juce::Rectangle<int> getMeterArea() const;
    juce::Rectangle<int> getPanKnobArea() const;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannelStrip)
};

} // namespace djr
