#include "TransportBar.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int podHeight = 26;
    constexpr int podRadius = 5;
    constexpr int podGap = 7;
    constexpr int masterMeterWidth = 92;

    void drawPod(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour fill)
    {
        Theme::drawCard(g, bounds, fill, Theme::outlineStrong(), static_cast<float>(podRadius));
    }

    void drawDivider(juce::Graphics& g, int x, juce::Rectangle<int> pod)
    {
        g.setColour(Theme::outlineStrong());
        g.fillRect(x, pod.getCentreY() - 8, 1, 16);
    }

    /** Fill that keeps the enclosing pod's rounded corners on the outer segments. */
    void fillSegment(juce::Graphics& g, juce::Rectangle<int> bounds, bool roundLeft, bool roundRight)
    {
        if (! roundLeft && ! roundRight)
        {
            g.fillRect(bounds);
            return;
        }

        juce::Path shape;
        shape.addRoundedRectangle(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
                                  static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight()),
                                  static_cast<float>(podRadius), static_cast<float>(podRadius),
                                  roundLeft, roundRight, roundLeft, roundRight);
        g.fillPath(shape);
    }
}

//==============================================================================
TransportBar::TransportButton::TransportButton(const juce::String& buttonName, Icon icon)
    : juce::Button(buttonName), iconKind(icon),
      activeColour(Theme::green()), iconColour(Theme::textSoft())
{
    setTooltip(buttonName);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TransportBar::TransportButton::setIcon(Icon icon)
{
    iconKind = icon;
    repaint();
}

void TransportBar::TransportButton::setActiveColour(juce::Colour colour)
{
    activeColour = colour;
    repaint();
}

void TransportBar::TransportButton::setActive(bool shouldBeActive)
{
    if (active == shouldBeActive)
        return;

    active = shouldBeActive;
    repaint();
}

void TransportBar::TransportButton::setIconColour(juce::Colour colour)
{
    iconColour = colour;
    repaint();
}

void TransportBar::TransportButton::setShowRightDivider(bool shouldShow)
{
    rightDivider = shouldShow;
    repaint();
}

void TransportBar::TransportButton::setRoundedEdges(bool left, bool right)
{
    roundLeft = left;
    roundRight = right;
    repaint();
}

void TransportBar::TransportButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds();

    if (active)
    {
        g.setColour(down ? activeColour.brighter(0.2f) : highlighted ? activeColour.brighter(0.12f) : activeColour);
        fillSegment(g, bounds, roundLeft, roundRight);
    }
    else if (highlighted || down)
    {
        g.setColour(down ? Theme::controlHover() : Theme::control());
        fillSegment(g, bounds, roundLeft, roundRight);
    }

    if (rightDivider)
    {
        g.setColour(Theme::outlineStrong());
        g.fillRect(bounds.getRight() - 1, bounds.getY(), 1, bounds.getHeight());
    }

    g.setColour(active ? Theme::windowBackground() : iconColour);
    Icons::draw(g, iconKind, bounds.toFloat().withSizeKeepingCentre(12.0f, 12.0f), 1.5f);
}

