#include "PreferencesDialog.h"

#include "audio/AudioEngine.h"
#include "plugins/PluginScanner.h"

namespace djr
{

namespace
{
    constexpr int cardWidth = 780;
    constexpr int cardHeight = 500;
    constexpr int sidebarWidth = 186;
    constexpr int sidebarItemHeight = 30;
    constexpr int contentPadding = 18;
    constexpr int themeCardWidth = 150;
    constexpr int themeCardHeight = 74;
    /** Room under the MIDI device list for the typing keyboard settings. */
    constexpr int typingSectionHeight = 104;
    constexpr int toggleRowHeight = 38;

    const char* const toggleLabels[] = {
        "Show tooltips on every control",
        "Auto-scroll the playlist during playback",
        "Open the plugin editor automatically after loading"
    };

    const char* const shortcutRows[] = {
        "Space", "Play / pause",
        "R", "Arm / disarm recording",
        "Ctrl + S", "Save project",
        "Ctrl + O", "Open project",
        "Ctrl + N", "New project",
        "Ctrl + scroll", "Zoom playlist / piano roll",
        "Shift + scroll", "Scroll horizontally",
        "Right click (note)", "Delete note",
        "Ctrl + Up / Down", "Raise / lower the keyboard octave"
    };
}

PreferencesDialog::PreferencesDialog(juce::AudioDeviceManager& deviceManagerToUse,
                                     AudioEngine& engineToUse)
    : deviceManager(deviceManagerToUse), audioEngine(engineToUse)
{
    closeButton.setDangerHover(true);
    closeButton.setCornerSize(6.0f);
    closeButton.addListener(this);
    addAndMakeVisible(closeButton);

    scanButton.setFillColour(Theme::purple());
    scanButton.addListener(this);
    addChildComponent(scanButton);

    scaleSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    scaleSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    scaleSlider.setRange(60.0, 140.0, 5.0);
    scaleSlider.setValue(100.0, juce::dontSendNotification);
    scaleSlider.addListener(this);
    addChildComponent(scaleSlider);

    for (auto* toggle : { &tooltipsSwitch, &autoScrollSwitch, &autoOpenEditorSwitch })
    {
        toggle->addListener(this);
        addChildComponent(toggle);
    }

    tooltipsSwitch.setToggleState(true, juce::dontSendNotification);
    autoScrollSwitch.setToggleState(true, juce::dontSendNotification);

    typingKeyboardSwitch.addListener(this);
    addChildComponent(typingKeyboardSwitch);
    typingKeyboardSwitch.setToggleState(true, juce::dontSendNotification);

    for (auto* chip : { &flKeymapChip, &simpleKeymapChip })
    {
        chip->setClickingTogglesState(true);
        chip->setRadioGroupId(0xC22);
        chip->addListener(this);
        addChildComponent(chip);
    }

    flKeymapChip.setToggleState(true, juce::dontSendNotification);

    for (auto* chip : { &englishChip, &indonesianChip })
    {
        chip->setClickingTogglesState(true);
        chip->setRadioGroupId(0xC23);
        chip->addListener(this);
        addChildComponent(chip);
    }

    englishChip.setToggleState(true, juce::dontSendNotification);

    for (auto* button : { &octaveDownButton, &octaveUpButton })
    {
        button->setCornerSize(5.0f);
        button->addListener(this);
        addChildComponent(button);
    }

    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 2, 0, 2, false, false, true, false);
    addChildComponent(audioSelector.get());

    midiSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 0, 0, 0, true, true, true, false);
    addChildComponent(midiSelector.get());

    refreshPluginPaths();
    refreshPageVisibility();
}

PreferencesDialog::~PreferencesDialog()
{
    closeButton.removeListener(this);
    scanButton.removeListener(this);
    scaleSlider.removeListener(this);

    for (auto* toggle : { &tooltipsSwitch, &autoScrollSwitch, &autoOpenEditorSwitch })
        toggle->removeListener(this);

    typingKeyboardSwitch.removeListener(this);
    flKeymapChip.removeListener(this);
    simpleKeymapChip.removeListener(this);
    octaveDownButton.removeListener(this);
    octaveUpButton.removeListener(this);
    englishChip.removeListener(this);
    indonesianChip.removeListener(this);
}

