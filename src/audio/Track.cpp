#include "Track.h"

#include <cmath>

namespace djr
{

namespace
{
    /** Below this two gains are the same and a ramp is not worth setting up. */
    constexpr float gainEpsilon = 1.0e-7f;
}

Track::Track(juce::String trackName, TrackKind kind)
    : name(std::move(trackName)), trackKind(kind)
{
    // Reserved up front so adding a lane never reallocates while the audio
    // thread might be part way through the list.
    automationLanes.reserve(static_cast<size_t>(maxAutomationLanes));

    for (auto& destination : sendDestination)
        destination.store(-1, std::memory_order_release);

    for (auto& level : sendLevel)
        level.store(0.0f, std::memory_order_release);

    for (auto& preFader : sendPreFader)
        preFader.store(false, std::memory_order_release);
}

void Track::setFrozenAudio(std::unique_ptr<AudioClip> clip)
{
    std::unique_ptr<AudioClip> previous;

    {
        const juce::SpinLock::ScopedLockType scoped(freezeLock);
        previous = std::move(frozenAudio);
        frozenAudio = std::move(clip);
        frozen.store(frozenAudio != nullptr, std::memory_order_release);
    }

    // Freeing the old clip releases its samples, which can be megabytes; do it
    // outside the lock the audio thread contends for.
    previous.reset();
}

bool Track::isFrozen() const noexcept
{
    return frozen.load(std::memory_order_acquire);
}

juce::File Track::getFrozenFile() const
{
    const juce::SpinLock::ScopedLockType scoped(freezeLock);
    return frozenAudio != nullptr ? frozenAudio->getFile() : juce::File();
}

void Track::setOutputDestination(int destination) noexcept
{
    outputDestination.store(destination < 0 ? masterDestination : destination,
                            std::memory_order_release);
}

int Track::getOutputDestination() const noexcept
{
    return outputDestination.load(std::memory_order_acquire);
}

void Track::setSend(int slot, const TrackSend& send) noexcept
{
    if (! juce::isPositiveAndBelow(slot, maxSends))
        return;

    const auto index = static_cast<size_t>(slot);
    sendDestination[index].store(send.destination < 0 ? -1 : send.destination, std::memory_order_release);
    sendLevel[index].store(juce::jlimit(0.0f, 2.0f, send.level), std::memory_order_release);
    sendPreFader[index].store(send.preFader, std::memory_order_release);
}

TrackSend Track::getSend(int slot) const noexcept
{
    if (! juce::isPositiveAndBelow(slot, maxSends))
        return {};

    const auto index = static_cast<size_t>(slot);
    return { sendDestination[index].load(std::memory_order_acquire),
             sendLevel[index].load(std::memory_order_acquire),
             sendPreFader[index].load(std::memory_order_acquire) };
}

bool Track::hasPreFaderSend() const noexcept
{
    for (int slot = 0; slot < maxSends; ++slot)
        if (const auto send = getSend(slot); send.isActive() && send.preFader)
            return true;

    return false;
}

void Track::dropRoutesTo(int destination) noexcept
{
    if (destination < 0)
        return;

    if (getOutputDestination() == destination)
        setOutputDestination(masterDestination);

    for (int slot = 0; slot < maxSends; ++slot)
        if (getSend(slot).destination == destination)
            setSend(slot, {});
}

void Track::remapDestinations(int removedIndex) noexcept
{
    if (removedIndex < 0)
        return;

    // Everything past the hole shuffles down by one; anything that pointed at
    // the hole itself has already been sent back to master by dropRoutesTo.
    const auto shift = [removedIndex] (int destination)
    {
        return destination > removedIndex ? destination - 1 : destination;
    };

    if (const auto output = getOutputDestination(); output >= 0)
        setOutputDestination(shift(output));

    for (int slot = 0; slot < maxSends; ++slot)
    {
        auto send = getSend(slot);

        if (send.destination >= 0)
        {
            send.destination = shift(send.destination);
            setSend(slot, send);
        }
    }
}

const juce::String& Track::getName() const noexcept
{
    return name;
}

void Track::setName(juce::String newName)
{
    // A blank header reads as a broken track, so an empty name is refused
    // rather than stored.
    if (newName.trim().isEmpty())
        return;

    name = std::move(newName).trim();
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

AutomationLane* Track::addAutomationLane(AutomationTarget target)
{
    if (const auto existing = findAutomationLane(target); existing >= 0)
        return getAutomationLane(existing);

    auto lane = std::make_unique<AutomationLane>(std::move(target));
    auto* raw = lane.get();

    const juce::SpinLock::ScopedLockType scoped(automationLock);

    if (static_cast<int>(automationLanes.size()) >= maxAutomationLanes)
        return nullptr;

    automationLanes.push_back(std::move(lane));
    automationLaneCount.store(static_cast<int>(automationLanes.size()), std::memory_order_release);
    return raw;
}

bool Track::removeAutomationLane(int index)
{
    std::unique_ptr<AutomationLane> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(automationLock);

        if (! juce::isPositiveAndBelow(index, static_cast<int>(automationLanes.size())))
            return false;

        detached = std::move(automationLanes[static_cast<size_t>(index)]);
        automationLanes.erase(automationLanes.begin() + index);
        automationLaneCount.store(static_cast<int>(automationLanes.size()), std::memory_order_release);
    }

    // The lane that was driving volume or pan is gone; stop reporting its value
    // or the fader would freeze where the curve happened to leave it.
    volumeAutomated.store(false, std::memory_order_release);
    panAutomated.store(false, std::memory_order_release);

    detached.reset();
    return true;
}

int Track::getNumAutomationLanes() const noexcept
{
    return automationLaneCount.load(std::memory_order_acquire);
}

AutomationLane* Track::getAutomationLane(int index) noexcept
{
    const juce::SpinLock::ScopedLockType scoped(automationLock);

    return juce::isPositiveAndBelow(index, static_cast<int>(automationLanes.size()))
        ? automationLanes[static_cast<size_t>(index)].get()
        : nullptr;
}

const AutomationLane* Track::getAutomationLane(int index) const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(automationLock);

