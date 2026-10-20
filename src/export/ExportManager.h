#pragma once

#include "audio/Mixer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

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
    };

    /** Renders `mixer` into `outputFile`. Returns false and fills `errorOut`
        when the file cannot be written.
    */
    bool render(Mixer& mixer,
                const juce::File& outputFile,
                const Options& options,
                juce::String& errorOut,
                std::function<void(double)> onProgress = {}) const;

    /** File extension for `format`, with the dot - ".wav", ".flac", ".aiff". */
    static juce::String extensionFor(Format format);
    /** What the export dialog lists for `format`, shallowest first. */
    static juce::Array<int> bitDepthsFor(Format format);
    /** The deepest depth `format` can store that is not deeper than
        `requested`, or its shallowest if `requested` is below all of them.
    */
    static int resolveBitDepth(Format format, int requested);
};

} // namespace djr
