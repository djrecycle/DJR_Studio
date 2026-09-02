#pragma once

#include "AppCommands.h"
#include "ArrangementView.h"
#include "BrowserPanel.h"
#include "DJRLookAndFeel.h"
#include "EditorPanel.h"
#include "InsertChainPanel.h"
#include "SampleEditorView.h"

#include "plugins/AudioEditorProcessor.h"
#include "MenuBarView.h"
#include "MixerView.h"
#include "PanelHost.h"
#include "PluginBrowserView.h"
#include "PreferencesDialog.h"
#include "ProjectManagerDialog.h"
#include "StatusBar.h"
#include "TransportBar.h"

#include "app/EditHistory.h"
#include "app/SessionState.h"
#include "app/Localisation.h"
#include "app/TypingKeyboard.h"
#include "audio/AudioEngine.h"
#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"
#include "midi/MidiEngine.h"
#include "midi/PianoRollModel.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginWindow.h"
#include "export/ExportManager.h"
#include "export/TrackRenderer.h"
#include "project/ProjectManager.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace djr
{

class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    enum class BrowserDockPosition
    {
        left,
        right,
        bottom
    };

    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    /** Plain container that forwards its resize to an owner supplied layout pass,
        so nested StretchableLayoutResizerBars actually re-run the layout.
    */
    class LayoutHost final : public juce::Component
    {
    public:
        std::function<void()> onResized;
        void resized() override { if (onResized) onResized(); }
    };

    void layoutWorkspace();
    void buildDefaultPanelLayout(PanelHost& host, juce::Rectangle<int> area);
    void handleCommand(AppCommand command);
    void setDisplayScalePercent(int percent);
    void applyThemeVariant(ThemeVariant variant);
    /** Installs a language and re-reads every caption that is not redrawn by a
        plain repaint - button texts and tooltips are set once, so they have to
        be asked for again.
    */
    void applyLanguage(Localisation::Language language);
    void toggleBrowserCollapsed();
    void toggleBrowserMinimized();
    void cycleBrowserDock();
    void toggleBrowserVisible();
    void refreshBrowserLayoutState();
    void refreshMenuState();
    void syncBrowserPluginLibrary();
    void showDialog(juce::Component* dialog);
    void closeDialogs();
    int getBrowserPrimarySize() const noexcept;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    /** Arms or disarms recording: audio to disk, MIDI into the active clip. */
    void toggleRecording();
    /** Arms everything and starts writing. Split out so the count-in can call it
        once the click has finished.
    */
    void beginRecording();
    void toggleMetronome();
    void setCountInBars(int bars);
    void collectRecordedNotes();
    /** Decodes `file` and drops it on `trackIndex` at `startBeat`. */
    bool addAudioClipToTrack(int trackIndex, const juce::File& file, double startBeat);
    /** Gives the built-in editor the two things only the host knows: which
        folder the project keeps its audio in, and where a "send" should land.
        Called for every plugin; does nothing for the ones that are not it.
    */
    void prepareBuiltInEditor(juce::AudioPluginInstance& plugin, int trackIndex);
    /** The built-in editor already on a track, or nullptr. */
    AudioEditorProcessor* findBuiltInEditor(int trackIndex);
    /** Hands a clip's audio to the editor and brings its window up. */
    void openClipInBuiltInEditor(AudioEditorProcessor& editor, int trackIndex, int clipIndex);
    /** First audio track at or after the selection, so takes land somewhere sensible. */
    int findAudioTrackForRecording();
    void newProject();
    void saveProject(bool forceChooser);
    void openProject();
    void openProjectFile(const juce::File& file);
    void exportAudio();
    /** Last beat any track has content on, so exports cover the whole song. */
    double getSongLengthBeats();
    void showError(const juce::String& title, const juce::String& message);
    void setStatusMessage(const juce::String& message);
    void markDirty();
    /** Which plugin of a track an "open editor" request refers to. */
    enum class PluginSlot
    {
        automatic,  ///< instrument if there is one, otherwise the last insert
        instrument,
        insert
    };

    void loadSelectedPluginIntoTrack(int pluginIndex, int trackIndex);
    void openTrackPluginEditor(int trackIndex);
    void openTrackPlugin(int trackIndex, PluginSlot slot, int insertIndex);
    /** `plugin` may be nullptr: a channel with no generator still has a window. */
    void showPluginWindow(juce::AudioProcessor* plugin, Track* track);
    /** Drops the generator-less channel window for `track`, if one is open. */
    void closeEmptyChannelWindow(Track* track);
    /** Drops every window whose track is no longer in the mixer. */
    void closeWindowsForMissingTracks();
    /** Rebuilds the pitch-preserved copies when the tempo has moved. */
    void prepareWarpedClips();
    /** Stops the arrangement view auto-scrolling while the piano roll has
        the room; restores it once the step sequencer takes over.
    */
    void handleEditorViewChanged();
    /** Keeps session, transport and the mode buttons in sync even when the
        requested mode is already selected in the transport bar.
    */
    void setPatternPlaybackMode(bool shouldUsePatternMode);
    /** Auditioning one pattern should only sound its own track. Captures
        whatever solo state the user had set by hand the first time it kicks
        in, and leaves it alone on every track switch after that, so
        endPatternAuditionSolo() has the pre-audition mix to hand back.
    */
    void beginPatternAuditionSolo(int trackIndex);
    /** Hands the manual solo state back once Song mode takes back over. */
    void endPatternAuditionSolo();
    void soloTrackExclusively(int trackIndex);
    void selectTrack(int trackIndex);
    /** Pushes the current selection into every panel and the audio engine. */
    void applySelectionToPanels();
    /** Reverses or replays the last edit, then re-reads the model everywhere. */
    void undoEdit();
    void redoEdit();
    /** Points every editor's undo hook at the history. */
    void wireUndoHooks();
    /** Asks for a new name for `patternIndex` and stores it on the session. */
    void renamePattern(int patternIndex);
    /** Asks for a new name for a track and pushes it everywhere it is shown. */
    void renameTrack(int trackIndex);
    /** Renders one track on its own to a wav under Renders/. Empty on failure,
        which it has already reported.
    */
    juce::File renderTrackToFile(int trackIndex, const juce::String& suffix);
    /** Freezes the track, or unfreezes it when it already is. */
    void freezeTrack(int trackIndex);
    /** Renders the track onto a new audio track, leaving the source alone. */
    void bounceTrackToAudio(int trackIndex);
    /** Pushes the active pattern's name into the editor header. */
    void refreshPatternName();
    /** Beats the active pattern loops for: the manual length, or its content. */
    double getEffectivePatternLength(int patternIndex);
    /** Re-points the loop and the playlist at the pattern's real length. */
    void refreshPatternLength();
    void synchroniseProjectState();
    /** Makes the mixer hold the project's tracks - as many as it names, each of
        the kind it asks for - and points the views at the new list.
    */
    void rebuildTrackListForProject();
    void applyProjectToSession();
    /** Rebuilds a track's plugins from the project, restoring their state. */
    /** Opens the sample editor on one audio clip, from a double click in the
        playlist or from the View menu.
    */
    void showSampleEditor(int trackIndex, int clipIndex);
    void restorePluginsForTrack(int trackIndex, const juce::Array<juce::var>& pluginStates);
    /** Reloads a track's audio clips from disk with their trim and warp. */
    void restoreAudioClipsForTrack(int trackIndex, const juce::Array<juce::var>& clipStates);
    /** Replaces a track's automation lanes with the ones in the project. */
    void restoreAutomationForTrack(int trackIndex, const juce::Array<juce::var>& laneStates);
    /** Re-points every output and send. Runs after the whole track list exists,
        because a destination is an index into it.
    */
    void restoreRoutingFromProject();
    /** Reloads a frozen track's render, or leaves the track unfrozen when the
        file has gone missing.
    */
    void restoreFreezeForTrack(int trackIndex, const juce::String& frozenPath);
    Track* getTrack(int index) noexcept;

    DJRLookAndFeel lookAndFeel;

    SessionState sessionState;
    AudioEngine audioEngine;
    /** Declared after the engine: it holds a reference to the engine's mixer. */
    EditHistory editHistory { audioEngine.getMixer(), &audioEngine.getTransport(), &sessionState };
    MidiEngine midiEngine;
    /** The computer keyboard as a MIDI controller, for when there is no
        hardware one plugged in.
    */
    TypingKeyboard typingKeyboard;
    PianoRollModel pianoRollModel;
    PluginManager pluginManager;
    ProjectManager projectManager;

    LayoutHost workspace;
    PanelHost panelHost;

    MenuBarView menuBar;
    TransportBar transportBar;
    BrowserPanel browserPanel;
    ArrangementView arrangementView;
    EditorPanel editorPanel;
    PluginBrowserView pluginBrowserView;
    InsertChainPanel insertChainPanel;
    SampleEditorView sampleEditorView;
    MixerView mixerView;
    StatusBar statusBar;

    ProjectManagerDialog projectManagerDialog;
    PreferencesDialog preferencesDialog;

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;

    ExportManager exportManager;
    TrackRenderer trackRenderer;
    juce::AudioFormatManager audioFormats;
    double recordingStartBeat = 0.0;
    /** Bars of click before recording starts; 0 disables the count-in. */
    int countInBars = 0;
    bool waitingForCountIn = false;
    /** Whether the click was already on, so the count-in can put it back. */
    bool metronomeWasEnabled = false;
    float displayScale = 1.0f;
    BrowserDockPosition browserDockPosition = BrowserDockPosition::left;
    bool browserCollapsed = false;
    bool browserMinimized = false;
    bool browserVisible = true;
    bool autoOpenPluginEditor = true;
    bool projectDirty = false;
    /** Captured once per piano-roll visit; the velocity-lane toggle also
        refreshes the editor, so it must not overwrite the original value.
    */
    bool pianoRollFollowOverrideActive = false;
    bool wasFollowingPlayhead = true;

    /** Set once when pattern-audition solo first kicks in, so a later track
        switch while still auditioning does not overwrite it with the
        exclusive-solo state audition itself put in place.
    */
    bool patternAuditionSoloActive = false;
    juce::Array<bool> soloStateBeforeAudition;

    /** Tempo the warped clips were last stretched for. */
    double lastWarpTempo = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace djr
