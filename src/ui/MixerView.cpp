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

    g.setColour(Theme::faintText());
    g.setFont(Theme::mono(10.0f));
    g.drawText(juce::String(mixer.getNumTracks()) + " channels - 1 bus",
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
            strip->setSelected(i == selectedTrack);
            holder.addAndMakeVisible(strip);
        }
    }

    auto* master = holder.strips.add(new MixerChannelStrip(mixer.getMasterBus()));
    holder.addAndMakeVisible(master);

    resized();
    holder.resized();
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

} // namespace djr
