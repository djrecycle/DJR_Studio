#include "MainComponent.h"

#include "Theme.h"
#include "project/ProjectTrackLayout.h"
#include "utils/FileUtils.h"
#include "utils/Logger.h"

#include <algorithm>

namespace djr
{

namespace
{
    constexpr int menuBarHeight = Metrics::menuBarHeight;
    constexpr int transportHeight = Metrics::transportHeight;
    constexpr int statusBarHeight = Metrics::statusBarHeight;
    constexpr int bottomBrowserHeight = 170;
    constexpr int gap = Metrics::gap;

    const juce::String playlistPanelId = "playlist";
    const juce::String editorPanelId = "editor";
    const juce::String mixerPanelId = "mixer";
    const juce::String pluginsPanelId = "plugins";
    const juce::String insertPanelId = "inserts";
    const juce::String samplePanelId = "sample";
}

MainComponent::MainComponent()
    : midiEngine(audioEngine.getDeviceManager(), audioEngine.getTransport()),
      transportBar(audioEngine),
      arrangementView(audioEngine.getMixer(), audioEngine.getTransport()),
      editorPanel(pianoRollModel, audioEngine.getTransport()),
      pluginBrowserView(pluginManager, audioEngine.getMixer()),
      insertChainPanel(audioEngine.getMixer()),
      mixerView(audioEngine.getMixer()),
      statusBar(audioEngine),
      preferencesDialog(audioEngine.getDeviceManager(), audioEngine)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    tooltipWindow = std::make_unique<juce::TooltipWindow>(this, 600);
    audioFormats.registerBasicFormats();

    workspace.onResized = [this] { layoutWorkspace(); };

    addAndMakeVisible(workspace);

    workspace.addAndMakeVisible(menuBar);
    workspace.addAndMakeVisible(transportBar);
    workspace.addAndMakeVisible(browserPanel);
    workspace.addAndMakeVisible(panelHost);
    workspace.addAndMakeVisible(statusBar);

    panelHost.addPanel(playlistPanelId, "Playlist", Icon::lines, arrangementView);
    panelHost.addPanel(editorPanelId, "Editor", Icon::notes, editorPanel);
    panelHost.addPanel(mixerPanelId, "Mixer", Icon::waveform, mixerView);
    panelHost.addPanel(pluginsPanelId, "Plugins", Icon::plug, pluginBrowserView);
    panelHost.addPanel(insertPanelId, "Insert Chain", Icon::panel, insertChainPanel);
    // Closed until a clip asks for it: a window with nothing in it
    // is a window in the way.
    panelHost.addPanel(samplePanelId, "Sample Editor", Icon::waveform, sampleEditorView);
    panelHost.setPanelOpen(samplePanelId, false);

    panelHost.setLayoutBuilder([this] (PanelHost& host, juce::Rectangle<int> area)
    {
        buildDefaultPanelLayout(host, area);
    });
    panelHost.onPanelStateChanged = [this] { refreshMenuState(); };

    workspace.addChildComponent(projectManagerDialog);
    workspace.addChildComponent(preferencesDialog);

    // Wiring -----------------------------------------------------------------
    menuBar.setCommandHandler([this] (AppCommand command) { handleCommand(command); });

    browserPanel.setCollapseToggleCallback([this] { toggleBrowserCollapsed(); });
    browserPanel.setMinimizeToggleCallback([this] { toggleBrowserMinimized(); });
    browserPanel.setDockCycleCallback([this] { cycleBrowserDock(); });
    // A file dragged onto the playlist - from the audio editor, or from a file
    // manager - lands on the track it was dropped on rather than the selected
    // one: the pointer already said which track was meant.
    arrangementView.setFileDropCallback([this] (const juce::File& file, int trackIndex, double beat)
    {
        addAudioClipToTrack(trackIndex, file, beat);
    });

    browserPanel.setFileActivatedCallback([this] (const juce::File& file)
    {
        if (file.hasFileExtension(".djrs"))
        {
            openProjectFile(file);
            return;
        }

        if (file.hasFileExtension(".wav;.aif;.aiff;.flac;.mp3;.ogg"))
        {
            // Drop it on the selected audio track at the playhead.
            addAudioClipToTrack(sessionState.getSelectedTrack(),
                                file,
                                audioEngine.getTransport().getPositionBeats());
            return;
        }

        setStatusMessage("Preview " + file.getFileName() + TRANS(" is not supported yet."));
    });

    // Double clicking a plugin in the browser loads it onto the selected track,
    // the same way double clicking a sample drops it on one.
    browserPanel.setPluginActivatedCallback([this] (int pluginIndex)
    {
        loadSelectedPluginIntoTrack(pluginIndex, sessionState.getSelectedTrack());
    });

    transportBar.setSnapChangeCallback([this] (SnapUnit unit)
    {
        // Each editor resolves Line and Cell against its own zoom and grid.
        arrangementView.setSnapUnit(unit);
        editorPanel.setSnapUnit(unit);
        setStatusMessage("Snap: " + getSnapUnitLabel(unit));
    });
    transportBar.setPatternModeChangeCallback([this] (bool patternMode)
    {
        sessionState.setSongMode(! patternMode);
        setStatusMessage(patternMode
                             ? TRANS("Pattern mode: the active pattern loops.")
                             : TRANS("Song mode: plays the pattern placements on the playlist."));
    });
    transportBar.setRecordToggleCallback([this] { toggleRecording(); });
    transportBar.setTimeSignatureChangeCallback([this] (int numerator, int denominator)
    {
        // Record before applying, or the snapshot captures the new value.
        editHistory.pushSnapshot("Time signature");
        audioEngine.getTransport().setTimeSignature(numerator, denominator);

        // Bar lines, the loop length and the step grid all derive from this.
        refreshPatternLength();
        arrangementView.repaint();
        editorPanel.repaint();
        markDirty();
        setStatusMessage("Time signature: "
                       + juce::String(audioEngine.getTransport().getTimeSignatureNumerator()) + "/"
                       + juce::String(audioEngine.getTransport().getTimeSignatureDenominator()));
    });
    transportBar.setMetronomeToggleCallback([this] { toggleMetronome(); });
    transportBar.setUndoCallback([this] { undoEdit(); });
    transportBar.setRedoCallback([this] { redoEdit(); });

    arrangementView.setTrackSelectedCallback([this] (int trackIndex) { selectTrack(trackIndex); });
    arrangementView.setTrackRenameCallback([this] (int trackIndex) { renameTrack(trackIndex); });
    arrangementView.setTrackFreezeCallback([this] (int trackIndex) { freezeTrack(trackIndex); });
    arrangementView.setTrackChannelCallback([this] (int trackIndex) { openTrackPluginEditor(trackIndex); });
    arrangementView.setTrackBounceCallback([this] (int trackIndex) { bounceTrackToAudio(trackIndex); });
    arrangementView.setClipEditedCallback([this]
    {
        pianoRollModel.notifyClipChanged();
        markDirty();
    });
    arrangementView.setAudioClipOpenRequestCallback([this] (int trackIndex, int clipIndex)
    {
        showSampleEditor(trackIndex, clipIndex);
    });
    arrangementView.setTrackListChangedCallback([this]
    {
        closeWindowsForMissingTracks();
        mixerView.refreshStrips();
        insertChainPanel.refresh();
        pluginBrowserView.refreshTrackList();
        selectTrack(arrangementView.getSelectedTrack());
        markDirty();
    });

    arrangementView.setClipOpenRequestCallback([this] (int trackIndex, int patternIndex)
    {
        // Double clicking a clip should land you on its notes: right track, right
        // pattern, editor open and showing the piano roll.
        selectTrack(trackIndex);
        sessionState.setActivePattern(patternIndex);
        panelHost.setPanelOpen(editorPanelId, true);

        // Editing notes wants the room, so the editor comes up full size.
        if (auto* panel = panelHost.findPanel(editorPanelId))
        {
            panel->setRolledUp(false);
            panel->setMaximised(true);
            panel->toFront(true);
        }

        editorPanel.showPianoRoll();
        refreshMenuState();
        setStatusMessage("Piano roll: " + juce::String(getTrack(trackIndex) != nullptr
                                                           ? getTrack(trackIndex)->getName()
                                                           : juce::String())
                       + " - PAT " + juce::String(patternIndex + 1));
    });

    arrangementView.setPatternNameProvider([this] (int patternIndex)
    {
        return sessionState.getPatternName(patternIndex);
    });
    arrangementView.setPatternLengthProvider([this] (int patternIndex)
    {
        return getEffectivePatternLength(patternIndex);
    });
    arrangementView.setPatternRenameCallback([this] (int patternIndex) { renamePattern(patternIndex); });
    editorPanel.setPatternRenameCallback([this] { renamePattern(sessionState.getActivePattern()); });

    wireUndoHooks();

    editorPanel.setViewChangedCallback([this] { refreshMenuState(); });
    editorPanel.setPatternChangedCallback([this] (int patternIndex)
    {
        sessionState.setActivePattern(patternIndex);
    });
    editorPanel.setPatternLengthChangedCallback([this] (double beats)
    {
        editHistory.pushSnapshot(TRANS("Pattern length"));
        sessionState.setPatternLengthBeats(sessionState.getActivePattern(), beats);
        refreshPatternLength();
        setStatusMessage(beats <= 0.0
                             ? TRANS("Pattern length follows its contents.")
                             : TRANS("Pattern length locked to ")
                                   + juce::String(beats / audioEngine.getTransport().getBeatsPerBar(), 0)
                                   + " bar.");
    });
    editorPanel.setKeyboardMessageCallback([this] (const juce::MidiMessage& message)
    {
        midiEngine.postLiveMessage(message);
    });

    // Driving the on-screen keyboard's state rather than the engine directly:
    // the keys light up and the live MIDI path is the same one the mouse and a
    // hardware controller already use, so there is only ever one route in.
    typingKeyboard.onNoteOn = [this] (int note, float velocity)
    {
        editorPanel.getKeyboardState().noteOn(1, note, velocity);
        editorPanel.ensureKeyVisible(note);
    };

    typingKeyboard.onNoteOff = [this] (int note)
    {
        editorPanel.getKeyboardState().noteOff(1, note, 0.0f);
    };

    pluginBrowserView.setLoadPluginCallback([this] (int pluginIndex, int trackIndex)
    {
        loadSelectedPluginIntoTrack(pluginIndex, trackIndex);
    });
    pluginBrowserView.setOpenEditorCallback([this] (int trackIndex) { openTrackPluginEditor(trackIndex); });
    pluginBrowserView.setStatusCallback([this] (const juce::String& text) { setStatusMessage(text); });

