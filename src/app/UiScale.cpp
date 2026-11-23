#include "UiScale.h"

#include "Settings.h"

#include <cmath>

namespace djr
{
namespace UiScale
{

namespace
{
    constexpr auto settingKey = "uiScale";

    /** The smallest logical work area the panel layout still fits in. Not the
        window's resize minimum: at 1024 wide the browser, playlist and plugin
        panels overlap and run off the right edge - seen on a 1366x768 screen
        at 124%, where the layout was cut off on the right and at the bottom.
    */
    constexpr int usableWidth = 1280;
    constexpr int usableHeight = 720;

    /** The layout was designed on a 1080-line screen at 100%. */
    constexpr double referenceHeight = 1080.0;
    constexpr double referenceDpi = 96.0;

    /** Down to the nearest quarter step, with a little slack so 1.98 still
        reads as 2.0 rather than 1.75.
    */
    double snapDown(double factor)
    {
        return std::floor((factor + 0.05) * 4.0) / 4.0;
    }

    /** The biggest factor at which the logical work area is still at least
        width x height.
    */
    double largestFactorFitting(const Metrics& metrics, int width, int height)
    {
        const auto platform = juce::jmax(0.1, metrics.platformScale);
        const auto byWidth = metrics.physicalUserArea.getWidth() / (platform * width);
        const auto byHeight = metrics.physicalUserArea.getHeight() / (platform * height);
        return juce::jmin(byWidth, byHeight);
    }
}

const juce::Array<int>& getChoices()
{
    static const juce::Array<int> choices { autoChoice, 75, 100, 125, 150, 175, 200, 250 };
    return choices;
}

double chooseAutoFactor(const Metrics& metrics)
{
    const auto physicalHeight = static_cast<double>(metrics.physicalTotalArea.getHeight());

    if (physicalHeight <= 0.0)
        return 1.0;

    // Two readings of "how dense is this screen", and the smaller one wins:
    //  - by resolution, 1080 lines = 100%, 1440 = 133%, 2160 = 200%;
    //  - by physical DPI, 96 = 100%, the way Chromium reads it.
    // Resolution alone would blow a 1080p 15" laptop's screen (DPI ~140) up
    // to the point the mixer no longer fits; DPI alone trusts EDID sizes,
    // which TVs and projectors often get wildly wrong. A DPI outside any real
    // monitor's range is treated as unknown.
    const auto byResolution = physicalHeight / referenceHeight;
    const auto dpiIsPlausible = metrics.dpi >= 60.0 && metrics.dpi <= 600.0;
    const auto byDpi = dpiIsPlausible ? metrics.dpi / referenceDpi : byResolution;

    const auto wantedTotal = juce::jmin(byResolution, byDpi);
    const auto platform = juce::jmax(0.1, metrics.platformScale);

    auto factor = snapDown(wantedTotal / platform);
    factor = juce::jmin(factor, snapDown(largestFactorFitting(metrics, usableWidth, usableHeight)));

    return juce::jmax(1.0, factor);
}

bool fits(int percent, const Metrics& metrics)
{
    return percent <= 100
        || percent / 100.0 <= largestFactorFitting(metrics, usableWidth, usableHeight) + 1.0e-6;
}

double resolveFactor(int choice, const Metrics& metrics)
{
    if (choice == autoChoice)
        return chooseAutoFactor(metrics);

    const auto requested = juce::jlimit(50, 400, choice) / 100.0;

    if (requested <= 1.0)
        return requested;

    const auto largest = largestFactorFitting(metrics, usableWidth, usableHeight);
    return juce::jlimit(1.0, requested, juce::jmax(1.0, largest));
}

std::optional<Metrics> measurePrimaryDisplay()
{
    auto& desktop = juce::Desktop::getInstance();
    const auto* display = desktop.getDisplays().getPrimaryDisplay();

    if (display == nullptr || display->scale <= 0.0)
        return std::nullopt;

    // A Display's areas are logical, i.e. already divided by its scale - which
    // includes the global factor installed right now. Multiplying back out
    // gives device pixels, so measuring again after apply() gives the same
    // answer.
    const auto toPhysical = [scale = display->scale] (juce::Rectangle<int> area)
    {
        return (area.toDouble() * scale).toNearestInt();
    };

    Metrics metrics;
    metrics.physicalTotalArea = toPhysical(display->totalArea);
    metrics.physicalUserArea = toPhysical(display->userArea);
    metrics.dpi = display->dpi;
    metrics.platformScale = display->scale / static_cast<double>(desktop.getGlobalScaleFactor());
    return metrics;
}

int loadSavedChoice()
{
    const auto saved = Settings::get(settingKey, "auto");

    if (saved == "auto")
        return autoChoice;

    const auto percent = saved.getIntValue();
    return getChoices().contains(percent) ? percent : autoChoice;
}

void saveChoice(int choice)
{
    Settings::set(settingKey, choice == autoChoice ? juce::String("auto") : juce::String(choice));
}

double apply(int choice)
{
    const auto metrics = measurePrimaryDisplay();

    if (! metrics.has_value())
        return 1.0;

    const auto factor = resolveFactor(choice, *metrics);
    juce::Desktop::getInstance().setGlobalScaleFactor(static_cast<float>(factor));
    return factor;
}

} // namespace UiScale
} // namespace djr
