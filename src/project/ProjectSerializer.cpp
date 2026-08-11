#include "ProjectSerializer.h"

namespace djr
{

bool ProjectSerializer::save(const Project& project, const juce::File& file, juce::String& error) const
{
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Cannot create project directory";
        return false;
    }

    const auto json = juce::JSON::toString(project.toVar(), true);
    if (! file.replaceWithText(json))
    {
        error = "Cannot write project file";
        return false;
    }

    error.clear();
    return true;
}

bool ProjectSerializer::load(Project& project, const juce::File& file, juce::String& error) const
{
    if (! file.existsAsFile())
    {
        error = "Project file does not exist";
        return false;
    }

    juce::var parsed;
    const auto result = juce::JSON::parse(file.loadFileAsString(), parsed);
    if (result.failed())
    {
        error = result.getErrorMessage();
        return false;
    }

    project.fromVar(parsed);
    project.projectFile = file;
    error.clear();
    return true;
}

} // namespace djr
