#include "PanelWindow.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int titleBarHeight = Metrics::panelTitleHeight;
    constexpr int titleButtonWidth = 17;
    constexpr int resizeBorder = 4;
}

//==============================================================================
PanelWindow::TitleButton::TitleButton(const juce::String& buttonName, Icon buttonIcon)
    : juce::Button(buttonName), icon(buttonIcon)
{
    setTooltip(buttonName);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void PanelWindow::TitleButton::setIcon(Icon newIcon)
{
    icon = newIcon;
    repaint();
}

void PanelWindow::TitleButton::setDangerHover(bool shouldUseDanger)
{
    danger = shouldUseDanger;
    repaint();
}

void PanelWindow::TitleButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();

    if (highlighted || down)
    {
        g.setColour(danger ? Theme::pink().withAlpha(down ? 0.45f : 0.30f)
                           : Theme::text().withAlpha(down ? 0.18f : 0.11f));
        g.fillRoundedRectangle(bounds, 3.0f);
    }

    g.setColour(highlighted && danger ? Theme::text() : Theme::mutedText());
    Icons::draw(g, icon, bounds.reduced(4.5f), 1.4f);
}

//==============================================================================
PanelWindow::PanelWindow(const juce::String& panelTitle, Icon panelIcon)
    : title(panelTitle), icon(panelIcon)
{
    closeButton.setDangerHover(true);

    rollUpButton.onClick = [this] { toggleRollUp(); };
    maximiseButton.onClick = [this] { toggleMaximise(); };
    closeButton.onClick = [this]
    {
        if (onCloseRequested)
            onCloseRequested();
    };

    for (auto* button : { &rollUpButton, &maximiseButton, &closeButton })
        addAndMakeVisible(button);

    constrainer.setMinimumSize(180, titleBarHeight + 40);

    resizer = std::make_unique<juce::ResizableBorderComponent>(this, &constrainer);
    resizer->setBorderThickness(juce::BorderSize<int>(resizeBorder));
    addAndMakeVisible(resizer.get());

    setBroughtToFrontOnMouseClick(true);
    setRepaintsOnMouseActivity(true);
}

PanelWindow::~PanelWindow() = default;

void PanelWindow::setContent(juce::Component* newContent)
{
    if (content != nullptr)
        removeChildComponent(content);

    content = newContent;

    if (content != nullptr)
    {
        addAndMakeVisible(content);
        content->toBack();
    }

    resized();
}

void PanelWindow::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    const auto active = isMouseOverOrDragging(true);

    g.setColour(Theme::panelDeep());
    g.fillRoundedRectangle(bounds.toFloat(), Metrics::panelRadius);

    // Title bar ---------------------------------------------------------------
    auto titleBar = getTitleBarBounds();
    juce::Path titleShape;
    titleShape.addRoundedRectangle(static_cast<float>(titleBar.getX()), static_cast<float>(titleBar.getY()),
                                   static_cast<float>(titleBar.getWidth()), static_cast<float>(titleBar.getHeight()),
                                   Metrics::panelRadius, Metrics::panelRadius,
                                   true, true, rolledUp, rolledUp);
    g.setColour(active ? Theme::control() : Theme::panelHeader());
    g.fillPath(titleShape);

    auto titleContent = titleBar.reduced(6, 0);
    g.setColour(Theme::accent());
    Icons::draw(g, icon, titleContent.removeFromLeft(11).toFloat().withSizeKeepingCentre(11.0f, 11.0f), 1.5f);
    titleContent.removeFromLeft(6);

    titleContent.removeFromRight(titleButtonWidth * 3 + 8);
    g.setColour(active ? Theme::text() : Theme::textSoft());
    g.setFont(Theme::display(12.0f));
    g.drawText(title, titleContent, juce::Justification::centredLeft, true);

    if (! rolledUp)
    {
        g.setColour(Theme::outline());
        g.fillRect(titleBar.getX(), titleBar.getBottom() - 1, titleBar.getWidth(), 1);
    }

    g.setColour(active ? Theme::outlineStrong() : Theme::outline());
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), Metrics::panelRadius, 1.0f);
}

