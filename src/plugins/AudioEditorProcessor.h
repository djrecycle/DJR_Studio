#pragma once

#include "audio/AudioClip.h"
#include "recording/SampleCapture.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <limits>
#include <memory>

namespace djr
{

/** The sample editor as an insert effect.

    This is not a window that happens to draw a waveform: it is a plugin
    sitting in an insert slot, which is what lets it see audio the timeline
    never holds - a live input, a synth's output, the mixer bus after everything
    ahead of it. That is the reason for wrapping the editor rather than only
    opening it on a clip.

    The audio still passes through untouched - an insert that is only watching
    has no business changing what it watches - but while armed it is also
    copied out, and what was copied becomes the clip the editor draws and edits.

    The draining and the rebuilding live here rather than in the editor, because
    a capture has to survive its window being closed: arming it, closing the window,
    playing, and opening it again is the ordinary way to use it.
*/
class AudioEditorProcessor final : public juce::AudioPluginInstance,
                                   private juce::Timer
{
public:
    /** What the browser and the project file know this plugin by. Saved into
        projects, so it is a promise: changing it orphans every insert slot
        that already refers to it.
    */
    static const char* const identifier;

    /** The entry the plugin library lists it under, built rather than scanned. */
    static juce::PluginDescription getDescription();
    /** Whether a description names this plugin - asked before a scanned plugin
        would be created, so the built-in never goes near a format manager.
    */
    static bool matches(const juce::PluginDescription& description);

    AudioEditorProcessor();

    /** The audio the editor is showing, or nullptr while there is none.
        Rebuilt as a capture grows, so it is fetched again each time rather
        than held.
    */
    AudioClip* getEditedClip() const noexcept;
    /** What the audio being edited is called - a file, a clip, or a capture. */
    juce::String getEditedName() const;

    /** Reads an audio file in and edits that instead. Returns why it failed,
        or an empty string.
    */
    juce::String loadFile(const juce::File& file);
    /** Takes audio handed in from elsewhere - a clip double clicked on the
        timeline - and edits that.
    */
    void adoptAudio(const juce::AudioBuffer<float>& source,
                    int numSamples,
                    double sourceSampleRate,
                    const juce::String& name);

    /** Where a capture is written when the project is saved, and where a
        "send" writes before handing the file over. The host owns the answer
        because it owns the project; without one, both fall back to the
        machine's music folder.
    */
    void setWorkingFolder(const juce::File& folder);
    /** What the editor's "Send to playlist" hands the host: a file on disk,
        already written. Nothing else about the timeline is the plugin's
        business.
    */
    void setSendToPlaylistCallback(std::function<void(const juce::File&, const juce::String&)> callback);
    /** True when there is a host to send to at all, so the button can be
        hidden rather than offered and then doing nothing.
    */
    bool canSendToPlaylist() const noexcept;
    /** Writes what is being edited to the working folder and hands it over. */
    void sendToPlaylist();
    /** Writes it out for a drag and returns the file, or an empty File when
        there is nothing to write. The drop decides where it lands, so nothing
        is told to the host here.
    */
    juce::File writeForDrag();
    /** Copies an edit made to the clip back over the audio it was built from,
        so the two never drift apart.
    */
    void commitClipEdits();

    /** Arms the capture, or stops it and shows what was caught. */
    void toggleCapture();
    /** Throws the capture away, back to the empty editor. */
    void clearCapture();
    bool isCapturing() const noexcept;
    double getCapturedSeconds() const noexcept;
    int getDroppedSamples() const noexcept;
    bool hasReachedCaptureLimit() const noexcept;
    /** The formats the editor writes exports with. Owned here rather than
        borrowed from the app: a plugin is handed to its editor and nothing
        else, and an export that silently does nothing because no format
        manager arrived is the worst way to find that out.
    */
    juce::AudioFormatManager& getAudioFormats() noexcept;

    /** Counts up whenever the clip is replaced, so an open editor can redraw
        when there is something new and stay still when there is not.
    */
    int getCaptureVersion() const noexcept;

    void fillInPluginDescription(juce::PluginDescription& description) const override;

    const juce::String getName() const override;
    void prepareToPlay(double sampleRate, int blockSize) override;
    void releaseResources() override;
    /** Only the float path is ours; the double one stays the base class's,
        which would otherwise be hidden rather than inherited.
    */
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /** Drains the ring and, when enough has arrived, rebuilds the clip. */
    void timerCallback() override;
    /** Replaces the clip with the capture as it now stands. */
    void rebuildClip();
    /** How often the clip may be rebuilt, in milliseconds.

        Rebuilding copies the whole capture and re-reads it for peaks, so the
        cost grows with the recording while the benefit - watching the waveform
        arrive - does not. A long capture is rebuilt more slowly for that
        reason, not because anything breaks.
    */
    int getRebuildIntervalMs() const noexcept;
    /** Where captures and sends are written. The project's Samples folder when
        the host has said so, the music folder when it has not.
    */
    juce::File getWorkingFolder() const;
    /** Writes what is being edited to `file` as a wav. Empty on success. */
    juce::String writeEditedAudioTo(const juce::File& file);

    SampleCapture capture;
    juce::AudioFormatManager audioFormats;
    std::unique_ptr<AudioClip> capturedClip;
    int captureVersion = 0;
    juce::uint32 lastRebuildMs = 0;
    juce::String editedName;
    juce::File workingFolder;
    /** The capture's own file inside the project, kept stable across saves so
        repeated saves overwrite one file instead of leaving a trail of them.
    */
    juce::String savedAudioFileName;
    std::function<void(const juce::File&, const juce::String&)> sendToPlaylistCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEditorProcessor)
};

} // namespace djr
