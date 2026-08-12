#pragma once

#include "MidiNote.h"
#include "audio/LiveMidiSource.h"
#include "audio/Transport.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>

namespace djr
{

/** Owns the MIDI input device, funnels what is played into the audio graph, and
    captures notes into clips while the transport is recording.
*/
class MidiEngine final : public LiveMidiSource,
                         private juce::MidiInputCallback,
                         public juce::ChangeBroadcaster
{
public:
    MidiEngine(juce::AudioDeviceManager& manager, Transport& transportToUse);
    ~MidiEngine() override;

    juce::Array<juce::MidiDeviceInfo> getAvailableInputs() const;
    bool setInputDevice(const juce::String& identifier);
    juce::String getCurrentInputIdentifier() const;

    int getLastNote() const noexcept;
    float getLastVelocity() const noexcept;

    /** The on-screen keyboard feeds the graph through here too. */
    void postLiveMessage(const juce::MidiMessage& message);

    void prepareLiveMidi(double sampleRate) override;
    void fetchLiveMidi(juce::MidiBuffer& destination, int numSamples) override;

    /** While armed, completed notes are queued for the piano roll to collect. */
    void setRecordingArmed(bool shouldRecord) noexcept;
    bool isRecordingArmed() const noexcept;
    juce::Array<MidiNote> takeRecordedNotes();
    void discardRecordedNotes();

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void handleMessage(const juce::MidiMessage& message);
    void beginRecordedNote(int pitch, float velocity);
    void finishRecordedNote(int pitch);

    juce::AudioDeviceManager& deviceManager;
    Transport& transport;
    juce::MidiMessageCollector collector;
    juce::String activeInputIdentifier;

    std::atomic<int> lastNote { -1 };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<bool> recordingArmed { false };
    std::atomic<bool> collectorReady { false };

    juce::CriticalSection recordLock;
    juce::Array<MidiNote> recordedNotes;
    std::array<double, 128> noteStartBeats {};
    std::array<float, 128> noteVelocities {};
    std::array<bool, 128> noteHeld {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiEngine)
};

} // namespace djr
