#pragma once

#include "AppCommands.h"
#include "ArrangementView.h"
#include "BrowserPanel.h"
#include "DJRLookAndFeel.h"
#include "EditorPanel.h"
#include "InsertChainPanel.h"
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
#include "audio/AudioEngine.h"
#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"
#include "midi/MidiEngine.h"
#include "midi/PianoRollModel.h"
#include "plugins/PluginManager.h"
#include "plugins/PluginWindow.h"
#include "export/ExportManager.h"
#include "project/ProjectManager.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace djr
{

class MainComponent final : public juce::Component,
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
    void toggleBrowserCollapsed();
    void toggleBrowserMinimized();
    void cycleBrowserDock();
    void toggleBrowserVisible();
    void refreshBrowserLayoutState();
    void refreshMenuState();
    void syncBrowserVst3Library();
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
    void showPluginWindow(juce::AudioPluginInstance& plugin);
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
    /** Pushes the active pattern's name into the editor header. */
    void refreshPatternName();
    /** Beats the active pattern loops for: the manual length, or its content. */
    double getEffectivePatternLength(int patternIndex);
    /** Re-points the loop and the playlist at the pattern's real length. */
    void refreshPatternLength();
    void synchroniseProjectState();
    void applyProjectToSession();
    /** Rebuilds a track's plugins from the project, restoring their state. */
    void restorePluginsForTrack(int trackIndex, const juce::Array<juce::var>& pluginStates);
    /** Reloads a track's audio clips from disk with their trim and warp. */
    void restoreAudioClipsForTrack(int trackIndex, const juce::Array<juce::var>& clipStates);
    Track* getTrack(int index) noexcept;

    DJRLookAndFeel lookAndFeel;

    SessionState sessionState;
    AudioEngine audioEngine;
    /** Declared after the engine: it holds a reference to the engine's mixer. */
    EditHistory editHistory { audioEngine.getMixer(), &audioEngine.getTransport(), &sessionState };
    MidiEngine midiEngine;
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
    MixerView mixerView;
    StatusBar statusBar;

    ProjectManagerDialog projectManagerDialog;
    PreferencesDialog preferencesDialog;

    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows;

    ExportManager exportManager;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace djr
