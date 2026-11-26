#include "AudioEngine.h"

#include "BlockSplitter.h"
#include "JackServer.h"
#include "app/Settings.h"
#include "utils/Logger.h"


namespace djr
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

namespace
{
    constexpr auto deviceStateKey = "audioDeviceState";
    constexpr auto bufferSizeKey = "audioBufferSize";
}

bool AudioEngine::initialise()
{
    // The device, rate and buffer picked in Preferences used to be forgotten
    // on every start, which put the app back on ALSA's default at 512 samples.
    std::unique_ptr<juce::XmlElement> saved;

    if (const auto text = Settings::get(deviceStateKey); text.isNotEmpty())
        saved = juce::parseXML(text);

    auto error = deviceManager.initialise(maxInputChannels, 2, saved.get(), true);

    if (error.isNotEmpty() && saved != nullptr)
    {
        // A saved device that has gone (unplugged, JACK not running): fall
        // back to whatever the system offers rather than starting silent.
        Logger::write("Saved audio device unavailable (" + error + "), using the default");
        saved.reset();
        error = deviceManager.initialise(maxInputChannels, 2, nullptr, true);
    }

    if (error.isNotEmpty())
    {
        Logger::write("Audio device initialise failed: " + error);
        return false;
    }

    // JACK is not chosen on a first run even though it is the low-latency
    // path: under PipeWire, ALSA's default device follows the default sink -
    // a Bluetooth headset connecting, say - and JACK does not. Preferences
    // offers the switch instead, with the latency it buys.
    //
    // A saved JACK setup whose device has gone (that headset, unplugged)
    // reopens on whichever JACK client sorts first, which can be PipeWire's
    // "Dummy" sink: point it back at real hardware.
    if (isUsingJack())
        connectJackToHardware();

    deviceManager.addAudioCallback(this);
    applySavedBufferSize();

    if (saved == nullptr)
        saveDeviceState();

    deviceManager.addChangeListener(this);
    initialised = true;

    Logger::write("Audio engine initialised: " + getAudioStatus());
    return true;
}

void AudioEngine::shutdown()
{
    if (initialised)
        deviceManager.removeChangeListener(this);

    deviceManager.removeAudioCallback(this);

    // The JACK period is the whole system's under PipeWire: give back what
    // was there before this app lowered it.
    if (jackBufferSizeToRestore > 0)
    {
        JackServer::setBufferSize(jackBufferSizeToRestore);
        jackBufferSizeToRestore = 0;
    }

    initialised = false;
}

void AudioEngine::connectJackToHardware()
{
    if (! isUsingJack())
        return;

    auto setup = deviceManager.getAudioDeviceSetup();
    const auto playback = JackServer::findHardwareClient(true);
    const auto capture = JackServer::findHardwareClient(false);
    auto changed = false;

    if (playback.isNotEmpty() && setup.outputDeviceName != playback)
    {
        setup.outputDeviceName = playback;
        changed = true;
    }

    if (capture.isNotEmpty() && setup.inputDeviceName != capture)
    {
        setup.inputDeviceName = capture;
        changed = true;
    }

    if (changed)
    {
        const auto error = deviceManager.setAudioDeviceSetup(setup, true);
        Logger::write("JACK connected to hardware (" + playback + " / " + capture + ")"
                      + (error.isNotEmpty() ? ": " + error : juce::String()));
    }
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager)
        saveDeviceState();
}

void AudioEngine::saveDeviceState()
{
    if (auto xml = deviceManager.createStateXml())
        Settings::set(deviceStateKey, xml->toString(juce::XmlElement::TextFormat().singleLine().withoutHeader()));
}

void AudioEngine::applySavedBufferSize()
{
    // Only a JACK period needs applying by hand: an ALSA buffer size is part
    // of the device state JUCE restores itself.
    const auto saved = Settings::get(bufferSizeKey).getIntValue();

    if (saved > 0 && isUsingJack() && getDeviceBufferSize() != saved)
        setBufferSize(saved);
}

const juce::Array<int>& AudioEngine::getBufferSizeChoices()
{
    static const juce::Array<int> choices { 64, 128, 256, 512, 1024 };
    return choices;
}

bool AudioEngine::isUsingJack() const
{
    const auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->getTypeName() == "JACK";
}