    return juce::isPositiveAndBelow(index, static_cast<int>(automationLanes.size()))
        ? automationLanes[static_cast<size_t>(index)].get()
        : nullptr;
}

int Track::findAutomationLane(const AutomationTarget& target) const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(automationLock);

    for (int i = 0; i < static_cast<int>(automationLanes.size()); ++i)
        if (automationLanes[static_cast<size_t>(i)]->getTarget().aimsAtSameParameter(target))
            return i;

    return -1;
}

std::vector<AutomationLaneState> Track::captureAutomation() const
{
    std::vector<AutomationLaneState> captured;

    const juce::SpinLock::ScopedLockType scoped(automationLock);
    captured.reserve(automationLanes.size());

    for (const auto& lane : automationLanes)
        if (lane != nullptr)
            captured.push_back(lane->captureState());

    return captured;
}

void Track::restoreAutomation(const std::vector<AutomationLaneState>& lanes)
{
    // Built outside the lock: allocating a whole set of lanes is not something
    // the audio thread should ever be made to wait for.
    std::vector<std::unique_ptr<AutomationLane>> rebuilt;
    rebuilt.reserve(static_cast<size_t>(maxAutomationLanes));

    for (const auto& state : lanes)
    {
        if (static_cast<int>(rebuilt.size()) >= maxAutomationLanes)
            break;

        auto lane = std::make_unique<AutomationLane>(state.target);
        lane->applyState(state);
        rebuilt.push_back(std::move(lane));
    }

    std::vector<std::unique_ptr<AutomationLane>> previous;

    {
        const juce::SpinLock::ScopedLockType scoped(automationLock);
        previous = std::move(automationLanes);
        automationLanes = std::move(rebuilt);
        automationLaneCount.store(static_cast<int>(automationLanes.size()), std::memory_order_release);
    }

    volumeAutomated.store(false, std::memory_order_release);
    panAutomated.store(false, std::memory_order_release);
    previous.clear();
}

float Track::getEffectiveVolume() const noexcept
{
    return volumeAutomated.load(std::memory_order_acquire)
        ? automatedVolume.load(std::memory_order_acquire)
        : getVolume();
}

