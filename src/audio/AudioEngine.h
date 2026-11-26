#pragma once

#include "LiveMidiSource.h"
#include "Metronome.h"
#include "Mixer.h"
#include "Transport.h"
#include "recording/Recorder.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>

namespace djr
{

class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::ChangeListener
{
public:
    /** How many input channels this app asks the device for - five stereo
        pairs, so a handful of tracks can each be pointed at their own live
        source (an External App's own JACK ports among them - see
        ExternalAppBridge) at the same time, not just one shared pair. A
        track that has not been given one of its own falls back to reading
        no input at all rather than silently sharing another track's.
    */
    static constexpr int maxInputChannels = 10;

    AudioEngine();
    ~AudioEngine() override;

    bool initialise();
    void shutdown();

    /** Switches to the JACK Audio device type, if not already active, and
        makes sure the switch actually opens with every input channel this
        app asks for (maxInputChannels). JUCE's own
        AudioDeviceManager::setCurrentAudioDeviceType() reopens the new type
        with whatever channel setup was last remembered for *that specific
        type* (AudioDeviceManager.cpp's lastDeviceTypeConfigs), not what
        initialise() originally asked for - the first time this runs, JACK
        has never been the current type before, so that remembered setup is
        just its own scan's stereo default, and the switch would otherwise
        silently reopen with only 2 input channels regardless of
        maxInputChannels.
    */
    void switchToJack();

    //==========================================================================
    // Latency

    /** Buffer sizes offered in Preferences, smallest (lowest latency) first. */
    static const juce::Array<int>& getBufferSizeChoices();

    bool isUsingJack() const;
    /** True when a JACK server - PipeWire's JACK layer counts - can be reached. */
    bool isJackAvailable();

    /** The device's buffer right now. Under JACK this follows the server, so
        it can differ from what the mixer was prepared for until the next
        restart - the callback splits larger blocks to cope (see
        audioDeviceIOCallbackWithContext).
    */
    int getDeviceBufferSize() const;

    /** Asks for a new buffer size. Under JACK this changes the server's period
        (system-wide under PipeWire) and the size found first is put back by
        shutdown(); otherwise the device is reopened at that size. The choice
        is saved and applied again on the next start. Message thread only.
    */
    bool setBufferSize(int frames);

    /** One buffer of output, in milliseconds - what a played note waits for
        before it can be heard, before the driver's own latency.
    */
    double getBufferLatencyMs() const;
    /** Buffer plus the device's reported output latency, in milliseconds. */
    double getOutputLatencyMs() const;

    juce::AudioDeviceManager& getDeviceManager() noexcept;
    Transport& getTransport() noexcept;
    Mixer& getMixer() noexcept;
    Metronome& getMetronome() noexcept;

    double getCurrentSampleRate() const noexcept;
    int getCurrentBufferSize() const noexcept;
    int getInputChannelCount() const noexcept;
    float getMasterPeak() const noexcept;
    float getInputPeak() const noexcept;
    juce::String getAudioStatus() const;

    /** Live MIDI merged into every block; pass nullptr to detach. */
    void setLiveMidiSource(LiveMidiSource* source);

    /** Starts writing the device input to `file`. Message thread only.

        `firstChannel` and `numChannels` say which of the device's inputs to
        capture, so an armed track records its own socket rather than whatever
        is wired into the first one.
    */
    bool startAudioRecording(const juce::File& file, int firstChannel = 0, int numChannels = 0);
    void stopAudioRecording();
    bool isAudioRecording() const noexcept;
    juce::File getCurrentRecordingFile() const;

    /** Runs `job` with the device callback detached, so nothing else touches
        the mixer while it is driven by hand.
    */
    void renderOffline(const std::function<void()>& job);

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void saveDeviceState();
    /** Points a JACK device at the server's real playback and capture
        hardware rather than whichever client JUCE found first.
    */
    void connectJackToHardware();
    void applySavedBufferSize();
    /** One block, no larger than the size the mixer was prepared for. */
    void renderBlock(const float* const* inputChannelData,
                     int numInputChannels,
                     float* const* outputChannelData,
                     int numOutputChannels,
                     int numSamples);

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;

    juce::AudioDeviceManager deviceManager;
    Transport transport;
    Mixer mixer;
    Metronome metronome;
    Recorder recorder;
    juce::AudioBuffer<float> inputBuffer;
    juce::MidiBuffer liveMidiBuffer;
    std::atomic<LiveMidiSource*> liveMidiSource { nullptr };
    juce::File currentRecordingFile;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> bufferSize { 512 };
    std::atomic<int> inputChannelCount { 0 };
    // What the mixer was last prepared for, so renderOffline can put it back
    // at the device's own channel count rather than assuming stereo.
    std::atomic<int> outputChannelCount { 2 };
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> inputPeak { 0.0f };
    /** The JACK period found before this app changed it, restored on shutdown. */
    int jackBufferSizeToRestore = 0;
    bool initialised = false;
};

} // namespace djr
