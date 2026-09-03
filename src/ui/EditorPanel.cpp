#include "EditorPanel.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int tabStripHeight = Metrics::panelToolbarHeight;
    constexpr int velocityLaneHeight = 52;
    constexpr int keyboardHeight = 34;
}

void EditorPanel::KeyboardBridge::handleNoteOn(juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (onMessage)
        onMessage(juce::MidiMessage::noteOn(channel, note, velocity));
}

void EditorPanel::KeyboardBridge::handleNoteOff(juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (onMessage)
        onMessage(juce::MidiMessage::noteOff(channel, note, velocity));
}

EditorPanel::EditorPanel(PianoRollModel& modelToUse, Transport& transport)
    : model(modelToUse),
      pianoRoll(modelToUse, transport),
      velocityLane(modelToUse, pianoRoll),
      stepSequencer(modelToUse, transport)
{
    for (auto* tab : { &pianoRollTab, &stepSequencerTab })
    {
        tab->addListener(this);
        addAndMakeVisible(tab);
    }

    velocityToggle.setCornerSize(5.0f);
    velocityToggle.addListener(this);
    velocityToggle.setTooltip("Tampilkan / sembunyikan velocity lane");
    addAndMakeVisible(velocityToggle);

    followButton.setIconInset(4.0f);
    followButton.addListener(this);
    followButton.setTooltip(TRANS("Follow playhead during playback"));
    addAndMakeVisible(followButton);

    addAndMakeVisible(pianoRoll);
    addAndMakeVisible(velocityLane);
    addChildComponent(stepSequencer);

    keyboard.setKeyWidth(11.0f);
    keyboard.setLowestVisibleKey(48);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::outline());
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::accent());
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::accent().withAlpha(0.35f));

    // MidiKeyboardComponent ships its own typing map (a w s e d f t g y h u j k
    // o l p ;) and asks for focus when clicked, so once you had touched the
    // strip with the mouse every letter played twice: once from TypingKeyboard's
    // map and once from JUCE's, on a different note. W gave D and C sharp
    // together, and A played a C the FL map does not even have. TypingKeyboard
    // polls regardless of focus, so this one has nothing left to do.
    keyboard.clearKeyMappings();
    keyboard.setWantsKeyboardFocus(false);

    keyboardState.addListener(&keyboardBridge);
    addAndMakeVisible(keyboard);

    buildToolButtons();
    refreshFollowButton();

    model.addChangeListener(this);
    refreshTabs();
}

void EditorPanel::buildToolButtons()
{
    // The playlist's Slip has no meaning for a note, so this set is one shorter.
    static const ToolButton definitions[] = {
        { PianoRollView::Tool::select,   Icon::marquee,     "Select - pick and move notes" },
        { PianoRollView::Tool::draw,     Icon::pencil,      "Draw - place notes" },
        { PianoRollView::Tool::erase,    Icon::eraser,      "Delete - remove notes" },
        { PianoRollView::Tool::mute,     Icon::speakerMute, "Mute - silence a note" },
        { PianoRollView::Tool::slice,    Icon::slice,       "Slice - cut a note" },
        { PianoRollView::Tool::zoom,     Icon::zoom,        "Zoom - drag out an area" },
        { PianoRollView::Tool::playback, Icon::play,        "Playback - click to play" }
    };

    for (const auto& definition : definitions)
    {
        auto* button = toolButtons.add(new IconChipButton(juce::translate(definition.tooltip), definition.icon));
        // Small chips need a tighter inset or the glyph is unreadable.
        button->setIconInset(3.5f);
        button->addListener(this);
        addAndMakeVisible(button);
    }

    refreshToolButtons();
}

void EditorPanel::refreshToolButtons()
{
    static const PianoRollView::Tool order[] = {
        PianoRollView::Tool::select, PianoRollView::Tool::draw, PianoRollView::Tool::erase,
        PianoRollView::Tool::mute, PianoRollView::Tool::slice, PianoRollView::Tool::zoom,
        PianoRollView::Tool::playback
    };

    for (int i = 0; i < toolButtons.size(); ++i)
        if (auto* button = toolButtons[i])
        {
            button->setHighlighted(order[i] == pianoRoll.getTool());
            // The tools belong to the roll, so they go away with it.
            button->setVisible(pianoRollVisible);
        }
}

