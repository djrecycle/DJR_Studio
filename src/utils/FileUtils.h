#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

class FileUtils
{
public:
    static juce::File getDefaultProjectRoot();
    static juce::File getUserVst3Folder();
};

} // namespace djr