void PanelWindow::resized()
{
    if (resizer != nullptr)
    {
        const auto userResizing = resizer->isMouseButtonDown();

        resizer->setBounds(getLocalBounds());
        resizer->setVisible(! maximised && ! rolledUp);

        if (userResizing)
        {
            restoredBounds = getBounds();
            notifyLayoutChanged();
        }
    }

    auto titleBar = getTitleBarBounds().reduced(6, 0);
    titleBar.removeFromTop((titleBarHeight - 14) / 2);
    titleBar.setHeight(14);

    closeButton.setBounds(titleBar.removeFromRight(titleButtonWidth));
    titleBar.removeFromRight(1);
    maximiseButton.setBounds(titleBar.removeFromRight(titleButtonWidth));
    titleBar.removeFromRight(1);
    rollUpButton.setBounds(titleBar.removeFromRight(titleButtonWidth));

    if (content != nullptr)
    {
        content->setVisible(! rolledUp);
        content->setBounds(getContentBounds());
    }
}

void PanelWindow::mouseDown(const juce::MouseEvent& event)
{
    if (onBroughtToFront)
        onBroughtToFront(*this);

    draggingTitle = ! maximised && getTitleBarBounds().contains(event.getPosition());

    if (draggingTitle)
        dragger.startDraggingComponent(this, event);
}

void PanelWindow::mouseDrag(const juce::MouseEvent& event)
{
    if (! draggingTitle)
        return;

    dragger.dragComponent(this, event, nullptr);

    if (auto* parent = getParentComponent())
    {
        // Keep at least the title bar reachable inside the workspace.
        const auto limited = getBounds()
            .constrainedWithin(parent->getLocalBounds().expanded(getWidth() - 60, 0)
                                                       .withTop(0)
                                                       .withBottom(parent->getHeight()));
        setBounds(limited);
    }

    if (! rolledUp)
        restoredBounds = getBounds();

    notifyLayoutChanged();
}

void PanelWindow::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (getTitleBarBounds().contains(event.getPosition()))
        toggleMaximise();
}

void PanelWindow::setRolledUp(bool shouldRollUp)
{
    if (rolledUp == shouldRollUp)
        return;

    if (shouldRollUp && ! maximised)
        restoredBounds = getBounds();

    rolledUp = shouldRollUp;
    rollUpButton.setTooltip(rolledUp ? TRANS("Collapse panel") : "Gulung panel");

    if (rolledUp)
        setBounds(getBounds().withHeight(titleBarHeight));
    else
        applyRestoredBounds();

    resized();
    repaint();
    notifyLayoutChanged();
}

bool PanelWindow::isRolledUp() const noexcept
{
    return rolledUp;
}

void PanelWindow::setMaximised(bool shouldMaximise)
{
    if (maximised == shouldMaximise)
        return;

    if (shouldMaximise && ! rolledUp)
        restoredBounds = getBounds();

    maximised = shouldMaximise;
    maximiseButton.setTooltip(maximised ? "Kembalikan ukuran" : "Perbesar penuh");

    if (maximised)
    {
        rolledUp = false;
        rollUpButton.setTooltip("Gulung panel");

        if (auto* parent = getParentComponent())
            setBounds(parent->getLocalBounds());

        // A maximised panel has to sit above the ones it now covers.
        toFront(false);
    }
    else
    {
        applyRestoredBounds();
    }

    resized();
    repaint();
    notifyLayoutChanged();
}

bool PanelWindow::isMaximised() const noexcept
{
    return maximised;
}

void PanelWindow::toggleRollUp()
{
    setRolledUp(! rolledUp);
}

void PanelWindow::toggleMaximise()
{
    setMaximised(! maximised);
}

void PanelWindow::setRestoredBounds(juce::Rectangle<int> bounds)
{
    restoredBounds = bounds;

    if (! maximised && ! rolledUp)
        setBounds(bounds);
}

juce::Rectangle<int> PanelWindow::getRestoredBounds() const noexcept
{
    return restoredBounds;
}

juce::Rectangle<int> PanelWindow::getContentBounds() const
{
    if (rolledUp)
        return {};

    return getLocalBounds().withTrimmedTop(titleBarHeight).reduced(1, 0).withTrimmedBottom(1);
}

juce::String PanelWindow::getPanelTitle() const
{
    return title;
}

juce::Rectangle<int> PanelWindow::getTitleBarBounds() const
{
    return getLocalBounds().withHeight(titleBarHeight);
}

void PanelWindow::applyRestoredBounds()
{
    if (restoredBounds.isEmpty())
    {
        if (auto* parent = getParentComponent())
            restoredBounds = parent->getLocalBounds().reduced(40);
        else
            return;
    }

    setBounds(restoredBounds);
}

void PanelWindow::notifyLayoutChanged()
{
    if (onLayoutChanged)
        onLayoutChanged(*this);
}

} // namespace djr
