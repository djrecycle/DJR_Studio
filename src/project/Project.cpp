#include "Project.h"

namespace djr
{

void Project::reset()
{
    name = "Untitled DJR Project";
    tempo = 120.0;
    timeSigNumerator = 4;
    timeSigDenominator = 4;
    sampleRate = 44100.0;
    projectFile = juce::File();
    samplesFolder = juce::File();
    recordingsFolder = juce::File();
    tracks.clear();
    patternNames.clear();
    patternLengths.clear();
}

juce::var Project::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("name", name);
    root->setProperty("tempo", tempo);
    root->setProperty("timeSigNumerator", timeSigNumerator);
    root->setProperty("timeSigDenominator", timeSigDenominator);
    root->setProperty("sampleRate", sampleRate);
    root->setProperty("samplesFolder", samplesFolder.getFullPathName());
    root->setProperty("recordingsFolder", recordingsFolder.getFullPathName());

    juce::Array<juce::var> trackArray;
    for (const auto& track : tracks)
    {
        auto* trackObject = new juce::DynamicObject();
        trackObject->setProperty("name", track.name);
        trackObject->setProperty("type", track.type);
        trackObject->setProperty("volume", track.volume);
        trackObject->setProperty("pan", track.pan);
        trackObject->setProperty("mute", track.mute);
        trackObject->setProperty("solo", track.solo);

        juce::Array<juce::var> pluginArray;
        for (const auto& plugin : track.plugins)
            pluginArray.add(plugin);

        trackObject->setProperty("plugins", pluginArray);
        trackObject->setProperty("midiNotes", track.midiNotes);

        juce::Array<juce::var> patternArray;
        for (const auto& pattern : track.patterns)
            patternArray.add(pattern);

        trackObject->setProperty("patterns", patternArray);
        trackObject->setProperty("placements", track.placements);
        trackObject->setProperty("pluginStates", track.pluginStates);
        trackObject->setProperty("audioClips", track.audioClips);
        trackObject->setProperty("automation", track.automation);
        trackObject->setProperty("outputDestination", track.outputDestination);
        trackObject->setProperty("inputChannel", track.inputChannel);
        trackObject->setProperty("inputStereo", track.inputStereo);
        trackObject->setProperty("sends", track.sends);
        trackObject->setProperty("channelSettings", track.channelSettings);
        trackObject->setProperty("frozenFile", track.frozenFile);
        trackObject->setProperty("laneHeight", track.laneHeight);
        trackArray.add(trackObject);
    }

    juce::Array<juce::var> patternNameArray;
    for (const auto& patternName : patternNames)
        patternNameArray.add(patternName);

    juce::Array<juce::var> patternLengthArray;
    for (const auto& length : patternLengths)
        patternLengthArray.add(length);

    root->setProperty("tracks", trackArray);
    root->setProperty("panelLayout", panelLayout);
    root->setProperty("patternNames", patternNameArray);
    root->setProperty("patternLengths", patternLengthArray);
    return root;
}

void Project::fromVar(const juce::var& value)
{
    reset();

    auto* root = value.getDynamicObject();
    if (root == nullptr)
        return;

    const auto getOr = [] (const juce::DynamicObject& object,
                           const juce::Identifier& property,
                           juce::var fallback)
    {
        auto propertyValue = object.getProperty(property);
        return propertyValue.isVoid() ? fallback : propertyValue;
    };

    name = getOr(*root, "name", name).toString();
    tempo = static_cast<double>(getOr(*root, "tempo", tempo));
    timeSigNumerator = static_cast<int>(getOr(*root, "timeSigNumerator", timeSigNumerator));
    timeSigDenominator = static_cast<int>(getOr(*root, "timeSigDenominator", timeSigDenominator));
    sampleRate = static_cast<double>(getOr(*root, "sampleRate", sampleRate));
    samplesFolder = juce::File(root->getProperty("samplesFolder").toString());
    recordingsFolder = juce::File(root->getProperty("recordingsFolder").toString());

    if (auto* patternNameArray = root->getProperty("patternNames").getArray())
        for (const auto& patternName : *patternNameArray)
            patternNames.add(patternName.toString());

    if (auto* patternLengthArray = root->getProperty("patternLengths").getArray())
        for (const auto& length : *patternLengthArray)
            patternLengths.add(static_cast<double>(length));

    if (auto* trackArray = root->getProperty("tracks").getArray())
    {
        for (const auto& entry : *trackArray)
        {
            auto* trackObject = entry.getDynamicObject();
            if (trackObject == nullptr)
                continue;

            ProjectTrackState track;
            track.name = trackObject->getProperty("name").toString();
            track.type = trackObject->getProperty("type").toString();
            track.volume = static_cast<float>(static_cast<double>(getOr(*trackObject, "volume", 0.8)));
            track.pan = static_cast<float>(static_cast<double>(getOr(*trackObject, "pan", 0.0)));
            track.mute = static_cast<bool>(getOr(*trackObject, "mute", false));
            track.solo = static_cast<bool>(getOr(*trackObject, "solo", false));

            if (auto* pluginArray = trackObject->getProperty("plugins").getArray())
                for (const auto& plugin : *pluginArray)
                    track.plugins.add(plugin.toString());

            if (auto* notesArray = trackObject->getProperty("midiNotes").getArray())
                for (const auto& note : *notesArray)
                    track.midiNotes.add(note);

            if (auto* patternArray = trackObject->getProperty("patterns").getArray())
            {
                for (const auto& pattern : *patternArray)
                {
                    juce::Array<juce::var> notes;

                    if (auto* noteArray = pattern.getArray())
                        for (const auto& note : *noteArray)
                            notes.add(note);

                    track.patterns.add(notes);
                }
            }

            if (auto* placementArray = trackObject->getProperty("placements").getArray())
                for (const auto& placement : *placementArray)
                    track.placements.add(placement);

            if (auto* pluginStateArray = trackObject->getProperty("pluginStates").getArray())
                for (const auto& plugin : *pluginStateArray)
                    track.pluginStates.add(plugin);

            if (auto* clipArray = trackObject->getProperty("audioClips").getArray())
                for (const auto& clip : *clipArray)
                    track.audioClips.add(clip);

            if (auto* automationArray = trackObject->getProperty("automation").getArray())
                for (const auto& lane : *automationArray)
                    track.automation.add(lane);

            // Absent in older files, which is what "-1" already means: master.
            track.outputDestination = static_cast<int>(getOr(*trackObject, "outputDestination", -1));
            track.inputChannel = static_cast<int>(getOr(*trackObject, "inputChannel", 0));
            track.inputStereo = static_cast<bool>(getOr(*trackObject, "inputStereo", true));
            track.channelSettings = trackObject->getProperty("channelSettings");

            if (auto* sendArray = trackObject->getProperty("sends").getArray())
                for (const auto& send : *sendArray)
                    track.sends.add(send);

            track.frozenFile = trackObject->getProperty("frozenFile").toString();

            track.laneHeight = static_cast<int>(getOr(*trackObject, "laneHeight", 0));

            tracks.add(track);
        }
    }
}

} // namespace djr