float Track::getEffectivePan() const noexcept
{
    return panAutomated.load(std::memory_order_acquire)
        ? automatedPan.load(std::memory_order_acquire)
        : getPan();
}

bool Track::isVolumeAutomated() const noexcept
{
    return volumeAutomated.load(std::memory_order_acquire);
}

bool Track::isPanAutomated() const noexcept
{
    return panAutomated.load(std::memory_order_acquire);
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

int Track::getPluginLatencySamples() const noexcept
{
    auto total = 0;

    {
        const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(instrumentLock));

        if (instrument != nullptr)
            total += juce::jmax(0, instrument->getLatencySamples());
    }

    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(pluginLock));

    for (int i = 0; i < pluginChain.size(); ++i)
        if (const auto* plugin = pluginChain.getPlugin(i))
            total += juce::jmax(0, plugin->getLatencySamples());

    return total;
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

juce::StringArray Track::getPluginFormatNames() const
{
    const juce::SpinLock::ScopedLockType scoped(const_cast<juce::SpinLock&>(pluginLock));
    return pluginChain.getPluginFormatNames();
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
                         const TrackPlaybackContext& context,
                         juce::AudioBuffer<float>* preFaderOut)
{
    buffer.clear();
    midi.clear();

    // Read the curves first: the mute check below returns early, and the mixer
    // faders should still show what the automation is doing on a muted track.
    readAutomation(context, buffer.getNumSamples());

    // Live playing is merged before rendering so the instrument - or the
    // preview synth - hears the keyboard as well as the sequence.
    if (context.liveMidi != nullptr && ! context.liveMidi->isEmpty())
        midi.addEvents(*context.liveMidi, 0, buffer.getNumSamples(), 0);

    // A frozen track plays its render instead of making the sound again: no
    // clips, no instrument, no inserts. That is the whole point - it is what
    // gives the CPU back. Volume and pan still run below, so it stays mixable.
    const auto playingFrozen = isFrozen();

    if (playingFrozen)
    {
        const juce::SpinLock::ScopedTryLockType scoped(freezeLock);

        if (scoped.isLocked() && frozenAudio != nullptr)
            frozenAudio->addToBuffer(buffer, context.startBeat, context.tempoBpm, context.sampleRate);
    }
    else
    {
        renderAudio(buffer, midi, context);
    }

    mixInInput(buffer, context);

    if (isMuted())
    {
        buffer.clear();
        midi.clear();

        // A muted track feeds its sends nothing either, so the pre-fader tap has
        // to be cleared too rather than left holding the previous block.
        if (preFaderOut != nullptr)
            preFaderOut->clear();

        peakLevel.store(0.0f, std::memory_order_release);
        peakLevelLeft.store(0.0f, std::memory_order_release);
        peakLevelRight.store(0.0f, std::memory_order_release);
        return;
    }

    // The render already went through both of these, so running them again
    // would apply every effect twice.
    if (! playingFrozen)
    {
        {
            // Instrument first: it is what turns this track's MIDI into audio.
            const juce::SpinLock::ScopedTryLockType scoped(instrumentLock);
            if (scoped.isLocked() && instrument != nullptr)
            {
                applyParameterAutomation(*instrument, AutomationTarget::instrumentSlot);
                PluginChain::processWithChannelAdaptation(*instrument, buffer, midi, instrumentScratch);
            }
        }

        // Never block the audio thread for a plugin edit: skip the chain for this
        // block instead, so the worst case is one unprocessed buffer.
        const juce::SpinLock::ScopedTryLockType scoped(pluginLock);
        if (scoped.isLocked() && ! pluginChain.isEmpty())
        {
            for (int slot = 0; pendingParameterCount > 0 && slot < pluginChain.size(); ++slot)
                if (auto* insert = pluginChain.getPlugin(slot))
                    applyParameterAutomation(*insert, slot);

            pluginChain.process(buffer, midi);
        }
    }

    // Taken here, between the inserts and the fader: that is what "pre-fader"
    // means, and it is why riding the fader does not also ride the send.
    if (preFaderOut != nullptr)
    {
        const auto numSamples = juce::jmin(preFaderOut->getNumSamples(), buffer.getNumSamples());

        for (int channel = 0; channel < preFaderOut->getNumChannels(); ++channel)
            if (channel < buffer.getNumChannels())
                preFaderOut->copyFrom(channel, 0, buffer, channel, 0, numSamples);
    }

    applyLevels(buffer);
    updatePeak(buffer);
}