    insertChainPanel.setLoadSelectedPluginCallback([this] (int trackIndex)
    {
        loadSelectedPluginIntoTrack(pluginBrowserView.getSelectedPluginIndex(), trackIndex);
    });
    insertChainPanel.setOpenEditorCallback([this] (int trackIndex) { openTrackPluginEditor(trackIndex); });
    insertChainPanel.setOpenSlotCallback([this] (int trackIndex, int slotIndex)
    {
        openTrackPlugin(trackIndex,
                        slotIndex < 0 ? PluginSlot::instrument : PluginSlot::insert,
                        juce::jmax(0, slotIndex));
    });

    projectManagerDialog.setCloseCallback([this] { closeDialogs(); });
    projectManagerDialog.setNewProjectCallback([this] { closeDialogs(); newProject(); });
    projectManagerDialog.setBrowseCallback([this] { closeDialogs(); openProject(); });
    projectManagerDialog.setOpenProjectCallback([this] (const juce::File& file)
    {
        closeDialogs();
        openProjectFile(file);
    });

    preferencesDialog.setTypingKeyboardEnabledCallback([this] (bool enabled)
    {
        typingKeyboard.setEnabled(enabled);
        setStatusMessage(enabled ? TRANS("Computer keyboard is now a MIDI controller.")
                                 : TRANS("The computer keyboard no longer plays notes."));
    });

    preferencesDialog.setTypingKeymapCallback([this] (int keymapIndex)
    {
        const auto keymap = keymapIndex == 1 ? TypingKeyboard::Keymap::simple
                                             : TypingKeyboard::Keymap::flStudio;
        typingKeyboard.setKeymap(keymap);
        setStatusMessage("Keymap: " + TypingKeyboard::getKeymapName(keymap));
    });

    preferencesDialog.setTypingOctaveCallback([this] (int octave)
    {
        typingKeyboard.setBaseOctave(octave);
        setStatusMessage(TRANS("Keyboard octave: ") + juce::String(typingKeyboard.getBaseOctave()));
    });

    preferencesDialog.setLanguageChangedCallback([this] (int index)
    {
        applyLanguage(index == 1 ? Localisation::Language::indonesian
                                 : Localisation::Language::english);
    });

    preferencesDialog.setCloseCallback([this] { closeDialogs(); });
    preferencesDialog.setThemeChangedCallback([this] (ThemeVariant variant) { applyThemeVariant(variant); });
    preferencesDialog.setScaleChangedCallback([this] (int percent) { setDisplayScalePercent(percent); });
    preferencesDialog.setScanRequestedCallback([this] { handleCommand(AppCommand::scanPlugins); });
    preferencesDialog.setTooltipsChangedCallback([this] (bool enabled)
    {
        tooltipWindow = enabled ? std::make_unique<juce::TooltipWindow>(this, 600) : nullptr;
    });
    preferencesDialog.setAutoScrollChangedCallback([this] (bool enabled)
    {
        arrangementView.setFollowPlayhead(enabled);
    });
    preferencesDialog.setAutoOpenEditorChangedCallback([this] (bool enabled)
    {
        autoOpenPluginEditor = enabled;
    });
    preferencesDialog.setToggleStates(true, arrangementView.isFollowingPlayhead(), autoOpenPluginEditor);
    preferencesDialog.setLanguageIndex(Localisation::getLanguage() == Localisation::Language::indonesian ? 1 : 0);
    preferencesDialog.setTypingKeyboardState(typingKeyboard.isEnabled(),
                                             typingKeyboard.getKeymap() == TypingKeyboard::Keymap::simple ? 1 : 0,
                                             typingKeyboard.getBaseOctave());

    mixerView.setTrackSelectedCallback([this] (int trackIndex) { selectTrack(trackIndex); });
    mixerView.setOpenChannelCallback([this] (int trackIndex) { openTrackPluginEditor(trackIndex); });

    // A lane created from a mixer strip has to show up as a playlist row, and
    // counts as an unsaved change like any other edit.
    mixerView.setAutomationChangedCallback([this]
    {
        arrangementView.repaint();
        markDirty();
    });

    sessionState.addChangeListener(this);
    pianoRollModel.addChangeListener(this);
    projectManager.addChangeListener(this);
    pluginManager.addChangeListener(this);

    setWantsKeyboardFocus(true);
    setSize(1600, 1000);

    if (! audioEngine.initialise())
        Logger::write("Audio engine started without an active device.");

    // Live playing reaches the graph only once the engine knows where to pull it.
    audioEngine.setLiveMidiSource(&midiEngine);
    startTimerHz(12);

    refreshBrowserLayoutState();
    syncBrowserPluginLibrary();
    selectTrack(0);
    refreshMenuState();
    browserPanel.refreshContent();
    projectDirty = false;
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.stopAudioRecording();
    audioEngine.setLiveMidiSource(nullptr);
    sessionState.removeChangeListener(this);
    pianoRollModel.removeChangeListener(this);
    projectManager.removeChangeListener(this);
    pluginManager.removeChangeListener(this);
    audioEngine.shutdown();

    tooltipWindow = nullptr;
    pluginWindows.clear();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::windowBackground());
}

void MainComponent::resized()
{
    workspace.setTransform(juce::AffineTransform());

    const auto area = getLocalBounds();
    const auto scaledWidth = juce::jmax(1, juce::roundToInt(static_cast<float>(area.getWidth()) / displayScale));
    const auto scaledHeight = juce::jmax(1, juce::roundToInt(static_cast<float>(area.getHeight()) / displayScale));
    workspace.setBounds(0, 0, scaledWidth, scaledHeight);
    workspace.setTransform(juce::AffineTransform::scale(displayScale));

    layoutWorkspace();
}

void MainComponent::layoutWorkspace()
{
    auto area = workspace.getLocalBounds();

    projectManagerDialog.setBounds(area);
    preferencesDialog.setBounds(area);

    menuBar.setBounds(area.removeFromTop(menuBarHeight));
    transportBar.setBounds(area.removeFromTop(transportHeight));
    statusBar.setBounds(area.removeFromBottom(statusBarHeight));

    area = area.reduced(gap);

    if (browserVisible)
    {
        const auto sideWidth = getBrowserPrimarySize();

        switch (browserDockPosition)
        {
            case BrowserDockPosition::right:
                browserPanel.setBounds(area.removeFromRight(sideWidth));
                area.removeFromRight(gap);
                break;

            case BrowserDockPosition::bottom:
            {
                const auto height = browserCollapsed ? 60 : (browserMinimized ? 118 : bottomBrowserHeight);
                browserPanel.setBounds(area.removeFromBottom(height));
                area.removeFromBottom(gap);
                break;
            }

            case BrowserDockPosition::left:
            default:
                browserPanel.setBounds(area.removeFromLeft(sideWidth));
                area.removeFromLeft(gap);
                break;
        }
    }

    panelHost.setBounds(area);
}

void MainComponent::buildDefaultPanelLayout(PanelHost& host, juce::Rectangle<int> area)
{
    // Familiar DAW tiling: playlist over editor on the left, plugin rack on the
    // right, mixer across the bottom. Every panel is free to move from here.
    const auto mixerHeight = juce::jlimit(120, 220, area.getHeight() / 3);
    const auto rightWidth = juce::jlimit(200, 320, area.getWidth() / 4);

    auto remaining = area;
    auto mixerRow = remaining.removeFromBottom(mixerHeight);
    remaining.removeFromBottom(gap);

    auto rightColumnArea = remaining.removeFromRight(rightWidth);
    remaining.removeFromRight(gap);

    auto pluginsArea = rightColumnArea.removeFromTop(juce::jmax(120, rightColumnArea.getHeight() * 3 / 5));
    rightColumnArea.removeFromTop(gap);

    auto playlistArea = remaining.removeFromTop(juce::jmax(120, remaining.getHeight() / 2));
    remaining.removeFromTop(gap);

    const auto place = [&host] (const juce::String& id, juce::Rectangle<int> bounds)
    {
        if (auto* panel = host.findPanel(id))
            panel->setRestoredBounds(bounds);
    };

    place(playlistPanelId, playlistArea);
    place(editorPanelId, remaining);
    place(pluginsPanelId, pluginsArea);
    place(insertPanelId, rightColumnArea);
    // Over the editor: it is the same kind of work on the same clip, and the
    // two are never wanted at once.
    place(samplePanelId, remaining);
    place(mixerPanelId, mixerRow);
}

void MainComponent::handleCommand(AppCommand command)
{
    switch (command)
    {
        case AppCommand::newProject:          newProject(); break;
        case AppCommand::openProject:         openProject(); break;
        case AppCommand::saveProject:         saveProject(false); break;
        case AppCommand::saveProjectAs:       saveProject(true); break;

        case AppCommand::exportAudio:
            exportAudio();
            break;

        case AppCommand::showProjectManager:
            projectManagerDialog.refresh();
            showDialog(&projectManagerDialog);
            break;

        case AppCommand::showPreferences:
            // Audio Device, not Appearance: the first thing a new user needs is
            // an input and an output, and the page was reachable only by
            // knowing to click past the theme picker.
            preferencesDialog.showPage(0);
            showDialog(&preferencesDialog);
            break;

        case AppCommand::audioSettings:
            preferencesDialog.showPage(0);
            showDialog(&preferencesDialog);
            break;

        case AppCommand::quit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;

        case AppCommand::scanPlugins:
            setStatusMessage("Scanning folder VST3...");
            pluginManager.scanPluginsAsync();
            browserPanel.setScanProgress(true, 0, 0, "");
            break;

        case AppCommand::about:
            // The version carries the git describe, so a bug report can always
            // be tied back to the exact commit it came from.
            showError("DJR_Studio",
                      "DJR_Studio " + juce::String(DJR_STUDIO_VERSION_STRING)
                          + "\n" + TRANS("Linux DAW - still in beta")
                          + "\nJUCE " + juce::SystemStats::getJUCEVersion());
            break;

        case AppCommand::toggleMetronome: toggleMetronome(); break;
        case AppCommand::countInOff:     setCountInBars(0); break;
        case AppCommand::countInOneBar:  setCountInBars(1); break;
        case AppCommand::countInTwoBars: setCountInBars(2); break;

        case AppCommand::undo: undoEdit(); break;
        case AppCommand::redo: redoEdit(); break;

        case AppCommand::togglePlaylist:    panelHost.togglePanel(playlistPanelId); break;
        case AppCommand::toggleEditor:      panelHost.togglePanel(editorPanelId); break;
        case AppCommand::toggleMixer:       panelHost.togglePanel(mixerPanelId); break;
        case AppCommand::togglePlugins:     panelHost.togglePanel(pluginsPanelId); break;
        case AppCommand::toggleInsertChain: panelHost.togglePanel(insertPanelId); break;
        case AppCommand::toggleSampleEditor: panelHost.togglePanel(samplePanelId); break;

        case AppCommand::resetPanelLayout:
            panelHost.resetLayout();
            setStatusMessage(TRANS("Panel layout reset to the default."));
            break;

        case AppCommand::toggleBrowser:        toggleBrowserVisible(); break;
        case AppCommand::cycleBrowserDock:     cycleBrowserDock(); break;

        case AppCommand::toggleVelocityLane:
            editorPanel.setVelocityLaneVisible(! editorPanel.isVelocityLaneVisible());
            refreshMenuState();
            break;

        case AppCommand::showPianoRoll:
            editorPanel.showPianoRoll();
            break;

        case AppCommand::showStepSequencer:
            editorPanel.showStepSequencer();
            break;
    }
}

