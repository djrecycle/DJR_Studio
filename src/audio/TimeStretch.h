#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace djr
{

/** Time stretching that leaves the pitch where it was.

    WSOLA: the signal is cut into overlapping windows and those windows are laid
    back down at a different spacing. Because every window keeps its own
    waveform, the pitch never moves - only how often the windows recur, which is
    the tempo. Resampling, the other kind of warp this app has, moves both.

    Written out rather than pulled in: the good stretching libraries are GPL and
    this project is MIT, and WSOLA is short enough to own.
*/
namespace TimeStretch
{
    /** Window laid down each step. About 46 ms at 44.1 kHz, which is long
        enough to hold a bass period and short enough not to smear transients.
    */
    constexpr int windowSize = 2048;
    /** How far the search may slide a window to find the phase that lines up. */
    constexpr int searchRadius = 256;

    /** Returns `source` playing `rate` times faster at the same pitch.

        rate > 1 makes it shorter, rate < 1 longer. A rate of one, or a source
        too short to window, comes back as a plain copy - stretching by nothing
        should not cost quality.
    */
    juce::AudioBuffer<float> process(const juce::AudioBuffer<float>& source, double rate);

    /** Samples `process` will return for this input, without doing the work. */
    int outputLengthFor(int sourceLength, double rate);
}

} // namespace djr
