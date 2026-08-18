#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace djr
{

/** Plays notes from the computer keyboard, wherever the focus happens to be.

    JUCE's MidiKeyboardComponent can already do this, but only while it holds
    keyboard focus - and the playlist, the piano roll and the window itself all
    want focus too, so in practice the little keyboard strip almost never had
    it and typing did nothing. A DAW's typing keyboard is expected to work no
    matter which panel was clicked last, so this polls the key states directly
    instead of waiting to be sent key events.

    Polling also solves note-off: keyPressed only ever reports a key going down,
    and repeats while it is held.
*/
class TypingKeyboard final : private juce::Timer
{
public:
    enum class Keymap
    {
        /** Two rows, two octaves, the layout FL and the trackers use:
            Z X C V B N M with S D G H J above, Q W E R T Y U with 2 3 5 6 7.
        */
        flStudio,
        /** The letter row reads straight up the scale: A S D F G H J = C D E F
            G A B, with the sharps on the row above.
        */
        simple
    };

    TypingKeyboard();
    ~TypingKeyboard() override;

    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const noexcept;

    void setKeymap(Keymap keymap);
    Keymap getKeymap() const noexcept;
    static juce::String getKeymapName(Keymap keymap);

    /** Octave of the lowest key in the map. Shifted by the octave buttons. */
    void setBaseOctave(int octave);
    int getBaseOctave() const noexcept;
    static constexpr int minOctave = 0;
    static constexpr int maxOctave = 8;

    void setVelocity(float newVelocity);
    float getVelocity() const noexcept;

    /** Fired for every note the typing keyboard starts and stops. */
    std::function<void(int /*note*/, float /*velocity*/)> onNoteOn;
    std::function<void(int /*note*/)> onNoteOff;

    /** Stops everything currently sounding - when the map changes, when focus
        moves to a text field, or when the feature is switched off.
    */
    void releaseAllNotes();

private:
    struct KeyMapping
    {
        int keyCode = 0;
        /** Semitones above the base octave's C. */
        int semitone = 0;
    };

    void timerCallback() override;
    /** False while a text field has focus, so typing a track name does not also
        play a tune, and while the app is not the active window.
    */
    static bool shouldListenToKeys();
    juce::Array<KeyMapping> getMappings() const;

    juce::Array<KeyMapping> mappings;
    /** Notes currently sounding, indexed by MIDI note. */
    std::array<bool, 128> notesDown {};
    Keymap currentKeymap = Keymap::flStudio;
    int baseOctave = 4;
    float velocity = 0.8f;
    bool enabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TypingKeyboard)
};

} // namespace djr
