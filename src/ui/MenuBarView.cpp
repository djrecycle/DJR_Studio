#include "MenuBarView.h"

#include "Theme.h"

namespace djr
{

namespace
{
    const char* const menuNames[] = { "File", "Edit", "View", "Tools", "Help" };
    constexpr int logoBadgeSize = 14;
    constexpr int logoTextWidth = 84;
    constexpr int itemHeight = 18;
}

MenuBarView::MenuBarView()
{
    for (auto* name : menuNames)
    {
        auto* item = menuItems.add(new MenuBarItem(name));
        item->addListener(this);
        addAndMakeVisible(item);
    }

    for (auto* button : { &projectsButton, &preferencesButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }
}

void MenuBarView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    g.setGradientFill(juce::ColourGradient(Theme::menuBarTop(), 0.0f, 0.0f,
                                           Theme::menuBarBottom(), 0.0f, static_cast<float>(bounds.getBottom()), false));
    g.fillRect(bounds);

    g.setColour(Theme::divider());
    g.fillRect(bounds.removeFromBottom(1));

    // Logo badge -------------------------------------------------------------
    auto logo = juce::Rectangle<int>(8, (getHeight() - logoBadgeSize) / 2, logoBadgeSize, logoBadgeSize).toFloat();
    g.setGradientFill(juce::ColourGradient(Theme::cyan(), logo.getX(), logo.getY(),
                                           Theme::purple(), logo.getRight(), logo.getBottom(), false));
    g.fillRoundedRectangle(logo, 4.0f);
    g.setColour(Theme::windowBackground());
    g.fillRoundedRectangle(logo.withSizeKeepingCentre(5.0f, 5.0f), 1.5f);

    g.setColour(Theme::text());
    g.setFont(Theme::display(15.0f));
    g.drawText("DJR_STUDIO",
               juce::Rectangle<int>(8 + logoBadgeSize + 6, 0, logoTextWidth, getHeight()),
               juce::Justification::centredLeft,
               false);

    // Project identity in the middle ----------------------------------------
    const auto folderFont = Theme::mono(11.5f);
    const auto nameFont = Theme::ui(12.5f, true);
    const auto stateFont = Theme::ui(11.0f);
    const auto stateText = unsavedChanges ? juce::String("unsaved") : juce::String("saved");

    const auto folderWidth = Theme::textWidth(folderFont, projectFolder);
    const auto nameWidth = Theme::textWidth(nameFont, projectName);
    const auto stateWidth = Theme::textWidth(stateFont, stateText);
    const auto totalWidth = folderWidth + 6 + nameWidth + 6 + 5 + 5 + stateWidth;

    auto centre = getLocalBounds().withSizeKeepingCentre(totalWidth, getHeight());

    g.setColour(Theme::mutedText());
    g.setFont(folderFont);
    g.drawText(projectFolder, centre.removeFromLeft(folderWidth), juce::Justification::centredLeft, false);
    centre.removeFromLeft(6);

    g.setColour(Theme::text());
    g.setFont(nameFont);
    g.drawText(projectName, centre.removeFromLeft(nameWidth), juce::Justification::centredLeft, false);
    centre.removeFromLeft(6);

    g.setColour(unsavedChanges ? Theme::amber() : Theme::green());
    g.fillEllipse(centre.removeFromLeft(5).withSizeKeepingCentre(5, 5).toFloat());
    centre.removeFromLeft(5);

    g.setColour(Theme::mutedText());
    g.setFont(stateFont);
    g.drawText(stateText, centre, juce::Justification::centredLeft, false);
}

void MenuBarView::resized()
{
    auto area = getLocalBounds();

    area.removeFromLeft(8 + logoBadgeSize + 6 + logoTextWidth + 6);

    for (auto* item : menuItems)
    {
        auto slot = area.removeFromLeft(item->getPreferredWidth());
        item->setBounds(slot.withSizeKeepingCentre(slot.getWidth(), itemHeight));
        area.removeFromLeft(1);
    }

    auto right = getLocalBounds().withTrimmedRight(8);
    preferencesButton.setBounds(right.removeFromRight(preferencesButton.getPreferredWidth())
                                     .withSizeKeepingCentre(preferencesButton.getPreferredWidth(), itemHeight));
    right.removeFromRight(5);
    projectsButton.setBounds(right.removeFromRight(projectsButton.getPreferredWidth())
                                  .withSizeKeepingCentre(projectsButton.getPreferredWidth(), itemHeight));
}

void MenuBarView::setCommandHandler(std::function<void(AppCommand)> handler)
{
    commandHandler = std::move(handler);
}

void MenuBarView::setProjectInfo(const juce::String& folder, const juce::String& name, bool hasUnsavedChanges)
{
    projectFolder = folder;
    projectName = name;
    unsavedChanges = hasUnsavedChanges;
    repaint();
}

void MenuBarView::setViewState(ViewState state)
{
    viewState = state;
}

void MenuBarView::invoke(AppCommand command) const
{
    if (commandHandler)
        commandHandler(command);
}

void MenuBarView::buttonClicked(juce::Button* button)
{
    if (button == &projectsButton)
    {
        invoke(AppCommand::showProjectManager);
        return;
    }

    if (button == &preferencesButton)
    {
        invoke(AppCommand::showPreferences);
        return;
    }

    for (int i = 0; i < menuItems.size(); ++i)
        if (button == menuItems[i])
            showMenuFor(i, *button);
}

void MenuBarView::showMenuFor(int menuIndex, juce::Button& source)
{
    juce::PopupMenu menu;

    switch (menuIndex)
    {
        case 0: // File
            menu.addItem(1, "New Project", true, false);
            menu.addItem(2, "Open Project...", true, false);
            menu.addSeparator();
            menu.addItem(3, "Save", true, false);
            menu.addItem(4, "Save As...", true, false);
            menu.addSeparator();
            menu.addItem(5, "Project Manager...", true, false);
            menu.addSeparator();
            menu.addItem(7, "Export Audio (WAV)...", true, false);
            menu.addSeparator();
            menu.addItem(6, "Quit", true, false);
            break;

        case 1: // Edit
            menu.addItem(12, viewState.undoName.isEmpty() ? "Undo"
                                                          : "Undo " + viewState.undoName,
                         viewState.canUndo, false);
            menu.addItem(13, viewState.redoName.isEmpty() ? "Redo"
                                                          : "Redo " + viewState.redoName,
                         viewState.canRedo, false);
            menu.addSeparator();
            menu.addItem(10, "Preferences...", true, false);
            menu.addSeparator();
            menu.addItem(11, "Audio Device Settings...", true, false);
            break;

        case 2: // View
            menu.addItem(20, "Playlist", true, viewState.playlistVisible);
            menu.addItem(26, "Editor", true, viewState.editorVisible);
            menu.addItem(27, "Mixer", true, viewState.mixerVisible);
            menu.addItem(28, "Plugins", true, viewState.pluginsVisible);
            menu.addItem(29, "Insert Chain", true, viewState.insertChainVisible);
            menu.addSeparator();
            menu.addItem(30, "Reset Panel Layout");
            menu.addSeparator();
            menu.addItem(21, "Velocity Lane", true, viewState.velocityLaneVisible);
            menu.addItem(22, "Browser", true, viewState.browserVisible);
            menu.addSeparator();
            menu.addItem(23, "Piano Roll", true, viewState.pianoRollSelected);
            menu.addItem(24, "Step Sequencer", true, ! viewState.pianoRollSelected);
            menu.addSeparator();
            menu.addItem(25, "Cycle Browser Dock", true, false);
            break;

        case 3: // Tools
            menu.addItem(42, "Metronome", true, viewState.metronomeOn);
            menu.addSeparator();
            menu.addSectionHeader(TRANS("Count-in before recording"));
            menu.addItem(43, TRANS("Off"), true, viewState.countInBars == 0);
            menu.addItem(44, "1 bar", true, viewState.countInBars == 1);
            menu.addItem(45, "2 bar", true, viewState.countInBars == 2);
            menu.addSeparator();
            menu.addItem(40, "Scan VST3 Plugins", true, false);
            menu.addItem(41, "Audio Device Settings...", true, false);
            break;

        case 4: // Help
        default:
            menu.addItem(50, "About DJR_Studio", true, false);
            break;
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&source)
                           .withMinimumWidth(190)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            switch (result)
            {
                case 1:  invoke(AppCommand::newProject); break;
                case 2:  invoke(AppCommand::openProject); break;
                case 3:  invoke(AppCommand::saveProject); break;
                case 4:  invoke(AppCommand::saveProjectAs); break;
                case 5:  invoke(AppCommand::showProjectManager); break;
                case 6:  invoke(AppCommand::quit); break;
                case 7:  invoke(AppCommand::exportAudio); break;
                case 10: invoke(AppCommand::showPreferences); break;
                case 11: invoke(AppCommand::audioSettings); break;
                case 20: invoke(AppCommand::togglePlaylist); break;
                case 21: invoke(AppCommand::toggleVelocityLane); break;
                case 22: invoke(AppCommand::toggleBrowser); break;
                case 23: invoke(AppCommand::showPianoRoll); break;
                case 24: invoke(AppCommand::showStepSequencer); break;
                case 25: invoke(AppCommand::cycleBrowserDock); break;
                case 12: invoke(AppCommand::undo); break;
                case 42: invoke(AppCommand::toggleMetronome); break;
                case 43: invoke(AppCommand::countInOff); break;
                case 44: invoke(AppCommand::countInOneBar); break;
                case 45: invoke(AppCommand::countInTwoBars); break;
                case 13: invoke(AppCommand::redo); break;
                case 26: invoke(AppCommand::toggleEditor); break;
                case 27: invoke(AppCommand::toggleMixer); break;
                case 28: invoke(AppCommand::togglePlugins); break;
                case 29: invoke(AppCommand::toggleInsertChain); break;
                case 30: invoke(AppCommand::resetPanelLayout); break;
                case 40: invoke(AppCommand::scanPlugins); break;
                case 41: invoke(AppCommand::audioSettings); break;
                case 50: invoke(AppCommand::about); break;
                default: break;
            }
        });
}

} // namespace djr
