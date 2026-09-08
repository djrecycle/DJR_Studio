#include "PluginChain.h"

namespace djr
{

PluginChain::PluginChain()
{
    plugins.reserve(static_cast<size_t>(maxPlugins));
}

void PluginChain::prepare(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;

    // Allocated up front so the audio thread never resizes it.
    scratch.setSize(maxPluginChannels, juce::jmax(1, blockSize), false, false, true);

    for (auto& slot : plugins)
        slot.plugin->prepareToPlay(currentSampleRate, currentBlockSize);
}

void PluginChain::configureAndPrepare(juce::AudioPluginInstance& plugin,
                                      bool isInstrument,
                                      double sampleRate,
                                      int blockSize)
{
    plugin.enableAllBuses();

    // Instruments are fed MIDI, not audio, so they get no inputs; effects run
    // in stereo. If a plugin refuses the layout it simply keeps its own.
    plugin.setPlayConfigDetails(isInstrument ? 0 : 2, 2, sampleRate, blockSize);
    plugin.prepareToPlay(sampleRate, blockSize);
}

void PluginChain::processWithChannelAdaptation(juce::AudioPluginInstance& plugin,
                                               juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& midi,
                                               juce::AudioBuffer<float>& scratchBuffer)
{
    const auto required = juce::jmax(plugin.getTotalNumInputChannels(),
                                     plugin.getTotalNumOutputChannels());
    const auto bufferChannels = buffer.getNumChannels();

    if (required == bufferChannels)
    {
        plugin.processBlock(buffer, midi);
        return;
    }

    if (required > bufferChannels)
    {
        // The plugin wants a wider buffer than the track carries; lend it the
        // pre-allocated scratch and copy the first channels back.
        if (required > scratchBuffer.getNumChannels() || buffer.getNumSamples() > scratchBuffer.getNumSamples())
            return;

        const auto numSamples = buffer.getNumSamples();
        juce::AudioBuffer<float> view(scratchBuffer.getArrayOfWritePointers(), required, numSamples);
        view.clear();

        for (int channel = 0; channel < bufferChannels; ++channel)
            view.copyFrom(channel, 0, buffer, channel, 0, numSamples);

        plugin.processBlock(view, midi);

        for (int channel = 0; channel < bufferChannels; ++channel)
            buffer.copyFrom(channel, 0, view, channel, 0, numSamples);

        return;
    }

    // The plugin is narrower than the track - a mono effect (or a mono-only
    // instrument) sitting on a stereo track is the real case, since every
    // track buffer in this app is stereo. Passing the buffer through
    // unmodified here, the way the required == bufferChannels branch does,
    // would only be half right: a plugin's processBlock only reads and
    // writes the channels its own buses actually have, via getBusBuffer, so
    // channel 0 would be processed and every channel after it would pass
    // through completely untouched - an unprocessed channel sitting next to
    // a processed one, not a plugin that quietly does nothing. Downmixing
    // every buffer channel into what the plugin has, then spreading its
    // result back across every buffer channel, is what a mono insert on a
    // stereo channel is supposed to sound like.
    //
    // A plugin reporting 0 channels either way (a MIDI-only effect with no
    // audio ports at all) has nothing to downmix into or spread back out of
    // - call it with the buffer unchanged, same as the equal-channels case
    // above, so it still gets a chance to process MIDI.
    if (required <= 0)
    {
        plugin.processBlock(buffer, midi);
        return;
    }

    if (required > scratchBuffer.getNumChannels() || buffer.getNumSamples() > scratchBuffer.getNumSamples())
        return;

    const auto numSamples = buffer.getNumSamples();
    juce::AudioBuffer<float> view(scratchBuffer.getArrayOfWritePointers(), required, numSamples);
    view.clear();

    const auto downmixScale = 1.0f / (float) bufferChannels;

    for (int channel = 0; channel < bufferChannels; ++channel)
        view.addFrom(channel % required, 0, buffer, channel, 0, numSamples, downmixScale);

    plugin.processBlock(view, midi);

    for (int channel = 0; channel < bufferChannels; ++channel)
        buffer.copyFrom(channel, 0, view, channel % required, 0, numSamples);
}

void PluginChain::addPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin == nullptr)
        return;

    configureAndPrepare(*plugin, false, currentSampleRate, currentBlockSize);
    adoptPreparedPlugin(std::move(plugin));
}

void PluginChain::adoptPreparedPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin)
{
    if (plugin == nullptr || size() >= maxPlugins)
        return;

    plugins.push_back(Slot { std::move(plugin), false });
}

std::vector<std::unique_ptr<juce::AudioPluginInstance>> PluginChain::detachAll()
{
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> detached;
    detached.reserve(plugins.size());

    for (auto& slot : plugins)
        detached.push_back(std::move(slot.plugin));

    plugins.clear();
    plugins.reserve(static_cast<size_t>(maxPlugins));
    return detached;
}

std::unique_ptr<juce::AudioPluginInstance> PluginChain::detachAt(int index)
{
    if (! juce::isPositiveAndBelow(index, size()))
        return nullptr;

    auto plugin = std::move(plugins[static_cast<size_t>(index)].plugin);
    plugins.erase(plugins.begin() + index);
    return plugin;
}

void PluginChain::moveTo(int fromIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow(fromIndex, size()) || ! juce::isPositiveAndBelow(toIndex, size())
        || fromIndex == toIndex)
        return;

    auto slot = std::move(plugins[static_cast<size_t>(fromIndex)]);
    plugins.erase(plugins.begin() + fromIndex);
    plugins.insert(plugins.begin() + toIndex, std::move(slot));
}

void PluginChain::clear()
{
    for (auto& slot : plugins)
        slot.plugin->releaseResources();
    plugins.clear();
    plugins.reserve(static_cast<size_t>(maxPlugins));
}

void PluginChain::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (auto& slot : plugins)
        if (! slot.bypassed)
            processWithChannelAdaptation(*slot.plugin, buffer, midi, scratch);
}

int PluginChain::size() const noexcept
{
    return static_cast<int>(plugins.size());
}

bool PluginChain::isEmpty() const noexcept
{
    return plugins.empty();
}

juce::AudioPluginInstance* PluginChain::getPlugin(int index) noexcept
{
    return juce::isPositiveAndBelow(index, size()) ? plugins[static_cast<size_t>(index)].plugin.get() : nullptr;
}

const juce::AudioPluginInstance* PluginChain::getPlugin(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, size()) ? plugins[static_cast<size_t>(index)].plugin.get() : nullptr;
}

juce::StringArray PluginChain::getPluginNames() const
{
    juce::StringArray names;
    for (const auto& slot : plugins)
        names.add(slot.plugin->getName());
    return names;
}

juce::StringArray PluginChain::getPluginFormatNames() const
{
    juce::StringArray formats;
    for (const auto& slot : plugins)
        formats.add(slot.plugin->getPluginDescription().pluginFormatName);
    return formats;
}

bool PluginChain::isBypassed(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, size()) && plugins[static_cast<size_t>(index)].bypassed;
}

void PluginChain::setBypassed(int index, bool shouldBypass) noexcept
{
    if (juce::isPositiveAndBelow(index, size()))
        plugins[static_cast<size_t>(index)].bypassed = shouldBypass;
}

} // namespace djr