void PreferencesDialog::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromString("b807080b"));

    const auto card = getCardBounds();

    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(card.toFloat().translated(0.0f, 12.0f).expanded(6.0f), 18.0f);

    Theme::drawCard(g, card, Theme::panel(), Theme::outlineStrong(), 14.0f);

    // Sidebar ----------------------------------------------------------------
    const auto sidebar = getSidebarBounds();
    juce::Path sidebarShape;
    sidebarShape.addRoundedRectangle(sidebar.getX(), sidebar.getY(), sidebar.getWidth(), sidebar.getHeight(),
                                     14.0f, 14.0f, true, false, true, false);
    g.setColour(juce::Colour::fromString("ff131720"));
    g.fillPath(sidebarShape);
    g.setColour(Theme::outline());
    g.fillRect(sidebar.getRight() - 1, sidebar.getY(), 1, sidebar.getHeight());

    Theme::drawCaption(g, sidebar.withTrimmedTop(14).withHeight(16).reduced(18, 0), "Preferences");

    for (int i = 0; i < pageNames.size(); ++i)
    {
        const auto item = getSidebarItemBounds(i);
        const auto active = i == currentPage;

        if (active)
        {
            g.setColour(Theme::control());
            g.fillRoundedRectangle(item.toFloat(), 6.0f);
        }

        g.setColour(active ? Theme::text() : Theme::textSoft());
        g.setFont(Theme::ui(12.5f, active));
        g.drawText(pageNames[i], item.reduced(10, 0), juce::Justification::centredLeft, true);
    }

    // Content header ---------------------------------------------------------
    auto content = getContentBounds();
    auto titleRow = content.removeFromTop(30);
    g.setColour(Theme::text());
    g.setFont(Theme::ui(15.0f, true));
    g.drawText(pageNames[currentPage], titleRow, juce::Justification::centredLeft, false);

    content.removeFromTop(14);

    if (currentPage == 1)
    {
        // Labels only; the controls themselves are laid out in resized().
        auto typingArea = getContentBounds().withTrimmedTop(30 + 14).removeFromBottom(typingSectionHeight);

        Theme::drawCaption(g, typingArea.removeFromTop(18), TRANS("Computer keyboard as MIDI"));

        auto switchRow = typingArea.removeFromTop(22);
        g.setColour(Theme::textSoft());
        g.setFont(Theme::ui(12.0f));
        g.drawText(TRANS("Play notes from the keyboard when there is no controller"),
                   switchRow.withTrimmedRight(46), juce::Justification::centredLeft, true);

        typingArea.removeFromTop(20);
        auto keymapRow = typingArea.removeFromTop(22);

        g.setColour(Theme::mutedText());
        g.setFont(Theme::mono(10.5f));
        g.drawText(TRANS("Octave") + " " + juce::String(typingOctave) + "   (Ctrl + Up/Down)",
                   keymapRow.withTrimmedRight(48), juce::Justification::centredRight, false);

        typingArea.removeFromTop(6);
        g.setColour(Theme::faintText());
        g.setFont(Theme::mono(10.0f));
        g.drawText(flKeymapChip.getToggleState()
                       ? TRANS("Z X C V B N M  +  Q W E R T Y U   (two octaves, like FL)")
                       : TRANS("A S D F G H J = C D E F G A B,  sharps on W E T Y U"),
                   typingArea.removeFromTop(16), juce::Justification::centredLeft, true);

        return;
    }

    if (currentPage == 2)
    {
        Theme::drawCaption(g, content.removeFromTop(16), TRANS("Plugin search paths"));

        // Rectangles come from layOutPluginPaths, so the text lands exactly
        // where the remove buttons were placed.
        for (int i = 0; i < pluginFormatNames.size(); ++i)
        {
            if (i >= static_cast<int>(pluginFormatHeaders.size()))
                break;

            g.setColour(Theme::mutedText());
            g.setFont(Theme::ui(11.0f, true));
            g.drawText(pluginFormatNames[i], pluginFormatHeaders[static_cast<size_t>(i)],
                       juce::Justification::centredLeft, false);
        }

        for (const auto& row : pluginPathRows)
        {
            if (row.bounds.isEmpty())
                continue;

            auto textArea = row.bounds.withTrimmedLeft(12);

            if (row.removable)
                textArea = textArea.withTrimmedRight(22);

            // A missing folder is worth seeing: it is the usual reason a plugin
            // the user swears is installed never turns up in a scan.
            const auto missing = ! row.path.isDirectory();

            g.setColour(missing ? Theme::amber()
                                : row.removable ? Theme::textSoft() : Theme::faintText());
            g.setFont(Theme::mono(11.0f));
            g.drawText(row.path.getFullPathName(), textArea, juce::Justification::centredLeft, true);
        }

        return;
    }

    if (currentPage == 3)
    {
        Theme::drawCaption(g, getToggleRowBounds(2).translated(0, toggleRowHeight + 4).withHeight(16),
                           TRANS("Language"));
    }

    if (currentPage == 4)
    {
        const auto rowCount = static_cast<int>(std::size(shortcutRows)) / 2;

        for (int i = 0; i < rowCount; ++i)
        {
            auto row = content.removeFromTop(30);
            g.setColour(juce::Colour::fromString("ff1e2532"));
            g.fillRect(row.withHeight(1).withY(row.getBottom() - 1));

            auto keyArea = row.removeFromLeft(150);
            g.setColour(Theme::accent());
            g.setFont(Theme::mono(11.5f));
            g.drawText(shortcutRows[i * 2], keyArea, juce::Justification::centredLeft, false);

            g.setColour(Theme::textSoft());
            g.setFont(Theme::ui(12.5f));
            g.drawText(shortcutRows[i * 2 + 1], row, juce::Justification::centredLeft, true);
        }

        return;
    }

    if (currentPage != 3)
        return;

    // Appearance -------------------------------------------------------------
    Theme::drawCaption(g, content.removeFromTop(16), "Theme");

    const ThemeVariant variants[] = { ThemeVariant::neonDark, ThemeVariant::amberStudio, ThemeVariant::iceGrey };

    for (int i = 0; i < 3; ++i)
    {
        const auto cardBounds = getThemeCardBounds(i);
        const auto accent = Theme::getVariantAccent(variants[i]);
        const auto selected = Theme::getVariant() == variants[i];

        Theme::drawCard(g, cardBounds, Theme::panelAlt(), selected ? accent : Theme::divider(), 9.0f);

        auto preview = cardBounds.reduced(10).removeFromTop(34);
        g.setGradientFill(juce::ColourGradient(Theme::windowBackground(), preview.getX(), preview.getY(),
                                               accent, preview.getRight(), preview.getBottom(), false));
        g.fillRoundedRectangle(preview.toFloat(), 6.0f);

        auto label = cardBounds.reduced(10).withTrimmedTop(34 + 8);
        auto dot = label.removeFromLeft(7).withSizeKeepingCentre(7, 7);
        g.setColour(accent);
        g.fillEllipse(dot.toFloat());
        label.removeFromLeft(6);
        g.setColour(Theme::text());
        g.setFont(Theme::ui(12.0f, true));
        g.drawText(Theme::getVariantName(variants[i]), label, juce::Justification::centredLeft, true);
    }

    auto scaleCaption = content.withTrimmedTop(themeCardHeight + 18).withHeight(16);
    Theme::drawCaption(g, scaleCaption, "Interface scale");

    g.setColour(Theme::text());
    g.setFont(Theme::mono(12.5f));
    g.drawText(juce::String(juce::roundToInt(scaleSlider.getValue())) + "%",
               juce::Rectangle<int>(scaleSlider.getRight() + 12, scaleSlider.getY(), 50, scaleSlider.getHeight()),
               juce::Justification::centredLeft,
               false);

    for (int i = 0; i < 3; ++i)
    {
        const auto row = getToggleRowBounds(i);
        g.setColour(juce::Colour::fromString("ff1e2532"));
        g.fillRect(row.withHeight(1).withY(row.getBottom() - 1));

        g.setColour(Theme::textSoft());
        g.setFont(Theme::ui(12.5f));
        g.drawText(toggleLabels[i], row.withTrimmedRight(50), juce::Justification::centredLeft, true);
    }

    // Input level meter pada audio page
    if (currentPage == 0)
    {
        const auto meterArea = getContentBounds().withTrimmedTop(30 + 14).removeFromBottom(50).reduced(10, 10);
        
        // Smooth input level untuk visual yang lebih halus
        const auto inputLevel = audioEngine.getInputPeak();
        const auto smoothFactor = 0.7f;
        smoothedInputLevel = smoothedInputLevel * smoothFactor + inputLevel * (1.0f - smoothFactor);

        // Draw label
        g.setColour(Theme::textSoft());
        g.setFont(Theme::ui(11.0f));
        g.drawText("Input Level", meterArea.withHeight(16), juce::Justification::centredLeft, false);

        // Draw meter
        auto meterBounds = meterArea.withY(meterArea.getY() + 18).withHeight(14);
        Theme::drawLevelMeter(g, meterBounds.toFloat(), smoothedInputLevel, false, 2.5f);
    }
}