void MainComponent::setDisplayScalePercent(int percent)
{
    const auto clamped = juce::jlimit(60, 140, percent);
    displayScale = static_cast<float>(clamped) / 100.0f;
    preferencesDialog.setScalePercent(clamped);
    resized();
    repaint();
}

void MainComponent::applyLanguage(Localisation::Language language)
{
    Localisation::setLanguage(language);
    Localisation::saveChoice(language);

    // Everything drawn in paint() picks the new language up on the next
    // repaint, which is the great majority of the interface. A handful of
    // captions were handed to a control once at construction - toolbar tooltips
    // and a few button labels - and those only change on the next start.
    setStatusMessage(Localisation::getLanguageName(language)
                         + " - " + TRANS("some labels change after a restart"));
    repaint();
}

void MainComponent::applyThemeVariant(ThemeVariant variant)
{
    Theme::setVariant(variant);
    lookAndFeel.refreshColours();
    sendLookAndFeelChange();
    repaint();
    setStatusMessage("Theme: " + Theme::getVariantName(variant) + ".");
}

void MainComponent::toggleBrowserCollapsed()
{
    browserCollapsed = ! browserCollapsed;
    refreshBrowserLayoutState();
    resized();
    repaint();
}

void MainComponent::toggleBrowserMinimized()
{
    if (browserCollapsed)
        browserCollapsed = false;

    browserMinimized = ! browserMinimized;
    refreshBrowserLayoutState();
    resized();
    repaint();
}

void MainComponent::cycleBrowserDock()
{
    if (browserDockPosition == BrowserDockPosition::left)
        browserDockPosition = BrowserDockPosition::right;
    else if (browserDockPosition == BrowserDockPosition::right)
        browserDockPosition = BrowserDockPosition::bottom;
    else
        browserDockPosition = BrowserDockPosition::left;

    refreshBrowserLayoutState();
    resized();
    repaint();
}

void MainComponent::toggleBrowserVisible()
{
    browserVisible = ! browserVisible;
    browserPanel.setVisible(browserVisible);
    refreshBrowserLayoutState();
    refreshMenuState();
    resized();
    repaint();
}

void MainComponent::refreshBrowserLayoutState()
{
    browserPanel.setCollapsed(browserCollapsed);
    browserPanel.setMinimized(browserMinimized);

    auto dock = BrowserPanel::DockPosition::left;
    if (browserDockPosition == BrowserDockPosition::right)
        dock = BrowserPanel::DockPosition::right;
    else if (browserDockPosition == BrowserDockPosition::bottom)
        dock = BrowserPanel::DockPosition::bottom;

    browserPanel.setDockPosition(dock);
    browserPanel.setVisible(browserVisible);

}

void MainComponent::refreshMenuState()
{
    ViewState state;
    state.playlistVisible = panelHost.isPanelOpen(playlistPanelId);
    state.editorVisible = panelHost.isPanelOpen(editorPanelId);
    state.mixerVisible = panelHost.isPanelOpen(mixerPanelId);
    state.pluginsVisible = panelHost.isPanelOpen(pluginsPanelId);
    state.insertChainVisible = panelHost.isPanelOpen(insertPanelId);
    state.sampleEditorVisible = panelHost.isPanelOpen(samplePanelId);
    state.velocityLaneVisible = editorPanel.isVelocityLaneVisible();
    state.browserVisible = browserVisible;
    state.pianoRollSelected = editorPanel.isPianoRollVisible();
    state.canUndo = editHistory.canUndo();
    state.canRedo = editHistory.canRedo();
    state.metronomeOn = audioEngine.getMetronome().isEnabled();
    state.countInBars = countInBars;
    state.undoName = editHistory.getUndoName();
    state.redoName = editHistory.getRedoName();
    menuBar.setViewState(state);

    // The header buttons follow the same state as the menu items.
    transportBar.setUndoState(state.canUndo, state.canRedo, state.undoName, state.redoName);
}

void MainComponent::syncBrowserPluginLibrary()
{
    const auto plugins = pluginManager.getKnownPlugins();
    browserPanel.setPluginList(plugins);
    browserPanel.setScanProgress(pluginManager.isScanning(), plugins.size(), plugins.size(), "");
    statusBar.setPluginCount(plugins.size());
}

void MainComponent::showDialog(juce::Component* dialog)
{
    closeDialogs();

    if (dialog == nullptr)
        return;

    dialog->setVisible(true);
    dialog->toFront(true);
}

void MainComponent::closeDialogs()
{
    projectManagerDialog.setVisible(false);
    preferencesDialog.setVisible(false);
}

int MainComponent::getBrowserPrimarySize() const noexcept
{
    if (browserCollapsed)
        return 30;

    if (browserMinimized)
        return 140;

    return Metrics::browserWidth;
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeDialogs();
        return true;
    }

    if (key == juce::KeyPress::spaceKey)
    {
        audioEngine.getTransport().togglePlayStop();
        return true;
    }

    // The typing keyboard gets first refusal on every letter it maps, which is
    // what FL does: with it on, R is F above middle C, not record. Bare-letter
    // shortcuts only exist for the keys it leaves alone, or when it is off.
    if (typingKeyboard.isKeyMapped(key))
        return false;

    if (key.getTextCharacter() == 'r' || key.getTextCharacter() == 'R')
    {
        toggleRecording();
        return true;
    }

    // Octave shift for the typing keyboard. Ctrl is needed rather than a bare
    // key because the FL layout leaves almost nothing on the letter rows free -
    // Z and X are notes there, which is where most DAWs put this.
    if ((key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown())
        && (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)))
    {
        const auto shift = key.isKeyCode(juce::KeyPress::upKey) ? 1 : -1;
        typingKeyboard.setBaseOctave(typingKeyboard.getBaseOctave() + shift);
        preferencesDialog.setTypingKeyboardState(typingKeyboard.isEnabled(),
                                                 typingKeyboard.getKeymap() == TypingKeyboard::Keymap::simple ? 1 : 0,
                                                 typingKeyboard.getBaseOctave());
        setStatusMessage(TRANS("Keyboard octave: ") + juce::String(typingKeyboard.getBaseOctave()));
        return true;
    }

    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
    {
        // Not getTextCharacter(): with ctrl held, X11 reports a control code
        // (ctrl+Z is 0x1A), so matching on the letter never fires on Linux.
        // getKeyCode() gives the plain uppercase key regardless of modifiers.
        const auto character = juce::CharacterFunctions::toLowerCase(
            static_cast<juce::juce_wchar>(key.getKeyCode()));

        if (character == 'z')
        {
            if (key.getModifiers().isShiftDown())
                redoEdit();
            else
                undoEdit();

            return true;
        }

        if (character == 'y')
        {
            redoEdit();
            return true;
        }

        if (character == 's')
        {
            saveProject(false);
            return true;
        }

        if (character == 'o')
        {
            openProject();
            return true;
        }

        if (character == 'n')
        {
            newProject();
            return true;
        }

        // R belongs to the typing keyboard while it is on, so record needs a
        // combination the note rows can never claim.
        if (character == 'r')
        {
            toggleRecording();
            return true;
        }
    }

    return false;
}

void MainComponent::prepareWarpedClips()
{
    const auto tempo = audioEngine.getTransport().getTempoBpm();

    if (std::abs(tempo - lastWarpTempo) < 1.0e-9)
        return;

    // Only clips that asked for pitch-preserved warp do any work here, and each
    // one keeps its own copy once built. Stretching a long take costs about a
    // second, which is why it is not attempted for every clip in the session.
    auto& mixer = audioEngine.getMixer();

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        auto* track = dynamic_cast<AudioTrack*>(mixer.getTrack(i));

        if (track == nullptr)
            continue;

        for (int clipIndex = 0; clipIndex < track->getNumClips(); ++clipIndex)
            if (auto* clip = track->getClip(clipIndex))
                clip->prepareWarp(tempo);
    }

    lastWarpTempo = tempo;
}

void MainComponent::timerCallback()
{
    // Here rather than at startup: the device can be swapped in Preferences,
    // and the input menu has to follow it. setDeviceInputCount ignores a value
    // it already has, so this costs nothing on the ticks where nothing changed.
    mixerView.setDeviceInputCount(audioEngine.getInputChannelCount());

    // Here rather than only after loading a plugin: a plugin may change its own
    // latency while it runs - an oversampling switch is the usual reason - and
    // nothing tells the host when it does.
    audioEngine.getMixer().refreshLatencyCompensation();
    prepareWarpedClips();

    // The count-in runs on the audio thread; this is where we notice it ended.
    if (waitingForCountIn && ! audioEngine.getMetronome().isCountingIn())
    {
        waitingForCountIn = false;
        audioEngine.getMetronome().setEnabled(metronomeWasEnabled);
        beginRecording();
    }

    collectRecordedNotes();
}

void MainComponent::toggleMetronome()
{
    auto& metronome = audioEngine.getMetronome();
    const auto turningOn = ! metronome.isEnabled();

    metronome.setEnabled(turningOn);
    transportBar.setMetronomeActive(turningOn);
    refreshMenuState();
    setStatusMessage(turningOn ? "Metronome nyala." : TRANS("Metronome off."));
}

