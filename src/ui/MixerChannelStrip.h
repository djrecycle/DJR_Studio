#pragma once

#include "audio/MasterBus.h"
#include "audio/Mixer.h"
#include "audio/Track.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** One mixer channel: pan knob, its insert effects, fader, stereo meters, its
    sends, mute/solo/arm and the level readout. Also renders the master bus when constructed with one.
*/
class MixerChannelStrip final : public juce::Component,
                                public juce::TooltipClient,
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
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    juce::String getTooltip() override;

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

    /** Double clicking the name opens the channel window, the way double
        clicking a channel does in FL. Never fired for the master strip: it has
        no track, and so no channel.
    */
    std::function<void()> onOpenChannel;

    /** "New bus" from a send slot's menu: the owner adds a bus and points
        `slot` at it. Called after the menu has closed; the owner rebuilds the
        strips, this one included, so it must not call back into the strip.
    */
    std::function<void(int slot)> onCreateBusForSend;

    /** "+ FX": the owner offers the plugin library and loads the choice as
        an insert on this strip's track.
    */
    std::function<void()> onAddInsert;
    /** Clicking an insert's name opens that plugin's window. */
    std::function<void(int insertIndex)> onOpenInsert;
    /** After an insert is bypassed, moved or removed from this strip, so other
        views of the chain (the Insert Chain panel) can catch up.
    */
    std::function<void()> onInsertsChanged;
    /** "... N more": the chain is longer than the strip shows. */
    std::function<void()> onShowInsertChain;

    /** How many insert rows a strip shows before folding the rest into a
        "... N more" row.
    */
    static constexpr int maxVisibleInserts = 4;
    /** Row markers in what getInsertRows() lists, besides insert indices. */
    static constexpr int addInsertRow = -1;
    static constexpr int moreInsertsRow = -2;
    /** What the insert section lists for a chain of `pluginCount`: the
        inserts (the first maxVisibleInserts - 1 and a "more" row past
        maxVisibleInserts), then "+ FX".
    */
    static std::vector<int> insertRowsFor(int pluginCount);

    /** Send level limits: 1.5 is +3.5 dB, the same headroom as the fader. */
    static constexpr float maxSendLevel = 1.5f;
    /** Where a send row of `width` pixels fills up to for `level`, 0..1. */
    static float sendFillRatio(float level) noexcept;
    /** A drag of `deltaPixels` from `startLevel`: the full range is
        `pixelsForFullRange` (four times finer with `fine`), clamped to it.
    */
    static float sendLevelAfterDrag(float startLevel, int deltaPixels, bool fine) noexcept;
    /** One mouse-wheel notch: half a dB up or down, from silence up to unity. */
    static float sendLevelAfterWheel(float level, float wheelDelta) noexcept;

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
    /** Name and pan rows plus the insert section: where the fader starts. */
    int getHeightAboveFader() const;
    std::vector<int> getInsertRows() const;
    int getInsertSectionHeight() const;
    juce::Rectangle<int> getInsertRowBounds(int rowIndex) const;
    /** Index into getInsertRows() under `position`, or -1. */
    int getInsertRowAt(juce::Point<int> position) const;
    /** The on/off dot at the left of an insert row. */
    static juce::Rectangle<int> getBypassDotArea(juce::Rectangle<int> row);
    void showInsertMenu(int insertIndex);
    void notifyInsertsChanged();

    /** Slots that actually point somewhere, in slot order. */
    std::vector<int> getAssignedSends() const;
    /** What the send section lists, top to bottom: each assigned slot, then
        the first free slot as the "+ Send" row while one is left. Empty for
        the master strip.
    */
    std::vector<int> getSendRows() const;
    int getSendSectionHeight() const;
    juce::Rectangle<int> getSendRowBounds(int rowIndex) const;
    /** Index into getSendRows() under `position`, or -1. */
    int getSendRowAt(juce::Point<int> position) const;
    /** Destination, pre/post fader and remove, for an assigned slot. */
    void showSendMenu(int slot);
    /** Pick a bus (or make one) for the free slot behind "+ Send". */
    void showAddSendMenu(int slot);
    void applySend(int slot, const TrackSend& send);

    /** The fader/meter block absorbs whatever height the strip has spare. */
    int getFaderRowHeight() const;
    juce::Rectangle<int> getFaderColumn() const;
    juce::Rectangle<int> getFaderHandle() const;
    juce::Rectangle<int> getMeterArea() const;
    juce::Rectangle<int> getPanKnobArea() const;
    /** The name plate at the top of the strip. */
    juce::Rectangle<int> getNameRowBounds() const;
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
    juce::Rectangle<int> getRoutingBounds() const;
    juce::String getOutputDestinationName() const;

    Track* track = nullptr;
    MasterBus* masterBus = nullptr;
    juce::Colour colour;

    float smoothedLeft = 0.0f;
    float smoothedRight = 0.0f;
    /** Everything but the meters, as last painted - see timerCallback. */
    juce::String lastStateKey;
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
    float sendDragStartLevel = 0.0f;
    juce::Point<int> hoverPosition { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerChannelStrip)
};

} // namespace djr