void PreferencesDialog::resized()
{
    auto card = getCardBounds();
    auto content = getContentBounds();

    closeButton.setBounds(content.removeFromTop(30).removeFromRight(26).withSizeKeepingCentre(26, 26));

    auto page = getContentBounds().withTrimmedTop(30 + 14);

    if (audioSelector != nullptr)
    {
        // Leave 60px at bottom for input level meter
        auto audioPage = page.withTrimmedBottom(60);
        audioSelector->setBounds(audioPage);
    }

    if (midiSelector != nullptr)
    {
        // The typing keyboard settings sit under the device list, so the MIDI
        // page answers both "which controller" and "no controller at all".
        auto midiPage = page;
        auto typingArea = midiPage.removeFromBottom(typingSectionHeight);
        midiSelector->setBounds(midiPage.withTrimmedBottom(10));

        typingArea.removeFromTop(18);
        auto switchRow = typingArea.removeFromTop(22);
        typingKeyboardSwitch.setBounds(switchRow.removeFromRight(40).withSizeKeepingCentre(40, 18));

        typingArea.removeFromTop(20);
        auto keymapRow = typingArea.removeFromTop(22);
        flKeymapChip.setBounds(keymapRow.removeFromLeft(flKeymapChip.getPreferredWidth())
                                   .withSizeKeepingCentre(flKeymapChip.getPreferredWidth(), 18));
        keymapRow.removeFromLeft(4);
        simpleKeymapChip.setBounds(keymapRow.removeFromLeft(simpleKeymapChip.getPreferredWidth())
                                       .withSizeKeepingCentre(simpleKeymapChip.getPreferredWidth(), 18));

        octaveUpButton.setBounds(keymapRow.removeFromRight(20).withSizeKeepingCentre(20, 20));
        keymapRow.removeFromRight(4);
        octaveDownButton.setBounds(keymapRow.removeFromRight(20).withSizeKeepingCentre(20, 20));
    }

    scanButton.setBounds(juce::Rectangle<int>(page.getX(),
                                              card.getBottom() - contentPadding - 30,
                                              scanButton.getPreferredWidth(),
                                              30));

    layOutPluginPaths();

    auto languageRow = getToggleRowBounds(2).translated(0, toggleRowHeight + 22).withHeight(20);
    englishChip.setBounds(languageRow.removeFromLeft(englishChip.getPreferredWidth())
                              .withSizeKeepingCentre(englishChip.getPreferredWidth(), 18));
    languageRow.removeFromLeft(4);
    indonesianChip.setBounds(languageRow.removeFromLeft(indonesianChip.getPreferredWidth())
                                 .withSizeKeepingCentre(indonesianChip.getPreferredWidth(), 18));

    const auto scaleTop = page.getY() + 16 + themeCardHeight + 18 + 16 + 6;
    scaleSlider.setBounds(page.getX(), scaleTop, page.getWidth() - 70, 20);

    for (int i = 0; i < 3; ++i)
    {
        auto row = getToggleRowBounds(i);
        SwitchButton* toggles[] = { &tooltipsSwitch, &autoScrollSwitch, &autoOpenEditorSwitch };
        toggles[i]->setBounds(row.removeFromRight(40));
    }
}

