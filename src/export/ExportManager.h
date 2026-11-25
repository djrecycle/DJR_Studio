#pragma once

#include "audio/Mixer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <vector>

namespace djr
{

/** Bounces the mixer to an audio file faster than real time.

    The mix is first rendered to a hidden 32-bit float wav next to the
    destination, then written out in the chosen format. Two passes are what
    make normalising possible - the peak is only known once the whole song
    has been heard - and they also mean the destination is only ever touched
    once the render itself has succeeded.

    The caller must make sure the audio device is not driving the same mixer
    while this runs - AudioEngine::renderOffline detaches the callback first.
*/
class ExportManager
{
public:
    enum class Format
    {
        wav,
        flac,
        aiff
    };

    struct Options
    {
        double sampleRate = 44100.0;
        int blockSize = 512;
        double tempoBpm = 120.0;
        double startBeat = 0.0;
        double lengthBeats = 16.0;
        int channels = 2;
        Format format = Format::wav;
        /** 16, 24 or 32 (32 is float, wav only). Anything the format cannot
            store is brought down to the nearest depth it can - see
            resolveBitDepth().
        */
        int bitDepth = 24;
        /** Scale the whole mix so its loudest sample lands on
            normaliseTargetDb. Silence is left as silence.
        */
        bool normalise = false;
        float normaliseTargetDb = -0.3f;
        bool songMode = true;
        /** Stop the decay tail as soon as it has gone quiet. Stems switch this
            off so every file comes out the same length and lines up when they
            are dropped side by side into another DAW.
        */
        bool trimSilentTail = true;
    };

    /** One file written by renderStems(). */
    struct Stem
    {
        int trackIndex = -1;
        juce::String trackName;
        juce::File file;
    };

    /** Renders `mixer` into `outputFile`. Returns false and fills `errorOut`
        when the file cannot be written.
    */
    bool render(Mixer& mixer,
                const juce::File& outputFile,
                const Options& options,
                juce::String& errorOut,
                std::function<void(double)> onProgress = {}) const;

    /** Renders one file per track into `folder`, each through the whole mixer
        with only that track playing (see Mixer::setIsolatedTrack). A stem
        therefore keeps its fader, pan, inserts and the buses it runs through,
        and the stems sum back to the mixdown.

        Buses get no stem of their own - their sound is already inside the
        stems of the tracks feeding them - and neither does a track that is
        muted, or routed into a muted bus, since it is not in the mix either.
        A track that renders to pure silence is left out rather than written as
        an empty file; `skippedOut` receives the names of those.

        Normalising is ignored: scaling every stem to its own peak would stop
        them adding up to the mix.

        Returns false and fills `errorOut` on the first stem that fails. Stems
        already finished by then are kept and listed in `stemsOut`.
    */
    bool renderStems(Mixer& mixer,
                     const juce::File& folder,
                     const Options& options,
                     std::vector<Stem>& stemsOut,
                     juce::StringArray& skippedOut,
                     juce::String& errorOut,
                     std::function<void(double)> onProgress = {}) const;

    /** Tracks that get a stem, in mixer order: everything that makes a sound
        of its own and can be heard in the mix.
    */
    static std::vector<int> stemTrackIndices(const Mixer& mixer);
    /** "03 Lead Synth.wav" - numbered by mixer position so the files sort in
        the same order as the channel strips, and stripped of anything a file
        system would refuse.
    */
    static juce::String stemFileName(int trackIndex, const juce::String& trackName, Format format);

    /** File extension for `format`, with the dot - ".wav", ".flac", ".aiff". */
    static juce::String extensionFor(Format format);
    /** What the export dialog lists for `format`, shallowest first. */
    static juce::Array<int> bitDepthsFor(Format format);
    /** The deepest depth `format` can store that is not deeper than
        `requested`, or its shallowest if `requested` is below all of them.
    */
    static int resolveBitDepth(Format format, int requested);

private:
    /** render(), also handing back the loudest sample of the mix before any
        normalising.
    */
    bool renderWithPeak(Mixer& mixer,
                        const juce::File& outputFile,
                        const Options& options,
                        juce::String& errorOut,
                        float& peakOut,
                        std::function<void(double)> onProgress) const;
};

} // namespace djr