void EditorPanel::refreshFollowButton()
{
    followButton.setIcon(pianoRoll.isFollowingPlayhead() ? Icon::chevronRight : Icon::minimise);
    // Following only means anything while the roll itself is on screen.
    followButton.setVisible(pianoRollVisible);
}

EditorPanel::~EditorPanel()
{
    keyboardState.removeListener(&keyboardBridge);
    model.removeChangeListener(this);
    for (auto* button : toolButtons)
        button->removeListener(this);

    pianoRollTab.removeListener(this);
    stepSequencerTab.removeListener(this);
    velocityToggle.removeListener(this);
    followButton.removeListener(this);
}

void EditorPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(Theme::panelDeep());

    auto strip = bounds.withHeight(tabStripHeight);
    g.setColour(Theme::panel());
    g.fillRect(strip);
    g.setColour(Theme::outline());
    g.fillRect(strip.removeFromBottom(1));

    // Right hand info cluster ------------------------------------------------
    auto info = bounds.withHeight(tabStripHeight).withTrimmedRight(8);
    info.setLeft(juce::jmax(info.getX(), getPatternLengthBounds().getRight() + 8));
    const auto noteCount = model.getNotes().size();

    const auto drawDivider = [&g] (juce::Rectangle<int> area)
    {
        g.setColour(Theme::outlineStrong());
        g.fillRect(area.withSizeKeepingCentre(1, 11));
    };

    auto toggleArea = velocityToggle.getBounds();
    g.setColour(velocityLaneVisible ? Theme::accent() : Theme::mutedText());
    g.setFont(Theme::ui(11.0f));
    g.drawText("Velocity", toggleArea, juce::Justification::centred, false);

    // Stop short of the follow button too, whether or not it is shown for the
    // current tab - its slot in the strip is reserved either way.
    info = info.withRight(followButton.getBounds().getX());
    drawDivider(info.removeFromRight(11));

    const auto notesText = juce::String(noteCount) + " notes";
    const auto notesWidth = Theme::textWidth(Theme::ui(11.0f), notesText);
    g.setColour(Theme::mutedText());
    g.setFont(Theme::ui(11.0f));
    g.drawText(notesText, info.removeFromRight(notesWidth), juce::Justification::centredRight, false);

    drawDivider(info.removeFromRight(11));

    // Pattern selector: which clip every editor here is looking at.
    const auto drawArrow = [&g] (juce::Rectangle<int> area, const juce::String& glyph, bool enabled)
    {
        g.setColour(enabled ? Theme::control() : Theme::control().withAlpha(0.4f));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(enabled ? Theme::text() : Theme::faintText());
        g.setFont(Theme::mono(10.0f));
        g.drawText(glyph, area, juce::Justification::centred, false);
    };

    const auto nextArrow = getNextPatternBounds();
    drawArrow(getPreviousPatternBounds(), "<", activePattern > 0);
    drawArrow(nextArrow, ">", activePattern < 15);

    const auto nameArea = getPatternNameBounds();
    g.setColour(Theme::accent());
    g.setFont(Theme::mono(11.0f, true));
    g.drawText(patternName.isNotEmpty() ? patternName : "PAT " + juce::String(activePattern + 1),
               nameArea,
               juce::Justification::centredLeft,
               false);

    // Whose notes these are, dimmer than the pattern badge: the pattern is what
    // is being edited, the track is where it belongs.
    if (trackName.isNotEmpty())
    {
        g.setColour(Theme::mutedText());
        g.setFont(Theme::ui(10.5f));
        g.drawText(trackName,
                   nameArea.withX(nameArea.getRight() + 6).withWidth(110),
                   juce::Justification::centredLeft,
                   true);
    }

    // The pattern's loop length, and whether it follows the notes or is pinned.
    const auto lengthArea = getPatternLengthBounds();
    g.setColour(Theme::control());
    g.fillRoundedRectangle(lengthArea.toFloat(), 3.0f);
    g.setColour(patternLengthLocked ? Theme::amber() : Theme::mutedText());
    g.setFont(Theme::mono(10.0f));
    const auto barBeats = pianoRoll.getBeatsPerBar();
    g.drawText(juce::String(patternLengthBeats / barBeats, patternLengthBeats < barBeats ? 2 : 0) + " bar"
                   + (patternLengthLocked ? "*" : ""),
               lengthArea,
               juce::Justification::centred,
               false);
}

