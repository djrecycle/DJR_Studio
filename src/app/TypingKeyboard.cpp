#include "TypingKeyboard.h"

namespace djr
{

namespace
{
    /** 60 Hz: fast enough that a note starts when the key does, cheap enough
        that polling three dozen keys costs nothing.
    */
    constexpr int pollHz = 60;

    /** FL's layout, and the one every tracker uses: the letter rows are two
        octaves of white keys, with the sharps sitting on the row above where a
        piano would put the black keys.
    */
    constexpr std::pair<char, int> flStudioKeys[] = {
        // Lower octave, white keys.
        { 'Z', 0 }, { 'X', 2 }, { 'C', 4 }, { 'V', 5 }, { 'B', 7 }, { 'N', 9 }, { 'M', 11 },
        { ',', 12 }, { '.', 14 }, { '/', 16 },
        // Lower octave, black keys.
        { 'S', 1 }, { 'D', 3 }, { 'G', 6 }, { 'H', 8 }, { 'J', 10 }, { 'L', 13 }, { ';', 15 },
        // Upper octave, white keys.
        { 'Q', 12 }, { 'W', 14 }, { 'E', 16 }, { 'R', 17 }, { 'T', 19 }, { 'Y', 21 }, { 'U', 23 },
        { 'I', 24 }, { 'O', 26 }, { 'P', 28 }, { '[', 29 }, { ']', 31 },
        // Upper octave, black keys.
        { '2', 13 }, { '3', 15 }, { '5', 18 }, { '6', 20 }, { '7', 22 },
        { '9', 25 }, { '0', 27 }, { '=', 30 },
    };

    /** The one Rangga asked for: the home row reads straight up the scale.
        Easier to find by touch, at the cost of only covering one octave and a
        bit rather than two.
    */
    constexpr std::pair<char, int> simpleKeys[] = {
        // White keys, straight up the scale: A S D F G H J = C D E F G A B.
        { 'A', 0 }, { 'S', 2 }, { 'D', 4 }, { 'F', 5 }, { 'G', 7 }, { 'H', 9 }, { 'J', 11 },
        { 'K', 12 }, { 'L', 14 }, { ';', 16 },
        // Sharps on the row above, where the black keys would sit.
        { 'W', 1 }, { 'E', 3 }, { 'T', 6 }, { 'Y', 8 }, { 'U', 10 }, { 'O', 13 }, { 'P', 15 },
    };
}

TypingKeyboard::TypingKeyboard()
{
    mappings = getMappings();
    startTimerHz(pollHz);
}

TypingKeyboard::~TypingKeyboard()
{
    stopTimer();
}

void TypingKeyboard::setEnabled(bool shouldBeEnabled)
{
    if (enabled == shouldBeEnabled)
        return;

    enabled = shouldBeEnabled;

    if (! enabled)
        releaseAllNotes();
}

bool TypingKeyboard::isEnabled() const noexcept
{
    return enabled;
}

void TypingKeyboard::setKeymap(Keymap keymap)
{
    if (currentKeymap == keymap)
        return;

    // Held notes belong to the old map; leaving them sounding would strand a
    // note nothing can now switch off.
    releaseAllNotes();
    currentKeymap = keymap;
    mappings = getMappings();
}

TypingKeyboard::Keymap TypingKeyboard::getKeymap() const noexcept
{
    return currentKeymap;
}

juce::String TypingKeyboard::getKeymapName(Keymap keymap)
{
    return keymap == Keymap::flStudio ? "FL Studio (Z X C V / Q W E R)"
                                      : "Sederhana (A S D F = C D E F)";
}

void TypingKeyboard::setBaseOctave(int octave)
{
    const auto clamped = juce::jlimit(minOctave, maxOctave, octave);

    if (baseOctave == clamped)
        return;

    releaseAllNotes();
    baseOctave = clamped;
}

int TypingKeyboard::getBaseOctave() const noexcept
{
    return baseOctave;
}

void TypingKeyboard::setVelocity(float newVelocity)
{
    velocity = juce::jlimit(0.0f, 1.0f, newVelocity);
}

float TypingKeyboard::getVelocity() const noexcept
{
    return velocity;
}

juce::Array<TypingKeyboard::KeyMapping> TypingKeyboard::getMappings() const
{
    juce::Array<KeyMapping> result;

    const auto add = [&result] (const auto& table)
    {
        for (const auto& entry : table)
            result.add({ static_cast<int>(entry.first), entry.second });
    };

    if (currentKeymap == Keymap::flStudio)
        add(flStudioKeys);
    else
        add(simpleKeys);

    return result;
}

bool TypingKeyboard::isKeyMapped(const juce::KeyPress& key) const
{
    if (! enabled)
        return false;

    const auto modifiers = key.getModifiers();

    if (modifiers.isCtrlDown() || modifiers.isCommandDown() || modifiers.isAltDown())
        return false;

    // Two codes because JUCE reports punctuation and letters differently
    // depending on the layout: getKeyCode() is the physical key, the text
    // character is what it typed. The maps are written in uppercase.
    const auto code = key.getKeyCode();
    const auto character = static_cast<int>(
        juce::CharacterFunctions::toUpperCase(key.getTextCharacter()));

    for (const auto& mapping : mappings)
        if (mapping.keyCode == code || mapping.keyCode == character)
            return true;

    return false;
}

bool TypingKeyboard::shouldListenToKeys()
{
    // Typing a track name must not also play a tune.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr
            || focused->findParentComponentOfClass<juce::TextEditor>() != nullptr)
            return false;