void MainComponent::setCountInBars(int bars)
{
    countInBars = juce::jlimit(0, 8, bars);
    refreshMenuState();
    setStatusMessage(countInBars == 0
                         ? TRANS("Count-in off.")
                         : "Count-in " + juce::String(countInBars) + TRANS(" bars before recording."));
}

void MainComponent::toggleRecording()
{
    auto& transport = audioEngine.getTransport();

    if (waitingForCountIn)
    {
        audioEngine.getMetronome().cancelCountIn();
        audioEngine.getMetronome().setEnabled(metronomeWasEnabled);
        waitingForCountIn = false;
        setStatusMessage(TRANS("Count-in cancelled."));
        return;
    }

    if (transport.isRecording())
    {
        transport.setRecording(false);
        midiEngine.setRecordingArmed(false);
        collectRecordedNotes();

        const auto file = audioEngine.getCurrentRecordingFile();
        const auto wasWritingAudio = audioEngine.isAudioRecording();
        audioEngine.stopAudioRecording();

        if (wasWritingAudio && file.existsAsFile())
        {
            browserPanel.refreshContent();

            // Close the loop: the take lands on the timeline where it started.
            const auto trackIndex = findAudioTrackForRecording();

            if (trackIndex < 0 || ! addAudioClipToTrack(trackIndex, file, recordingStartBeat))
                setStatusMessage(TRANS("Recording saved: ") + file.getFileName()
                                     + " (" + juce::File::descriptionOfSizeInBytes(file.getSize()) + ")");
        }
        else
        {
            setStatusMessage(TRANS("Recording finished."));
        }

        markDirty();
        return;
    }

    // Count the click in first, if asked. Recording arms when it finishes.
    if (countInBars > 0)
    {
        auto& metronome = audioEngine.getMetronome();
        metronomeWasEnabled = metronome.isEnabled();
        waitingForCountIn = true;

        const auto beats = juce::roundToInt(countInBars * transport.getBeatsPerBar());
        metronome.startCountIn(beats, transport.getTempoBpm(), transport.getBeatsPerBar());

        setStatusMessage("Count-in " + juce::String(countInBars) + " bar...");
        return;
    }

    beginRecording();
}

void MainComponent::beginRecording()
{
    auto& transport = audioEngine.getTransport();

    midiEngine.discardRecordedNotes();
    midiEngine.setRecordingArmed(true);
    recordingStartBeat = transport.getPositionBeats();
    transport.setRecording(true);

    // Recording with the transport parked would capture nothing.
    if (! transport.isPlaying())
        transport.play();

    // Audio capture only makes sense when the device actually has inputs.
    if (audioEngine.getInputChannelCount() > 0)
    {
        const auto folder = FileUtils::getDefaultProjectRoot().getChildFile("Recordings");
        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
        const auto file = folder.getChildFile("take-" + stamp + ".wav");

        // The take lands on this track afterwards, so it is also the track
        // whose input should be captured.
        auto firstChannel = 0;
        auto numChannels = 0;

        if (auto* target = getTrack(findAudioTrackForRecording()))
        {
            firstChannel = juce::jmax(0, target->getInputChannel());
            numChannels = target->getInputChannelCount();
        }

        if (audioEngine.startAudioRecording(file, firstChannel, numChannels))
            setStatusMessage(TRANS("Recording to ") + file.getFileName() + TRANS(" - MIDI was recorded too."));
        else
            setStatusMessage(TRANS("Audio input could not be recorded; MIDI still was."));
    }
    else
    {
        setStatusMessage(TRANS("No audio input - recording MIDI only."));
    }
}

bool MainComponent::addAudioClipToTrack(int trackIndex, const juce::File& file, double startBeat)
{
    auto* audioTrack = dynamic_cast<AudioTrack*>(getTrack(trackIndex));

    if (audioTrack == nullptr)
    {
        setStatusMessage(TRANS("Pick an audio track first - ") + file.getFileName() + TRANS(" cannot be placed on a MIDI track."));
        return false;
    }

    juce::String error;
    auto clip = AudioClip::createFromFile(file, audioEngine.getCurrentSampleRate(), audioFormats, error);

    if (clip == nullptr)
    {
        setStatusMessage(error);
        return false;
    }

    const auto name = clip->getName();
    const auto lengthSeconds = clip->getPlayLengthSeconds();
    clip->setOriginalTempo(audioEngine.getTransport().getTempoBpm());
    clip->setStartBeat(startBeat);
    audioTrack->addClip(std::move(clip));

    arrangementView.repaint();
    markDirty();

    setStatusMessage(name + " (" + juce::String(lengthSeconds, 1) + TRANS(" s) landed on ")
                         + audioTrack->getName() + TRANS(" at beat ") + juce::String(startBeat, 2) + ".");
    return true;
}

int MainComponent::findAudioTrackForRecording()
{
    auto& mixer = audioEngine.getMixer();

    // Prefer an armed audio track, then the selection, then the first one.
    for (int i = 0; i < mixer.getNumTracks(); ++i)
        if (auto* track = mixer.getTrack(i))
            if (track->getKind() == TrackKind::audio && track->isRecordArmed())
                return i;

    const auto selected = sessionState.getSelectedTrack();

    if (auto* track = mixer.getTrack(selected))
        if (track->getKind() == TrackKind::audio)
            return selected;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        if (auto* track = mixer.getTrack(i))
            if (track->getKind() == TrackKind::audio)
                return i;

    return -1;
}

void MainComponent::collectRecordedNotes()
{
    auto captured = midiEngine.takeRecordedNotes();
    if (captured.isEmpty())
        return;

    auto notes = pianoRollModel.getNotes();
    notes.addArray(captured);
    pianoRollModel.setNotes(notes);

    setStatusMessage(juce::String(captured.size()) + TRANS(" MIDI notes recorded."));
}

void MainComponent::newProject()
{
    projectManager.newProject(FileUtils::getDefaultProjectRoot().getChildFile("Untitled"));
    for (int i = 0; i < audioEngine.getMixer().getNumTracks(); ++i)
    {
        if (auto* midiTrack = dynamic_cast<MidiTrack*>(getTrack(i)))
        {
            for (int pattern = 0; pattern < MidiTrack::maxPatterns; ++pattern)
                midiTrack->getClip(pattern).setNotes({});

            midiTrack->clearPlacements();
        }
    }

    pianoRollModel.notifyClipChanged();
    projectDirty = false;
    refreshMenuState();
    setStatusMessage(TRANS("New project created."));
}

void MainComponent::saveProject(bool forceChooser)
{
    synchroniseProjectState();

    const auto currentFile = projectManager.getProject().projectFile;

    if (! forceChooser && currentFile.existsAsFile())
    {
        juce::String error;
        if (projectManager.saveProject(currentFile, error))
        {
            projectDirty = false;
            refreshMenuState();
            setStatusMessage(TRANS("Saved: ") + currentFile.getFileName());
        }
        else
        {
            showError("Save failed", error);
        }

        return;
    }

    auto start = currentFile.existsAsFile()
        ? currentFile
        : FileUtils::getDefaultProjectRoot().getChildFile("Untitled.djrs");

    fileChooser = std::make_unique<juce::FileChooser>("Save DJR_Studio project", start, "*.djrs");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                           | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            if (! file.hasFileExtension(".djrs"))
                file = file.withFileExtension(".djrs");

            juce::String error;
            if (! projectManager.saveProject(file, error))
            {
                showError("Save failed", error);
                return;
            }

            projectDirty = false;
            refreshMenuState();
            setStatusMessage(TRANS("Saved: ") + file.getFileName());
        });
}

void MainComponent::openProject()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open DJR_Studio project",
                                                      FileUtils::getDefaultProjectRoot(),
                                                      "*.djrs");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();
            if (file != juce::File())
                openProjectFile(file);
        });
}

void MainComponent::openProjectFile(const juce::File& file)
{
    juce::String error;

    if (! projectManager.loadProject(file, error))
    {
        showError("Open failed", error);
        return;
    }

    projectDirty = false;
    setStatusMessage(TRANS("Project opened: ") + file.getFileName());
}

double MainComponent::getSongLengthBeats()
{
    auto& mixer = audioEngine.getMixer();
    const auto tempo = audioEngine.getTransport().getTempoBpm();
    auto last = 0.0;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        if (const auto* midiTrack = dynamic_cast<const MidiTrack*>(mixer.getTrack(i)))
        {
            for (const auto& placement : midiTrack->getPlacements())
                last = juce::jmax(last, placement.startBeat + placement.lengthBeats);

            for (int pattern = 0; pattern < MidiTrack::maxPatterns; ++pattern)
                for (const auto& note : midiTrack->getClip(pattern).getNotesSnapshot())
                    last = juce::jmax(last, note.startBeat + note.lengthBeats);
        }

        if (const auto* audioTrack = dynamic_cast<const AudioTrack*>(mixer.getTrack(i)))
            for (const auto* clip : audioTrack->getClipsSnapshot())
                if (clip != nullptr)
                    last = juce::jmax(last, clip->getStartBeat() + clip->getLengthBeats(tempo));
    }

    // Round up to a whole bar in the current signature, and never export nothing.
    const auto barBeats = audioEngine.getTransport().getBeatsPerBar();
    return juce::jmax(barBeats, std::ceil(last / barBeats) * barBeats);
}

void MainComponent::exportAudio()
{
    auto start = projectManager.getProject().projectFile;
    start = start.existsAsFile()
        ? start.getSiblingFile(start.getFileNameWithoutExtension() + ".wav")
        : FileUtils::getDefaultProjectRoot().getChildFile("Export.wav");

    fileChooser = std::make_unique<juce::FileChooser>(TRANS("Export audio to WAV"), start, "*.wav");

    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode
                           | juce::FileBrowserComponent::canSelectFiles
                           | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == juce::File())
                return;

            if (! file.hasFileExtension(".wav"))
                file = file.withFileExtension(".wav");

            ExportManager::Options options;
            options.sampleRate = audioEngine.getCurrentSampleRate();
            options.blockSize = audioEngine.getCurrentBufferSize();
            options.tempoBpm = audioEngine.getTransport().getTempoBpm();
            options.startBeat = 0.0;
            options.lengthBeats = getSongLengthBeats();
            options.songMode = sessionState.isSongMode();

            setStatusMessage("Rendering "
                           + juce::String(options.lengthBeats / audioEngine.getTransport().getBeatsPerBar(), 1)
                           + " bar...");

            juce::String error;
            auto succeeded = false;

            // The device must let go of the mixer before we drive it by hand.
            audioEngine.renderOffline([&]
            {
                succeeded = exportManager.render(audioEngine.getMixer(), file, options, error);
            });

            if (! succeeded)
            {
                showError(TRANS("Export failed"), error);
                return;
            }

            browserPanel.refreshContent();
            setStatusMessage(TRANS("Export finished: ") + file.getFileName()
                                 + " (" + juce::File::descriptionOfSizeInBytes(file.getSize()) + ")");
        });
}

