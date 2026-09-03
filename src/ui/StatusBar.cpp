#include "StatusBar.h"

#include "Theme.h"

namespace djr
{

namespace
{
    constexpr juce::int64 messageLifetimeMs = 7000;
}

StatusBar::StatusBar(AudioEngine& audioEngine)
    : engine(audioEngine)
{
    startTimerHz(4);
}

void StatusBar::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    g.setColour(Theme::inset());
    g.fillRect(bounds);
    g.setColour(Theme::outline());
    g.fillRect(bounds.withHeight(1));

    auto area = bounds.reduced(8, 0);
    const auto font = Theme::mono(10.0f);
    g.setFont(font);

    const auto measure = [&font] (const juce::String& text)
    {
        return Theme::textWidth(font, text);
    };

    auto& deviceManager = engine.getDeviceManager();
    auto* device = deviceManager.getCurrentAudioDevice();

    const auto deviceText = device != nullptr
        ? juce::String::fromUTF8("\xe2\x97\x8f ") + deviceManager.getCurrentAudioDeviceType() + " - " + device->getName()
        : juce::String::fromUTF8("\xe2\x97\x8f ") + TRANS("No audio device");

    g.setColour(device != nullptr ? Theme::green() : Theme::pink());
    g.drawText(deviceText, area.removeFromLeft(measure(deviceText)), juce::Justification::centredLeft, false);
    area.removeFromLeft(11);

    const auto sampleRate = engine.getCurrentSampleRate();
    const auto bufferSize = engine.getCurrentBufferSize();
    const auto latencyMs = sampleRate > 0.0 ? bufferSize / sampleRate * 1000.0 : 0.0;
    auto formatText = juce::String(sampleRate, 0) + " Hz - "
                    + juce::String(bufferSize) + " smp - "
                    + juce::String(latencyMs, 1) + " ms";

    // Only when there is some. Plugin latency is invisible otherwise, and the
    // usual first symptom is a mix that feels loose without anyone knowing why.
    if (const auto compensated = engine.getMixer().getReportedLatencySamples(); compensated > 0)
        formatText += " (+" + juce::String(sampleRate > 0.0 ? compensated / sampleRate * 1000.0 : 0.0, 1)
                    + " ms PDC)";

    g.setColour(Theme::mutedText());
    g.drawText(formatText, area.removeFromLeft(measure(formatText)), juce::Justification::centredLeft, false);
    area.removeFromLeft(11);

    const auto pluginText = "Plugin: " + juce::String(pluginCount) + " scanned";
    g.drawText(pluginText, area.removeFromLeft(measure(pluginText)), juce::Justification::centredLeft, false);
    area.removeFromLeft(11);

    // CPU meter on the right -------------------------------------------------
    auto right = area;
    const auto hintText = message.isNotEmpty()
        ? message
        : juce::String("Space play - Ctrl+R rec - Ctrl+S save");

    const auto percentText = juce::String(juce::roundToInt(cpuUsage * 100.0f)) + "%";

    g.setColour(Theme::faintText());
    g.drawText(hintText, right.removeFromRight(juce::jmin(right.getWidth(), measure(hintText) + 4)),
               juce::Justification::centredRight, true);
    right.removeFromRight(11);

    g.setColour(Theme::mutedText());
    g.drawText(percentText, right.removeFromRight(measure(percentText)), juce::Justification::centredRight, false);
    right.removeFromRight(6);

    auto meter = right.removeFromRight(52).withSizeKeepingCentre(52, 4);
    g.setColour(Theme::panelAlt());
    g.fillRoundedRectangle(meter.toFloat(), 2.0f);
    g.setColour(cpuUsage > 0.85f ? Theme::pink() : cpuUsage > 0.6f ? Theme::amber() : Theme::accent());
    g.fillRoundedRectangle(meter.toFloat().withWidth(meter.getWidth() * juce::jlimit(0.0f, 1.0f, cpuUsage)), 2.0f);
    right.removeFromRight(5);

    g.setColour(Theme::mutedText());
    g.drawText("CPU", right.removeFromRight(measure("CPU")), juce::Justification::centredRight, false);
}

void StatusBar::setPluginCount(int count)
{
    if (pluginCount == count)
        return;

    pluginCount = count;
    repaint();
}

void StatusBar::setMessage(const juce::String& newMessage)
{
    message = newMessage;
    messageExpiryMs = juce::Time::currentTimeMillis() + messageLifetimeMs;
    repaint();
}

void StatusBar::timerCallback()
{
    cpuUsage = static_cast<float>(engine.getDeviceManager().getCpuUsage());

    if (message.isNotEmpty() && juce::Time::currentTimeMillis() > messageExpiryMs)
        message.clear();

    repaint();
}

} // namespace djr
