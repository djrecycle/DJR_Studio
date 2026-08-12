#include "Icons.h"

namespace djr
{

bool Icons::isFilled(Icon icon) noexcept
{
    return icon == Icon::play || icon == Icon::pause || icon == Icon::stop
        || icon == Icon::record || icon == Icon::caretUp || icon == Icon::caretDown
        || icon == Icon::chevronDown;
}

juce::Path Icons::path(Icon icon, juce::Rectangle<float> bounds)
{
    juce::Path p;

    const auto x = bounds.getX();
    const auto y = bounds.getY();
    const auto w = bounds.getWidth();
    const auto h = bounds.getHeight();
    const auto cx = bounds.getCentreX();
    const auto cy = bounds.getCentreY();

    const auto px = [x, w] (float relative) { return x + w * relative; };
    const auto py = [y, h] (float relative) { return y + h * relative; };

    switch (icon)
    {
        case Icon::play:
            p.startNewSubPath(px(0.22f), py(0.14f));
            p.lineTo(px(0.84f), cy);
            p.lineTo(px(0.22f), py(0.86f));
            p.closeSubPath();
            break;

        case Icon::pause:
            p.addRectangle(px(0.19f), py(0.16f), w * 0.22f, h * 0.68f);
            p.addRectangle(px(0.59f), py(0.16f), w * 0.22f, h * 0.68f);
            break;

        case Icon::stop:
            p.addRoundedRectangle(px(0.19f), py(0.19f), w * 0.62f, h * 0.62f, w * 0.09f);
            break;

        case Icon::record:
            p.addEllipse(bounds.reduced(w * 0.19f, h * 0.19f));
            break;

        case Icon::loop:
            p.addCentredArc(cx, cy, w * 0.34f, h * 0.34f, 0.0f,
                            juce::MathConstants<float>::pi * 0.35f,
                            juce::MathConstants<float>::pi * 1.95f, true);
            p.startNewSubPath(px(0.18f), py(0.30f));
            p.lineTo(px(0.18f), py(0.62f));
            p.lineTo(px(0.48f), py(0.62f));
            break;

        case Icon::undo:
            // An arc curving back on itself, with the arrow head on the left.
            p.addCentredArc(cx, py(0.60f), w * 0.30f, h * 0.26f, 0.0f,
                            juce::MathConstants<float>::pi * 1.15f,
                            juce::MathConstants<float>::pi * 2.30f, true);
            p.startNewSubPath(px(0.20f), py(0.20f));
            p.lineTo(px(0.20f), py(0.48f));
            p.lineTo(px(0.46f), py(0.48f));
            break;

        case Icon::redo:
            // The same shape mirrored, so the pair reads as a matched set.
            p.addCentredArc(cx, py(0.60f), w * 0.30f, h * 0.26f, 0.0f,
                            juce::MathConstants<float>::pi * 0.70f,
                            juce::MathConstants<float>::pi * 1.85f, true);
            p.startNewSubPath(px(0.80f), py(0.20f));
            p.lineTo(px(0.80f), py(0.48f));
            p.lineTo(px(0.54f), py(0.48f));
            break;

        case Icon::metronome:
            p.startNewSubPath(px(0.24f), py(0.86f));
            p.lineTo(px(0.44f), py(0.14f));
            p.lineTo(px(0.58f), py(0.14f));
            p.lineTo(px(0.78f), py(0.86f));
            p.closeSubPath();
            p.startNewSubPath(px(0.32f), py(0.60f));
            p.lineTo(px(0.82f), py(0.28f));
            break;

        case Icon::folder:
            p.startNewSubPath(px(0.10f), py(0.34f));
            p.lineTo(px(0.36f), py(0.34f));
            p.lineTo(px(0.45f), py(0.19f));
            p.lineTo(px(0.92f), py(0.19f));
            p.lineTo(px(0.92f), py(0.81f));
            p.lineTo(px(0.10f), py(0.81f));
            p.closeSubPath();
            break;

        case Icon::gear:
            p.addEllipse(bounds.reduced(w * 0.34f, h * 0.34f));
            p.addEllipse(bounds.reduced(w * 0.11f, h * 0.11f));
            break;

        case Icon::search:
            p.addEllipse(px(0.12f), py(0.12f), w * 0.56f, h * 0.56f);
            p.startNewSubPath(px(0.62f), py(0.62f));
            p.lineTo(px(0.90f), py(0.90f));
            break;

        case Icon::panel:
            p.addRoundedRectangle(px(0.11f), py(0.14f), w * 0.78f, h * 0.72f, w * 0.09f);
            p.startNewSubPath(px(0.36f), py(0.14f));
            p.lineTo(px(0.36f), py(0.86f));
            break;

        case Icon::chevronLeft:
            p.startNewSubPath(px(0.66f), py(0.16f));
            p.lineTo(px(0.32f), cy);
            p.lineTo(px(0.66f), py(0.84f));
            break;

        case Icon::chevronRight:
            p.startNewSubPath(px(0.34f), py(0.16f));
            p.lineTo(px(0.68f), cy);
            p.lineTo(px(0.34f), py(0.84f));
            break;

        case Icon::chevronDown:
        case Icon::caretDown:
            p.startNewSubPath(px(0.50f), py(0.72f));
            p.lineTo(px(0.14f), py(0.30f));
            p.lineTo(px(0.86f), py(0.30f));
            p.closeSubPath();
            break;

        case Icon::caretUp:
            p.startNewSubPath(px(0.50f), py(0.28f));
            p.lineTo(px(0.86f), py(0.70f));
            p.lineTo(px(0.14f), py(0.70f));
            p.closeSubPath();
            break;

        case Icon::minimise:
            p.startNewSubPath(px(0.20f), cy);
            p.lineTo(px(0.80f), cy);
            break;

        case Icon::restore:
            p.addRoundedRectangle(px(0.22f), py(0.22f), w * 0.56f, h * 0.56f, w * 0.08f);
            break;

        case Icon::dockLeft:
            p.addRoundedRectangle(px(0.10f), py(0.16f), w * 0.80f, h * 0.68f, w * 0.08f);
            p.startNewSubPath(px(0.38f), py(0.16f));
            p.lineTo(px(0.38f), py(0.84f));
            break;

        case Icon::dockRight:
            p.addRoundedRectangle(px(0.10f), py(0.16f), w * 0.80f, h * 0.68f, w * 0.08f);
            p.startNewSubPath(px(0.62f), py(0.16f));
            p.lineTo(px(0.62f), py(0.84f));
            break;

        case Icon::dockBottom:
            p.addRoundedRectangle(px(0.10f), py(0.16f), w * 0.80f, h * 0.68f, w * 0.08f);
            p.startNewSubPath(px(0.10f), py(0.60f));
            p.lineTo(px(0.90f), py(0.60f));
            break;

        case Icon::grid:
            p.addRoundedRectangle(px(0.13f), py(0.13f), w * 0.30f, h * 0.30f, w * 0.06f);
            p.addRoundedRectangle(px(0.57f), py(0.13f), w * 0.30f, h * 0.30f, w * 0.06f);
            p.addRoundedRectangle(px(0.13f), py(0.57f), w * 0.30f, h * 0.30f, w * 0.06f);
            p.addRoundedRectangle(px(0.57f), py(0.57f), w * 0.30f, h * 0.30f, w * 0.06f);
            break;

        case Icon::lines:
            p.startNewSubPath(px(0.15f), py(0.28f));
            p.lineTo(px(0.85f), py(0.28f));
            p.startNewSubPath(px(0.15f), py(0.50f));
            p.lineTo(px(0.62f), py(0.50f));
            p.startNewSubPath(px(0.15f), py(0.72f));
            p.lineTo(px(0.74f), py(0.72f));
            break;

        case Icon::magnet:
            // Horseshoe with two legs.
            p.startNewSubPath(px(0.22f), py(0.78f));
            p.lineTo(px(0.22f), py(0.46f));
            p.addArc(px(0.22f), py(0.16f), w * 0.56f, h * 0.60f,
                     juce::MathConstants<float>::pi * 1.5f,
                     juce::MathConstants<float>::pi * 2.5f, false);
            p.lineTo(px(0.78f), py(0.78f));
            p.startNewSubPath(px(0.22f), py(0.64f));
            p.lineTo(px(0.40f), py(0.64f));
            p.startNewSubPath(px(0.60f), py(0.64f));
            p.lineTo(px(0.78f), py(0.64f));
            break;

        case Icon::pencil:
            p.startNewSubPath(px(0.20f), py(0.80f));
            p.lineTo(px(0.30f), py(0.78f));
            p.lineTo(px(0.80f), py(0.28f));
            p.lineTo(px(0.72f), py(0.20f));
            p.lineTo(px(0.22f), py(0.70f));
            p.closeSubPath();
            break;

        case Icon::eraser:
            p.startNewSubPath(px(0.22f), py(0.22f));
            p.lineTo(px(0.78f), py(0.78f));
            p.startNewSubPath(px(0.78f), py(0.22f));
            p.lineTo(px(0.22f), py(0.78f));
            break;

        case Icon::speakerMute:
            p.startNewSubPath(px(0.16f), py(0.38f));
            p.lineTo(px(0.32f), py(0.38f));
            p.lineTo(px(0.50f), py(0.20f));
            p.lineTo(px(0.50f), py(0.80f));
            p.lineTo(px(0.32f), py(0.62f));
            p.lineTo(px(0.16f), py(0.62f));
            p.closeSubPath();
            p.startNewSubPath(px(0.64f), py(0.38f));
            p.lineTo(px(0.86f), py(0.62f));
            p.startNewSubPath(px(0.86f), py(0.38f));
            p.lineTo(px(0.64f), py(0.62f));
            break;

        case Icon::slip:
            // Content sliding inside a fixed frame.
            p.addRectangle(px(0.12f), py(0.28f), w * 0.76f, h * 0.44f);
            p.startNewSubPath(px(0.30f), py(0.50f));
            p.lineTo(px(0.70f), py(0.50f));
            p.startNewSubPath(px(0.30f), py(0.50f));
            p.lineTo(px(0.40f), py(0.40f));
            p.startNewSubPath(px(0.30f), py(0.50f));
            p.lineTo(px(0.40f), py(0.60f));
            p.startNewSubPath(px(0.70f), py(0.50f));
            p.lineTo(px(0.60f), py(0.40f));
            p.startNewSubPath(px(0.70f), py(0.50f));
            p.lineTo(px(0.60f), py(0.60f));
            break;

        case Icon::slice:
            // Razor: a blade over a cut line.
            p.startNewSubPath(px(0.20f), py(0.62f));
            p.lineTo(px(0.62f), py(0.20f));
            p.lineTo(px(0.80f), py(0.38f));
            p.lineTo(px(0.38f), py(0.80f));
            p.closeSubPath();
            p.startNewSubPath(px(0.12f), py(0.88f));
            p.lineTo(px(0.88f), py(0.88f));
            break;

        case Icon::marquee:
            // Dashed selection rectangle.
            p.startNewSubPath(px(0.16f), py(0.22f));
            p.lineTo(px(0.40f), py(0.22f));
            p.startNewSubPath(px(0.60f), py(0.22f));
            p.lineTo(px(0.84f), py(0.22f));
            p.startNewSubPath(px(0.84f), py(0.22f));
            p.lineTo(px(0.84f), py(0.46f));
            p.startNewSubPath(px(0.84f), py(0.66f));
            p.lineTo(px(0.84f), py(0.78f));
            p.lineTo(px(0.60f), py(0.78f));
            p.startNewSubPath(px(0.40f), py(0.78f));
            p.lineTo(px(0.16f), py(0.78f));
            p.lineTo(px(0.16f), py(0.66f));
            p.startNewSubPath(px(0.16f), py(0.46f));
            p.lineTo(px(0.16f), py(0.22f));
            break;

        case Icon::zoom:
            p.addEllipse(px(0.16f), py(0.16f), w * 0.48f, h * 0.48f);
            p.startNewSubPath(px(0.60f), py(0.60f));
            p.lineTo(px(0.86f), py(0.86f));
            p.startNewSubPath(px(0.28f), py(0.40f));
            p.lineTo(px(0.52f), py(0.40f));
            break;

        case Icon::plus:
            p.startNewSubPath(cx, py(0.18f));
            p.lineTo(cx, py(0.82f));
            p.startNewSubPath(px(0.18f), cy);
            p.lineTo(px(0.82f), cy);
            break;

        case Icon::close:
            p.startNewSubPath(px(0.24f), py(0.24f));
            p.lineTo(px(0.76f), py(0.76f));
            p.startNewSubPath(px(0.76f), py(0.24f));
            p.lineTo(px(0.24f), py(0.76f));
            break;

        case Icon::plug:
            p.addRoundedRectangle(px(0.29f), py(0.21f), w * 0.42f, h * 0.58f, w * 0.09f);
            p.startNewSubPath(px(0.41f), py(0.21f));
            p.lineTo(px(0.41f), py(0.07f));
            p.startNewSubPath(px(0.59f), py(0.21f));
            p.lineTo(px(0.59f), py(0.07f));
            p.startNewSubPath(px(0.41f), py(0.79f));
            p.lineTo(px(0.41f), py(0.93f));
            p.startNewSubPath(px(0.59f), py(0.79f));
            p.lineTo(px(0.59f), py(0.93f));
            break;

        case Icon::waveform:
            p.startNewSubPath(px(0.06f), cy);
            for (int i = 0; i < 6; ++i)
            {
                const auto sx = px(0.16f + static_cast<float>(i) * 0.13f);
                p.lineTo(sx, py(i % 2 == 0 ? 0.20f : 0.80f));
            }
            p.lineTo(px(0.94f), cy);
            break;

        case Icon::notes:
            p.addRoundedRectangle(px(0.10f), py(0.24f), w * 0.34f, h * 0.16f, w * 0.05f);
            p.addRoundedRectangle(px(0.48f), py(0.46f), w * 0.42f, h * 0.16f, w * 0.05f);
            p.addRoundedRectangle(px(0.20f), py(0.68f), w * 0.30f, h * 0.16f, w * 0.05f);
            break;

        case Icon::steps:
            p.addRoundedRectangle(px(0.10f), py(0.36f), w * 0.22f, h * 0.28f, w * 0.06f);
            p.addRoundedRectangle(px(0.39f), py(0.18f), w * 0.22f, h * 0.64f, w * 0.06f);
            p.addRoundedRectangle(px(0.68f), py(0.36f), w * 0.22f, h * 0.28f, w * 0.06f);
            break;
    }

    return p;
}

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, float strokeThickness)
{
    const auto shape = path(icon, bounds);

    if (isFilled(icon))
        g.fillPath(shape);
    else
        g.strokePath(shape, juce::PathStrokeType(strokeThickness,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

} // namespace djr
