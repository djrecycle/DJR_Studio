#pragma once

#include "UiControls.h"

#include "audio/AudioClip.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace djr
{

/** The sample editor, in the narrow sense the timeline leaves room for.

    Deliberately not a second playlist. Trimming, slicing and slipping a clip
    are already what the playlist does with a mouse, and a window that offered
    them again would be two answers to the same question. What is here is what
    the timeline cannot do: edits that rewrite the audio - normalise and
    reverse - and a zoom that goes all the way down to one sample per pixel and
    past it, where the samples are drawn as the points they are.

    The edits apply to the part of the source the clip plays, which is what the
    trim already decides. That keeps the trim as the one way to say "this bit".
*/
class SampleEditorView final : public juce::Component,
                               private juce::Button::Listener
{
public:
    SampleEditorView();
    ~SampleEditorView() override;

    /** Shows whichever clip `source` answers with.

        A function rather than a pointer on purpose. Undo replaces a track's
        whole clip list, and deleting a clip frees it - a pointer kept here
        would outlive either one. Asking again costs a lookup and cannot dangle.
    */
    void setClipSource(std::function<AudioClip*()> source, const juce::String& title);
    /** Redraws from the clip as it now stands, after an edit made elsewhere. */
    void refresh();
    /** Fits the whole sample across the view.

        A capture calls this while the audio is still arriving, so the waveform
        stays whole as it grows - the same thing the eye expects a recorder to
        do. Once it stops, the zoom belongs to whoever is reading it.
    */
    void zoomToWholeSample();

    /** A line in the header for the outcome of something the reader just did -
        an export written, or the reason it was not.

        Kept until something replaces it rather than timed out: a message that
        vanishes on its own is one the reader can miss entirely.
    */
    void setNotice(const juce::String& text);

    /** What is drawn when there is no clip. The panel is opened by double
        clicking a clip, the editor plugin is not, so the same view has two
        different true things to say about why it is empty.
    */
    void setEmptyMessage(const juce::String& message);

    /** What writes the exported file, and where the chooser starts looking.
        Both come from the host: it owns the formats and it knows the project.
    */
    void setExportContext(juce::AudioFormatManager* formats, juce::File defaultFolder);
    /** Called after an export, with the file written or an empty file and the
        reason it failed.
    */
    void setExportCallback(std::function<void(const juce::File&, const juce::String&)> callback);

    /** Turns on the capture controls and says what they drive.

        Only the editor plugin has audio passing through it to capture; the
        panel is opened on a clip that is already in memory and has nothing to
        record, so it never asks for these and never shows them.
    */
    void setCaptureCallbacks(std::function<void()> onToggleCapture,
                             std::function<void()> onClearCapture);
    /** What the capture is doing, for the header. Pushed in rather than polled
        so the view keeps knowing nothing about what is behind it.
    */
    void setCaptureState(bool capturing, double seconds, int droppedSamples, bool reachedLimit);

    /** Turns on the button that opens an audio file, and says what it drives.
        The panel edits a clip that is already on the timeline and has its own
        file; only the plugin has a reason to go looking for another one.
    */
    void setLoadCallback(std::function<void()> onLoad);
    /** Turns on the button that hands the edited audio back to the timeline. */
    void setSendCallback(std::function<void()> onSend);
    /** What a drag off that button carries: a file already written, or an empty
        File when there is nothing to write.

        Clicking sends it where the host decides; dragging lets the reader pick
        the track and the bar themselves. Same audio, two ways of aiming it.
    */
    void setDragExportCallback(std::function<juce::File()> onDragExport);
    /** What the header calls the audio, when that is not the clip's own name. */
    void setTitle(const juce::String& title);

    /** Called after an edit, with a name for the undo step. */
    void setEditCallback(std::function<void(const juce::String&)> callback);
    /** Called before an edit, so the host can snapshot for undo. */
    void setBeforeEditCallback(std::function<void(const juce::String&)> callback);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    void buttonClicked(juce::Button* button) override;
    /** Writes the audio out and lets the desktop carry it to the playlist. */
    void startSendDrag(const juce::MouseEvent& event);
    void applyEdit(AudioClip::SampleEdit edit);
    /** Asks where to write, then writes the played region there. */
    void exportSample();
    /** Writes `file`, picking the format from its extension. Empty on success,
        otherwise why it failed.
    */
    juce::String writeSampleTo(const juce::File& file);

    /** Where the waveform is drawn, below the header and the ruler. */
    juce::Rectangle<int> getWaveformArea() const;
    /** One channel's lane inside the waveform area. */
    juce::Rectangle<int> getChannelLane(int channel, int numChannels) const;
    void drawChannel(juce::Graphics& g, int channel, juce::Rectangle<int> lane) const;
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> area) const;
    void drawEmptyState(juce::Graphics& g) const;
    /** The capture's state as one line, or empty when there is nothing to say. */
    juce::String getCaptureStatusText() const;

    /** Fits the whole source across the view, which is where it starts. */
    void zoomToFit();
    /** Multiplies the zoom, keeping whatever is under `anchorX` where it is -
        otherwise zooming in on a detail loses the detail.
    */
    void zoomBy(double factor, int anchorX);
    void scrollBy(int pixels);
    /** Keeps the view inside the audio after a zoom or a scroll. */
    void clampView();
    /** Samples per pixel at the fit-the-window zoom, the floor for zooming out. */
    double getFitSamplesPerPixel() const;
    int getTotalSamples() const;
    /** Source sample under a pixel, and the pixel a sample sits at. */
    double sampleAtX(int x) const;
    double xAtSample(double sample) const;

    /** Looks the clip up again. Called at the top of anything that uses it, so
        the pointer below is never older than the call it is used in.
    */
    void refreshClipPointer();

    std::function<AudioClip*()> clipSource;
    juce::String emptyMessage { TRANS("Double click an audio clip in the playlist to edit its samples.") };
    AudioClip* clip = nullptr;
    juce::String clipTitle;

    /** How much of the source one pixel covers. Below 1 the drawing switches
        from a peak envelope to the samples themselves.
    */
    double samplesPerPixel = 1.0;
    /** Leftmost source sample on screen, as a double so a zoomed-in view does
        not jitter by a whole sample every time it is scrolled.
    */
    double viewStartSample = 0.0;
    int dragStartX = 0;
    double dragStartSample = 0.0;

    PillButton recordButton { "Record", Icon::record };
    PillButton clearButton { "Clear" };
    PillButton loadButton { "Load...", Icon::folder };
    PillButton sendButton { "To playlist", Icon::chevronRight };
    PillButton normaliseButton { "Normalize", Icon::waveform };
    PillButton reverseButton { "Reverse", Icon::undo };
    IconChipButton zoomInButton { "Zoom in", Icon::zoom };
    IconChipButton zoomOutButton { "Zoom out", Icon::zoom };
    PillButton fitButton { "Fit" };
    PillButton exportButton { "Export...", Icon::folder };

    juce::AudioFormatManager* exportFormats = nullptr;
    /** Where the last export actually landed, which is not always where it was
        asked to: an unwritable extension is turned into a wav.
    */
    juce::File exportedFile;
    juce::File exportFolder;
    /** Kept alive while the chooser is on screen; it runs asynchronously. */
    std::unique_ptr<juce::FileChooser> exportChooser;
    std::function<void(const juce::File&, const juce::String&)> exportCallback;

    std::function<void()> captureCallback;
    std::function<void()> clearCaptureCallback;
    std::function<void()> loadCallback;
    std::function<void()> sendCallback;
    std::function<juce::File()> dragExportCallback;
    /** One drag must not start a second one while the first is still live. */
    bool dragInProgress = false;
    bool captureControlsVisible = false;
    bool captureActive = false;
    double captureSeconds = 0.0;
    int captureDropped = 0;
    bool captureLimitReached = false;
    juce::String notice;

    std::function<void(const juce::String&)> editCallback;
    std::function<void(const juce::String&)> beforeEditCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleEditorView)
};

} // namespace djr