    // A popup menu or an alert window has the keyboard for its own letter
    // navigation while it is up.
    if (juce::Component::getCurrentlyModalComponent() != nullptr)
        return false;

    // Another application has the keyboard; its keys are not ours to read.
    return juce::Process::isForegroundProcess();
}

void TypingKeyboard::timerCallback()
{
    if (! enabled || ! shouldListenToKeys())
    {
        releaseAllNotes();
        return;
    }

    // Polling reads the raw key state, which knows nothing about modifiers, so
    // without this ctrl+S saved the project and played a C sharp on the way.
    //
    // The cached flags rather than getCurrentModifiersRealtime(): that one costs
    // an X round trip, overwrites JUCE's global modifier state and marks it
    // stale on every call, and does not even read alt - it only refreshes shift
    // and ctrl from the pointer query. Sixty of those a second buys nothing the
    // cache does not already have, since key events keep the cache current and
    // shouldListenToKeys() has established the events are reaching us.
    const auto modifiers = juce::ModifierKeys::getCurrentModifiers();

    if (modifiers.isCtrlDown() || modifiers.isCommandDown() || modifiers.isAltDown())
    {
        releaseAllNotes();
        return;
    }

    const auto lowestNote = juce::jlimit(0, 127, baseOctave * 12);

    // What should be sounding, worked out from the keys actually held down.
    std::array<bool, 128> wanted {};

    for (const auto& mapping : mappings)
    {
        const auto note = lowestNote + mapping.semitone;

        if (! juce::isPositiveAndBelow(note, 128))
            continue;

        if (juce::KeyPress::isKeyCurrentlyDown(mapping.keyCode))
            wanted[static_cast<size_t>(note)] = true;
    }

    for (int note = 0; note < 128; ++note)
    {
        const auto index = static_cast<size_t>(note);

        if (wanted[index] == notesDown[index])
            continue;

        notesDown[index] = wanted[index];

        if (wanted[index])
        {
            if (onNoteOn)
                onNoteOn(note, velocity);
        }
        else if (onNoteOff)
        {
            onNoteOff(note);
        }
    }
}

void TypingKeyboard::releaseAllNotes()
{
    for (int note = 0; note < 128; ++note)
    {
        if (! notesDown[static_cast<size_t>(note)])
            continue;

        notesDown[static_cast<size_t>(note)] = false;

        if (onNoteOff)
            onNoteOff(note);
    }
}

} // namespace djr
