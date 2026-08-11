#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

class Logger
{
public:
    static void write(const juce::String& message);
};

} // namespace djr
