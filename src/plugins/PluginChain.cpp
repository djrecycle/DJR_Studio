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

    for (auto& plugin : plugins)
        plugin->prepareToPlay(currentSampleRate, currentBlockSize);
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

    if (required <= buffer.getNumChannels())
    {
        plugin.processBlock(buffer, midi);
        return;
    }

    // The plugin wants a wider buffer than the track carries; lend it the
    // pre-allocated scratch and copy the first channels back.
    if (required > scratchBuffer.getNumChannels() || buffer.getNumSamples() > scratchBuffer.getNumSamples())
        return;

    const auto numSamples = buffer.getNumSamples();
    juce::AudioBuffer<float> view(scratchBuffer.getArrayOfWritePointers(), required, numSamples);
    view.clear();

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        view.copyFrom(channel, 0, buffer, channel, 0, numSamples);

    plugin.processBlock(view, midi);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom(channel, 0, view, channel, 0, numSamples);
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

    plugins.push_back(std::move(plugin));
}

std::vector<std::unique_ptr<juce::AudioPluginInstance>> PluginChain::detachAll()
{
    auto detached = std::move(plugins);
    plugins.clear();
    plugins.reserve(static_cast<size_t>(maxPlugins));
    return detached;
}

void PluginChain::clear()
{
    for (auto& plugin : plugins)
        plugin->releaseResources();
    plugins.clear();
    plugins.reserve(static_cast<size_t>(maxPlugins));
}

void PluginChain::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    for (auto& plugin : plugins)
        processWithChannelAdaptation(*plugin, buffer, midi, scratch);
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
    return juce::isPositiveAndBelow(index, size()) ? plugins[static_cast<size_t>(index)].get() : nullptr;
}

const juce::AudioPluginInstance* PluginChain::getPlugin(int index) const noexcept
{
    return juce::isPositiveAndBelow(index, size()) ? plugins[static_cast<size_t>(index)].get() : nullptr;
}

juce::StringArray PluginChain::getPluginNames() const
{
    juce::StringArray names;
    for (const auto& plugin : plugins)
        names.add(plugin->getName());
    return names;
}

juce::StringArray PluginChain::getPluginFormatNames() const
{
    juce::StringArray formats;
    for (const auto& plugin : plugins)
        formats.add(plugin->getPluginDescription().pluginFormatName);
    return formats;
}

} // namespace djr
