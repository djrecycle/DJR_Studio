#include "PanelHost.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int minVisibleTitleWidth = 90;
}

PanelHost::PanelHost()
{
    setInterceptsMouseClicks(true, true);
}

PanelWindow& PanelHost::addPanel(const juce::String& panelId,
                                 const juce::String& title,
                                 Icon icon,
                                 juce::Component& content)
{
    auto* panel = panels.add(new PanelWindow(title, icon));
    panelIds.add(panelId);

    panel->setContent(&content);
    panel->onCloseRequested = [this, panelId] { setPanelOpen(panelId, false); };
    panel->onLayoutChanged = [this] (PanelWindow&)
    {
        if (! applyingLayout)
        {
            userArranged = true;
            layoutSize = getLocalBounds();
        }

        notifyStateChanged();
    };
    panel->onBroughtToFront = [] (PanelWindow& source) { source.toFront(false); };
    panel->addMouseListener(this, true);

    addAndMakeVisible(panel);
    return *panel;
}

PanelWindow* PanelHost::findPanel(const juce::String& panelId) const
{
    const auto index = panelIds.indexOf(panelId);
    return juce::isPositiveAndBelow(index, panels.size()) ? panels[index] : nullptr;
}

juce::StringArray PanelHost::getPanelIds() const
{
    return panelIds;
}

bool PanelHost::isPanelOpen(const juce::String& panelId) const
{
    const auto* panel = findPanel(panelId);
    return panel != nullptr && panel->isVisible();
}

void PanelHost::setPanelOpen(const juce::String& panelId, bool shouldBeOpen)
{
    auto* panel = findPanel(panelId);
    if (panel == nullptr || panel->isVisible() == shouldBeOpen)
        return;

    panel->setVisible(shouldBeOpen);

    if (shouldBeOpen)
    {
        keepPanelOnScreen(*panel);
        panel->toFront(false);
    }

    notifyStateChanged();
}

void PanelHost::togglePanel(const juce::String& panelId)
{
    setPanelOpen(panelId, ! isPanelOpen(panelId));
}

void PanelHost::setLayoutBuilder(std::function<void(PanelHost&, juce::Rectangle<int>)> builder)
{
    layoutBuilder = std::move(builder);
}

void PanelHost::resetLayout()
{
    if (! layoutBuilder || getWidth() <= 0 || getHeight() <= 0)
        return;

    const juce::ScopedValueSetter<bool> guard(applyingLayout, true);

    for (auto* panel : panels)
    {
        panel->setMaximised(false);
        panel->setRolledUp(false);
    }

    layoutBuilder(*this, getLocalBounds());
    userArranged = false;
    layoutSize = getLocalBounds();
    notifyStateChanged();
}

juce::var PanelHost::getLayoutState() const
{
    juce::Array<juce::var> entries;

    for (int i = 0; i < panels.size(); ++i)
    {
        const auto* panel = panels[i];

        if (panel == nullptr)
            continue;

        // Store the restored bounds, not the live ones: a maximised or rolled
        // up panel should come back to where it actually lives.
        const auto bounds = panel->getRestoredBounds();

        auto* object = new juce::DynamicObject();
        object->setProperty("id", panelIds[i]);
        object->setProperty("x", bounds.getX());
        object->setProperty("y", bounds.getY());
        object->setProperty("width", bounds.getWidth());
        object->setProperty("height", bounds.getHeight());
        object->setProperty("open", panel->isVisible());
        object->setProperty("rolledUp", panel->isRolledUp());
        object->setProperty("maximised", panel->isMaximised());
        // What window the bounds were made for, so they can be fitted to
        // whichever window the project is opened in.
        object->setProperty("hostWidth", getWidth());
        object->setProperty("hostHeight", getHeight());
        entries.add(object);
    }

    return entries;
}