void PreferencesDialog::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.getPosition();

    if (! getCardBounds().contains(position))
    {
        if (closeCallback)
            closeCallback();

        return;
    }

    for (int i = 0; i < pageNames.size(); ++i)
    {
        if (getSidebarItemBounds(i).contains(position))
        {
            showPage(i);
            return;
        }
    }

    if (currentPage != 3)
        return;

    const ThemeVariant variants[] = { ThemeVariant::neonDark, ThemeVariant::amberStudio, ThemeVariant::iceGrey };

    for (int i = 0; i < 3; ++i)
    {
        if (! getThemeCardBounds(i).contains(position))
            continue;

        Theme::setVariant(variants[i]);

        if (themeChangedCallback)
            themeChangedCallback(variants[i]);

        repaint();
        return;
    }
}

void PreferencesDialog::showPage(int pageIndex)
{
    currentPage = juce::jlimit(0, pageNames.size() - 1, pageIndex);
    refreshPageVisibility();
    resized();
    repaint();
}

void PreferencesDialog::setScalePercent(int percent)
{
    scaleSlider.setValue(juce::jlimit(60, 140, percent), juce::dontSendNotification);
    repaint();
}

void PreferencesDialog::setToggleStates(bool tooltips, bool autoScroll, bool autoOpenEditor)
{
    tooltipsSwitch.setToggleState(tooltips, juce::dontSendNotification);
    autoScrollSwitch.setToggleState(autoScroll, juce::dontSendNotification);
    autoOpenEditorSwitch.setToggleState(autoOpenEditor, juce::dontSendNotification);
    repaint();
}

