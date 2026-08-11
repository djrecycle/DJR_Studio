#pragma once

#include "Project.h"

namespace djr
{

class ProjectSerializer
{
public:
    bool save(const Project& project, const juce::File& file, juce::String& error) const;
    bool load(Project& project, const juce::File& file, juce::String& error) const;
};

} // namespace djr
