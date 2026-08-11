#include "Track.h"

namespace djr
{

Track::Track(juce::String trackName, TrackKind kind)
    : name(std::move(trackName)), trackKind(kind)
{
}

const juce::String& Track::getName() const noexcept
{
    return name;
}

TrackKind Track::getKind() const noexcept
{
    return trackKind;
}

void Track::setVolume(float newVolume) noexcept
{
    volume.store(juce::jlimit(0.0f, 2.0f, newVolume), std::memory_order_release);
}

float Track::getVolume() const noexcept
{
    return volume.load(std::memory_order_acquire);
}

void Track::setPan(float newPan) noexcept
{
    pan.store(juce::jlimit(-1.0f, 1.0f, newPan), std::memory_order_release);
}

float Track::getPan() const noexcept
{
    return pan.load(std::memory_order_acquire);
}

void Track::setMuted(bool shouldMute) noexcept
{
    muted.store(shouldMute, std::memory_order_release);
}

bool Track::isMuted() const noexcept
{
    return muted.load(std::memory_order_acquire);
}

void Track::setSoloed(bool shouldSolo) noexcept
{
    soloed.store(shouldSolo, std::memory_order_release);
}

bool Track::isSoloed() const noexcept
{
    return soloed.load(std::memory_order_acquire);
}

void Track::setRecordArmed(bool shouldArm) noexcept
{
    recordArmed.store(shouldArm, std::memory_order_release);
}

bool Track::isRecordArmed() const noexcept
{
    return recordArmed.load(std::memory_order_acquire);
}

float Track::getPeakLevel() const noexcept
{
    return peakLevel.load(std::memory_order_acquire);
}

float Track::getPeakLevel(int channel) const noexcept
{
    if (channel == 0)
        return peakLevelLeft.load(std::memory_order_acquire);

    if (channel == 1)
        return peakLevelRight.load(std::memory_order_acquire);

    return getPeakLevel();
}

void Track::addPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin == nullptr)
        return;

    // Configuring and preparing allocates and can take milliseconds, so it must
    // happen before the audio thread can be made to wait on us.
    PluginChain::configureAndPrepare(*plugin,
                                     false,
                                     preparedSampleRate.load(std::memory_order_acquire),
                                     preparedBlockSize.load(std::memory_order_acquire));

    const juce::SpinLock::ScopedLockType scoped(pluginLock);
    pluginChain.adoptPreparedPlugin(std::move(plugin));
}

void Track::clearPlugins()
{
    // Detach under the lock, destroy outside it: releaseResources also blocks.
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(pluginLock);
        detached = pluginChain.detachAll();
    }

    for (auto& plugin : detached)
        if (plugin != nullptr)
            plugin->releaseResources();
}

void Track::setInstrument(std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    std::unique_ptr<juce::AudioPluginInstance> previous;

    if (plugin != nullptr)
        PluginChain::configureAndPrepare(*plugin,
                                         true,
                                         preparedSampleRate.load(std::memory_order_acquire),
                                         preparedBlockSize.load(std::memory_order_acquire));

    {
        const juce::SpinLock::ScopedLockType scoped(instrumentLock);
        previous = std::move(instrument);
        instrument = std::move(plugin);
        instrumentPresent.store(instrument != nullptr, std::memory_order_release);
    }

    if (previous != nullptr)
        previous->releaseResources();
}

void Track::clearInstrument()
{
    setInstrument(nullptr);
}

bool Track::hasInstrument() const noexcept
{
    return instrumentPresent.load(std::memory_order_acquire);
}

juce::AudioPluginInstance* Track::getInstrument() noexcept
{
    const juce::SpinLock::ScopedLockType scoped(instrumentLock);
    return instrument.get();
}

juce::String Track::getInstrumentName() const
{
    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(instrumentLock));
    return instrument != nullptr ? instrument->getName() : juce::String();
}

void Track::setInputMonitoring(bool shouldMonitor) noexcept
{
    inputMonitoring.store(shouldMonitor, std::memory_order_release);
}

bool Track::isInputMonitoring() const noexcept
{
    return inputMonitoring.load(std::memory_order_acquire);
}

int Track::getPluginCount() const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(pluginLock));
    return pluginChain.size();
}

juce::AudioPluginInstance* Track::getPlugin(int index) noexcept
{
    const juce::SpinLock::ScopedLockType scoped(pluginLock);
    return pluginChain.getPlugin(index);
}