void PreferencesDialog::setCloseCallback(std::function<void()> callback)
{
    closeCallback = std::move(callback);
}

void PreferencesDialog::setThemeChangedCallback(std::function<void(ThemeVariant)> callback)
{
    themeChangedCallback = std::move(callback);
}

void PreferencesDialog::setScaleChangedCallback(std::function<void(int)> callback)
{
    scaleChangedCallback = std::move(callback);
}

void PreferencesDialog::setTooltipsChangedCallback(std::function<void(bool)> callback)
{
    tooltipsChangedCallback = std::move(callback);
}

void PreferencesDialog::setAutoScrollChangedCallback(std::function<void(bool)> callback)
{
    autoScrollChangedCallback = std::move(callback);
}

void PreferencesDialog::setAutoOpenEditorChangedCallback(std::function<void(bool)> callback)
{
    autoOpenEditorChangedCallback = std::move(callback);
}

void PreferencesDialog::setScanRequestedCallback(std::function<void()> callback)
{
    scanRequestedCallback = std::move(callback);
}

void PreferencesDialog::buttonClicked(juce::Button* button)
{
    // The path buttons are rebuilt whenever the list changes, so they are
    // matched by position rather than by a stored pointer.
    for (int i = 0; i < addPathButtons.size(); ++i)
    {
        if (button != addPathButtons[i] || i >= pluginFormatNames.size())
            continue;

        chooseFolderForFormat(pluginFormatNames[i]);
        return;
    }

    for (int i = 0; i < removePathButtons.size(); ++i)
    {
        if (button != removePathButtons[i])
            continue;

        // The nth remove button belongs to the nth removable row.
        auto removableIndex = 0;

        for (const auto& row : pluginPathRows)
        {
            if (! row.removable)
                continue;

            if (removableIndex++ != i)
                continue;

            PluginScanner::removeUserPath(row.formatName, row.path);
            refreshPluginPaths();
            return;
        }

        return;
    }

    if (button == &closeButton)
    {
        if (closeCallback)
            closeCallback();
    }
    else if (button == &scanButton)
    {
        if (scanRequestedCallback)
            scanRequestedCallback();
    }
    else if (button == &tooltipsSwitch)
    {
        if (tooltipsChangedCallback)
            tooltipsChangedCallback(tooltipsSwitch.getToggleState());
    }
    else if (button == &autoScrollSwitch)
    {
        if (autoScrollChangedCallback)
            autoScrollChangedCallback(autoScrollSwitch.getToggleState());
    }
    else if (button == &autoOpenEditorSwitch)
    {
        if (autoOpenEditorChangedCallback)
            autoOpenEditorChangedCallback(autoOpenEditorSwitch.getToggleState());
    }
    else if (button == &typingKeyboardSwitch)
    {
        if (typingKeyboardEnabledCallback)
            typingKeyboardEnabledCallback(typingKeyboardSwitch.getToggleState());
    }
    else if (button == &flKeymapChip || button == &simpleKeymapChip)
    {
        // Read the resulting state, not which chip was clicked: a radio group
        // also notifies the one it switched off.
        if (typingKeymapCallback)
            typingKeymapCallback(simpleKeymapChip.getToggleState() ? 1 : 0);

        repaint();
    }
    else if (button == &englishChip || button == &indonesianChip)
    {
        // The resulting state, not which chip was clicked: a radio group also
        // notifies the one it switched off.
        if (languageChangedCallback)
            languageChangedCallback(indonesianChip.getToggleState() ? 1 : 0);
    }
    else if (button == &octaveDownButton || button == &octaveUpButton)
    {
        typingOctave = juce::jlimit(0, 8, typingOctave + (button == &octaveUpButton ? 1 : -1));

        if (typingOctaveCallback)
            typingOctaveCallback(typingOctave);

        repaint();
    }
}

