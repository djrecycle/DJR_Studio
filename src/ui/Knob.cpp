#include "Knob.h"

namespace djr
{

namespace
{
    /** Leaves a gap at the bottom, the way a real knob's pointer never points
        straight down. 7 o'clock to 5 o'clock, the range every DAW uses.
    */
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;
}

Knob::Knob(const juce::String& captionText, Style style)
    : caption(captionText), knobStyle(style)
{
    setSliderStyle(juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters(startAngle, endAngle, true);

    // A knob is a fine control; dragging the height of the dial should not
    // sweep the whole range.
    setMouseDragSensitivity(180);
    setTooltip(captionText);
}

void Knob::setAwaitingEngine(bool isAwaiting)
{
    if (awaitingEngine == isAwaiting)
        return;

    awaitingEngine = isAwaiting;
    setEnabled(! isAwaiting);
    setTooltip(isAwaiting ? caption + " " + TRANS("(not wired up yet)") : caption);
    repaint();
}

bool Knob::isAwaitingEngine() const noexcept
{
    return awaitingEngine;
}

void Knob::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    auto captionArea = area.removeFromBottom(captionHeight);

    // Square, so the dial cannot go oval in a layout that is not.
    const auto side = juce::jmin(area.getWidth(), area.getHeight());
    const auto dial = area.withSizeKeepingCentre(side, side).toFloat().reduced(2.0f);
    const auto centre = dial.getCentre();
    const auto radius = dial.getWidth() * 0.5f;

    const auto proportion = getRange().getLength() > 0.0
        ? static_cast<float>((getValue() - getMinimum()) / getRange().getLength())
        : 0.0f;
    const auto angle = startAngle + proportion * (endAngle - startAngle);

    const auto live = ! awaitingEngine;
    const auto arcColour = live ? Theme::accent() : Theme::mutedText().withAlpha(0.45f);

    // Body -------------------------------------------------------------------
    g.setColour(live ? Theme::control() : Theme::control().withAlpha(0.5f));
    g.fillEllipse(dial);
    g.setColour(live ? Theme::outlineStrong() : Theme::outline());
    g.drawEllipse(dial, 1.0f);

    // Track and arc ----------------------------------------------------------
    const auto arcRadius = radius + 3.0f;
    const auto thickness = 2.4f;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                        startAngle, endAngle, true);
    g.setColour(Theme::meterTrack());
    g.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Bipolar knobs read as a deviation from centre, so their arc starts at
    // twelve o'clock: half a turn of pan left has to look like the mirror of
    // half a turn right, not like a quarter-full volume.
    const auto arcFrom = knobStyle == Style::bipolar
        ? (startAngle + endAngle) * 0.5f
        : startAngle;

    if (std::abs(angle - arcFrom) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                          juce::jmin(arcFrom, angle), juce::jmax(arcFrom, angle), true);
        g.setColour(arcColour);
        g.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // Pointer ----------------------------------------------------------------
    juce::Path pointer;
    pointer.startNewSubPath(0.0f, -radius * 0.82f);
    pointer.lineTo(0.0f, -radius * 0.30f);
    g.setColour(live ? Theme::text() : Theme::faintText());
    g.strokePath(pointer, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded),
                 juce::AffineTransform::rotation(angle).translated(centre));

    // Caption ----------------------------------------------------------------
    g.setColour(live ? Theme::mutedText() : Theme::faintText());
    g.setFont(Theme::caps(8.5f));
    g.drawText(caption, captionArea, juce::Justification::centred, false);
}

} // namespace djr
