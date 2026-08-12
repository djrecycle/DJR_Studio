#pragma once

#include "Project.h"
#include "ProjectSerializer.h"

#include <juce_events/juce_events.h>

namespace djr
{

class ProjectManager final : public juce::ChangeBroadcaster
{
public:
    ProjectManager();

    Project& getProject() noexcept;
    const Project& getProject() const noexcept;

    void newProject(const juce::File& folder);
    bool saveProject(const juce::File& file, juce::String& error);
    bool loadProject(const juce::File& file, juce::String& error);

private:
    Project project;
    ProjectSerializer serializer;
};

} // namespace djr