juce::File MainComponent::renderTrackToFile(int trackIndex, const juce::String& suffix)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return {};

    const auto folder = FileUtils::getDefaultProjectRoot().getChildFile("Renders");

    // The track name is in the file name so a folder full of renders is still
    // readable, and the stamp keeps a re-freeze from overwriting a file another
    // clip might still be reading.
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto file = folder.getChildFile(track->getName().retainCharacters(
                                              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
                                          + "-" + suffix + "-" + stamp + ".wav");

    TrackRenderer::Options options;
    options.sampleRate = audioEngine.getCurrentSampleRate();
    options.blockSize = audioEngine.getCurrentBufferSize();
    options.tempoBpm = audioEngine.getTransport().getTempoBpm();
    options.startBeat = 0.0;
    options.lengthBeats = getSongLengthBeats();
    options.songMode = sessionState.isSongMode();

    juce::String error;
    auto succeeded = false;

    // The device must let go of the track before we drive it by hand, exactly
    // as a full export does.
    audioEngine.renderOffline([&]
    {
        succeeded = trackRenderer.render(*track, file, options, error);
    });

    if (! succeeded)
    {
        showError(TRANS("Track render failed"), error);
        return {};
    }

    return file;
}

void MainComponent::freezeTrack(int trackIndex)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return;

    if (track->isFrozen())
    {
        // Unfreeze: the clips, instrument and inserts were never touched, so
        // dropping the render is all it takes to get them back.
        track->setFrozenAudio(nullptr);
        mixerView.repaint();
        arrangementView.repaint();
        markDirty();
        setStatusMessage("Unfreeze: " + track->getName() + TRANS(" is processed live again."));
        return;
    }

    setStatusMessage("Freeze " + track->getName() + "...");

    const auto file = renderTrackToFile(trackIndex, "freeze");

    if (file == juce::File())
        return;

    juce::String error;
    auto clip = AudioClip::createFromFile(file, audioEngine.getCurrentSampleRate(), audioFormats, error);

    if (clip == nullptr)
    {
        showError(TRANS("Freeze failed"), TRANS("The render could not be read back: ") + error);
        return;
    }

    // The render is already at the session tempo and must play back sample for
    // sample, so warping it would resample what was just rendered.
    clip->setWarpEnabled(false);
    track->setFrozenAudio(std::move(clip));

    browserPanel.refreshContent();
    mixerView.repaint();
    arrangementView.repaint();
    markDirty();
    setStatusMessage(TRANS("Freeze finished: ") + track->getName()
                         + " (" + juce::File::descriptionOfSizeInBytes(file.getSize()) + ")");
}

void MainComponent::bounceTrackToAudio(int trackIndex)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return;

    setStatusMessage("Bounce " + track->getName() + "...");

    const auto sourceName = track->getName();
    const auto file = renderTrackToFile(trackIndex, "bounce");

    if (file == juce::File())
        return;

    // A bounce is a new audio track rather than a replacement: the source is
    // left exactly as it was, so the render can be thrown away without having
    // destroyed anything.
    editHistory.pushSnapshot(TRANS("Bounce to audio"));

    auto* bounced = audioEngine.getMixer().addTrack(std::make_unique<AudioTrack>(sourceName + " (bounce)"));

    if (bounced == nullptr)
    {
        showError(TRANS("Bounce failed"), TRANS("The mixer is full - no more tracks can be added."));
        return;
    }

    const auto bouncedIndex = audioEngine.getMixer().indexOf(bounced);

    arrangementView.notifyTrackListChanged();
    mixerView.refreshStrips();
    insertChainPanel.refresh();
    pluginBrowserView.refreshTrackList();

    if (! addAudioClipToTrack(bouncedIndex, file, 0.0))
    {
        showError(TRANS("Bounce failed"), TRANS("The render could not be placed on the new track."));
        return;
    }

    browserPanel.refreshContent();
    selectTrack(bouncedIndex);
    markDirty();
    setStatusMessage(TRANS("Bounce finished: ") + file.getFileName()
                         + " (" + juce::File::descriptionOfSizeInBytes(file.getSize()) + ")");
}

void MainComponent::showError(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, title, message);
}

void MainComponent::setStatusMessage(const juce::String& message)
{
    statusBar.setMessage(message);
}

void MainComponent::markDirty()
{
    projectDirty = true;

    const auto& project = projectManager.getProject();
    const auto file = project.projectFile;
    menuBar.setProjectInfo(file.existsAsFile() ? file.getParentDirectory().getFullPathName() + "/"
                                               : FileUtils::getDefaultProjectRoot().getFullPathName() + "/",
                           file.existsAsFile() ? file.getFileName() : juce::String("Untitled.djrs"),
                           projectDirty);
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &sessionState)
    {
        applySelectionToPanels();
        return;
    }

    if (source == &pianoRollModel)
    {
        // The model edits the track's clip in place, so the engine already has
        // the change; only the project snapshot and the playlist need telling.
        refreshPatternLength();
        synchroniseProjectState();
        arrangementView.repaint();
        projectDirty = true;
        markDirty();
        return;
    }

    if (source == &projectManager)
    {
        applyProjectToSession();

        const auto& project = projectManager.getProject();
        const auto file = project.projectFile;
        menuBar.setProjectInfo(file.existsAsFile() ? file.getParentDirectory().getFullPathName() + "/"
                                                   : FileUtils::getDefaultProjectRoot().getFullPathName() + "/",
                               file.existsAsFile() ? file.getFileName() : juce::String("Untitled.djrs"),
                               projectDirty);
        return;
    }

    if (source == &pluginManager)
    {
        syncBrowserPluginLibrary();
        insertChainPanel.refresh();
    }
}

void MainComponent::loadSelectedPluginIntoTrack(int pluginIndex, int trackIndex)
{
    const auto plugins = pluginManager.getKnownPlugins();
    auto* track = getTrack(trackIndex);

    if (! juce::isPositiveAndBelow(pluginIndex, plugins.size()) || track == nullptr)
    {
        setStatusMessage(TRANS("Pick a plugin in the PLUGINS panel first."));
        return;
    }

    const auto description = plugins[pluginIndex];
    setStatusMessage(TRANS("Loading ") + description.name + TRANS(" into ") + track->getName() + "...");

    pluginManager.createPluginAsync(description,
                                    audioEngine.getCurrentSampleRate(),
                                    audioEngine.getCurrentBufferSize(),
        [this, trackIndex, description] (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String error)
        {
            if (instance == nullptr)
            {
                setStatusMessage(error.isNotEmpty() ? error : TRANS("The plugin could not be created."));
                return;
            }

            if (auto* targetTrack = getTrack(trackIndex))
            {
                prepareBuiltInEditor(*instance, trackIndex);

                // Instruments own the track's synth slot; everything else is an insert.
                if (description.isInstrument)
                {
                    targetTrack->setInstrument(std::move(instance));

                    // Whatever channel window was open for this track showed the
                    // preview synth's placeholder page. It is no longer true.
                    closeEmptyChannelWindow(targetTrack);
                }
                else
                {
                    targetTrack->addPlugin(std::move(instance));
                }

                mixerView.repaint();
                insertChainPanel.refresh();
                pluginBrowserView.refreshTrackList();
                setStatusMessage(description.name
                                     + (description.isInstrument ? TRANS(" loaded as the instrument on ") : TRANS(" loaded as an insert on "))
                                     + targetTrack->getName() + ".");

                if (autoOpenPluginEditor)
                    openTrackPluginEditor(trackIndex);

                markDirty();
                synchroniseProjectState();
            }
        });
}

void MainComponent::openTrackPluginEditor(int trackIndex)
{
    openTrackPlugin(trackIndex, PluginSlot::automatic, 0);
}

void MainComponent::openTrackPlugin(int trackIndex, PluginSlot slot, int insertIndex)
{
    auto* track = getTrack(trackIndex);
    if (track == nullptr)
        return;

    if (slot != PluginSlot::insert)
    {
        if (auto* synth = track->getInstrument())
        {
            showPluginWindow(synth, track);
            return;
        }

        if (slot == PluginSlot::instrument)
        {
            setStatusMessage(TRANS("This track has no instrument yet."));
            return;
        }
    }

    if (track->getPluginCount() == 0)
    {
        // A MIDI channel with nothing loaded is not empty: it plays through its
        // preview synth, and the channel's own envelope, filter and arpeggiator
        // pages act on it. So the window opens with no generator in it rather
        // than refusing, the way FL opens a channel whatever is inside it.
        const auto kind = track->getKind();

        if (slot == PluginSlot::automatic && (kind == TrackKind::midi || kind == TrackKind::instrument))
        {
            showPluginWindow(nullptr, track);
            return;
        }

        setStatusMessage(TRANS("This track has no plugin to open."));
        return;
    }

    const auto index = juce::jlimit(0, track->getPluginCount() - 1,
                                    slot == PluginSlot::insert ? insertIndex : track->getPluginCount() - 1);

    if (auto* plugin = track->getPlugin(index))
        showPluginWindow(plugin, track);
}

void MainComponent::prepareBuiltInEditor(juce::AudioPluginInstance& plugin, int trackIndex)
{
    auto* editor = dynamic_cast<AudioEditorProcessor*>(&plugin);

    if (editor == nullptr)
        return;

    // Where its captures are written, and where a send lands: the project's own
    // Samples folder, so a project carries its audio rather than pointing at
    // somewhere on this machine.
    editor->setWorkingFolder(projectManager.getProject().samplesFolder);

    editor->setSendToPlaylistCallback([this, trackIndex] (const juce::File& file, const juce::String& error)
    {
        if (file == juce::File())
        {
            setStatusMessage(error.isNotEmpty() ? error : TRANS("Nothing was sent."));
            return;
        }

        // Onto the track the plugin itself sits on, at the playhead: the answer
        // to "where did it go" should be somewhere the user was already looking.
        addAudioClipToTrack(trackIndex, file, audioEngine.getTransport().getPositionBeats());
    });

    // Dragging aims it instead of accepting the default. The audio is written
    // only once the position turns out to be a track, so a drag let go over
    // nothing costs nothing.
    editor->setDropAtCallback([this] (juce::Point<int> screenPosition, const std::function<juce::File()>& writeAudio)
    {
        auto dropTrack = -1;
        auto dropBeat = 0.0;

        if (! arrangementView.findDropTarget(screenPosition, dropTrack, dropBeat))
        {
            setStatusMessage(TRANS("Let go over a playlist track to place it there."));
            return;
        }

        const auto file = writeAudio();

        if (file == juce::File())
        {
            setStatusMessage(TRANS("There is nothing to place yet."));
            return;
        }

        addAudioClipToTrack(dropTrack, file, dropBeat);
    });
}

