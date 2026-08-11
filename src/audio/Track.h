#pragma once

#include "plugins/PluginChain.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>

namespace djr
{

enum class TrackKind
{
    audio,
    midi,
    instrument,
    bus
};

struct TrackPlaybackContext
{
    double sampleRate = 44100.0;
    double tempoBpm = 120.0;
    double startBeat = 0.0;
    double endBeat = 0.0;
    bool isPlaying = false;
    /** Song mode plays timeline placements; pattern mode loops one pattern. */
    bool songMode = false;
    /** Live audio input for this block, or nullptr when the device has no inputs. */
    const juce::AudioBuffer<float>* inputBuffer = nullptr;
    /** Freshly played MIDI, set only for the track that should receive it. */
    const juce::MidiBuffer* liveMidi = nullptr;
};

class Track
{
public:
    Track(juce::String trackName, TrackKind kind);
    virtual ~Track() = default;

    const juce::String& getName() const noexcept;
    TrackKind getKind() const noexcept;

    void setVolume(float newVolume) noexcept;
    float getVolume() const noexcept;
    void setPan(float newPan) noexcept;
    float getPan() const noexcept;
    void setMuted(bool shouldMute) noexcept;
    bool isMuted() const noexcept;
    void setSoloed(bool shouldSolo) noexcept;
    bool isSoloed() const noexcept;
    void setRecordArmed(bool shouldArm) noexcept;
    bool isRecordArmed() const noexcept;
    float getPeakLevel() const noexcept;
    /** Per-channel peak, 0 = left, 1 = right. Falls back to the mono peak. */
    float getPeakLevel(int channel) const noexcept;

    /** Prepares the plugin on the calling thread, then swaps it in under a short lock. */
    void addPlugin(std::unique_ptr<juce::AudioPluginInstance> plugin);
    void clearPlugins();

    /** The instrument turns this track's MIDI into audio; it runs before the inserts. */
    void setInstrument(std::unique_ptr<juce::AudioPluginInstance> plugin);
    void clearInstrument();
    bool hasInstrument() const noexcept;
    juce::AudioPluginInstance* getInstrument() noexcept;
    juce::String getInstrumentName() const;
    /** Input monitoring: pass the device input through this track. */
    void setInputMonitoring(bool shouldMonitor) noexcept;
    bool isInputMonitoring() const noexcept;
    int getPluginCount() const noexcept;
    juce::AudioPluginInstance* getPlugin(int index) noexcept;
    const juce::AudioPluginInstance* getPlugin(int index) const noexcept;
    juce::StringArray getPluginNames() const;

    virtual void prepare(double sampleRate, int blockSize);
    virtual void processAudio(juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& midi,
                              const TrackPlaybackContext& context);

protected:
    virtual void renderAudio(juce::AudioBuffer<float>& buffer,
                             juce::MidiBuffer& midi,
                             const TrackPlaybackContext& context);
    void updatePeak(const juce::AudioBuffer<float>& buffer) noexcept;

private:
    void applyPan(juce::AudioBuffer<float>& buffer) const noexcept;
    void mixInInput(juce::AudioBuffer<float>& buffer, const TrackPlaybackContext& context) const noexcept;

    juce::String name;
    TrackKind trackKind;
    juce::SpinLock pluginLock;
    PluginChain pluginChain;
    juce::SpinLock instrumentLock;
    std::unique_ptr<juce::AudioPluginInstance> instrument;
    juce::AudioBuffer<float> instrumentScratch;
    std::atomic<bool> instrumentPresent { false };
    std::atomic<double> preparedSampleRate { 44100.0 };
    std::atomic<int> preparedBlockSize { 512 };
    std::atomic<bool> inputMonitoring { false };
    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    std::atomic<bool> recordArmed { false };
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<float> peakLevelLeft { 0.0f };
    std::atomic<float> peakLevelRight { 0.0f };
};

} // namespace djr
