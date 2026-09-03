#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    ThemeVariant currentVariant = ThemeVariant::neonDark;

    juce::Colour hex(const char* argb)
    {
        return juce::Colour::fromString(juce::String("ff") + argb);
    }

    /** Warms or cools every surface colour so a theme switch is visible across the shell. */
    juce::Colour surface(juce::Colour base)
    {
        switch (currentVariant)
        {
            case ThemeVariant::amberStudio: return base.interpolatedWith(hex("ffc857"), 0.055f);
            case ThemeVariant::iceGrey:     return base.interpolatedWith(hex("8fb8ff"), 0.05f);
            case ThemeVariant::neonDark:
            default:                        return base;
        }
    }

    juce::String pickFamily(const juce::StringArray& candidates, const juce::String& fallback)
    {
        static const auto available = juce::Font::findAllTypefaceNames();

        for (const auto& candidate : candidates)
            if (available.contains(candidate))
                return candidate;

        return fallback;
    }

    juce::Font makeFont(const juce::String& family, float height, int styleFlags)
    {
        return juce::Font(juce::FontOptions(family, height, styleFlags));
    }
}

void Theme::setVariant(ThemeVariant variant) noexcept
{
    currentVariant = variant;
}

ThemeVariant Theme::getVariant() noexcept
{
    return currentVariant;
}

juce::String Theme::getVariantName(ThemeVariant variant)
{
    switch (variant)
    {
        case ThemeVariant::amberStudio: return "Amber Studio";
        case ThemeVariant::iceGrey:     return "Ice Grey";
        case ThemeVariant::neonDark:
        default:                        return "Neon Dark";
    }
}

juce::Colour Theme::getVariantAccent(ThemeVariant variant)
{
    switch (variant)
    {
        case ThemeVariant::amberStudio: return hex("ffc857");
        case ThemeVariant::iceGrey:     return hex("8fb8ff");
        case ThemeVariant::neonDark:
        default:                        return hex("19d7ff");
    }
}

juce::Colour Theme::windowBackground()     { return surface(hex("0d0f14")); }
juce::Colour Theme::menuBarTop()           { return surface(hex("1c2130")); }
juce::Colour Theme::menuBarBottom()        { return surface(hex("171b26")); }
juce::Colour Theme::transportBackground()  { return surface(hex("12161f")); }
juce::Colour Theme::panel()                { return surface(hex("161a24")); }
juce::Colour Theme::panelAlt()             { return surface(hex("1b2130")); }
juce::Colour Theme::panelDeep()            { return surface(hex("12161f")); }
juce::Colour Theme::panelHeader()          { return surface(hex("171c27")); }
juce::Colour Theme::inset()                { return surface(hex("0f1319")); }
juce::Colour Theme::meterTrack()           { return surface(hex("0d1013")); }
juce::Colour Theme::control()              { return surface(hex("212837")); }
juce::Colour Theme::controlHover()         { return surface(hex("2b3345")); }
juce::Colour Theme::outline()              { return surface(hex("242b3a")); }
juce::Colour Theme::outlineStrong()        { return surface(hex("2c3548")); }
juce::Colour Theme::divider()              { return surface(hex("262d3d")); }

juce::Colour Theme::text()                 { return hex("ecf3ff"); }
juce::Colour Theme::textSoft()             { return hex("c3cee0"); }
juce::Colour Theme::mutedText()            { return hex("7d8ba6"); }
juce::Colour Theme::faintText()            { return hex("5f6b82"); }

juce::Colour Theme::accent()               { return getVariantAccent(currentVariant); }
juce::Colour Theme::cyan()                 { return hex("19d7ff"); }
juce::Colour Theme::purple()               { return hex("9b5cff"); }
juce::Colour Theme::green()                { return hex("44ff9a"); }
juce::Colour Theme::amber()                { return hex("ffc857"); }
juce::Colour Theme::pink()                 { return hex("ff4f7b"); }
juce::Colour Theme::grid()                 { return surface(hex("30384a")); }
juce::Colour Theme::gridBeat()             { return surface(hex("232b3a")); }
juce::Colour Theme::gridBar()              { return surface(hex("303a4d")); }

juce::Colour Theme::trackColour(int index)
{
    static const juce::Colour palette[] = {
        hex("19d7ff"), hex("9b5cff"), hex("44ff9a"),
        hex("ffc857"), hex("ff4f7b"), hex("7d8ba6")
    };

    const auto count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[((index % count) + count) % count];
}

namespace
{
    /** Every call site keeps its original design size; this pulls the whole shell
        down to the tight, FL-style density without retuning each font by hand.
    */
    constexpr float fontScale = 0.84f;

