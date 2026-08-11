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
    ProjectTrackState audioTrack;
    audioTrack.name = "Audio 1";
    audioTrack.type = "audio";
    project.tracks.add(audioTrack);

    ProjectTrackState midiTrack;
    midiTrack.name = "MIDI 1";
    midiTrack.type = "midi";
    project.tracks.add(midiTrack);
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
