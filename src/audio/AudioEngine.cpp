#include "AudioEngine.h"

#include "utils/Logger.h"

namespace djr
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialise()
{
    const auto error = deviceManager.initialise(2, 2, nullptr, true);
    if (error.isNotEmpty())
    {
        Logger::write("Audio device initialise failed: " + error);
        return false;
    }

    deviceManager.addAudioCallback(this);
    Logger::write("Audio engine initialised: " + getAudioStatus());
    return true;
}

void AudioEngine::shutdown()
{
    deviceManager.removeAudioCallback(this);
}

juce::AudioDeviceManager& AudioEngine::getDeviceManager() noexcept
{
    return deviceManager;
}

Transport& AudioEngine::getTransport() noexcept
{
    return transport;
}

Mixer& AudioEngine::getMixer() noexcept
{
    return mixer;
}

Metronome& AudioEngine::getMetronome() noexcept
{
    return metronome;
}

double AudioEngine::getCurrentSampleRate() const noexcept
{
    return sampleRate.load(std::memory_order_acquire);
}

int AudioEngine::getCurrentBufferSize() const noexcept
{
    return bufferSize.load(std::memory_order_acquire);
}

int AudioEngine::getInputChannelCount() const noexcept
{
    return inputChannelCount.load(std::memory_order_acquire);
}

float AudioEngine::getMasterPeak() const noexcept
{
    return masterPeak.load(std::memory_order_acquire);
}

float AudioEngine::getInputPeak() const noexcept
{
    return inputPeak.load(std::memory_order_acquire);
}

juce::String AudioEngine::getAudioStatus() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getName() + " / " + juce::String(getCurrentSampleRate(), 0) + " Hz / "
             + juce::String(getCurrentBufferSize()) + " samples";

    return "No audio device";
}

void AudioEngine::setLiveMidiSource(LiveMidiSource* source)
{
    liveMidiSource.store(source, std::memory_order_release);

    if (source != nullptr && getCurrentSampleRate() > 0.0)
        source->prepareLiveMidi(getCurrentSampleRate());
}

bool AudioEngine::startAudioRecording(const juce::File& file)
{
    if (getInputChannelCount() <= 0)
    {
        Logger::write("Recording refused: audio device has no inputs.");
        return false;
    }

    const auto channels = juce::jlimit(1, 2, getInputChannelCount());

    if (! recorder.startRecording(file, getCurrentSampleRate(), channels))
    {
        Logger::write("Recording could not be started: " + file.getFullPathName());
        return false;
    }

    currentRecordingFile = file;
    return true;
}

void AudioEngine::stopAudioRecording()
{
    recorder.stop();
}

bool AudioEngine::isAudioRecording() const noexcept
{
    return recorder.isRecording();
}

juce::File AudioEngine::getCurrentRecordingFile() const
{
    return currentRecordingFile;
}

void AudioEngine::renderOffline(const std::function<void()>& job)
{
    if (! job)
        return;

    const auto wasPlaying = transport.isPlaying();
    transport.stop();

    // Detach so the device thread cannot be inside mixer.process while we drive it.
    deviceManager.removeAudioCallback(this);
    job();
    deviceManager.addAudioCallback(this);

    // The mixer was re-prepared at the export settings; put it back.
    mixer.prepare(getCurrentSampleRate(), getCurrentBufferSize());
    metronome.prepare(getCurrentSampleRate());

    if (wasPlaying)
        transport.play();
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    sampleRate.store(device->getCurrentSampleRate(), std::memory_order_release);
    bufferSize.store(device->getCurrentBufferSizeSamples(), std::memory_order_release);

    const auto numInputs = juce::jmax(0, device->getActiveInputChannels().countNumberOfSetBits());
    inputBuffer.setSize(juce::jmax(1, numInputs), juce::jmax(1, getCurrentBufferSize()), false, true, true);
    inputChannelCount.store(numInputs, std::memory_order_release);

    liveMidiBuffer.ensureSize(1024);

    if (auto* source = liveMidiSource.load(std::memory_order_acquire))
        source->prepareLiveMidi(getCurrentSampleRate());

    mixer.prepare(getCurrentSampleRate(), getCurrentBufferSize());
    metronome.prepare(getCurrentSampleRate());
}

void AudioEngine::audioDeviceStopped()
{
    masterPeak.store(0.0f, std::memory_order_release);
    inputPeak.store(0.0f, std::memory_order_release);
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                   int numInputChannels,
                                                   float* const* outputChannelData,
                                                   int numOutputChannels,
                                                   int numSamples,
                                                   const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(context);
    juce::ScopedNoDenormals noDenormals;

    // Copy the device input so tracks can monitor or record it without holding
    // onto driver-owned pointers.
    const auto usableInputs = juce::jmin(numInputChannels, inputBuffer.getNumChannels());

    if (usableInputs > 0 && numSamples <= inputBuffer.getNumSamples())
    {
        for (int channel = 0; channel < usableInputs; ++channel)
            if (inputChannelData[channel] != nullptr)
                inputBuffer.copyFrom(channel, 0, inputChannelData[channel], numSamples);
    }

    // Track input peak for level meter in preferences
    auto inputPeakLevel = 0.0f;
    if (usableInputs > 0)
    {
        for (int channel = 0; channel < usableInputs; ++channel)
            inputPeakLevel = juce::jmax(inputPeakLevel, inputBuffer.getMagnitude(channel, 0, numSamples));
    }
    inputPeak.store(inputPeakLevel, std::memory_order_release);

    if (usableInputs > 0 && recorder.isRecording())
        recorder.processInputBlock(inputChannelData, numInputChannels, numSamples);

    liveMidiBuffer.clear();
    if (auto* source = liveMidiSource.load(std::memory_order_acquire))
        source->fetchLiveMidi(liveMidiBuffer, numSamples);

    juce::AudioBuffer<float> output(outputChannelData, numOutputChannels, numSamples);

    const auto blockStartBeat = transport.getPositionBeats();
    const auto beatsAdvanced = (static_cast<double>(numSamples) / getCurrentSampleRate()) * (transport.getTempoBpm() / 60.0);
    const auto playing = transport.isPlaying();

    TrackPlaybackContext playbackContext;
    playbackContext.sampleRate = getCurrentSampleRate();
    playbackContext.tempoBpm = transport.getTempoBpm();
    playbackContext.startBeat = blockStartBeat;
    playbackContext.endBeat = blockStartBeat + beatsAdvanced;
    playbackContext.isPlaying = playing;
    playbackContext.songMode = transport.isSongMode();
    playbackContext.inputBuffer = usableInputs > 0 ? &inputBuffer : nullptr;
    playbackContext.liveMidi = &liveMidiBuffer;

    // Always run the graph: instruments, live MIDI and input monitoring have to
    // be audible while the transport is stopped too.
    mixer.process(output, playbackContext);

    // The click sits on top of the mix and is deliberately not recorded: it is
    // added after the recorder has already taken the input. It still runs while
    // the transport is stopped, because a count-in clicks before playback rolls.
    metronome.process(output,
                      blockStartBeat,
                      playing ? playbackContext.endBeat : blockStartBeat,
                      transport.getBeatsPerBar());

    if (playing)
        transport.advanceSamples(numSamples, getCurrentSampleRate());

    masterPeak.store(mixer.getMasterBus().getPeakLevel(), std::memory_order_release);
}

} // namespace djr