void Track::readAutomation(const TrackPlaybackContext& context, int numSamples) noexcept
{
    blockVolumeAutomated = false;
    blockPanAutomated = false;
    pendingParameterCount = 0;
    blockAutomationChunks = juce::jlimit(1, maxAutomationChunks,
                                         (juce::jmax(1, numSamples) + automationChunkSamples - 1)
                                             / automationChunkSamples);

    if (automationLaneCount.load(std::memory_order_acquire) == 0)
    {
        volumeAutomated.store(false, std::memory_order_release);
        panAutomated.store(false, std::memory_order_release);
        return;
    }

    const juce::SpinLock::ScopedTryLockType scoped(automationLock);

    // Losing the race with an edit costs one block of the previous value, the
    // same bargain the plugin chain already makes.
    if (! scoped.isLocked())
        return;

    // A stopped transport still reads at the playhead, so dragging it shows what
    // the curve does there instead of freezing on the last value played.
    const auto endBeat = context.isPlaying ? context.endBeat : context.startBeat;

    // Big enough for every sub-block boundary; 65 doubles of stack is nothing
    // next to what a plugin will ask for in the same callback.
    double sampled[maxAutomationChunks + 1];

    for (const auto& lane : automationLanes)
    {
        if (lane == nullptr)
            continue;

        const auto& laneTarget = lane->getTarget();

        // A plugin parameter cannot ramp inside a block, so it only needs the
        // one value at the block boundary.
        const auto wanted = laneTarget.kind == AutomationTarget::Kind::pluginParameter
            ? 1
            : blockAutomationChunks + 1;

        if (! lane->sampleRange(context.startBeat, endBeat, sampled, wanted))
            continue;

        switch (laneTarget.kind)
        {
            case AutomationTarget::Kind::trackVolume:
                for (int i = 0; i < wanted; ++i)
                    blockVolumeCurve[static_cast<size_t>(i)] =
                        static_cast<float>(laneTarget.toParameterValue(sampled[i]));

                blockVolumeAutomated = true;
                automatedVolume.store(blockVolumeCurve[static_cast<size_t>(wanted - 1)],
                                      std::memory_order_release);
                break;

            case AutomationTarget::Kind::trackPan:
                for (int i = 0; i < wanted; ++i)
                    blockPanCurve[static_cast<size_t>(i)] =
                        static_cast<float>(laneTarget.toParameterValue(sampled[i]));

                blockPanAutomated = true;
                automatedPan.store(blockPanCurve[static_cast<size_t>(wanted - 1)],
                                   std::memory_order_release);
                break;

            case AutomationTarget::Kind::pluginParameter:
                if (pendingParameterCount < maxAutomationLanes)
                    pendingParameters[static_cast<size_t>(pendingParameterCount++)] =
                        { laneTarget.pluginSlot,
                          laneTarget.parameterIndex,
                          static_cast<float>(laneTarget.toParameterValue(sampled[0])) };
                break;
        }
    }

    volumeAutomated.store(blockVolumeAutomated, std::memory_order_release);
    panAutomated.store(blockPanAutomated, std::memory_order_release);
}

void Track::applyParameterAutomation(juce::AudioPluginInstance& plugin, int pluginSlot) noexcept
{
    if (pendingParameterCount == 0)
        return;

    // setValue is the path a host uses from the audio thread; the notifying
    // variant is the one that would call back into the message thread.
    const auto& parameters = plugin.getParameters();

    for (int i = 0; i < pendingParameterCount; ++i)
    {
        const auto& pending = pendingParameters[static_cast<size_t>(i)];

        if (pending.pluginSlot != pluginSlot)
            continue;

        if (juce::isPositiveAndBelow(pending.parameterIndex, parameters.size()))
            parameters[pending.parameterIndex]->setValue(pending.value);
    }
}

void Track::setInputChannel(int firstChannel) noexcept
{
    inputChannel.store(juce::jmax(noInput, firstChannel), std::memory_order_release);
}

