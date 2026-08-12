#pragma once

#include <atomic>

namespace djr
{

class Transport
{
public:
    void play() noexcept;
    void stop() noexcept;
    void pause() noexcept;
    void togglePlayStop() noexcept;

    bool isPlaying() const noexcept;
    bool isRecording() const noexcept;
    void setRecording(bool shouldRecord) noexcept;

    double getTempoBpm() const noexcept;
    void setTempoBpm(double newTempo) noexcept;

    /** Time signature. Everything else in the app counts in quarter notes, so
        this only decides how long a bar is: 6/8 is six eighths, which is three
        quarter note beats.
    */
    void setTimeSignature(int numerator, int denominator) noexcept;
    int getTimeSignatureNumerator() const noexcept;
    int getTimeSignatureDenominator() const noexcept;
    /** Quarter note beats in one bar, derived from the signature. */
    double getBeatsPerBar() const noexcept;

    double getPositionBeats() const noexcept;
    void setPositionBeats(double beats) noexcept;
    void advanceSamples(int numSamples, double sampleRate) noexcept;

    /** Song mode plays the timeline; pattern mode loops the active pattern. */
    void setSongMode(bool shouldPlaySong) noexcept;
    bool isSongMode() const noexcept;

    /** Pattern mode wraps playback inside [loopStart, loopEnd). */
    void setLoopEnabled(bool shouldLoop) noexcept;
    bool isLoopEnabled() const noexcept;
    void setLoopRangeBeats(double startBeat, double endBeat) noexcept;
    double getLoopStartBeats() const noexcept;
    double getLoopEndBeats() const noexcept;

private:
    std::atomic<bool> playing { false };
    std::atomic<bool> recording { false };
    std::atomic<double> tempoBpm { 120.0 };
    std::atomic<int> timeSigNumerator { 4 };
    std::atomic<int> timeSigDenominator { 4 };
    std::atomic<double> positionBeats { 0.0 };
    std::atomic<bool> songMode { false };
    std::atomic<bool> looping { false };
    std::atomic<double> loopStartBeats { 0.0 };
    std::atomic<double> loopEndBeats { 16.0 };
};

} // namespace djr
