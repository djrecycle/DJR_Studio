#pragma once

#include "PianoRollView.h"
#include "StepSequencerView.h"
#include "UiControls.h"
#include "VelocityLane.h"

#include "audio/Transport.h"
#include "midi/PianoRollModel.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace djr
{

/** Tabbed editor card holding the piano roll (plus velocity lane) and the step
    sequencer.
*/
class EditorPanel final : public juce::Component,
                          private juce::Button::Listener,
                          private juce::ChangeListener
{
public:
    EditorPanel(PianoRollModel& model, Transport& transport);
    ~EditorPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void showPianoRoll();
    void showStepSequencer();
    bool isPianoRollVisible() const noexcept;

    /** Passes the snap choice down to the piano roll, which resolves it. */
    void setSnapUnit(SnapUnit unit);
    /** Brackets note drags in the roll and the velocity lane for undo. */
    void setNoteGestureCallback(std::function<void(bool)> callback);

    /** The scale highlighted in the piano roll's key rows. */
    void setScale(const Scale& newScale);
    const Scale& getScale() const noexcept;
    /** Fired when the user picks a scale from the roll's own badge menu. */
    void setScaleChangedCallback(std::function<void(Scale)> callback);

    void setVelocityLaneVisible(bool shouldBeVisible);
    bool isVelocityLaneVisible() const noexcept;

    void setViewChangedCallback(std::function<void()> callback);
    /** Shows which pattern the editors are on and lets the user change it. */
    void setActivePattern(int patternIndex);
    void setPatternChangedCallback(std::function<void(int)> callback);
    /** The pattern's display name, shown in place of the "PAT n" default. */
    void setPatternName(const juce::String& name);
    /** Which track's notes are on screen.

        A pattern number says which pattern; it does not say whose. Opening the
        roll from a clip on the playlist is exactly the moment that question
        arises, so the answer is kept next to the pattern badge.
    */
    void setTrackName(const juce::String& name);
    /** Fired when the name badge is clicked, to ask the host for a rename. */
    void setPatternRenameCallback(std::function<void()> callback);
    /** Shows the pattern's loop length; `locked` means it was set by hand. */
    void setPatternLengthBeats(double beats, bool locked);
    void setPatternLengthChangedCallback(std::function<void(double)> callback);

    /** One entry in the track-name badge's picker menu. */
    struct PickableTrack
    {
        int index = -1;
        juce::String name;
    };
    /** Every MIDI track the roll could switch to, in mixer order - queried
        fresh each time the track name badge is clicked, so the menu always
        matches whatever the mixer currently holds.
    */
    void setTrackListProvider(std::function<std::vector<PickableTrack>()> provider);
    /** Which track the badge's list should tick as current. Independent of
        setTrackName: a track can be selected before its name is known to be
        worth showing (an audio track selected mid-switch, say).
    */
    void setSelectedTrackIndex(int trackIndex) noexcept;
    /** Fired with the chosen track's index when a pick is made in that menu. */
    void setTrackChangedCallback(std::function<void(int)> callback);

    /** Notes played on the on-screen keyboard, ready for the live MIDI path. */
    void setKeyboardMessageCallback(std::function<void(const juce::MidiMessage&)> callback);
    /** The state behind the on-screen keyboard. Driving it lights the keys up
        and feeds the live MIDI path in one go, so the typing keyboard does not
        need a second route to the engine.
    */
    juce::MidiKeyboardState& getKeyboardState() noexcept;
    /** Keeps the visible range around `note`, so playing high or low on the
        typing keyboard does not run off the end of the strip.
    */
    void ensureKeyVisible(int note);

private:
    class KeyboardBridge final : public juce::MidiKeyboardState::Listener
    {
    public:
        void handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity) override;
        void handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity) override;

        std::function<void(const juce::MidiMessage&)> onMessage;
    };

    void buttonClicked(juce::Button* button) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refreshTabs();

    PianoRollModel& model;

    struct ToolButton
    {
        PianoRollView::Tool tool;
        Icon icon;
        const char* tooltip;
    };

    void buildToolButtons();
    void refreshToolButtons();
    void refreshFollowButton();

    juce::OwnedArray<IconChipButton> toolButtons;

    UnderlineTab pianoRollTab { "Piano Roll", Icon::notes };
    UnderlineTab stepSequencerTab { "Step Sequencer", Icon::steps };
    HitAreaButton velocityToggle { "Velocity lane" };
    /** Toggles the roll's own follow-the-playhead autoscroll, same idea as the
        playlist's follow button.
    */
    IconChipButton followButton { "Ikuti playhead", Icon::chevronRight };

    PianoRollView pianoRoll;
    VelocityLane velocityLane;
    StepSequencerView stepSequencer;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    KeyboardBridge keyboardBridge;

    juce::Rectangle<int> getPreviousPatternBounds() const;
    juce::Rectangle<int> getNextPatternBounds() const;
    /** The name badge; it widens with the name so long titles stay readable. */
    juce::Rectangle<int> getPatternNameBounds() const;
    juce::Rectangle<int> getPatternLengthBounds() const;
    juce::Rectangle<int> getInstrumentNameBounds() const;
    /** Scale + chord stamp badge, sat in the strip level with the Piano Roll
        tab rather than crammed into one of the roll's own corners.
    */
    juce::Rectangle<int> getHelperBadgeBounds() const;
    void showPatternLengthMenu();
    void showTrackMenu();

    std::function<void()> viewChangedCallback;
    std::function<void(int)> patternChangedCallback;
    std::function<std::vector<PickableTrack>()> trackListProvider;
    std::function<void(int)> trackChangedCallback;
    std::function<void(double)> patternLengthChangedCallback;
    std::function<void()> patternRenameCallback;
    juce::String patternName;
    juce::String trackName;
    int selectedTrackIndex = -1;
    int activePattern = 0;
    double patternLengthBeats = 4.0;
    bool patternLengthLocked = false;
    bool pianoRollVisible = true;
    bool velocityLaneVisible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorPanel)
};

} // namespace djr
