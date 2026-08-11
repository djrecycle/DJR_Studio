#include "PreferencesDialog.h"

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
    constexpr int toggleRowHeight = 38;

    const char* const toggleLabels[] = {
        "Tampilkan tooltip di semua kontrol",
        "Auto-scroll playlist saat playback",
        "Buka plugin editor otomatis setelah load"
    };

    const char* const shortcutRows[] = {
        "Space", "Play / pause",
        "R", "Arm / disarm recording",
        "Ctrl + S", "Simpan project",
        "Ctrl + O", "Buka project",
        "Ctrl + N", "Project baru",
        "Ctrl + scroll", "Zoom playlist / piano roll",
        "Shift + scroll", "Geser horizontal",
        "Klik kanan (note)", "Hapus note"
    };
}

PreferencesDialog::PreferencesDialog(juce::AudioDeviceManager& deviceManagerToUse)
    : deviceManager(deviceManagerToUse)
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

    audioSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 2, 0, 2, false, false, true, false);
    addChildComponent(audioSelector.get());

    midiSelector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 0, 0, 0, true, true, true, false);
    addChildComponent(midiSelector.get());

    refreshPageVisibility();
}

PreferencesDialog::~PreferencesDialog()
{
    closeButton.removeListener(this);
    scanButton.removeListener(this);
    scaleSlider.removeListener(this);

    for (auto* toggle : { &tooltipsSwitch, &autoScrollSwitch, &autoOpenEditorSwitch })
        toggle->removeListener(this);
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

    if (currentPage == 2)
    {
        Theme::drawCaption(g, content.removeFromTop(16), "VST3 search paths");
        content.removeFromTop(6);

        const auto paths = PluginScanner::getDefaultVst3Paths();
        g.setColour(Theme::textSoft());
        g.setFont(Theme::mono(11.5f));

        for (int i = 0; i < paths.getNumPaths(); ++i)
            g.drawText(paths[i].getFullPathName(), content.removeFromTop(20), juce::Justification::centredLeft, true);

        return;
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
}

void PreferencesDialog::resized()
{
    auto card = getCardBounds();
    auto content = getContentBounds();

    closeButton.setBounds(content.removeFromTop(30).removeFromRight(26).withSizeKeepingCentre(26, 26));

    auto page = getContentBounds().withTrimmedTop(30 + 14);

    if (audioSelector != nullptr)
        audioSelector->setBounds(page);

    if (midiSelector != nullptr)
        midiSelector->setBounds(page);

    scanButton.setBounds(juce::Rectangle<int>(page.getX(),
                                              card.getBottom() - contentPadding - 30,
                                              scanButton.getPreferredWidth(),
                                              30));

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
}

void PreferencesDialog::sliderValueChanged(juce::Slider* slider)
{
    if (slider != &scaleSlider)
        return;

    if (scaleChangedCallback)
        scaleChangedCallback(juce::roundToInt(scaleSlider.getValue()));

    repaint();
}

void PreferencesDialog::refreshPageVisibility()
{
    if (audioSelector != nullptr)
        audioSelector->setVisible(currentPage == 0);

    if (midiSelector != nullptr)
        midiSelector->setVisible(currentPage == 1);

    scanButton.setVisible(currentPage == 2);
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