//==============================================================================
TransportBar::SegmentButton::SegmentButton(const juce::String& buttonText, Icon icon)
    : juce::Button(buttonText), iconKind(icon)
{
    setButtonText(buttonText);
    setClickingTogglesState(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void TransportBar::SegmentButton::setRoundedEdges(bool left, bool right)
{
    roundLeft = left;
    roundRight = right;
    repaint();
}

int TransportBar::SegmentButton::getPreferredWidth() const
{
    return 16 + 11 + 5 + Theme::textWidth(Theme::ui(12.0f, true), getButtonText());
}

void TransportBar::SegmentButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds();
    const auto active = getToggleState();

    if (active)
    {
        g.setColour(down || highlighted ? Theme::accent().brighter(0.12f) : Theme::accent());
        fillSegment(g, bounds, roundLeft, roundRight);
    }
    else if (highlighted || down)
    {
        g.setColour(Theme::control());
        fillSegment(g, bounds, roundLeft, roundRight);
    }

    const auto foreground = active ? Theme::windowBackground() : Theme::textSoft();
    auto content = bounds.reduced(8, 0);
    const auto iconArea = content.removeFromLeft(11).toFloat().withSizeKeepingCentre(11.0f, 11.0f);
    content.removeFromLeft(5);

    g.setColour(foreground);
    Icons::draw(g, iconKind, iconArea, 1.4f);
    g.setFont(Theme::ui(12.0f, true));
    g.drawText(getButtonText(), content, juce::Justification::centredLeft, false);
}

//==============================================================================
TransportBar::TransportBar(AudioEngine& audioEngine)
    : engine(audioEngine), transport(audioEngine.getTransport())
{
    playButton.setActiveColour(Theme::green());
    playButton.setIconColour(Theme::green());
    recordButton.setActiveColour(Theme::pink().withAlpha(0.28f));
    loopButton.setActiveColour(Theme::accent().withAlpha(0.30f));
    metronomeButton.setActiveColour(Theme::amber().withAlpha(0.32f));
    metronomeButton.setShowRightDivider(false);
    playButton.setRoundedEdges(true, false);
    metronomeButton.setRoundedEdges(false, true);

    for (auto* button : { &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    for (auto* button : { &tempoUpButton, &tempoDownButton })
    {
        button->addListener(this);
        button->setCornerSize(2.0f);
        button->setIconInset(1.5f);
        addAndMakeVisible(button);
    }

    for (auto* button : { &patternButton, &songButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    patternButton.setToggleState(true, juce::dontSendNotification);
    patternButton.setRoundedEdges(true, false);
    songButton.setRoundedEdges(false, true);

    snapButton.addListener(this);
    snapButton.setTooltip("Snap grid");
    addAndMakeVisible(snapButton);

    for (auto* button : { &undoButton, &redoButton })
    {
        button->setIconInset(4.0f);
        button->addListener(this);
        addAndMakeVisible(button);
    }

    transport.setLoopEnabled(patternMode);
    transport.setLoopRangeBeats(0.0, 16.0);

    startTimerHz(30);
}

TransportBar::~TransportBar()
{
    const juce::Array<juce::Button*> buttons {
        &playButton, &stopButton, &recordButton, &loopButton,
        &tempoUpButton, &tempoDownButton,
        &patternButton, &songButton, &snapButton, &undoButton, &redoButton, &metronomeButton
    };

    for (auto* button : buttons)
        button->removeListener(this);
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(Theme::transportBackground());
    g.setColour(Theme::divider());
    g.fillRect(getLocalBounds().removeFromBottom(1));

    drawPod(g, transportPod, Theme::panelAlt());

    // Tempo ------------------------------------------------------------------
    drawPod(g, tempoPod, Theme::panelAlt());
    Theme::drawCaption(g, tempoPod.withX(tempoPod.getX() + 6).withWidth(24), "BPM");

    g.setColour(Theme::text());
    g.setFont(Theme::mono(15.0f, true));
    g.drawText(juce::String(transport.getTempoBpm(), 1), tempoValueArea, juce::Justification::centredLeft, false);

    drawDivider(g, signatureArea.getX() - 6, tempoPod);
    Theme::drawCaption(g, signatureArea.withWidth(20), "Sig");
    g.setColour(Theme::textSoft());
    g.setFont(Theme::mono(13.0f));
    g.drawText(juce::String(transport.getTimeSignatureNumerator()) + "/"
                   + juce::String(transport.getTimeSignatureDenominator()),
               signatureArea.withTrimmedLeft(22),
               juce::Justification::centredLeft,
               false);

    // Position ---------------------------------------------------------------
    drawPod(g, positionPod, Theme::inset());
    Theme::drawCaption(g, positionArea.withWidth(22), "Pos");
    g.setColour(Theme::green());
    g.setFont(Theme::mono(15.0f, true));
    g.drawText(getPositionText(), positionArea.withTrimmedLeft(24), juce::Justification::centredLeft, false);

    drawDivider(g, timeArea.getX() - 6, positionPod);
    Theme::drawCaption(g, timeArea.withWidth(26), "Time");
    g.setColour(Theme::textSoft());
    g.setFont(Theme::mono(15.0f));
    g.drawText(getTimeText(), timeArea.withTrimmedLeft(28), juce::Justification::centredLeft, false);

    // Mode / snap ------------------------------------------------------------
    drawPod(g, modePod, Theme::panelAlt());
    drawPod(g, snapPod, Theme::panelAlt());

    auto snapContent = snapPod.reduced(6, 0);
    Theme::drawCaption(g, snapContent.removeFromLeft(26), "Snap");
    snapContent.removeFromLeft(4);
    g.setColour(Theme::text());
    g.setFont(Theme::mono(12.5f));
    const auto snapText = getSnapUnitLabel(snapUnit);
    const auto snapWidth = Theme::textWidth(Theme::mono(12.5f), snapText);
    g.drawText(snapText, snapContent.removeFromLeft(snapWidth), juce::Justification::centredLeft, false);
    snapContent.removeFromLeft(4);
    g.setColour(Theme::mutedText());
    Icons::draw(g, Icon::caretDown, snapContent.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 5.0f));

    drawPod(g, historyPod, Theme::panelAlt());

    // Master meter -----------------------------------------------------------
    drawPod(g, masterPod, Theme::panelAlt());
    Theme::drawCaption(g, masterPod.reduced(6, 0).removeFromLeft(36), "Master");

    auto meters = masterMeterArea;
    Theme::drawLevelMeter(g, meters.removeFromTop(5).toFloat(), smoothedMasterLeft, false, 2.5f);
    meters.removeFromTop(2);
    Theme::drawLevelMeter(g, meters.removeFromTop(5).toFloat(), smoothedMasterRight, false, 2.5f);

    g.setColour(Theme::textSoft());
    g.setFont(Theme::mono(11.5f));
    g.drawText(getMasterDbText(),
               masterPod.withTrimmedRight(6).removeFromRight(32),
               juce::Justification::centredRight,
               false);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced(7, 0);
    const auto podTop = (getHeight() - podHeight) / 2;
    const auto podRow = [podTop] (juce::Rectangle<int> r) { return r.withY(podTop).withHeight(podHeight); };

    transportPod = podRow(area.removeFromLeft(139));
    auto transportRow = transportPod;
    playButton.setBounds(transportRow.removeFromLeft(31));
    stopButton.setBounds(transportRow.removeFromLeft(27));
    recordButton.setBounds(transportRow.removeFromLeft(27));
    loopButton.setBounds(transportRow.removeFromLeft(27));
    metronomeButton.setBounds(transportRow);
    area.removeFromLeft(podGap);

    tempoPod = podRow(area.removeFromLeft(160));
    auto tempoRow = tempoPod.reduced(6, 0);
    tempoRow.removeFromLeft(24);
    tempoValueArea = tempoRow.removeFromLeft(40);
    tempoRow.removeFromLeft(3);
    auto nudge = tempoRow.removeFromLeft(12).withSizeKeepingCentre(12, 20);
    tempoUpButton.setBounds(nudge.removeFromTop(9));
    nudge.removeFromTop(2);
    tempoDownButton.setBounds(nudge.removeFromTop(9));
    tempoRow.removeFromLeft(12);
    signatureArea = tempoRow;
    area.removeFromLeft(podGap);

    positionPod = podRow(area.removeFromLeft(180));
    auto positionRow = positionPod.reduced(6, 0);
    positionArea = positionRow.removeFromLeft(80);
    positionRow.removeFromLeft(12);
    timeArea = positionRow;
    area.removeFromLeft(podGap);

    const auto modeWidth = patternButton.getPreferredWidth() + songButton.getPreferredWidth();
    modePod = podRow(area.removeFromLeft(modeWidth));
    auto modeRow = modePod;
    patternButton.setBounds(modeRow.removeFromLeft(patternButton.getPreferredWidth()));
    songButton.setBounds(modeRow);
    area.removeFromLeft(podGap);

    // Wide enough for the longest FL label, "1/6 step".
    snapPod = podRow(area.removeFromLeft(122));
    snapButton.setBounds(snapPod);

    area.removeFromLeft(podGap);
    historyPod = podRow(area.removeFromLeft(52));

    auto historyRow = historyPod.reduced(5, 0);
    undoButton.setBounds(historyRow.removeFromLeft(19).withSizeKeepingCentre(19, 19));
    historyRow.removeFromLeft(4);
    redoButton.setBounds(historyRow.removeFromLeft(19).withSizeKeepingCentre(19, 19));

    masterPod = podRow(area.removeFromRight(184));
    auto masterRow = masterPod.reduced(6, 0);
    masterRow.removeFromLeft(36 + 5);
    masterMeterArea = masterRow.removeFromLeft(masterMeterWidth).withSizeKeepingCentre(masterMeterWidth, 12);
}

void TransportBar::mouseDown(const juce::MouseEvent& event)
{
    if (signatureArea.contains(event.getPosition()))
    {
        showTimeSignatureMenu();
        return;
    }

    if (tempoValueArea.contains(event.getPosition()))
        tempoDragStart = transport.getTempoBpm();
}

void TransportBar::showTimeSignatureMenu()
{
    static const std::pair<int, int> signatures[] = {
        { 4, 4 }, { 3, 4 }, { 2, 4 }, { 5, 4 }, { 6, 4 } ,
        { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 }
    };

    juce::PopupMenu menu;
    menu.addSectionHeader("Time signature");

    for (int i = 0; i < static_cast<int>(std::size(signatures)); ++i)
    {
        const auto ticked = signatures[i].first == transport.getTimeSignatureNumerator()
                         && signatures[i].second == transport.getTimeSignatureDenominator();

        menu.addItem(i + 1,
                     juce::String(signatures[i].first) + "/" + juce::String(signatures[i].second),
                     true,
                     ticked);
    }

    // Anchor to the readout itself, not the whole bar, so the menu drops open
    // under what was clicked.
    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetScreenArea(localAreaToGlobal(signatureArea))
                           .withMinimumWidth(110)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            if (! juce::isPositiveAndBelow(result - 1, static_cast<int>(std::size(signatures))))
                return;

            // The host applies it, so it can record an undo point first.
            if (timeSignatureChangeCallback)
                timeSignatureChangeCallback(signatures[result - 1].first, signatures[result - 1].second);

            repaint();
        });
}

void TransportBar::mouseDrag(const juce::MouseEvent& event)
{
    if (! tempoValueArea.contains(event.getMouseDownPosition()))
        return;

    const auto step = event.mods.isShiftDown() ? 0.05 : 0.25;
    transport.setTempoBpm(tempoDragStart - event.getDistanceFromDragStartY() * step);
    repaint(tempoPod);
}

void TransportBar::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (! tempoPod.contains(event.getPosition()))
        return;

    nudgeTempo(wheel.deltaY > 0.0f ? 1.0 : -1.0);
}

void TransportBar::buttonClicked(juce::Button* button)
{
    if (button == &playButton)
        transport.togglePlayStop();
    else if (button == &stopButton)
        transport.stop();
    else if (button == &recordButton)
    {
        if (recordToggleCallback)
            recordToggleCallback();
        else
            transport.setRecording(! transport.isRecording());
    }
    else if (button == &loopButton)
        transport.setLoopEnabled(! transport.isLoopEnabled());
    else if (button == &tempoUpButton)
        nudgeTempo(juce::ModifierKeys::currentModifiers.isShiftDown() ? 0.1 : 1.0);
    else if (button == &tempoDownButton)
        nudgeTempo(juce::ModifierKeys::currentModifiers.isShiftDown() ? -0.1 : -1.0);
    else if (button == &patternButton)
        setPatternMode(true);
    else if (button == &songButton)
        setPatternMode(false);
    else if (button == &metronomeButton)
    {
        if (metronomeToggleCallback)
            metronomeToggleCallback();
    }
    else if (button == &snapButton)
        showSnapMenu();
    else if (button == &undoButton)
    {
        if (undoCallback)
            undoCallback();
    }
    else if (button == &redoButton)
    {
        if (redoCallback)
            redoCallback();
    }

    refreshButtons();
    repaint();
}

void TransportBar::timerCallback()
{
    const auto& master = engine.getMixer().getMasterBus();
    const auto decay = 0.25f;
    smoothedMasterLeft = juce::jmax(Theme::meterPosition(master.getPeakLevel(0)),
                                    smoothedMasterLeft - decay);
    smoothedMasterRight = juce::jmax(Theme::meterPosition(master.getPeakLevel(1)),
                                     smoothedMasterRight - decay);

    refreshButtons();
    repaint();
}

void TransportBar::refreshButtons()
{
    playButton.setIcon(transport.isPlaying() ? Icon::pause : Icon::play);
    playButton.setActive(transport.isPlaying());
    recordButton.setActive(transport.isRecording());
    recordButton.setIconColour(transport.isRecording() ? Theme::pink() : Theme::mutedText());
    loopButton.setActive(transport.isLoopEnabled());

    patternButton.setToggleState(patternMode, juce::dontSendNotification);
    songButton.setToggleState(! patternMode, juce::dontSendNotification);
}

void TransportBar::nudgeTempo(double delta)
{
    transport.setTempoBpm(transport.getTempoBpm() + delta);
    repaint(tempoPod);
}

void TransportBar::showSnapMenu()
{
    auto count = 0;
    const auto* order = getSnapMenuOrder(count);

    juce::PopupMenu menu;
    for (int i = 0; i < count; ++i)
    {
        menu.addItem(i + 1, getSnapUnitLabel(order[i]), true, order[i] == snapUnit);

        // FL groups the view relative choices away from the fixed lengths.
        if (order[i] == SnapUnit::none)
            menu.addSeparator();
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(&snapButton)
                           .withMinimumWidth(100)
                           .withStandardItemHeight(21),
        [this] (int result)
        {
            auto resultCount = 0;
            const auto* entries = getSnapMenuOrder(resultCount);

            if (! juce::isPositiveAndBelow(result - 1, resultCount))
                return;

            snapUnit = entries[result - 1];

            if (snapChangeCallback)
                snapChangeCallback(snapUnit);

            repaint();
        });
}

void TransportBar::setMetronomeToggleCallback(std::function<void()> callback)
{
    metronomeToggleCallback = std::move(callback);
}

void TransportBar::setMetronomeActive(bool isActive)
{
    metronomeButton.setActive(isActive);
}

void TransportBar::setTimeSignatureChangeCallback(std::function<void(int, int)> callback)
{
    timeSignatureChangeCallback = std::move(callback);
}

void TransportBar::setUndoCallback(std::function<void()> callback)
{
    undoCallback = std::move(callback);
}

void TransportBar::setRedoCallback(std::function<void()> callback)
{
    redoCallback = std::move(callback);
}

void TransportBar::setUndoState(bool canUndo, bool canRedo,
                                const juce::String& undoName, const juce::String& redoName)
{
    undoButton.setEnabled(canUndo);
    redoButton.setEnabled(canRedo);
    undoButton.setTooltip(canUndo && undoName.isNotEmpty() ? "Undo " + undoName : "Undo");
    redoButton.setTooltip(canRedo && redoName.isNotEmpty() ? "Redo " + redoName : "Redo");
}

void TransportBar::setSnapChangeCallback(std::function<void(SnapUnit)> callback)
{
    snapChangeCallback = std::move(callback);
}

void TransportBar::setPatternModeChangeCallback(std::function<void(bool)> callback)
{
    patternModeChangeCallback = std::move(callback);
}

void TransportBar::setRecordToggleCallback(std::function<void()> callback)
{
    recordToggleCallback = std::move(callback);
}

void TransportBar::setPatternMode(bool shouldUsePatternMode)
{
    if (patternMode == shouldUsePatternMode)
        return;

    patternMode = shouldUsePatternMode;
    transport.setLoopEnabled(patternMode);

    if (patternModeChangeCallback)
        patternModeChangeCallback(patternMode);

    refreshButtons();
    repaint();
}

bool TransportBar::isPatternMode() const noexcept
{
    return patternMode;
}

SnapUnit TransportBar::getSnapUnit() const noexcept
{
    return snapUnit;
}

juce::String TransportBar::getPositionText() const
{
    const auto beats = transport.getPositionBeats();
    const auto bar = static_cast<int>(std::floor(beats / 4.0)) + 1;
    const auto beatInBar = static_cast<int>(std::floor(std::fmod(beats, 4.0))) + 1;
    const auto tick = static_cast<int>(std::fmod(beats, 1.0) * 96.0);

    return juce::String(bar) + ":" + juce::String(beatInBar) + ":" + juce::String(tick).paddedLeft('0', 2);
}

juce::String TransportBar::getTimeText() const
{
    const auto tempo = juce::jmax(1.0, transport.getTempoBpm());
    const auto seconds = transport.getPositionBeats() * 60.0 / tempo;
    const auto minutes = static_cast<int>(seconds / 60.0);
    const auto wholeSeconds = static_cast<int>(std::fmod(seconds, 60.0));
    const auto tenths = static_cast<int>(std::fmod(seconds, 1.0) * 10.0);

    return juce::String(minutes) + ":" + juce::String(wholeSeconds).paddedLeft('0', 2) + "." + juce::String(tenths);
}

juce::String TransportBar::getMasterDbText() const
{
    const auto peak = juce::jmax(smoothedMasterLeft, smoothedMasterRight);
    if (peak <= 0.0001f)
        return "-inf";

    return juce::String(juce::Decibels::gainToDecibels(peak), 1);
}

} // namespace djr
