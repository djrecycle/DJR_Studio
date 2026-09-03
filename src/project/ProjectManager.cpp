#include "ProjectManager.h"

#include "utils/Logger.h"

namespace djr
{

ProjectManager::ProjectManager()
{
    newProject(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                   .getChildFile("DJR_Studio Projects")
                   .getChildFile("Untitled"));
}

Project& ProjectManager::getProject() noexcept
{
    return project;
}

const Project& ProjectManager::getProject() const noexcept
{
    return project;
}

void ProjectManager::newProject(const juce::File& folder)
{
    project.reset();
    project.samplesFolder = folder.getChildFile("Samples");
    project.recordingsFolder = folder.getChildFile("Recordings");

    const auto addTrack = [this] (const char* name, const char* type)
    {
        ProjectTrackState state;
        state.name = name;
        state.type = type;
        project.tracks.add(state);
    };

    // The six tracks a fresh mixer builds itself, in the same order and of the
    // same kinds - see Mixer's constructor. Opening a project now makes the
    // track list match the file, so a shorter list here would be a new project
    // that throws most of the default session away.
    addTrack("Drums", "midi");
    addTrack("Bass", "midi");
    addTrack("Pad", "midi");
    addTrack("Vox", "audio");
    addTrack("FX", "audio");
    addTrack("Keys", "midi");

    sendChangeMessage();
}

bool ProjectManager::saveProject(const juce::File& file, juce::String& error)
{
    project.projectFile = file;
    const auto ok = serializer.save(project, file, error);
    if (ok)
        Logger::write("Project saved: " + file.getFullPathName());

    return ok;
}

bool ProjectManager::loadProject(const juce::File& file, juce::String& error)
{
    const auto ok = serializer.load(project, file, error);
    if (ok)
    {
        Logger::write("Project loaded: " + file.getFullPathName());
        sendChangeMessage();
    }

    return ok;
}

} // namespace djr
