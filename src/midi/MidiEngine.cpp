#include "MidiEngine.h"

#include "utils/Logger.h"

namespace djr
{

namespace
{
    constexpr double minimumNoteLengthBeats = 0.0625;
}

MidiEngine::MidiEngine(juce::AudioDeviceManager& manager, Transport& transportToUse)
    : deviceManager(manager), transport(transportToUse)
{
    noteStartBeats.fill(0.0);
    noteVelocities.fill(0.0f);
    noteHeld.fill(false);
}

MidiEngine::~MidiEngine()
{
    if (activeInputIdentifier.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(activeInputIdentifier, this);
        deviceManager.setMidiInputDeviceEnabled(activeInputIdentifier, false);
    }
}

juce::Array<juce::MidiDeviceInfo> MidiEngine::getAvailableInputs() const
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiEngine::setInputDevice(const juce::String& identifier)
{
    if (activeInputIdentifier == identifier)
        return true;

    if (activeInputIdentifier.isNotEmpty())
    {
        deviceManager.removeMidiInputDeviceCallback(activeInputIdentifier, this);
        deviceManager.setMidiInputDeviceEnabled(activeInputIdentifier, false);
    }

    activeInputIdentifier.clear();

    if (identifier.isEmpty())
        return true;

    const auto devices = getAvailableInputs();
    const auto exists = std::any_of(devices.begin(), devices.end(),
                                    [&identifier] (const auto& device) { return device.identifier == identifier; });

    if (! exists)
        return false;

    deviceManager.setMidiInputDeviceEnabled(identifier, true);
    deviceManager.addMidiInputDeviceCallback(identifier, this);
    activeInputIdentifier = identifier;
    Logger::write("MIDI input selected: " + identifier);
    return true;
}

juce::String MidiEngine::getCurrentInputIdentifier() const
{
    return activeInputIdentifier;
}

int MidiEngine::getLastNote() const noexcept
{
    return lastNote.load(std::memory_order_acquire);
}

float MidiEngine::getLastVelocity() const noexcept
{
    return lastVelocity.load(std::memory_order_acquire);
}

void MidiEngine::postLiveMessage(const juce::MidiMessage& message)
{
    handleMessage(message);
}

void MidiEngine::prepareLiveMidi(double sampleRate)
{
    if (sampleRate <= 0.0)
        return;

    collector.reset(sampleRate);
    collectorReady.store(true, std::memory_order_release);
}

void MidiEngine::fetchLiveMidi(juce::MidiBuffer& destination, int numSamples)
{
    if (! collectorReady.load(std::memory_order_acquire))
        return;

    collector.removeNextBlockOfMessages(destination, numSamples);
}

void MidiEngine::setRecordingArmed(bool shouldRecord) noexcept
{
    if (recordingArmed.exchange(shouldRecord, std::memory_order_acq_rel) == shouldRecord)
        return;

    if (! shouldRecord)
    {
        // Close anything still held, so a key released after the take cannot
        // bleed into the next one.
        const juce::ScopedLock scoped(recordLock);

        for (int pitch = 0; pitch < 128; ++pitch)
            if (noteHeld[static_cast<size_t>(pitch)])
                finishRecordedNote(pitch);
    }
}

bool MidiEngine::isRecordingArmed() const noexcept
{
    return recordingArmed.load(std::memory_order_acquire);
}

juce::Array<MidiNote> MidiEngine::takeRecordedNotes()
{
    const juce::ScopedLock scoped(recordLock);
    auto taken = recordedNotes;
    recordedNotes.clearQuick();
    return taken;
}

void MidiEngine::discardRecordedNotes()
{
    const juce::ScopedLock scoped(recordLock);
    recordedNotes.clearQuick();
    noteHeld.fill(false);
}

void MidiEngine::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message)
{
    juce::ignoreUnused(source);
    handleMessage(message);
}

void MidiEngine::handleMessage(const juce::MidiMessage& message)
{
    // The collector timestamps against the audio clock, so live playing lands
    // at the right sample inside the next block.
    if (collectorReady.load(std::memory_order_acquire))
        collector.addMessageToQueue(message);

    if (message.isNoteOn())
    {
        lastNote.store(message.getNoteNumber(), std::memory_order_release);
        lastVelocity.store(message.getFloatVelocity(), std::memory_order_release);

        if (isRecordingArmed())
        {
            const juce::ScopedLock scoped(recordLock);
            beginRecordedNote(message.getNoteNumber(), message.getFloatVelocity());
        }

        sendChangeMessage();
        return;
    }

    if (message.isNoteOff())
    {
        lastVelocity.store(0.0f, std::memory_order_release);

        if (isRecordingArmed())
        {
            const juce::ScopedLock scoped(recordLock);
            finishRecordedNote(message.getNoteNumber());
        }

        sendChangeMessage();
    }
}

void MidiEngine::beginRecordedNote(int pitch, float velocity)
{
    if (! juce::isPositiveAndBelow(pitch, 128))
        return;

    const auto index = static_cast<size_t>(pitch);
    noteStartBeats[index] = juce::jmax(0.0, transport.getPositionBeats());
    noteVelocities[index] = velocity;
    noteHeld[index] = true;
}

void MidiEngine::finishRecordedNote(int pitch)
{
    if (! juce::isPositiveAndBelow(pitch, 128))
        return;

    const auto index = static_cast<size_t>(pitch);

    if (! noteHeld[index])
        return;

    noteHeld[index] = false;

    MidiNote note;
    note.pitch = pitch;
    note.velocity = noteVelocities[index];
    note.startBeat = noteStartBeats[index];
    note.lengthBeats = juce::jmax(minimumNoteLengthBeats,
                                  transport.getPositionBeats() - noteStartBeats[index]);

    recordedNotes.add(note);
}

} // namespace djr
