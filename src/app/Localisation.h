#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** The app's language.

    Source strings are written in **English** and wrapped in TRANS(). That makes
    English the language you get with no translation table installed at all -
    there is no "English table" that could fall out of step with the source, and
    a string somebody forgets to translate degrades to readable English rather
    than to a missing-key placeholder.

    Indonesian is a translation table over those English strings.
*/
class Localisation
{
public:
    enum class Language
    {
        english,
        indonesian
    };

    /** Installs the mappings for `language`. English clears them. */
    static void setLanguage(Language language);
    static Language getLanguage() noexcept;
    static juce::String getLanguageName(Language language);
    /** Every language the build ships, in menu order. */
    static juce::Array<Language> getAvailableLanguages();

    /** Remembers the choice across runs. Without this the "restart to see the
        rest" advice would be a lie: the app would come back in English.
    */
    static void saveChoice(Language language);
    static Language loadSavedChoice();

    /** Reads the saved choice, defaulting to English. */
    static Language languageFromString(const juce::String& value);
    static juce::String languageToString(Language language);

private:
    static juce::String getIndonesianTranslations();
};

} // namespace djr