const juce::AudioPluginInstance* Track::getPlugin(int index) const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(pluginLock));
    return pluginChain.getPlugin(index);
}

juce::StringArray Track::getPluginNames() const
{
    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(pluginLock));
    return pluginChain.getPluginNames();
}

void Track::prepare(double sampleRate, int blockSize)
{
    preparedSampleRate.store(sampleRate, std::memory_order_release);
    preparedBlockSize.store(blockSize, std::memory_order_release);
    instrumentScratch.setSize(PluginChain::maxPluginChannels, juce::jmax(1, blockSize), false, false, true);

    {
        const juce::SpinLock::ScopedLockType scoped(pluginLock);
        pluginChain.prepare(sampleRate, blockSize);
    }

    const juce::SpinLock::ScopedLockType scoped(instrumentLock);
    if (instrument != nullptr)
        PluginChain::configureAndPrepare(*instrument, true, sampleRate, blockSize);
}

void Track::processAudio(juce::AudioBuffer<float>& buffer,
                         juce::MidiBuffer& midi,
                         const TrackPlaybackContext& context)
{
    buffer.clear();
    midi.clear();

    // Live playing is merged before rendering so the instrument - or the
    // preview synth - hears the keyboard as well as the sequence.
    if (context.liveMidi != nullptr && ! context.liveMidi->isEmpty())
        midi.addEvents(*context.liveMidi, 0, buffer.getNumSamples(), 0);

    renderAudio(buffer, midi, context);
    mixInInput(buffer, context);

    if (isMuted())
    {
        buffer.clear();
        midi.clear();
        peakLevel.store(0.0f, std::memory_order_release);
        peakLevelLeft.store(0.0f, std::memory_order_release);
        peakLevelRight.store(0.0f, std::memory_order_release);
        return;
    }

    {
        // Instrument first: it is what turns this track's MIDI into audio.
        const juce::SpinLock::ScopedTryLockType scoped(instrumentLock);
        if (scoped.isLocked() && instrument != nullptr)
            PluginChain::processWithChannelAdaptation(*instrument, buffer, midi, instrumentScratch);
    }

    {
        // Never block the audio thread for a plugin edit: skip the chain for this
        // block instead, so the worst case is one unprocessed buffer.
        const juce::SpinLock::ScopedTryLockType scoped(pluginLock);
        if (scoped.isLocked() && ! pluginChain.isEmpty())
            pluginChain.process(buffer, midi);
    }

    buffer.applyGain(getVolume());
    applyPan(buffer);
    updatePeak(buffer);
}

void Track::mixInInput(juce::AudioBuffer<float>& buffer, const TrackPlaybackContext& context) const noexcept
{
    if (! isInputMonitoring() || context.inputBuffer == nullptr)
        return;

    const auto& input = *context.inputBuffer;
    const auto numSamples = juce::jmin(buffer.getNumSamples(), input.getNumSamples());

    if (numSamples <= 0 || input.getNumChannels() <= 0)
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto sourceChannel = juce::jmin(channel, input.getNumChannels() - 1);
        buffer.addFrom(channel, 0, input, sourceChannel, 0, numSamples);
    }
}

void Track::renderAudio(juce::AudioBuffer<float>& buffer,
                        juce::MidiBuffer& midi,
                        const TrackPlaybackContext& context)
{
    juce::ignoreUnused(buffer, midi, context);
}

void Track::updatePeak(const juce::AudioBuffer<float>& buffer) noexcept
{
    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));

    peakLevel.store(peak, std::memory_order_release);

    const auto channelPeak = [&buffer] (int channel)
    {
        return juce::isPositiveAndBelow(channel, buffer.getNumChannels())
            ? buffer.getMagnitude(channel, 0, buffer.getNumSamples())
            : 0.0f;
    };

    peakLevelLeft.store(buffer.getNumChannels() > 0 ? channelPeak(0) : 0.0f, std::memory_order_release);
    peakLevelRight.store(buffer.getNumChannels() > 1 ? channelPeak(1) : channelPeak(0), std::memory_order_release);
}

void Track::applyPan(juce::AudioBuffer<float>& buffer) const noexcept
{
    if (buffer.getNumChannels() < 2)
        return;

    const auto panValue = getPan();
    const auto leftGain = juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, panValue));
    const auto rightGain = juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, panValue));

    buffer.applyGain(0, 0, buffer.getNumSamples(), leftGain);
    buffer.applyGain(1, 0, buffer.getNumSamples(), rightGain);
}

} // namespace djr