juce::Rectangle<int> EditorPanel::getPatternNameBounds() const
{
    const auto next = getNextPatternBounds();
    const auto text = patternName.isNotEmpty() ? patternName : "PAT " + juce::String(activePattern + 1);
    const auto width = juce::jlimit(42, 150, Theme::textWidth(Theme::mono(11.0f, true), text) + 6);

    return juce::Rectangle<int>(next.getRight() + 5, next.getY(), width, next.getHeight());
}

juce::Rectangle<int> EditorPanel::getPatternLengthBounds() const
{
    // Past the track name when there is one, so the two never sit on top of
    // each other on a narrow panel.
    const auto name = trackName.isEmpty()
        ? getPatternNameBounds()
        : getPatternNameBounds().withWidth(getPatternNameBounds().getWidth() + 6
                                               + juce::jmin(110, Theme::textWidth(Theme::ui(10.5f), trackName) + 4));
    return juce::Rectangle<int>(name.getRight() + 8, name.getY(), 48, name.getHeight());
}
juce::Rectangle<int> EditorPanel::getInstrumentNameBounds() const
{
    const auto nameArea = getPatternNameBounds();

    // The same width getPatternLengthBounds measures to place the badge after
    // the name, so the two never overlap. A fixed width would sit underneath
    // the badge on a short name and only work because of the order the clicks
    // happen to be tested in. Empty on an audio track, where no name is drawn:
    // an invisible region that swallows clicks is worse than none at all.
    const auto width = trackName.isEmpty()
        ? 0
        : juce::jmin(110, Theme::textWidth(Theme::ui(10.5f), trackName) + 4);

    return juce::Rectangle<int>(nameArea.getRight() + 6, nameArea.getY(), width, nameArea.getHeight());
}

void EditorPanel::setPatternLengthBeats(double beats, bool locked)
{
    if (std::abs(patternLengthBeats - beats) < 1.0e-9 && patternLengthLocked == locked)
        return;

    patternLengthBeats = beats;
    patternLengthLocked = locked;

    // The step grid pages over the same span the pattern loops for.
    stepSequencer.setPatternLengthBeats(beats);
    repaint();
}

void EditorPanel::setPatternLengthChangedCallback(std::function<void(double)> callback)
{
    patternLengthChangedCallback = std::move(callback);
}

void EditorPanel::setSnapUnit(SnapUnit unit)
{
    pianoRoll.setSnapUnit(unit);
}

void EditorPanel::setNoteGestureCallback(std::function<void(bool)> callback)
{
    pianoRoll.onEditGesture = callback;
    velocityLane.onEditGesture = std::move(callback);
}

void EditorPanel::setTrackName(const juce::String& name)
{
    if (name == trackName)
        return;

    trackName = name;
    resized();
    repaint();
}

void EditorPanel::setPatternName(const juce::String& name)
{
    if (patternName == name)
        return;

    patternName = name;
    repaint();
}

void EditorPanel::setPatternRenameCallback(std::function<void()> callback)
{
    patternRenameCallback = std::move(callback);
}

void EditorPanel::setTrackListProvider(std::function<std::vector<PickableTrack>()> provider)
{
    trackListProvider = std::move(provider);
}

void EditorPanel::setSelectedTrackIndex(int trackIndex) noexcept
{
    selectedTrackIndex = trackIndex;
}

void EditorPanel::setTrackChangedCallback(std::function<void(int)> callback)
{
    trackChangedCallback = std::move(callback);
}

