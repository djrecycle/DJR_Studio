#pragma once

#include "AudioClip.h"
#include "AutomationLane.h"
#include "ChannelSettings.h"
#include "plugins/PluginChain.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

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
    /** Everything routed into this bus for this block. The mixer fills it before
        the bus is processed, which the process order guarantees is possible.
        Only set for bus tracks.
    */
    const juce::AudioBuffer<float>* busInput = nullptr;
};

/** One send: a tap off a track into a bus, on top of wherever the track's main
    output already goes.
*/
struct TrackSend
{
    /** Bus track index, or -1 when the slot is unused. */
    int destination = -1;
    float level = 0.0f;
    /** Pre-fader taps the signal before volume and pan, so riding the fader does
        not also ride the amount going to the reverb.
    */
    bool preFader = false;

    bool isActive() const noexcept { return destination >= 0 && level > 0.0f; }
};

class Track
{
public:
    Track(juce::String trackName, TrackKind kind);
    virtual ~Track() = default;

    const juce::String& getName() const noexcept;
    /** Message thread only, like every reader of the name: the audio thread
        never looks at it, so this needs no lock.
    */
    void setName(juce::String newName);
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

    // Routing ----------------------------------------------------------------
    /** Four is what a mix actually reaches for - reverb, delay, parallel
        compression, a headphone feed - and keeps the array free of allocation.
    */
    static constexpr int maxSends = 4;
    /** The main output when it is not aimed at a bus. */
    static constexpr int masterDestination = -1;

    /** Routing is written through the Mixer, never straight onto a track: only
        the mixer can see the whole graph, and only it can tell that a route
        would feed back into itself.
    */
    void setOutputDestination(int destination) noexcept;
    int getOutputDestination() const noexcept;
    void setSend(int slot, const TrackSend& send) noexcept;
    TrackSend getSend(int slot) const noexcept;
    /** Whether any slot wants the signal from before the fader, so the mixer
        only pays for that copy when something is listening.
    */
    bool hasPreFaderSend() const noexcept;
    /** Turns off every send aimed at `destination`, and sends the main output
        back to master if it pointed there. For a bus that is going away.
    */
    void dropRoutesTo(int destination) noexcept;
    /** Shifts destination indices after a track is removed from the list. */
    void remapDestinations(int removedIndex) noexcept;

    // Freeze -----------------------------------------------------------------
    /** Plays `clip` in place of everything this track would otherwise render:
        its own clips, its instrument and its inserts are all skipped.

        The clip holds the **pre-fader** signal, so volume and pan still apply
        on top - a frozen track is still yours to mix. Pass nullptr to unfreeze.
    */
    void setFrozenAudio(std::unique_ptr<AudioClip> clip);
    bool isFrozen() const noexcept;
    /** The file the frozen audio came from, so a project can find it again. */
    juce::File getFrozenFile() const;

    // Automation -------------------------------------------------------------
    /** More lanes than this on one track is a mixer, not an automation set. */
    static constexpr int maxAutomationLanes = 32;

    /** Adds a lane for `target`, or hands back the one already aiming there.
        Two lanes on one parameter would take turns overwriting each other and
        nothing on screen would say which one won.
    */
    AutomationLane* addAutomationLane(AutomationTarget target);
    bool removeAutomationLane(int index);
    int getNumAutomationLanes() const noexcept;
    AutomationLane* getAutomationLane(int index) noexcept;
    const AutomationLane* getAutomationLane(int index) const noexcept;
    int findAutomationLane(const AutomationTarget& target) const noexcept;
    /** Whole set as plain data, for undo snapshots and project files. */
    std::vector<AutomationLaneState> captureAutomation() const;
    void restoreAutomation(const std::vector<AutomationLaneState>& lanes);

    /** Volume and pan as the automation last left them, so a fader can follow
        the curve instead of sitting at the value nobody is listening to.
    */
    float getEffectiveVolume() const noexcept;
    float getEffectivePan() const noexcept;
    bool isVolumeAutomated() const noexcept;
    bool isPanAutomated() const noexcept;

    /** The channel's own envelope, LFO, filter and arpeggiator - the stage FL
        puts between the generator and the inserts. Every track carries one; it
        only does anything on a track that has notes to gate it.
    */
    ChannelSettings& getChannelSettings() noexcept;
    const ChannelSettings& getChannelSettings() const noexcept;

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

    /** Which of the device's input channels feeds this track, for monitoring
        and for recording. Mono takes the one channel; stereo takes it and the
        next. noInput silences both.

        Per track rather than per device: recording a guitar on one track and a
        microphone on another is the whole point of having more than one input.
    */
    static constexpr int noInput = -1;
    void setInputChannel(int firstChannel) noexcept;
    int getInputChannel() const noexcept;
    void setInputStereo(bool shouldBeStereo) noexcept;
    bool isInputStereo() const noexcept;
    /** How many channels this track's input takes: 0, 1 or 2. */
    int getInputChannelCount() const noexcept;
    int getPluginCount() const noexcept;
    /** Samples of delay this track's own plugins introduce: the instrument plus
        every insert. A linear-phase EQ or a lookahead compressor answers with
        hundreds or thousands, and without compensating for it the track drifts
        late against everything else.
    */
    int getPluginLatencySamples() const noexcept;
    juce::AudioPluginInstance* getPlugin(int index) noexcept;
    const juce::AudioPluginInstance* getPlugin(int index) const noexcept;
    juce::StringArray getPluginNames() const;
    /** The format of each insert, in the same order as getPluginNames. */
    juce::StringArray getPluginFormatNames() const;

