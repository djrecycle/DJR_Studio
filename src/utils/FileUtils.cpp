#include "FileUtils.h"

namespace djr
{

juce::File FileUtils::getDefaultProjectRoot()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("DJR_Studio Projects");
}

juce::File FileUtils::getUserVst3Folder()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3");
}

} // namespace djr