void EditorPanel::showPatternLengthMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("Pattern length"));
    menu.addItem(1, "Ikuti isi (auto)", true, ! patternLengthLocked);
    menu.addSeparator();

    const int bars[] = { 1, 2, 4, 8, 16 };

    // A bar is however long the time signature says, so 4 bars of 3/4 is 12 beats.
    const auto barBeats = pianoRoll.getBeatsPerBar();

    for (int i = 0; i < static_cast<int>(std::size(bars)); ++i)
        menu.addItem(2 + i,
                     juce::String(bars[i]) + " bar",
                     true,
                     patternLengthLocked && std::abs(patternLengthBeats - bars[i] * barBeats) < 1.0e-9);

    // Anchored to the badge that opened it, not to the whole panel: targeting
    // the component drops the menu at the panel's edge, nowhere near the click.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(localAreaToGlobal(getPatternLengthBounds()))
                           .withMinimumWidth(160)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (result <= 0 || ! patternLengthChangedCallback)
                return;

            const int chosen[] = { 1, 2, 4, 8, 16 };
            patternLengthChangedCallback(result == 1 ? 0.0
                                                     : chosen[result - 2] * pianoRoll.getBeatsPerBar());
        });
}

void EditorPanel::showTrackMenu()
{
    if (! trackListProvider)
        return;

    const auto tracks = trackListProvider();

    if (tracks.empty())
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("Switch track"));

    for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
    {
        const auto& track = tracks[static_cast<size_t>(i)];
        menu.addItem(i + 1, track.name, true, track.index == selectedTrackIndex);
    }

    // Anchored to the badge that opened it, like the pattern length menu.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(localAreaToGlobal(getInstrumentNameBounds()))
                           .withMinimumWidth(180)
                           .withStandardItemHeight(21),
        [this, tracks] (int result)
        {
            if (result <= 0 || ! trackChangedCallback)
                return;

            trackChangedCallback(tracks[static_cast<size_t>(result - 1)].index);
        });
}

juce::Rectangle<int> EditorPanel::getPreviousPatternBounds() const
{
    const auto strip = getLocalBounds().withHeight(tabStripHeight);
    const auto left = stepSequencerTab.getRight() + 10;
    return juce::Rectangle<int>(left, strip.getY() + 4, 15, tabStripHeight - 8);
}

juce::Rectangle<int> EditorPanel::getNextPatternBounds() const
{
    const auto previous = getPreviousPatternBounds();
    return previous.withX(previous.getRight() + 2);
}

void EditorPanel::setActivePattern(int patternIndex)
{
    const auto clamped = juce::jlimit(0, 15, patternIndex);

    if (clamped == activePattern)
        return;

    activePattern = clamped;
    repaint();
}

void EditorPanel::setPatternChangedCallback(std::function<void(int)> callback)
{
    patternChangedCallback = std::move(callback);
}

void EditorPanel::mouseDown(const juce::MouseEvent& event)
{
    if (getPatternLengthBounds().contains(event.getPosition()))
    {
        showPatternLengthMenu();
        return;
    }

    if (getPatternNameBounds().contains(event.getPosition()))
    {
        if (patternRenameCallback)
            patternRenameCallback();

        return;
    }
    if (getInstrumentNameBounds().contains(event.getPosition()))
    {
        showTrackMenu();
        return;
    }

    const auto step = getPreviousPatternBounds().contains(event.getPosition()) ? -1
                    : getNextPatternBounds().contains(event.getPosition())     ? 1
                                                                               : 0;

    if (step == 0)
        return;

    const auto target = juce::jlimit(0, 15, activePattern + step);

    if (target != activePattern && patternChangedCallback)
        patternChangedCallback(target);
}

void EditorPanel::resized()
{
    auto area = getLocalBounds();
    auto strip = area.removeFromTop(tabStripHeight).reduced(5, 0);

    pianoRollTab.setBounds(strip.removeFromLeft(pianoRollTab.getPreferredWidth()));
    strip.removeFromLeft(2);
    stepSequencerTab.setBounds(strip.removeFromLeft(stepSequencerTab.getPreferredWidth()));

    velocityToggle.setBounds(strip.removeFromRight(58).withSizeKeepingCentre(58, 16));
    followButton.setBounds(strip.removeFromRight(19).withSizeKeepingCentre(18, 18));
    strip.removeFromRight(4);

    // Tools sit after the pattern controls, in the gap before the note count.
    auto tools = strip.withX(getPatternLengthBounds().getRight() + 10)
                      .withWidth(toolButtons.size() * 21);

    for (auto* button : toolButtons)
    {
        button->setBounds(tools.removeFromLeft(19).withSizeKeepingCentre(19, 19));
        tools.removeFromLeft(2);
    }

    // The keyboard is how you audition an instrument without MIDI hardware, so
    // it stays available in both editor modes when there is room for it.
    if (area.getHeight() > keyboardHeight + 60)
        keyboard.setBounds(area.removeFromBottom(keyboardHeight));
    else
        keyboard.setBounds({});

    if (pianoRollVisible)
    {
        stepSequencer.setBounds({});

        if (velocityLaneVisible && area.getHeight() > velocityLaneHeight + 36)
            velocityLane.setBounds(area.removeFromBottom(velocityLaneHeight));
        else
            velocityLane.setBounds({});

        pianoRoll.setBounds(area);
    }
    else
    {
        pianoRoll.setBounds({});
        velocityLane.setBounds({});
        stepSequencer.setBounds(area);
    }
}

