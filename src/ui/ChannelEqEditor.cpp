#include "ChannelEqEditor.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr double minCurveHz = 20.0;
    constexpr double maxCurveHz = 20000.0;
    constexpr float curveRangeDb = 15.0f;
    constexpr int padding = 10;
    constexpr int headerHeight = 22;

    juce::String describeDb(double db)
    {
        return (db > 0.05 ? "+" : "") + juce::String(db, 1) + " dB";
    }

    juce::String describeHz(double hz)
    {
        return hz >= 1000.0 ? juce::String(hz / 1000.0, hz >= 10000.0 ? 0 : 1) + " kHz"
                            : juce::String(juce::roundToInt(hz)) + " Hz";
    }
}

ChannelEqEditor::ChannelEqEditor(ChannelEq& eqToEdit, const juce::String& name, juce::Colour accentColour, double rate)
    : eq(eqToEdit), trackName(name), accent(accentColour), sampleRate(rate > 0.0 ? rate : 44100.0)
{
    for (auto* knob : { &lowKnob, &midKnob, &highKnob })
    {
        knob->setRange(ChannelEq::minGainDb, ChannelEq::maxGainDb, 0.1);
        knob->setDoubleClickReturnValue(true, 0.0);
        knob->textFromValueFunction = [] (double value) { return describeDb(value); };
    }

    freqKnob.setRange(ChannelEq::minMidHz, ChannelEq::maxMidHz, 1.0);
    freqKnob.setSkewFactorFromMidPoint(1000.0);
    freqKnob.setDoubleClickReturnValue(true, ChannelEq::defaultMidHz);
    freqKnob.textFromValueFunction = [] (double value) { return describeHz(value); };

    for (auto* knob : { &lowKnob, &midKnob, &freqKnob, &highKnob })
    {
        knob->setPopupDisplayEnabled(true, true, this);
        knob->onValueChange = [this] { pushSettings(); };
        addAndMakeVisible(knob);
    }

    onSwitch.addListener(this);
    resetButton.addListener(this);
    resetButton.setCornerSize(5.0f);
    addAndMakeVisible(onSwitch);
    addAndMakeVisible(resetButton);

    pullSettings();
    setSize(preferredWidth, preferredHeight);
}

ChannelEqEditor::~ChannelEqEditor()
{
    onSwitch.removeListener(this);
    resetButton.removeListener(this);
}

void ChannelEqEditor::pullSettings()
{
    const auto settings = eq.getSettings();
    onSwitch.setToggleState(settings.enabled, juce::dontSendNotification);
    lowKnob.setValue(settings.lowDb, juce::dontSendNotification);
    midKnob.setValue(settings.midDb, juce::dontSendNotification);
    freqKnob.setValue(settings.midHz, juce::dontSendNotification);
    highKnob.setValue(settings.highDb, juce::dontSendNotification);
}

void ChannelEqEditor::pushSettings()
{
    ChannelEq::Settings settings;
    settings.enabled = onSwitch.getToggleState();
    settings.lowDb = static_cast<float>(lowKnob.getValue());
    settings.midDb = static_cast<float>(midKnob.getValue());
    settings.midHz = static_cast<float>(freqKnob.getValue());
    settings.highDb = static_cast<float>(highKnob.getValue());
    eq.setSettings(settings);

    repaint();

    if (onChange)
        onChange();
}

void ChannelEqEditor::buttonClicked(juce::Button* button)
{
    if (button == &resetButton)
    {
        const auto wasEnabled = onSwitch.getToggleState();
        eq.setSettings({});
        pullSettings();
        onSwitch.setToggleState(wasEnabled, juce::dontSendNotification);
        pushSettings();
        return;
    }

    if (button == &onSwitch)
        pushSettings();
}

juce::Rectangle<int> ChannelEqEditor::getCurveBounds() const
{
    return getLocalBounds().reduced(padding).withTrimmedTop(headerHeight + 4).withHeight(78);
}

