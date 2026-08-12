#pragma once

#include "audio/Mixer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace djr
{

/** Bounces the mixer to a wav file faster than real time.

    The caller must make sure the audio device is not driving the same mixer
    while this runs - AudioEngine::renderOffline detaches the callback first.
*/
class ExportManager
{
public:
    struct Options
    {
        double sampleRate = 44100.0;
        int blockSize = 512;
        double tempoBpm = 120.0;
        double startBeat = 0.0;
        double lengthBeats = 16.0;
        int channels = 2;
        int bitDepth = 24;
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
};

} // namespace djr