    float scaled(float height) noexcept
    {
        return juce::jmax(7.5f, height * fontScale);
    }
}

juce::Font Theme::ui(float height, bool bold)
{
    static const auto family = pickFamily({ "Barlow", "Inter", "DejaVu Sans" },
                                          juce::Font::getDefaultSansSerifFontName());
    return makeFont(family, scaled(height), bold ? juce::Font::bold : juce::Font::plain);
}

juce::Font Theme::display(float height)
{
    static const auto family = pickFamily({ "Barlow Condensed", "Barlow", "Inter Display", "Inter", "DejaVu Sans Condensed" },
                                          juce::Font::getDefaultSansSerifFontName());
    return makeFont(family, scaled(height), juce::Font::bold).withExtraKerningFactor(0.09f);
}

juce::Font Theme::mono(float height, bool bold)
{
    static const auto family = pickFamily({ "JetBrains Mono", "DejaVu Sans Mono", "Bitstream Vera Sans Mono" },
                                          juce::Font::getDefaultMonospacedFontName());
    return makeFont(family, scaled(height), bold ? juce::Font::bold : juce::Font::plain);
}

juce::Font Theme::caps(float height)
{
    return ui(height, true).withExtraKerningFactor(0.13f);
}

int Theme::textWidth(const juce::Font& font, const juce::String& text)
{
    // A small allowance keeps hinting / kerning rounding from clipping the last glyph.
    return juce::roundToInt(std::ceil(juce::GlyphArrangement::getStringWidth(font, text))) + 4;
}

void Theme::drawCard(juce::Graphics& g, juce::Rectangle<int> bounds, float radius)
{
    drawCard(g, bounds, panel(), outline(), radius);
}

void Theme::drawCard(juce::Graphics& g,
                     juce::Rectangle<int> bounds,
                     juce::Colour fill,
                     juce::Colour border,
                     float radius)
{
    const auto area = bounds.toFloat().reduced(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(area, radius);
    g.setColour(border);
    g.drawRoundedRectangle(area, radius, 1.0f);
}

void Theme::drawCaption(juce::Graphics& g,
                        juce::Rectangle<int> bounds,
                        const juce::String& caption,
                        juce::Justification justification)
{
    g.setColour(mutedText());
    g.setFont(caps(9.5f));
    g.drawText(caption.toUpperCase(), bounds, justification, false);
}

void Theme::drawPanelTitle(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& title)
{
    g.setColour(accent());
    g.setFont(display(14.0f));
    g.drawText(title.toUpperCase(), bounds, juce::Justification::centredLeft, false);
}

float Theme::meterPosition(float amplitude) noexcept
{
    // -60 dBFS at the bottom, 0 at the top. A note played through the preview
    // synth peaks around -17 dBFS, which is a normal level and a fifth of the
    // bar drawn linearly - so the meter looked broken while the sound was fine.
    constexpr auto floorDb = -60.0f;

    if (amplitude <= 0.0f)
        return 0.0f;

    // By hand rather than through juce::Decibels: that lives in the audio
    // module, and this file is the one place the look of things is decided.
    const auto db = 20.0f * std::log10(amplitude);
    return juce::jlimit(0.0f, 1.0f, (db - floorDb) / -floorDb);
}

void Theme::drawLevelMeter(juce::Graphics& g,
                           juce::Rectangle<float> bounds,
                           float fill,
                           bool vertical,
                           float cornerSize)
{
    g.setColour(meterTrack());
    g.fillRoundedRectangle(bounds, cornerSize);

    const auto clamped = juce::jlimit(0.0f, 1.0f, fill);
    if (clamped <= 0.001f)
        return;

    juce::ColourGradient gradient;
    if (vertical)
        gradient = juce::ColourGradient(green(), bounds.getX(), bounds.getBottom(),
                                        pink(), bounds.getX(), bounds.getY(), false);
    else
        gradient = juce::ColourGradient(green(), bounds.getX(), bounds.getY(),
                                        pink(), bounds.getRight(), bounds.getY(), false);

    gradient.addColour(0.72, cyan());
    gradient.addColour(0.92, amber());

    juce::Path bar;
    auto filled = bounds;
    if (vertical)
        filled = filled.withTop(bounds.getBottom() - bounds.getHeight() * clamped);
    else
        filled = filled.withWidth(bounds.getWidth() * clamped);

    bar.addRoundedRectangle(filled, cornerSize);

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(bar);
    g.setGradientFill(gradient);
    g.fillRect(bounds);
}

} // namespace djr