void ChannelEqEditor::paint(juce::Graphics& g)
{
    g.fillAll(Theme::panel());

    auto header = getLocalBounds().reduced(padding).withHeight(headerHeight);
    g.setColour(accent);
    g.fillRoundedRectangle(header.removeFromLeft(3).withSizeKeepingCentre(3, 12).toFloat(), 1.5f);
    header.removeFromLeft(6);
    g.setColour(Theme::text());
    g.setFont(Theme::ui(12.0f, true));
    g.drawText("EQ - " + trackName, header, juce::Justification::centredLeft, true);

    const auto curve = getCurveBounds();
    g.setColour(Theme::inset());
    g.fillRoundedRectangle(curve.toFloat(), 4.0f);
    drawCurve(g, curve.toFloat().reduced(2.0f), eq.getSettings(), sampleRate, accent, true);

    // Values under the knobs: a knob's angle alone does not say "+3.5 dB".
    g.setFont(Theme::mono(9.0f));
    g.setColour(Theme::textSoft());

    for (auto* knob : { &lowKnob, &midKnob, &freqKnob, &highKnob })
        g.drawText(knob->getTextFromValue(knob->getValue()),
                   knob->getBounds().withY(knob->getBottom()).withHeight(12),
                   juce::Justification::centred, false);
}

void ChannelEqEditor::resized()
{
    auto header = getLocalBounds().reduced(padding).withHeight(headerHeight);
    resetButton.setBounds(header.removeFromRight(resetButton.getPreferredWidth()).withSizeKeepingCentre(resetButton.getPreferredWidth(), 20));
    header.removeFromRight(8);
    onSwitch.setBounds(header.removeFromRight(36).withSizeKeepingCentre(36, 18));

    auto knobRow = getLocalBounds().reduced(padding);
    knobRow.removeFromTop(getCurveBounds().getBottom() - padding + 6);
    knobRow = knobRow.withHeight(Knob::preferredSize);

    const auto width = knobRow.getWidth() / 4;

    for (auto* knob : { &lowKnob, &midKnob, &freqKnob, &highKnob })
        knob->setBounds(knobRow.removeFromLeft(width).withSizeKeepingCentre(Knob::preferredSize, Knob::preferredSize));
}

void ChannelEqEditor::drawCurve(juce::Graphics& g,
                                juce::Rectangle<float> area,
                                const ChannelEq::Settings& settings,
                                double rate,
                                juce::Colour colour,
                                bool withGrid)
{
    if (area.isEmpty())
        return;

    const auto yForDb = [area] (float db)
    {
        const auto clampedDb = juce::jlimit(-curveRangeDb, curveRangeDb, db);
        return area.getCentreY() - clampedDb / curveRangeDb * (area.getHeight() * 0.5f);
    };

    if (withGrid)
    {
        g.setColour(Theme::outline().withAlpha(0.6f));

        for (const auto hz : { 100.0, 1000.0, 10000.0 })
        {
            const auto x = area.getX() + area.getWidth()
                         * static_cast<float>(std::log(hz / minCurveHz) / std::log(maxCurveHz / minCurveHz));
            g.drawVerticalLine(juce::roundToInt(x), area.getY(), area.getBottom());
        }
    }

    g.setColour(Theme::outlineStrong().withAlpha(0.7f));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());

    const auto steps = juce::jmax(8, juce::roundToInt(area.getWidth() / 2.0f));
    juce::Path path;

    for (int i = 0; i <= steps; ++i)
    {
        const auto proportion = static_cast<double>(i) / steps;
        const auto hz = minCurveHz * std::pow(maxCurveHz / minCurveHz, proportion);
        const auto x = area.getX() + area.getWidth() * static_cast<float>(proportion);
        const auto y = yForDb(ChannelEq::magnitudeDb(settings, rate, hz));

        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    const auto active = ! settings.isNeutral();
    g.setColour(active ? colour : Theme::faintText());
    g.strokePath(path, juce::PathStrokeType(active ? 1.5f : 1.0f));
}

} // namespace djr
