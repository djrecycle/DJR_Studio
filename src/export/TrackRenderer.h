#pragma once

#include "audio/Track.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace djr
{

/** Renders one track on its own, faster than real time.

    ExportManager bounces the whole mixer; this bounces a single track, which is
    what freeze and bounce-to-audio both need. It drives `Track::processAudio`
    by hand rather than going through the Mixer, so nothing else in the session
    leaks into the file and the track's sends do not get counted twice.

    The signal captured is the **pre-fader** one, through the same `preFaderOut`
    tap the sends use. A frozen track therefore still answers to its own volume
    and pan; baking the fader in would freeze a mix decision along with the
    sound, and moving the fader afterwards would apply it twice.

    The caller must make sure the audio device is not driving the same track
    while this runs - AudioEngine::renderOffline detaches the callback first.
*/
class TrackRenderer
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
        /** Song mode reads the timeline; pattern mode loops the active pattern. */
        bool songMode = true;
        /** Seconds of decay written after the last beat, so a reverb tail or a
            long release is not cut off mid-ring.
        */
        double tailSeconds = 2.0;
    };

    /** Renders `track` into `outputFile`. Returns false and fills `errorOut`
        when the file cannot be written or the options make no sense.
    */
    bool render(Track& track,
                const juce::File& outputFile,
                const Options& options,
                juce::String& errorOut,
                std::function<void(double)> onProgress = {}) const;
};

} // namespace djr