int Track::getInputChannel() const noexcept
{
    return inputChannel.load(std::memory_order_acquire);
}

void Track::setInputStereo(bool shouldBeStereo) noexcept
{
    inputStereo.store(shouldBeStereo, std::memory_order_release);
}

bool Track::isInputStereo() const noexcept
{
    return inputStereo.load(std::memory_order_acquire);
}

int Track::getInputChannelCount() const noexcept
{
    if (getInputChannel() < 0)
        return 0;

    return isInputStereo() ? 2 : 1;
}

void Track::mixInInput(juce::AudioBuffer<float>& buffer, const TrackPlaybackContext& context) const noexcept
{
    if (! isInputMonitoring() || context.inputBuffer == nullptr)
        return;

    const auto first = getInputChannel();

    if (first < 0)
        return;

    const auto& input = *context.inputBuffer;
    const auto numSamples = juce::jmin(buffer.getNumSamples(), input.getNumSamples());

    if (numSamples <= 0 || input.getNumChannels() <= 0)
        return;

    const auto stereo = isInputStereo();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        // Mono feeds every output channel from its one input; stereo takes the
        // pair starting at the chosen channel. Clamped, so asking for In 2 on a
        // one-input device gives In 1 rather than reading past the buffer.
        const auto offset = stereo ? juce::jmin(channel, 1) : 0;
        const auto source = juce::jlimit(0, input.getNumChannels() - 1, first + offset);
        buffer.addFrom(channel, 0, input, source, 0, numSamples);
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

void Track::applyLevels(juce::AudioBuffer<float>& buffer) const noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    const auto stereo = buffer.getNumChannels() >= 2;
    const auto storedVolume = getVolume();
    const auto storedPan = getPan();

    const auto leftGain = [] (float panValue)
    {
        return juce::jlimit(0.0f, 1.0f, 1.0f - juce::jmax(0.0f, panValue));
    };

    const auto rightGain = [] (float panValue)
    {
        return juce::jlimit(0.0f, 1.0f, 1.0f + juce::jmin(0.0f, panValue));
    };

    // Nothing being automated is by far the common case, and it costs one gain
    // call for the whole buffer.
    if (! blockVolumeAutomated && ! blockPanAutomated)
    {
        buffer.applyGain(storedVolume);

        if (stereo)
        {
            buffer.applyGain(0, 0, numSamples, leftGain(storedPan));
            buffer.applyGain(1, 0, numSamples, rightGain(storedPan));
        }

        return;
    }

    // Automation is followed in sub-blocks and ramped between them: one straight
    // line per buffer would flatten a fast sweep into audible stair steps, and a
    // plain step would click on every buffer boundary.
    for (int chunk = 0; chunk < blockAutomationChunks; ++chunk)
    {
        const auto begin = chunk * numSamples / blockAutomationChunks;
        const auto length = (chunk + 1) * numSamples / blockAutomationChunks - begin;

        if (length <= 0)
            continue;

        const auto index = static_cast<size_t>(chunk);
        const auto volumeStart = blockVolumeAutomated ? blockVolumeCurve[index] : storedVolume;
        const auto volumeEnd = blockVolumeAutomated ? blockVolumeCurve[index + 1] : storedVolume;

        if (std::abs(volumeEnd - volumeStart) < gainEpsilon)
            buffer.applyGain(begin, length, volumeStart);
        else
            buffer.applyGainRamp(begin, length, volumeStart, volumeEnd);

        if (! stereo)
            continue;

        const auto panStart = blockPanAutomated ? blockPanCurve[index] : storedPan;
        const auto panEnd = blockPanAutomated ? blockPanCurve[index + 1] : storedPan;

        if (std::abs(panEnd - panStart) < gainEpsilon)
        {
            buffer.applyGain(0, begin, length, leftGain(panStart));
            buffer.applyGain(1, begin, length, rightGain(panStart));
            continue;
        }

        buffer.applyGainRamp(0, begin, length, leftGain(panStart), leftGain(panEnd));
        buffer.applyGainRamp(1, begin, length, rightGain(panStart), rightGain(panEnd));
    }
}

} // namespace djr