bool AudioEngine::isJackAvailable()
{
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type != nullptr && type->getTypeName() == "JACK")
            return JackServer::getBufferSize().has_value();

    return false;
}

int AudioEngine::getDeviceBufferSize() const
{
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getCurrentBufferSizeSamples();

    return getCurrentBufferSize();
}

bool AudioEngine::setBufferSize(int frames)
{
    if (frames <= 0)
        return false;

    auto ok = false;

    if (isUsingJack())
    {
        if (jackBufferSizeToRestore <= 0)
            if (const auto current = JackServer::getBufferSize())
                jackBufferSizeToRestore = *current;

        ok = JackServer::setBufferSize(frames);
    }
    else
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = frames;
        ok = deviceManager.setAudioDeviceSetup(setup, true).isEmpty();
    }

    if (ok)
        Settings::set(bufferSizeKey, juce::String(frames));

    Logger::write("Buffer size " + juce::String(frames) + (ok ? " set: " : " refused: ") + getAudioStatus());
    return ok;
}

double AudioEngine::getBufferLatencyMs() const
{
    const auto rate = getCurrentSampleRate();
    return rate > 0.0 ? 1000.0 * getDeviceBufferSize() / rate : 0.0;
}

double AudioEngine::getOutputLatencyMs() const
{
    const auto rate = getCurrentSampleRate();
    auto samples = getDeviceBufferSize();

    if (auto* device = deviceManager.getCurrentAudioDevice())
        samples += juce::jmax(0, device->getOutputLatencyInSamples());

    return rate > 0.0 ? 1000.0 * samples / rate : 0.0;
}

void AudioEngine::switchToJack()
{
    deviceManager.setCurrentAudioDeviceType("JACK", true);
    connectJackToHardware();

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.inputChannels.setRange(0, maxInputChannels, true);
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange(0, 2, true);

    deviceManager.setAudioDeviceSetup(setup, true);
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
             + juce::String(getDeviceBufferSize()) + " samples";

    return "No audio device";
}

void AudioEngine::setLiveMidiSource(LiveMidiSource* source)
{
    liveMidiSource.store(source, std::memory_order_release);

    if (source != nullptr && getCurrentSampleRate() > 0.0)
        source->prepareLiveMidi(getCurrentSampleRate());
}

bool AudioEngine::startAudioRecording(const juce::File& file, int firstChannel, int numChannels)
{
    if (getInputChannelCount() <= 0)
    {
        Logger::write("Recording refused: audio device has no inputs.");
        return false;
    }

    // Nothing asked for means the whole device, which is what recording did
    // before a track could name its own input.
    const auto requested = numChannels > 0 ? numChannels : getInputChannelCount();
    const auto channels = juce::jlimit(1, 2, requested);
    const auto start = juce::jlimit(0, juce::jmax(0, getInputChannelCount() - 1), firstChannel);

    if (! recorder.startRecording(file, getCurrentSampleRate(), channels, start))
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
    mixer.prepare(getCurrentSampleRate(), getCurrentBufferSize(),
                  outputChannelCount.load(std::memory_order_acquire));
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

    const auto numOutputs = juce::jmax(1, device->getActiveOutputChannels().countNumberOfSetBits());
    outputChannelCount.store(numOutputs, std::memory_order_release);

    liveMidiBuffer.ensureSize(1024);

    if (auto* source = liveMidiSource.load(std::memory_order_acquire))
        source->prepareLiveMidi(getCurrentSampleRate());

    mixer.prepare(getCurrentSampleRate(), getCurrentBufferSize(), numOutputs);
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

    // Larger blocks than the mixer was prepared for are split - see
    // renderInBlocksOf for when a JACK period grows under a running device.
    renderInBlocksOf(getCurrentBufferSize(), inputChannelData, numInputChannels,
                     outputChannelData, numOutputChannels, numSamples,
                     [this] (const float* const* ins, int numIns, float* const* outs, int numOuts, int count)
                     {
                         renderBlock(ins, numIns, outs, numOuts, count);
                     });
}

void AudioEngine::renderBlock(const float* const* inputChannelData,
                              int numInputChannels,
                              float* const* outputChannelData,
                              int numOutputChannels,
                              int numSamples)
{

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
