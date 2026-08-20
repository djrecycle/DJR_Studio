#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** Actions the menu bar / header can ask MainComponent to perform. */
enum class AppCommand
{
    newProject,
    openProject,
    saveProject,
    saveProjectAs,
    showProjectManager,
    exportAudio,
    showPreferences,
    quit,

    undo,
    redo,

    toggleMetronome,
    countInOff,
    countInOneBar,
    countInTwoBars,

    scanPlugins,
    audioSettings,
    about,

    togglePlaylist,
    toggleEditor,
    toggleMixer,
    togglePlugins,
    toggleInsertChain,
    toggleSampleEditor,
    resetPanelLayout,
    toggleVelocityLane,
    toggleBrowser,
    cycleBrowserDock,
    showPianoRoll,
    showStepSequencer
};

/** Snapshot of the toggles the View menu needs to render its tick marks. */
struct ViewState
{
    bool playlistVisible = true;
    bool editorVisible = true;
    bool mixerVisible = true;
    bool pluginsVisible = true;
    bool insertChainVisible = true;
    bool sampleEditorVisible = false;
    bool velocityLaneVisible = true;
    bool browserVisible = true;
    bool pianoRollSelected = true;
    /** Greys out Undo/Redo, and names what they would reverse. */
    bool canUndo = false;
    bool canRedo = false;
    bool metronomeOn = false;
    /** 0 = count-in off, otherwise the number of bars. */
    int countInBars = 0;
    juce::String undoName;
    juce::String redoName;
};

} // namespace djr