void PanelHost::applyLayoutState(const juce::var& state)
{
    auto* entries = state.getArray();

    if (entries == nullptr || entries->isEmpty())
        return;

    const juce::ScopedValueSetter<bool> guard(applyingLayout, true);

    // The size of window this layout was made for: stored since the host size
    // was saved with it, otherwise read off the open panels' own extent.
    juce::Rectangle<int> savedFor;

    for (const auto& entry : *entries)
    {
        auto* object = entry.getDynamicObject();

        if (object == nullptr)
            continue;

        const auto hostWidth = static_cast<int>(object->getProperty("hostWidth"));
        const auto hostHeight = static_cast<int>(object->getProperty("hostHeight"));

        if (hostWidth > 0 && hostHeight > 0)
            savedFor = { hostWidth, hostHeight };
        else if (static_cast<bool>(object->getProperty("open")))
            savedFor = savedFor.getUnion({ 0, 0,
                                           static_cast<int>(object->getProperty("x")) + static_cast<int>(object->getProperty("width")),
                                           static_cast<int>(object->getProperty("y")) + static_cast<int>(object->getProperty("height")) });

        auto* panel = findPanel(object->getProperty("id").toString());

        if (panel == nullptr)
            continue;

        panel->setMaximised(false);
        panel->setRolledUp(false);

        const juce::Rectangle<int> bounds(static_cast<int>(object->getProperty("x")),
                                          static_cast<int>(object->getProperty("y")),
                                          juce::jmax(180, static_cast<int>(object->getProperty("width"))),
                                          juce::jmax(60, static_cast<int>(object->getProperty("height"))));

        panel->setRestoredBounds(bounds);
        panel->setVisible(static_cast<bool>(object->getProperty("open")));
        panel->setRolledUp(static_cast<bool>(object->getProperty("rolledUp")));
        panel->setMaximised(static_cast<bool>(object->getProperty("maximised")));
    }

    // A restored layout is the user's own arrangement, so stop auto re-tiling -
    // but fit it to this window rather than keep the size it was saved at.
    userArranged = true;
    layoutSize = savedFor;
    fitLayoutToHost();

    for (auto* panel : panels)
        if (! panel->isMaximised())
            keepPanelOnScreen(*panel);

    notifyStateChanged();
}

void PanelHost::fitLayoutToHost()
{
    const auto host = getLocalBounds();

    if (host.isEmpty() || layoutSize.isEmpty()
        || (layoutSize.getWidth() == host.getWidth() && layoutSize.getHeight() == host.getHeight()))
    {
        if (! host.isEmpty())
            layoutSize = host;

        return;
    }

    const auto sx = static_cast<double>(host.getWidth()) / layoutSize.getWidth();
    const auto sy = static_cast<double>(host.getHeight()) / layoutSize.getHeight();

    for (auto* panel : panels)
    {
        const auto restored = panel->getRestoredBounds();

        if (restored.isEmpty())
            continue;

        const auto left = juce::roundToInt(restored.getX() * sx);
        const auto right = juce::roundToInt(restored.getRight() * sx);
        const auto y0 = juce::roundToInt(restored.getY() * sy);
        const auto bottom = juce::roundToInt(restored.getBottom() * sy);
        const juce::Rectangle<int> scaled(left, y0, juce::jmax(180, right - left), juce::jmax(60, bottom - y0));

        panel->setRestoredBounds(scaled);

        if (panel->isMaximised())
            panel->setBounds(host);
        else if (panel->isRolledUp())
            panel->setBounds(scaled.withHeight(panel->getHeight()));
    }

    layoutSize = host;
}

void PanelHost::paint(juce::Graphics& g)
{
    g.fillAll(Theme::windowBackground());

    // Faint workspace grid, so an empty desktop still reads as a surface.
    g.setColour(Theme::outline().withAlpha(0.35f));

    for (int x = 24; x < getWidth(); x += 24)
        for (int y = 24; y < getHeight(); y += 24)
            g.fillRect(x, y, 1, 1);
}

void PanelHost::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Before the user has arranged anything, follow the host so the default
    // tiling always fits the real window instead of a stale startup size.
    if (! userArranged && layoutBuilder)
    {
        resetLayout();
        return;
    }

    fitLayoutToHost();

    for (auto* panel : panels)
    {
        if (panel->isMaximised())
            panel->setBounds(getLocalBounds());
        else
            keepPanelOnScreen(*panel);
    }
}

void PanelHost::mouseDown(const juce::MouseEvent& event)
{
    for (auto* panel : panels)
    {
        if (event.eventComponent == panel || panel->isParentOf(event.eventComponent))
        {
            panel->toFront(false);
            return;
        }
    }
}

void PanelHost::keepPanelOnScreen(PanelWindow& panel)
{
    auto bounds = panel.getBounds();

    if (! panel.isRolledUp())
        bounds.setSize(juce::jmin(bounds.getWidth(), juce::jmax(180, getWidth())),
                       juce::jmin(bounds.getHeight(), juce::jmax(60, getHeight())));

    bounds.setX(juce::jlimit(minVisibleTitleWidth - bounds.getWidth(),
                             juce::jmax(0, getWidth() - minVisibleTitleWidth),
                             bounds.getX()));
    bounds.setY(juce::jlimit(0, juce::jmax(0, getHeight() - Metrics::panelTitleHeight), bounds.getY()));

    panel.setBounds(bounds);

    if (! panel.isRolledUp())
        panel.setRestoredBounds(bounds);
}

void PanelHost::notifyStateChanged()
{
    if (onPanelStateChanged)
        onPanelStateChanged();
}

} // namespace djr
