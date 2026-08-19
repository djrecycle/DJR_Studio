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

class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool initialise();
    void shutdown();

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
    std::atomic<float> masterPeak { 0.0f };
    std::atomic<float> inputPeak { 0.0f };
};

} // namespace djr
