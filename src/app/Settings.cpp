#include "Settings.h"

namespace djr
{
namespace Settings
{

namespace
{
    juce::File getSettingsFile()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("DJR_Studio")
            .getChildFile("settings.properties");
    }

    juce::PropertySet load()
    {
        juce::PropertySet settings;
        const auto file = getSettingsFile();

        if (! file.existsAsFile())
            return settings;

        if (auto xml = juce::parseXML(file))
            settings.restoreFromXml(*xml);

        return settings;
    }
}

juce::String get(const juce::String& key, const juce::String& fallback)
{
    return load().getValue(key, fallback);
}

void set(const juce::String& key, const juce::String& value)
{
    auto settings = load();
    settings.setValue(key, value);

    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();

    if (auto xml = settings.createXml("DJR_Studio"))
        xml->writeTo(file);
}

} // namespace Settings
} // namespace djr