void EditorPanel::showPianoRoll()
{
    if (pianoRollVisible)
        return;

    pianoRollVisible = true;
    refreshTabs();
}

void EditorPanel::showStepSequencer()
{
    if (! pianoRollVisible)
        return;

    pianoRollVisible = false;
    refreshTabs();
}

bool EditorPanel::isPianoRollVisible() const noexcept
{
    return pianoRollVisible;
}

void EditorPanel::setVelocityLaneVisible(bool shouldBeVisible)
{
    if (velocityLaneVisible == shouldBeVisible)
        return;

    velocityLaneVisible = shouldBeVisible;
    refreshTabs();
}

bool EditorPanel::isVelocityLaneVisible() const noexcept
{
    return velocityLaneVisible;
}

void EditorPanel::setViewChangedCallback(std::function<void()> callback)
{
    viewChangedCallback = std::move(callback);
}

juce::MidiKeyboardState& EditorPanel::getKeyboardState() noexcept
{
    return keyboardState;
}

void EditorPanel::ensureKeyVisible(int note)
{
    const auto lowest = static_cast<int>(keyboard.getLowestVisibleKey());
    const auto span = juce::jmax(12, static_cast<int>(keyboard.getWidth() / juce::jmax(1.0f, keyboard.getKeyWidth())));

    if (note < lowest)
        keyboard.setLowestVisibleKey(juce::jmax(0, note - 2));
    else if (note >= lowest + span)
        keyboard.setLowestVisibleKey(juce::jlimit(0, 108, note - span + 3));
}

void EditorPanel::setKeyboardMessageCallback(std::function<void(const juce::MidiMessage&)> callback)
{
    keyboardBridge.onMessage = std::move(callback);
}

void EditorPanel::buttonClicked(juce::Button* button)
{
    if (button == &pianoRollTab)
        showPianoRoll();
    else if (button == &stepSequencerTab)
        showStepSequencer();
    else if (button == &velocityToggle)
    {
        setVelocityLaneVisible(! velocityLaneVisible);
    }
    else if (button == &followButton)
    {
        pianoRoll.setFollowPlayhead(! pianoRoll.isFollowingPlayhead());
        refreshFollowButton();
    }
    else
    {
        static const PianoRollView::Tool order[] = {
            PianoRollView::Tool::select, PianoRollView::Tool::draw, PianoRollView::Tool::erase,
            PianoRollView::Tool::mute, PianoRollView::Tool::slice, PianoRollView::Tool::zoom,
            PianoRollView::Tool::playback
        };

        for (int i = 0; i < toolButtons.size(); ++i)
        {
            if (toolButtons[i] != button)
                continue;

            pianoRoll.setTool(order[i]);
            refreshToolButtons();
            break;
        }
    }
}

void EditorPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint(getLocalBounds().withHeight(tabStripHeight));
}

void EditorPanel::refreshTabs()
{
    refreshToolButtons();
    refreshFollowButton();

    pianoRollTab.setToggleState(pianoRollVisible, juce::dontSendNotification);
    stepSequencerTab.setToggleState(! pianoRollVisible, juce::dontSendNotification);

    pianoRoll.setVisible(pianoRollVisible);
    velocityLane.setVisible(pianoRollVisible && velocityLaneVisible);
    stepSequencer.setVisible(! pianoRollVisible);

    resized();
    repaint();

    if (viewChangedCallback)
        viewChangedCallback();
}

} // namespace djr
