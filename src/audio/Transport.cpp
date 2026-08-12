#include "Transport.h"

#include <algorithm>
#include <cmath>

namespace djr
{

void Transport::play() noexcept
{
    playing.store(true, std::memory_order_release);
}

void Transport::stop() noexcept
{
    playing.store(false, std::memory_order_release);
    recording.store(false, std::memory_order_release);
    positionBeats.store(0.0, std::memory_order_release);
}

void Transport::pause() noexcept
{
    playing.store(false, std::memory_order_release);
}

void Transport::togglePlayStop() noexcept
{
    isPlaying() ? pause() : play();
}

bool Transport::isPlaying() const noexcept
{
    return playing.load(std::memory_order_acquire);
}

bool Transport::isRecording() const noexcept
{
    return recording.load(std::memory_order_acquire);
}

void Transport::setRecording(bool shouldRecord) noexcept
{
    recording.store(shouldRecord, std::memory_order_release);
    if (shouldRecord)
        playing.store(true, std::memory_order_release);
}

double Transport::getTempoBpm() const noexcept
{
    return tempoBpm.load(std::memory_order_acquire);
}

void Transport::setTempoBpm(double newTempo) noexcept
{
    tempoBpm.store(std::clamp(newTempo, 20.0, 300.0), std::memory_order_release);
}

void Transport::setTimeSignature(int numerator, int denominator) noexcept
{
    timeSigNumerator.store(std::clamp(numerator, 1, 32), std::memory_order_release);

    // Only note values that divide a whole note are meaningful; anything else
    // falls back to quarters rather than producing a nonsense bar length.
    const auto valid = denominator == 1 || denominator == 2 || denominator == 4
                    || denominator == 8 || denominator == 16;

    timeSigDenominator.store(valid ? denominator : 4, std::memory_order_release);
}

int Transport::getTimeSignatureNumerator() const noexcept
{
    return timeSigNumerator.load(std::memory_order_acquire);
}

int Transport::getTimeSignatureDenominator() const noexcept
{
    return timeSigDenominator.load(std::memory_order_acquire);
}

double Transport::getBeatsPerBar() const noexcept
{
    // A beat is a quarter note here, so 6/8 gives 6 * 4/8 = 3 beats to the bar.
    return timeSigNumerator.load(std::memory_order_acquire)
         * (4.0 / timeSigDenominator.load(std::memory_order_acquire));
}

double Transport::getPositionBeats() const noexcept
{
    return positionBeats.load(std::memory_order_acquire);
}

void Transport::setPositionBeats(double beats) noexcept
{
    positionBeats.store(std::max(0.0, beats), std::memory_order_release);
}

void Transport::advanceSamples(int numSamples, double sampleRate) noexcept
{
    if (! isPlaying() || sampleRate <= 0.0 || numSamples <= 0)
        return;

    const auto seconds = static_cast<double>(numSamples) / sampleRate;
    const auto beats = seconds * (getTempoBpm() / 60.0);
    auto next = getPositionBeats() + beats;

    if (isLoopEnabled())
    {
        const auto start = getLoopStartBeats();
        const auto end = getLoopEndBeats();
        const auto length = end - start;

        if (length > 0.0 && next >= end)
            next = start + std::fmod(next - start, length);
    }

    positionBeats.store(next, std::memory_order_release);
}

void Transport::setSongMode(bool shouldPlaySong) noexcept
{
    songMode.store(shouldPlaySong, std::memory_order_release);
}

bool Transport::isSongMode() const noexcept
{
    return songMode.load(std::memory_order_acquire);
}

void Transport::setLoopEnabled(bool shouldLoop) noexcept
{
    looping.store(shouldLoop, std::memory_order_release);
}

bool Transport::isLoopEnabled() const noexcept
{
    return looping.load(std::memory_order_acquire);
}

void Transport::setLoopRangeBeats(double startBeat, double endBeat) noexcept
{
    const auto safeStart = std::max(0.0, startBeat);
    loopStartBeats.store(safeStart, std::memory_order_release);
    loopEndBeats.store(std::max(safeStart + 0.25, endBeat), std::memory_order_release);
}

double Transport::getLoopStartBeats() const noexcept
{
    return loopStartBeats.load(std::memory_order_acquire);
}

double Transport::getLoopEndBeats() const noexcept
{
    return loopEndBeats.load(std::memory_order_acquire);
}

} // namespace djr
