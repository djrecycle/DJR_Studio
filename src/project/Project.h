#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

struct ProjectTrackState
{
    juce::String name;
    juce::String type;
    float volume = 0.8f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    /** Legacy single clip, kept so older projects still open. */
    juce::Array<juce::var> midiNotes;
    /** One entry per pattern, each holding that pattern's notes. */
    juce::Array<juce::Array<juce::var>> patterns;
    /** Where the patterns sit on the song timeline. */
    juce::Array<juce::var> placements;
    /** Plugin names only; superseded by pluginStates but kept for old files. */
    juce::StringArray plugins;
    /** Instrument and inserts with their saved parameter state. */
    juce::Array<juce::var> pluginStates;
    /** Audio clips laid out on this track. */
    juce::Array<juce::var> audioClips;
    /** Automation lanes: their target, their curve and whether they are live. */
    juce::Array<juce::var> automation;
    /** Where the main output goes: -1 for master, otherwise a bus track index. */
    int outputDestination = -1;
    /** Which device input feeds this track: -1 for none. Channel 0 as a stereo
        pair is what every track did before this was selectable, so a project
        saved without it opens sounding the same.
    */
    int inputChannel = 0;
    bool inputStereo = true;
    /** Send slots, each with its destination, level and pre/post fader flag. */
    juce::Array<juce::var> sends;
    /** The render a frozen track plays instead of its own content. Empty means
        the track is not frozen; the clips and plugins are saved either way, so
        unfreezing after reopening still gets everything back.
    */
    juce::String frozenFile;
    /** The channel's own envelope, LFO, filter and arpeggiator settings.
        Void on a track that was saved before they existed, which reads back as
        the defaults - everything off, so an old project sounds unchanged.
    */
    juce::var channelSettings;
    /** How the preview instrument is shaped - waveform and ADSR. Void on a
        track that has a real instrument, and on anything saved before the
        Generator page had controls, which reads back as the old fixed sine.
    */
    juce::var previewSynth;
    /** Playlist lane height in pixels. Zero means the default height. */
    int laneHeight = 0;
};

class Project
{
public:
    juce::String name = "Untitled DJR Project";
    double tempo = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    double sampleRate = 44100.0;
    juce::File projectFile;
    juce::File samplesFolder;
    juce::File recordingsFolder;
    juce::Array<ProjectTrackState> tracks;
    /** Where the floating panels were, so a project reopens as you left it. */
    juce::var panelLayout;
    /** Pattern names, index 0..15. Empty entries keep the "PAT n" default. */
    juce::StringArray patternNames;
    /** Manually pinned pattern lengths in beats. Zero means follow the notes. */
    juce::Array<double> patternLengths;
    /** The piano roll's scale highlight: root pitch class (0 = C) and a
        djr::ScaleType cast to int. Type 0 is chromatic - off - which is what
        every project saved before this existed reads back as.
    */
    int scaleRoot = 0;
    int scaleType = 0;

    void reset();
    juce::var toVar() const;
    void fromVar(const juce::var& value);
};

} // namespace djr
