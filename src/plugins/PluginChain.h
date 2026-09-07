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
    /** Detaches just the plugin at `index`, shifting the ones after it down
        one slot - nullptr, and the chain untouched, if `index` is out of
        range. Same reason as detachAll(): the caller destroys (and, for a
        still-audible one, releases) it outside whatever lock protected
        this call.
    */
    std::unique_ptr<juce::AudioPluginInstance> detachAt(int index);
    /** Moves the plugin at `fromIndex` to sit at `toIndex`, shifting
        whatever was between them. A no-op if either index is out of range
        or they are equal - no plugin is destroyed either way.
    */
    void moveTo(int fromIndex, int toIndex);
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
    /** The format each insert came from, so a slot can say LV2 or VST3 rather
        than assuming.
    */
    juce::StringArray getPluginFormatNames() const;

    /** A bypassed plugin stays in the chain - still prepared, still holding
        whatever state it remembers (an EQ curve, a wet/dry mix) - but
        process() skips calling it, so the signal passes through untouched.
        Silencing it this way rather than removing it is the point: turning
        it back on picks up exactly where it left off. Out of range answers
        false / does nothing.
    */
    bool isBypassed(int index) const noexcept;
    void setBypassed(int index, bool shouldBypass) noexcept;

private:
    /** A plugin plus whether process() currently skips it. Kept together so
        moveTo() carries the bypass state along with the plugin it belongs
        to, rather than leaving it pinned to a chain position.
    */
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        bool bypassed = false;
    };

    std::vector<Slot> plugins;
    juce::AudioBuffer<float> scratch;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
};

} // namespace djr
