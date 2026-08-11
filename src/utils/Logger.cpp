#include "Logger.h"

namespace djr
{

void Logger::write(const juce::String& message)
{
    juce::Logger::writeToLog("[DJR_Studio] " + message);
}

} // namespace djr