void MainComponent::showPluginWindow(juce::AudioProcessor* plugin, Track* track)
{
    for (auto& window : pluginWindows)
    {
        if (window == nullptr)
            continue;

        // A window with no plugin in it is identified by its track instead:
        // there is no processor to match it by, and one channel wants one window.
        const auto sameWindow = plugin != nullptr
                                    ? window->getProcessor() == plugin
                                    : (window->getProcessor() == nullptr && window->getTrack() == track);

        if (sameWindow)
        {
            window->setVisible(true);
            window->toFront(true);
            return;
        }
    }

    // The channel window opened before anything was loaded is now out of date:
    // this track has a generator, and the placeholder page would sit there
    // claiming it does not.
    if (plugin != nullptr && track != nullptr)
        closeEmptyChannelWindow(track);

    auto window = std::make_unique<PluginWindow>(plugin, track);
    window->toFront(true);
    pluginWindows.push_back(std::move(window));
}

void MainComponent::closeWindowsForMissingTracks()
{
    // A deleted track takes its plugins with it, so a window still pointing at
    // either is holding a dangling pointer - and it is still on screen.
    const auto gone = std::remove_if(pluginWindows.begin(), pluginWindows.end(),
        [this] (const std::unique_ptr<PluginWindow>& window)
        {
            if (window == nullptr)
                return true;

            auto* track = window->getTrack();

            if (track == nullptr)
                return false;

            auto& mixer = audioEngine.getMixer();

            for (int i = 0; i < mixer.getNumTracks(); ++i)
                if (mixer.getTrack(i) == track)
                    return false;

            return true;
        });

    pluginWindows.erase(gone, pluginWindows.end());
}

void MainComponent::closeEmptyChannelWindow(Track* track)
{
    const auto stale = std::remove_if(pluginWindows.begin(), pluginWindows.end(),
        [track] (const std::unique_ptr<PluginWindow>& window)
        {
            return window != nullptr && window->getProcessor() == nullptr && window->getTrack() == track;
        });

    pluginWindows.erase(stale, pluginWindows.end());
}

void MainComponent::selectTrack(int trackIndex)
{
    const auto clamped = juce::jlimit(0, juce::jmax(0, audioEngine.getMixer().getNumTracks() - 1), trackIndex);

    if (clamped == sessionState.getSelectedTrack())
    {
        // Same track, but the panels may still be stale after an add or remove.
        applySelectionToPanels();
        return;
    }

    sessionState.setSelectedTrack(clamped);
}

double MainComponent::getEffectivePatternLength(int patternIndex)
{
    const auto manual = sessionState.getPatternLengthBeats(patternIndex);

    if (manual > 0.0)
        return manual;

    // Auto: follow the furthest note in this pattern across every track,
    // rounded up to a whole bar so the loop lands musically.
    auto& mixer = audioEngine.getMixer();
    auto lastBeat = 0.0;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        if (const auto* midiTrack = dynamic_cast<const MidiTrack*>(mixer.getTrack(i)))
            for (const auto& note : midiTrack->getClip(patternIndex).getNotesSnapshot())
                lastBeat = juce::jmax(lastBeat, note.startBeat + note.lengthBeats);

    const auto barBeats = audioEngine.getTransport().getBeatsPerBar();
    return juce::jmax(barBeats, std::ceil(lastBeat / barBeats) * barBeats);
}

void MainComponent::wireUndoHooks()
{
    // Recording a point changes what Undo/Redo would do, so the menu has to be
    // rebuilt: it caches its state rather than asking each time it opens.
    sampleEditorView.setExportCallback([this] (const juce::File& file, const juce::String& error)
    {
        if (error.isNotEmpty())
        {
            setStatusMessage(TRANS("Export failed: ") + error);
            return;
        }

        setStatusMessage(TRANS("Sample exported: ") + file.getFileName());
        browserPanel.refreshContent();
    });

    sampleEditorView.setBeforeEditCallback([this] (const juce::String& name)
    {
        editHistory.pushSnapshot(name);
        refreshMenuState();
    });
    sampleEditorView.setEditCallback([this] (const juce::String& name)
    {
        if (name.isEmpty())
        {
            // The edit had nothing to do. Nothing was snapshotted either, so
            // there is only the status line to answer with.
            setStatusMessage(TRANS("Nothing to change: the clip is already like that."));
            return;
        }

        arrangementView.repaint();
        markDirty();
        setStatusMessage(name);
    });

    arrangementView.setUndoHooks([this] (const juce::String& name)
                                 {
                                     editHistory.pushSnapshot(name);
                                     refreshMenuState();
                                 },
                                 [this] (bool opening)
                                 {
                                     if (opening)
                                         editHistory.beginGesture({});
                                     else
                                         editHistory.endGesture();
                                 });

    // Every note editor writes through the model, so one hook covers the piano
    // roll, the velocity lane and the step sequencer alike.
    pianoRollModel.onBeforeEdit = [this]
    {
        // Only the first edit of a drag is a new undo step, and only then is
        // there anything for the menus to say differently. Refreshing them on
        // every mouse move made dragging a note feel like wading.
        if (editHistory.pushSnapshot("Edit note"))
            refreshMenuState();
    };

    const auto bracket = [this] (bool opening)
    {
        if (opening)
            editHistory.beginGesture({});
        else
            editHistory.endGesture();
    };

    editorPanel.setNoteGestureCallback(bracket);

    editHistory.onChanged = [this]
    {
        // The model changed underneath every panel, so make them all re-read it.
        pianoRollModel.notifyClipChanged();
        arrangementView.repaint();
        editorPanel.repaint();
        // The clip it was showing has been replaced by the restored one, so it
        // has to look the clip up again rather than redraw the old pointer.
        sampleEditorView.refresh();
        refreshMenuState();
        markDirty();
    };
}

void MainComponent::undoEdit()
{
    if (! editHistory.canUndo())
    {
        setStatusMessage(TRANS("Nothing to undo."));
        return;
    }

    const auto name = editHistory.getUndoName();
    editHistory.undo();
    setStatusMessage("Undo: " + (name.isEmpty() ? TRANS("the last edit") : name));
}

void MainComponent::redoEdit()
{
    if (! editHistory.canRedo())
    {
        setStatusMessage(TRANS("Nothing to redo."));
        return;
    }

    const auto name = editHistory.getRedoName();
    editHistory.redo();
    setStatusMessage("Redo: " + (name.isEmpty() ? TRANS("the last edit") : name));
}

void MainComponent::renameTrack(int trackIndex)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return;

    auto* window = new juce::AlertWindow(TRANS("Rename track"),
                                         TRANS("Name for track ") + juce::String(trackIndex + 1) + ":",
                                         juce::AlertWindow::NoIcon);

    window->addTextEditor("name", track->getName(), juce::String());
    window->addButton(TRANS("Save"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true,
        juce::ModalCallbackFunction::create([this, trackIndex, window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned(window);

            if (result != 1)
                return;

            auto* renamed = getTrack(trackIndex);

            if (renamed == nullptr)
                return;

            editHistory.pushSnapshot(TRANS("Rename track"));
            renamed->setName(owned->getTextEditorContents("name"));

            // The name is shown in four places at once, so all of them are
            // re-read rather than left to notice on their own.
            arrangementView.repaint();
            mixerView.refreshStrips();
            mixerView.setSelectedTrack(sessionState.getSelectedTrack());
            insertChainPanel.refresh();
            markDirty();
            setStatusMessage(TRANS("Track: ") + renamed->getName());
        }),
        false);
}