void PreferencesDialog::setTypingKeyboardState(bool enabled, int keymapIndex, int octave)
{
    typingKeyboardSwitch.setToggleState(enabled, juce::dontSendNotification);
    flKeymapChip.setToggleState(keymapIndex == 0, juce::dontSendNotification);
    simpleKeymapChip.setToggleState(keymapIndex == 1, juce::dontSendNotification);
    typingOctave = juce::jlimit(0, 8, octave);
    repaint();
}

void PreferencesDialog::setLanguageIndex(int index)
{
    englishChip.setToggleState(index == 0, juce::dontSendNotification);
    indonesianChip.setToggleState(index == 1, juce::dontSendNotification);
    repaint();
}

void PreferencesDialog::setLanguageChangedCallback(std::function<void(int)> callback)
{
    languageChangedCallback = std::move(callback);
}

void PreferencesDialog::setTypingKeyboardEnabledCallback(std::function<void(bool)> callback)
{
    typingKeyboardEnabledCallback = std::move(callback);
}

void PreferencesDialog::setTypingKeymapCallback(std::function<void(int)> callback)
{
    typingKeymapCallback = std::move(callback);
}

void PreferencesDialog::setTypingOctaveCallback(std::function<void(int)> callback)
{
    typingOctaveCallback = std::move(callback);
}

void PreferencesDialog::sliderValueChanged(juce::Slider* slider)
{
    if (slider != &scaleSlider)
        return;

    if (scaleChangedCallback)
        scaleChangedCallback(juce::roundToInt(scaleSlider.getValue()));

    repaint();
}

void PreferencesDialog::refreshPluginPaths()
{
    pluginPathRows.clear();
    pluginFormatNames.clear();
    removePathButtons.clear();
    addPathButtons.clear();

    // Asked of the formats themselves, so this page cannot drift out of step
    // with what the scanner actually looks at.
    juce::AudioPluginFormatManager formats;
    formats.addDefaultFormats();

    for (auto* format : formats.getFormats())
    {
        if (format == nullptr)
            continue;

        const auto formatName = format->getName();
        pluginFormatNames.add(formatName);

        const auto builtIn = PluginScanner::getBuiltInPathsFor(*format);

        for (int i = 0; i < builtIn.getNumPaths(); ++i)
            pluginPathRows.push_back({ formatName, builtIn[i], false, {} });

        const auto userPaths = PluginScanner::getUserPathsFor(formatName);

        for (int i = 0; i < userPaths.getNumPaths(); ++i)
        {
            pluginPathRows.push_back({ formatName, userPaths[i], true, {} });

            auto* remove = new IconChipButton(TRANS("Remove this folder"), Icon::close);
            remove->setDangerHover(true);
            remove->setCornerSize(4.0f);
            remove->setIconInset(5.0f);
            remove->addListener(this);
            addChildComponent(remove);
            removePathButtons.add(remove);
        }

        auto* add = new PillButton(TRANS("Add folder"), Icon::plus, PillButton::Style::outline);
        add->setCornerSize(5.0f);
        add->addListener(this);
        addChildComponent(add);
        addPathButtons.add(add);
    }

    refreshPageVisibility();
    resized();
    repaint();
}

