#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** The handful of preferences that outlive a run, in one small XML file.

    Every setting lives in the same file, so each write has to be a read, a
    change and a write back. Writing a fresh PropertySet with only your own key
    in it - which is what the language setting used to do - silently deletes
    everybody else's.
*/
namespace Settings
{
    juce::String get(const juce::String& key, const juce::String& fallback = {});
    void set(const juce::String& key, const juce::String& value);
}

} // namespace djr