void MainComponent::renamePattern(int patternIndex)
{
    if (! juce::isPositiveAndBelow(patternIndex, SessionState::maxPatterns))
        return;

    auto* window = new juce::AlertWindow(TRANS("Rename pattern"),
                                         TRANS("Name for PAT ") + juce::String(patternIndex + 1) + ":",
                                         juce::AlertWindow::NoIcon);

    window->addTextEditor("name", sessionState.getCustomPatternName(patternIndex), juce::String());
    window->addButton(TRANS("Save"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(TRANS("Cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true,
        juce::ModalCallbackFunction::create([this, patternIndex, window] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned(window);

            if (result != 1)
                return;

            // An empty name is how you go back to the "PAT n" default.
            editHistory.pushSnapshot(TRANS("Rename pattern"));
            sessionState.setPatternName(patternIndex, owned->getTextEditorContents("name"));
            refreshPatternName();
            arrangementView.repaint();
            markDirty();
            setStatusMessage("Pattern: " + sessionState.getPatternName(patternIndex));
        }),
        false);
}

void MainComponent::refreshPatternName()
{
    editorPanel.setPatternName(sessionState.getPatternName(sessionState.getActivePattern()));
}

void MainComponent::refreshPatternLength()
{
    const auto length = getEffectivePatternLength(sessionState.getActivePattern());

    // This is the bug fix: the loop used to be pinned at 16 beats forever, so a
    // one bar pattern played followed by three bars of silence.
    audioEngine.getTransport().setLoopRangeBeats(0.0, length);
    arrangementView.setPatternLengthBeats(length);
    editorPanel.setPatternLengthBeats(length, sessionState.getPatternLengthBeats(sessionState.getActivePattern()) > 0.0);
}

void MainComponent::applySelectionToPanels()
{
    const auto trackIndex = juce::jlimit(0,
                                         juce::jmax(0, audioEngine.getMixer().getNumTracks() - 1),
                                         sessionState.getSelectedTrack());

    // Every MIDI track edits and plays the same pattern slot.
    const auto pattern = sessionState.getActivePattern();

    for (int i = 0; i < audioEngine.getMixer().getNumTracks(); ++i)
        if (auto* midi = dynamic_cast<MidiTrack*>(getTrack(i)))
            midi->setActivePattern(pattern);

    audioEngine.getTransport().setSongMode(sessionState.isSongMode());
    editorPanel.setActivePattern(pattern);
    arrangementView.setActivePattern(pattern);
    refreshPatternName();
    refreshPatternLength();

    // Whose notes the roll is showing. Only a track that has notes puts its
    // name up there: on an audio track the roll is empty and naming it would
    // suggest the two had something to do with each other.
    const auto* selected = getTrack(trackIndex);
    editorPanel.setTrackName(dynamic_cast<const MidiTrack*>(selected) != nullptr ? selected->getName()
                                                                                 : juce::String());

    arrangementView.setSelectedTrack(trackIndex);
    insertChainPanel.setSelectedTrack(trackIndex);
    pluginBrowserView.setTargetTrack(trackIndex);
    mixerView.setSelectedTrack(trackIndex);

    // Playing the keyboard should sound on whichever track is selected.
    audioEngine.getMixer().setLiveMidiTarget(trackIndex);

    // The piano roll, velocity lane and step sequencer are all views onto the
    // selected track's clip - point them at it, or blank them for audio tracks.
    auto* midiTrack = dynamic_cast<MidiTrack*>(getTrack(trackIndex));
    pianoRollModel.setTargetClip(midiTrack != nullptr ? &midiTrack->getClip() : nullptr);

    if (auto* track = getTrack(trackIndex))
    {
        // A bus is neither MIDI nor audio, and calling it an audio track was
        // simply wrong once buses existed.
        const auto note = track->getKind() == TrackKind::midi ? juce::String()
                        : track->getKind() == TrackKind::bus
                              ? juce::String(TRANS(" (bus - receives sends from other tracks)"))
                              : juce::String(TRANS(" (audio track - the MIDI editor is empty)"));

        setStatusMessage(TRANS("Active track: ") + track->getName() + note);
    }
}

void MainComponent::synchroniseProjectState()
{
    auto& project = projectManager.getProject();
    project.tempo = audioEngine.getTransport().getTempoBpm();
    project.timeSigNumerator = audioEngine.getTransport().getTimeSignatureNumerator();
    project.timeSigDenominator = audioEngine.getTransport().getTimeSignatureDenominator();
    project.sampleRate = audioEngine.getCurrentSampleRate();
    project.tracks.clear();
    project.panelLayout = panelHost.getLayoutState();

    project.patternNames.clear();
    project.patternLengths.clear();
    for (int i = 0; i < SessionState::maxPatterns; ++i)
    {
        project.patternNames.add(sessionState.getCustomPatternName(i));
        project.patternLengths.add(sessionState.getPatternLengthBeats(i));
    }

    for (int i = 0; i < audioEngine.getMixer().getNumTracks(); ++i)
    {
        const auto* track = audioEngine.getMixer().getTrack(i);
        if (track == nullptr)
            continue;

        ProjectTrackState state;
        state.name = track->getName();
        state.type = track->getKind() == TrackKind::midi ? "midi"
                   : track->getKind() == TrackKind::bus ? "bus"
                                                        : "audio";
        state.outputDestination = track->getOutputDestination();
        state.inputChannel = track->getInputChannel();
        state.inputStereo = track->isInputStereo();
        state.channelSettings = track->getChannelSettings().toVar();
        state.frozenFile = track->isFrozen() ? track->getFrozenFile().getFullPathName() : juce::String();

        for (int slot = 0; slot < Track::maxSends; ++slot)
        {
            const auto send = track->getSend(slot);

            auto* sendObject = new juce::DynamicObject();
            sendObject->setProperty("destination", send.destination);
            sendObject->setProperty("level", send.level);
            sendObject->setProperty("preFader", send.preFader);
            state.sends.add(juce::var(sendObject));
        }

        state.volume = track->getVolume();
        state.pan = track->getPan();
        state.mute = track->isMuted();
        state.solo = track->isSoloed();
        state.plugins = track->getPluginNames();
        state.laneHeight = arrangementView.getRowHeight(i);

        // Plugins are saved with their own parameter state so a project reopens
        // sounding the way it was left, not just with the right names.
        const auto capturePlugin = [this] (juce::AudioPluginInstance& plugin, bool isInstrument)
        {
            // Save-as moves the project; the plugin is told again rather than
            // writing its audio next to where the project used to be.
            if (auto* editor = dynamic_cast<AudioEditorProcessor*>(&plugin))
                editor->setWorkingFolder(projectManager.getProject().samplesFolder);

            juce::MemoryBlock stateBlock;
            plugin.getStateInformation(stateBlock);

            auto* object = new juce::DynamicObject();
            object->setProperty("name", plugin.getName());
            object->setProperty("isInstrument", isInstrument);
            object->setProperty("state", stateBlock.toBase64Encoding());

            if (auto xml = plugin.getPluginDescription().createXml())
                object->setProperty("description", xml->toString());

            return juce::var(object);
        };

        auto* editableTrack = audioEngine.getMixer().getTrack(i);

        if (editableTrack != nullptr)
        {
            if (auto* instrument = editableTrack->getInstrument())
                state.pluginStates.add(capturePlugin(*instrument, true));

            for (int slot = 0; slot < editableTrack->getPluginCount(); ++slot)
                if (auto* insert = editableTrack->getPlugin(slot))
                    state.pluginStates.add(capturePlugin(*insert, false));

            for (int lane = 0; lane < editableTrack->getNumAutomationLanes(); ++lane)
                if (const auto* automationLane = editableTrack->getAutomationLane(lane))
                    state.automation.add(automationLane->toVar());
        }

        if (const auto* audioTrack = dynamic_cast<const AudioTrack*>(track))
            for (const auto* clip : audioTrack->getClipsSnapshot())
                if (clip != nullptr)
                    state.audioClips.add(clip->toVar());

        // Every pattern of the track is saved, plus where they sit in the song.
        if (const auto* midiTrack = dynamic_cast<const MidiTrack*>(track))
        {
            for (int pattern = 0; pattern < MidiTrack::maxPatterns; ++pattern)
            {
                juce::Array<juce::var> patternNotes;

                for (const auto& note : midiTrack->getClip(pattern).getNotesSnapshot())
                    patternNotes.add(note.toVar());

                state.patterns.add(patternNotes);
            }

            for (const auto& placement : midiTrack->getPlacements())
                state.placements.add(placement.toVar());

            // Kept for older projects that only understood one clip.
            for (const auto& note : midiTrack->getClip().getNotesSnapshot())
                state.midiNotes.add(note.toVar());
        }

        project.tracks.add(state);
    }
}

void MainComponent::showSampleEditor(int trackIndex, int clipIndex)
{
    auto* audioTrack = dynamic_cast<AudioTrack*>(getTrack(trackIndex));
    auto* clip = audioTrack != nullptr ? audioTrack->getClip(clipIndex) : nullptr;

    if (clip == nullptr)
    {
        setStatusMessage(TRANS("That clip has no audio to edit."));
        return;
    }

    selectTrack(trackIndex);

    // The panel keeps following the clip even though the double click now opens
    // the plugin. The two edit different things on purpose: the panel rewrites
    // the clip on the timeline and can be undone, the plugin works on a copy
    // that comes back only when it is sent back. View - Sample Editor is still
    // the way to the first one.
    //
    // Looked up by index rather than held: undo swaps a track's whole clip list
    // out, and a pointer taken now would point at a clip that no longer exists.
    sampleEditorView.setClipSource([this, trackIndex, clipIndex] () -> AudioClip*
                                   {
                                       auto* track = dynamic_cast<AudioTrack*>(getTrack(trackIndex));
                                       return track != nullptr ? track->getClip(clipIndex) : nullptr;
                                   },
                                   audioTrack->getName() + " - " + clip->getName());

    // The project's own Samples folder is where an exported edit belongs, so
    // that is where the chooser opens.
    sampleEditorView.setExportContext(&audioFormats, projectManager.getProject().samplesFolder);
    refreshMenuState();

    if (auto* editor = findBuiltInEditor(trackIndex))
    {
        openClipInBuiltInEditor(*editor, trackIndex, clipIndex);
        return;
    }

    // No editor on this track yet, so one is added. It is a real insert and it
    // is saved with the project, which is the price of the double click doing
    // what was asked of it.
    setStatusMessage(TRANS("Opening ") + clip->getName() + TRANS(" in the audio editor..."));

    pluginManager.createPluginAsync(AudioEditorProcessor::getDescription(),
                                    audioEngine.getCurrentSampleRate(),
                                    audioEngine.getCurrentBufferSize(),
        [this, trackIndex, clipIndex] (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String error)
        {
            if (instance == nullptr)
            {
                setStatusMessage(error.isNotEmpty() ? error : TRANS("The audio editor could not be created."));
                return;
            }

            auto* target = getTrack(trackIndex);

            if (target == nullptr)
                return;

            prepareBuiltInEditor(*instance, trackIndex);
            target->addPlugin(std::move(instance));

            mixerView.repaint();
            insertChainPanel.refresh();
            markDirty();
            synchroniseProjectState();

            if (auto* editor = findBuiltInEditor(trackIndex))
                openClipInBuiltInEditor(*editor, trackIndex, clipIndex);
        });
}

AudioEditorProcessor* MainComponent::findBuiltInEditor(int trackIndex)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return nullptr;

    // The first one on the track: a second editor would be a second answer to
    // "where did my clip go".
    for (int slot = 0; slot < track->getPluginCount(); ++slot)
        if (auto* editor = dynamic_cast<AudioEditorProcessor*>(track->getPlugin(slot)))
            return editor;

    return nullptr;
}

void MainComponent::openClipInBuiltInEditor(AudioEditorProcessor& editor, int trackIndex, int clipIndex)
{
    auto* audioTrack = dynamic_cast<AudioTrack*>(getTrack(trackIndex));
    auto* clip = audioTrack != nullptr ? audioTrack->getClip(clipIndex) : nullptr;

    if (clip == nullptr)
        return;

    // The part the clip plays, not the whole file: the trim on the timeline is
    // already the user saying which bit they mean.
    int first = 0;
    int length = 0;
    clip->getPlayedRegion(first, length);

    juce::AudioBuffer<float> audio;

    if (length <= 0 || ! clip->copySamples(first, length, audio))
    {
        setStatusMessage(TRANS("That clip's audio could not be read."));
        return;
    }

    editor.adoptAudio(audio, length, clip->getClipSampleRate(), clip->getName());
    showPluginWindow(&editor, getTrack(trackIndex));

    setStatusMessage(clip->getName() + TRANS(" opened in the audio editor - send it back when you are done."));
}

void MainComponent::restorePluginsForTrack(int trackIndex, const juce::Array<juce::var>& pluginStates)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr || pluginStates.isEmpty())
        return;

    track->clearInstrument();
    track->clearPlugins();

    for (const auto& entry : pluginStates)
    {
        auto* object = entry.getDynamicObject();

        if (object == nullptr)
            continue;

        const auto xmlText = object->getProperty("description").toString();
        const auto stateBase64 = object->getProperty("state").toString();
        const auto isInstrument = static_cast<bool>(object->getProperty("isInstrument"));
        const auto name = object->getProperty("name").toString();

        juce::PluginDescription description;

        if (auto xml = juce::parseXML(xmlText); xml == nullptr || ! description.loadFromXml(*xml))
        {
            setStatusMessage("Plugin " + name + TRANS(" could not be restored: its description is damaged."));
            continue;
        }

        pluginManager.createPluginAsync(description,
                                        audioEngine.getCurrentSampleRate(),
                                        audioEngine.getCurrentBufferSize(),
            [this, trackIndex, isInstrument, stateBase64, name]
            (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String error)
            {
                if (instance == nullptr)
                {
                    setStatusMessage("Plugin " + name + TRANS(" could not be found")
                                         + (error.isNotEmpty() ? ": " + error : "."));
                    return;
                }

                prepareBuiltInEditor(*instance, trackIndex);

                // Restore the saved parameters before the plugin joins the graph.
                juce::MemoryBlock stateBlock;
                if (stateBlock.fromBase64Encoding(stateBase64) && stateBlock.getSize() > 0)
                    instance->setStateInformation(stateBlock.getData(),
                                                  static_cast<int>(stateBlock.getSize()));

                if (auto* target = getTrack(trackIndex))
                {
                    if (isInstrument)
                        target->setInstrument(std::move(instance));
                    else
                        target->addPlugin(std::move(instance));

                    insertChainPanel.refresh();
                    mixerView.repaint();
                }
            });
    }
}

