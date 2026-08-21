#include "MixerView.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr int mixerHeaderHeight = 20;
    constexpr int stripWidth = 72;
    constexpr int stripGap = 3;
}

void MixerView::StripHolder::resized()
{
    auto area = getLocalBounds();

    for (auto* strip : strips)
    {
        strip->setBounds(area.removeFromLeft(stripWidth));
        area.removeFromLeft(stripGap);
    }
}

MixerView::MixerView(Mixer& mixerToUse)
    : mixer(mixerToUse)
{
    viewport.setViewedComponent(&holder, false);
    viewport.setScrollBarsShown(false, true, false, true);
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);

    refreshStrips();
}

void MixerView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(Theme::panelDeep());

    auto header = bounds.withHeight(mixerHeaderHeight);
    g.setColour(Theme::panel());
    g.fillRect(header);
    g.setColour(Theme::outline());
    g.fillRect(header.removeFromBottom(1));

    auto content = header.reduced(8, 0);

    // Buses used to be a fixed count of one - the master. Now that a session can
    // hold real ones, counting them as channels made the header say "7 channels"
    // for six channels and a bus.
    auto busCount = 1;

    for (int i = 0; i < mixer.getNumTracks(); ++i)
        if (const auto* track = mixer.getTrack(i); track != nullptr && track->getKind() == TrackKind::bus)
            ++busCount;

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(10.0f));
    g.drawText(juce::String(mixer.getNumTracks() - busCount + 1) + " channels - "
                   + juce::String(busCount) + " bus",
               content.removeFromLeft(140),
               juce::Justification::centredLeft,
               false);

    const auto masterPeak = mixer.getMasterBus().getPeakLevel();
    const auto db = masterPeak <= 0.0001f ? juce::String("-inf")
                                          : juce::String(juce::Decibels::gainToDecibels(masterPeak), 1);

    g.setColour(Theme::mutedText());
    g.setFont(Theme::mono(10.0f));
    g.drawText("MASTER " + db + " dB", content, juce::Justification::centredRight, false);
}

void MixerView::resized()
{
    viewport.setBounds(getLocalBounds().withTrimmedTop(mixerHeaderHeight).reduced(6, 5));

    const auto count = holder.strips.size();
    holder.setSize(juce::jmax(viewport.getWidth(), count * (stripWidth + stripGap)),
                   juce::jmax(1, viewport.getHeight() - 7));
}

void MixerView::setDeviceInputCount(int count)
{
    if (deviceInputCount == count)
        return;

    deviceInputCount = count;

    for (auto* strip : holder.strips)
        strip->setDeviceInputCount(count);
}

void MixerView::refreshStrips()
{
    holder.strips.clear();

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        if (auto* track = mixer.getTrack(i))
        {
            auto* strip = holder.strips.add(new MixerChannelStrip(*track, i));
            strip->onSelected = [this, i]
            {
                if (trackSelectedCallback)
                    trackSelectedCallback(i);
            };

            strip->onAutomationChanged = [this]
            {
                if (automationChangedCallback)
                    automationChangedCallback();
            };

            strip->onOpenChannel = [this, i]
            {
                if (openChannelCallback)
                    openChannelCallback(i);
            };

            // Routing is set through the mixer, which is the only thing that can
            // see the whole graph and refuse a route that would feed back.
            strip->setMixer(&mixer, i);
            strip->setDeviceInputCount(deviceInputCount);
            strip->onRoutingChanged = [this]
            {
                if (automationChangedCallback)
                    automationChangedCallback();

                repaint();
            };
            strip->setSelected(i == selectedTrack);
            holder.addAndMakeVisible(strip);
        }
    }

    auto* master = holder.strips.add(new MixerChannelStrip(mixer.getMasterBus()));
    holder.addAndMakeVisible(master);

    resized();
    holder.resized();

    // The header counts channels and buses, so it goes stale the moment the
    // track list changes - laying the strips out again does not redraw it.
    repaint();
}

void MixerView::setSelectedTrack(int trackIndex)
{
    selectedTrack = trackIndex;

    for (int i = 0; i < holder.strips.size(); ++i)
        if (auto* strip = holder.strips[i])
            strip->setSelected(! strip->isMaster() && i == selectedTrack);
}

void MixerView::setTrackSelectedCallback(std::function<void(int)> callback)
{
    trackSelectedCallback = std::move(callback);
}

void MixerView::setOpenChannelCallback(std::function<void(int)> callback)
{
    openChannelCallback = std::move(callback);
}

void MixerView::setAutomationChangedCallback(std::function<void()> callback)
{
    automationChangedCallback = std::move(callback);
}

} // namespace djr