    virtual void prepare(double sampleRate, int blockSize);
    /** `preFaderOut`, when given, receives the signal as it stands after the
        inserts but before volume and pan - what a pre-fader send taps.
    */
    virtual void processAudio(juce::AudioBuffer<float>& buffer,
                              juce::MidiBuffer& midi,
                              const TrackPlaybackContext& context,
                              juce::AudioBuffer<float>* preFaderOut = nullptr);

protected:
    virtual void renderAudio(juce::AudioBuffer<float>& buffer,
                             juce::MidiBuffer& midi,
                             const TrackPlaybackContext& context);
    void updatePeak(const juce::AudioBuffer<float>& buffer) noexcept;

private:
    /** One plugin parameter this block wants written, gathered before the plugin
        locks are taken so the read and the write never nest.
    */
    struct PendingParameter
    {
        int pluginSlot = AutomationTarget::instrumentSlot;
        int parameterIndex = 0;
        float value = 0.0f;
    };

    /** Automation is evaluated this often inside a block rather than once per
        buffer: at 44.1 kHz that is ~0.7 ms, far finer than any drawn curve, so
        a steep sweep is followed instead of being flattened into one straight
        line per buffer.
    */
    static constexpr int automationChunkSamples = 32;
    static constexpr int maxAutomationChunks = 64;

    /** Reads every lane for this block. Audio thread only. */
    void readAutomation(const TrackPlaybackContext& context, int numSamples) noexcept;
    /** Writes whatever this block's lanes asked of `pluginSlot`. */
    void applyParameterAutomation(juce::AudioPluginInstance& plugin, int pluginSlot) noexcept;
    /** Volume and pan, ramped across the block when automation is moving them. */
    void applyLevels(juce::AudioBuffer<float>& buffer) const noexcept;
    void mixInInput(juce::AudioBuffer<float>& buffer, const TrackPlaybackContext& context) const noexcept;

    juce::String name;
    TrackKind trackKind;
    ChannelSettings channelSettings;
    juce::SpinLock pluginLock;
    PluginChain pluginChain;
    juce::SpinLock instrumentLock;
    std::unique_ptr<juce::AudioPluginInstance> instrument;
    juce::AudioBuffer<float> instrumentScratch;
    std::atomic<bool> instrumentPresent { false };
    std::atomic<double> preparedSampleRate { 44100.0 };
    std::atomic<int> preparedBlockSize { 512 };
    std::atomic<bool> inputMonitoring { false };
    /** Channel 0 as a stereo pair is what every track did before this was
        selectable, so an untouched project sounds exactly as it did.
    */
    std::atomic<int> inputChannel { 0 };
    std::atomic<bool> inputStereo { true };
    std::atomic<float> volume { 0.8f };
    std::atomic<float> pan { 0.0f };
    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    std::atomic<bool> recordArmed { false };
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<float> peakLevelLeft { 0.0f };
    std::atomic<float> peakLevelRight { 0.0f };

    /** Guards the lane list. Editing a lane's points guards itself; this one is
        only about lanes coming and going.
    */
    mutable juce::SpinLock automationLock;
    std::vector<std::unique_ptr<AutomationLane>> automationLanes;
    std::atomic<int> automationLaneCount { 0 };

    // Block-local automation results. Only the audio thread reads or writes
    // these, so they are deliberately not atomic.
    std::array<PendingParameter, maxAutomationLanes> pendingParameters {};
    int pendingParameterCount = 0;
    /** One value per sub-block boundary, so there is one more of these than
        there are chunks.
    */
    std::array<float, maxAutomationChunks + 1> blockVolumeCurve {};
    std::array<float, maxAutomationChunks + 1> blockPanCurve {};
    int blockAutomationChunks = 1;
    bool blockVolumeAutomated = false;
    bool blockPanAutomated = false;

    // Published for the mixer faders.
    std::atomic<float> automatedVolume { 0.8f };
    std::atomic<float> automatedPan { 0.0f };
    std::atomic<bool> volumeAutomated { false };
    std::atomic<bool> panAutomated { false };

    // Routing. Kept as separate atomics rather than one guarded struct: a send
    // whose level and destination land a block apart is inaudible, and this
    // keeps the audio thread free of another lock.
    /** Guards the frozen clip. Swapped from the message thread, try-locked by
        the audio thread, like everything else here.
    */
    mutable juce::SpinLock freezeLock;
    std::unique_ptr<AudioClip> frozenAudio;
    std::atomic<bool> frozen { false };

    std::atomic<int> outputDestination { masterDestination };
    std::array<std::atomic<int>, maxSends> sendDestination {};
    std::array<std::atomic<float>, maxSends> sendLevel {};
    std::array<std::atomic<bool>, maxSends> sendPreFader {};
};

} // namespace djr
