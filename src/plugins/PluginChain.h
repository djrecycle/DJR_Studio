#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace djr
{

class PluginChain
{
public:
    /** Reserve enough slots that adopting a plugin never reallocates under a lock. */
    static constexpr int maxPlugins = 32;

    PluginChain();

    void prepare(double sampleRate, int blockSize);
    void addPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin);
    /** Takes a plugin the caller has already prepared - safe to call under a lock. */
    void adoptPreparedPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin);
    /** Hands the plugins back so the caller can release them outside the lock. */
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> detachAll();
    void clear();
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    /** Widest channel count a plugin may ask for before we skip it. */
    static constexpr int maxPluginChannels = 8;

    /** Configures bus layout and prepares a freshly created plugin. */
    static void configureAndPrepare(juce::AudioPluginInstance& plugin,
                                    bool isInstrument,
                                    double sampleRate,
                                    int blockSize);

    /** Runs a plugin that may want more channels than the track buffer carries. */
    static void processWithChannelAdaptation(juce::AudioPluginInstance& plugin,
                                             juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midi,
                                             juce::AudioBuffer<float>& scratch);

    int size() const noexcept;
    bool isEmpty() const noexcept;
    juce::AudioPluginInstance* getPlugin(int index) noexcept;
    const juce::AudioPluginInstance* getPlugin(int index) const noexcept;
    juce::StringArray getPluginNames() const;

private:
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> plugins;
    juce::AudioBuffer<float> scratch;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};

} // namespace djr