void MainComponent::restoreAudioClipsForTrack(int trackIndex, const juce::Array<juce::var>& clipStates)
{
    auto* audioTrack = dynamic_cast<AudioTrack*>(getTrack(trackIndex));

    if (audioTrack == nullptr)
        return;

    audioTrack->clearClips();

    for (const auto& entry : clipStates)
    {
        const auto file = AudioClip::getFileFromVar(entry);

        juce::String error;
        auto clip = AudioClip::createFromFile(file, audioEngine.getCurrentSampleRate(), audioFormats, error);

        if (clip == nullptr)
        {
            setStatusMessage(error.isNotEmpty() ? error
                                                : TRANS("Missing audio clip: ") + file.getFileName());
            continue;
        }

        clip->applyStateFromVar(entry);
        audioTrack->addClip(std::move(clip));
    }
}

void MainComponent::restoreAutomationForTrack(int trackIndex, const juce::Array<juce::var>& laneStates)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return;

    // A project with no automation still has to clear whatever the previous one
    // left behind, so this runs even for an empty array.
    std::vector<AutomationLaneState> restored;
    restored.reserve(static_cast<size_t>(laneStates.size()));

    for (const auto& entry : laneStates)
        if (auto lane = AutomationLane::fromVar(entry))
            restored.push_back(lane->captureState());

    track->restoreAutomation(restored);
}

void MainComponent::rebuildTrackListForProject()
{
    auto& mixer = audioEngine.getMixer();

    if (! applyProjectTrackLayout(mixer, projectManager.getProject().tracks))
        return;

    // Opening a project is not an edit, so the panels are brought up to date
    // without the file ending up marked as changed by it.
    const auto wasDirty = projectDirty;

    // A track that changed kind, or one this project does not have, has just
    // been destroyed - and the strips are still holding pointers into it, so they
    // are rebuilt before anything can paint one.
    mixerView.refreshStrips();

    // The selection can now be past the end of a shorter list; it is clamped
    // before the views are told, so they never look up a track that is gone.
    const auto selected = juce::jlimit(0, juce::jmax(0, mixer.getNumTracks() - 1),
                                       sessionState.getSelectedTrack());
    sessionState.setSelectedTrack(selected);
    arrangementView.setSelectedTrack(selected);
    arrangementView.notifyTrackListChanged();

    projectDirty = wasDirty;
}

void MainComponent::restoreFreezeForTrack(int trackIndex, const juce::String& frozenPath)
{
    auto* track = getTrack(trackIndex);

    if (track == nullptr)
        return;

    // Runs even for an empty path, so a track does not keep the previous
    // session's render.
    if (frozenPath.isEmpty())
    {
        track->setFrozenAudio(nullptr);
        return;
    }

    const juce::File file(frozenPath);

    // A missing render is not worth refusing to open the project over: the
    // track still has its clips and plugins, so it simply plays them again.
    if (! file.existsAsFile())
    {
        track->setFrozenAudio(nullptr);
        setStatusMessage("Freeze " + track->getName() + TRANS(" is missing: ") + file.getFileName()
                             + TRANS(" - the track is processed live again."));
        return;
    }

    juce::String error;
    auto clip = AudioClip::createFromFile(file, audioEngine.getCurrentSampleRate(), audioFormats, error);

    if (clip == nullptr)
    {
        track->setFrozenAudio(nullptr);
        setStatusMessage("Freeze " + track->getName() + TRANS(" could not be read: ") + error);
        return;
    }

    clip->setWarpEnabled(false);
    track->setFrozenAudio(std::move(clip));
}

void MainComponent::restoreRoutingFromProject()
{
    const auto& project = projectManager.getProject();
    auto& mixer = audioEngine.getMixer();

    // Every route is cleared first, so a track the file says nothing about does
    // not keep whatever the previous session pointed it at.
    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        mixer.setTrackOutput(i, Track::masterDestination);

        for (int slot = 0; slot < Track::maxSends; ++slot)
            mixer.setTrackSend(i, slot, {});
    }

    for (int i = 0; i < project.tracks.size() && i < mixer.getNumTracks(); ++i)
    {
        const auto& projectTrack = project.tracks.getReference(i);

        // Set through the mixer rather than onto the track: a file that has been
        // hand edited, or written by an older build, could describe a loop, and
        // the mixer is what refuses one.
        mixer.setTrackOutput(i, projectTrack.outputDestination);

        if (auto* restored = mixer.getTrack(i))
        {
            restored->setInputChannel(projectTrack.inputChannel);
            restored->setInputStereo(projectTrack.inputStereo);
            restored->getChannelSettings().fromVar(projectTrack.channelSettings);
        }

        for (int slot = 0; slot < projectTrack.sends.size() && slot < Track::maxSends; ++slot)
        {
            auto* sendObject = projectTrack.sends[slot].getDynamicObject();

            if (sendObject == nullptr)
                continue;

            TrackSend send;
            send.destination = static_cast<int>(sendObject->getProperty("destination"));
            send.level = static_cast<float>(static_cast<double>(sendObject->getProperty("level")));
            send.preFader = static_cast<bool>(sendObject->getProperty("preFader"));

            mixer.setTrackSend(i, slot, send);
        }
    }
}

void MainComponent::applyProjectToSession()
{
    const auto& project = projectManager.getProject();
    audioEngine.getTransport().setTempoBpm(project.tempo);
    audioEngine.getTransport().setTimeSignature(project.timeSigNumerator, project.timeSigDenominator);

    // The session has to hold the project's tracks before any of their state is
    // applied: the loop below reaches a seventh track only once one exists, and
    // it can only tell a MIDI track from an audio one once the kinds match.
    rebuildTrackListForProject();

    for (int i = 0; i < project.tracks.size(); ++i)
    {
        auto* runtimeTrack = getTrack(i);
        if (runtimeTrack == nullptr)
            continue;

        const auto& projectTrack = project.tracks.getReference(i);
        // Saved since the first version of the format but never read back, so a
        // renamed track reopened under its old default name.
        runtimeTrack->setName(projectTrack.name);
        runtimeTrack->setVolume(projectTrack.volume);
        runtimeTrack->setPan(projectTrack.pan);
        runtimeTrack->setMuted(projectTrack.mute);
        runtimeTrack->setSoloed(projectTrack.solo);
        arrangementView.setRowHeight(i, projectTrack.laneHeight);

        if (auto* midiTrack = dynamic_cast<MidiTrack*>(runtimeTrack))
        {
            if (! projectTrack.patterns.isEmpty())
            {
                for (int pattern = 0; pattern < projectTrack.patterns.size()
                                      && pattern < MidiTrack::maxPatterns; ++pattern)
                {
                    juce::Array<MidiNote> notes;

                    for (const auto& noteValue : projectTrack.patterns[pattern])
                        notes.add(MidiNote::fromVar(noteValue));

                    midiTrack->getClip(pattern).setNotes(notes);
                }
            }
            else
            {
                // Older project: one clip, restore it into the first pattern.
                juce::Array<MidiNote> notes;
                for (const auto& noteValue : projectTrack.midiNotes)
                    notes.add(MidiNote::fromVar(noteValue));

                midiTrack->getClip(0).setNotes(notes);
            }

            midiTrack->clearPlacements();
            for (const auto& placementValue : projectTrack.placements)
                midiTrack->addPlacement(PatternPlacement::fromVar(placementValue));
        }

        restoreAudioClipsForTrack(i, projectTrack.audioClips);
        restorePluginsForTrack(i, projectTrack.pluginStates);
        restoreAutomationForTrack(i, projectTrack.automation);
        restoreFreezeForTrack(i, projectTrack.frozenFile);
    }

    // Routing comes last and in its own pass: a destination is a track index, so
    // every track has to exist before any of them can point at another.
    restoreRoutingFromProject();

    panelHost.applyLayoutState(project.panelLayout);

    for (int i = 0; i < SessionState::maxPatterns; ++i)
    {
        sessionState.setPatternName(i, i < project.patternNames.size() ? project.patternNames[i]
                                                                       : juce::String());
        sessionState.setPatternLengthBeats(i, i < project.patternLengths.size() ? project.patternLengths[i]
                                                                                : 0.0);
    }

    // Re-point the editors at the freshly loaded clip.
    applySelectionToPanels();
    pianoRollModel.notifyClipChanged();
    mixerView.repaint();
    arrangementView.repaint();
    insertChainPanel.refresh();
    setStatusMessage(TRANS("Project opened: patterns, audio clips, plugins and the panel layout were restored."));
}

Track* MainComponent::getTrack(int index) noexcept
{
    return audioEngine.getMixer().getTrack(index);
}

} // namespace djr