void PreferencesDialog::layOutPluginPaths()
{
    auto content = getContentBounds().withTrimmedTop(30 + 14);
    content.removeFromTop(16 + 6);  // the "Plugin search paths" caption

    pluginFormatHeaders.clear();

    auto rowIndex = size_t {};
    auto removeIndex = 0;

    for (int formatIndex = 0; formatIndex < pluginFormatNames.size(); ++formatIndex)
    {
        const auto& formatName = pluginFormatNames[formatIndex];

        pluginFormatHeaders.push_back(content.removeFromTop(18));

        for (; rowIndex < pluginPathRows.size(); ++rowIndex)
        {
            auto& row = pluginPathRows[rowIndex];

            if (row.formatName != formatName)
                break;

            auto bounds = content.removeFromTop(18);
            row.bounds = bounds;

            if (! row.removable)
                continue;

            if (auto* button = removePathButtons[removeIndex++])
                button->setBounds(bounds.removeFromRight(18).withSizeKeepingCentre(16, 16));
        }

        auto buttonRow = content.removeFromTop(24);

        if (auto* add = addPathButtons[formatIndex])
            add->setBounds(buttonRow.removeFromLeft(add->getPreferredWidth())
                               .withSizeKeepingCentre(add->getPreferredWidth(), 22));

        content.removeFromTop(8);
    }
}

void PreferencesDialog::chooseFolderForFormat(const juce::String& formatName)
{
    pathChooser = std::make_unique<juce::FileChooser>(
        TRANS("Choose a folder to search for ") + formatName + TRANS(" plugins"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    pathChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
        [this, formatName] (const juce::FileChooser& chooser)
        {
            const auto folder = chooser.getResult();

            if (folder == juce::File() || ! folder.isDirectory())
                return;

            if (PluginScanner::addUserPath(formatName, folder))
                refreshPluginPaths();
        });
}

void PreferencesDialog::refreshPageVisibility()
{
    if (audioSelector != nullptr)
        audioSelector->setVisible(currentPage == 0);

    if (midiSelector != nullptr)
        midiSelector->setVisible(currentPage == 1);

    englishChip.setVisible(currentPage == 3);
    indonesianChip.setVisible(currentPage == 3);
    typingKeyboardSwitch.setVisible(currentPage == 1);
    flKeymapChip.setVisible(currentPage == 1);
    simpleKeymapChip.setVisible(currentPage == 1);
    octaveDownButton.setVisible(currentPage == 1);
    octaveUpButton.setVisible(currentPage == 1);

    scanButton.setVisible(currentPage == 2);

    for (auto* button : removePathButtons)
        button->setVisible(currentPage == 2);

    for (auto* button : addPathButtons)
        button->setVisible(currentPage == 2);
    scaleSlider.setVisible(currentPage == 3);
    tooltipsSwitch.setVisible(currentPage == 3);
    autoScrollSwitch.setVisible(currentPage == 3);
    autoOpenEditorSwitch.setVisible(currentPage == 3);
}

juce::Rectangle<int> PreferencesDialog::getCardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre(juce::jmin(cardWidth, getWidth() - 40),
                                                  juce::jmin(cardHeight, getHeight() - 40));
}

juce::Rectangle<int> PreferencesDialog::getSidebarBounds() const
{
    return getCardBounds().withWidth(sidebarWidth);
}

juce::Rectangle<int> PreferencesDialog::getContentBounds() const
{
    return getCardBounds().withTrimmedLeft(sidebarWidth).reduced(contentPadding, contentPadding);
}

juce::Rectangle<int> PreferencesDialog::getSidebarItemBounds(int index) const
{
    const auto sidebar = getSidebarBounds();
    const auto top = sidebar.getY() + 14 + 16 + 8 + index * (sidebarItemHeight + 2);

    return { sidebar.getX() + 10, top, sidebar.getWidth() - 20, sidebarItemHeight };
}

juce::Rectangle<int> PreferencesDialog::getThemeCardBounds(int index) const
{
    const auto content = getContentBounds().withTrimmedTop(30 + 14 + 16 + 6);
    return { content.getX() + index * (themeCardWidth + 10), content.getY(), themeCardWidth, themeCardHeight };
}

juce::Rectangle<int> PreferencesDialog::getToggleRowBounds(int index) const
{
    const auto content = getContentBounds().withTrimmedTop(30 + 14);
    const auto top = content.getY() + 16 + themeCardHeight + 18 + 16 + 6 + 20 + 20 + index * toggleRowHeight;

    return { content.getX(), top, content.getWidth(), toggleRowHeight };
}

} // namespace djr
