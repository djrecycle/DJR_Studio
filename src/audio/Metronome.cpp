#include "Metronome.h"

#include <cmath>

namespace djr
{

namespace
{
    /** How long one click rings for. Short enough not to blur into the next. */
    constexpr double clickSeconds = 0.045;
    constexpr double accentHz = 1600.0;
    constexpr double beatHz = 1000.0;
    constexpr float clickGain = 0.35f;
}

void Metronome::prepare(double sampleRate) noexcept
{
    preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    reset();
}

void Metronome::setEnabled(bool shouldBeEnabled) noexcept
{
    enabled.store(shouldBeEnabled, std::memory_order_release);
}

bool Metronome::isEnabled() const noexcept
{
    return enabled.load(std::memory_order_acquire);
}

void Metronome::reset() noexcept
{
    remainingSamples = 0;
    envelope = 0.0;
    phase = 0.0;
}

void Metronome::trigger(bool accented) noexcept
{
    phase = 0.0;
    phaseDelta = juce::MathConstants<double>::twoPi * (accented ? accentHz : beatHz) / preparedSampleRate;
    envelope = 1.0;

    // Exponential decay that reaches near silence by the end of the click.
    const auto lengthSamples = juce::jmax(1, juce::roundToInt(clickSeconds * preparedSampleRate));
    envelopeDecay = std::pow(0.0005, 1.0 / static_cast<double>(lengthSamples));
    remainingSamples = lengthSamples;
}

void Metronome::renderInto(juce::AudioBuffer<float>& output, int startSample, int numSamples) noexcept
{
    if (remainingSamples <= 0)
        return;

    const auto count = juce::jmin(numSamples, remainingSamples);

    for (int i = 0; i < count; ++i)
    {
        const auto value = static_cast<float>(std::sin(phase) * envelope) * clickGain;

        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.addSample(channel, startSample + i, value);

        phase += phaseDelta;
        envelope *= envelopeDecay;
    }

    remainingSamples -= count;
}

void Metronome::startCountIn(int beats, double tempoBpm, double beatsPerBar) noexcept
{
    // Post the settings first, then the request: the audio thread only reads
    // them once it sees a non-zero beat count.
    pendingTempoBpm.store(tempoBpm > 0.0 ? tempoBpm : 120.0, std::memory_order_release);
    pendingBeatsPerBar.store(beatsPerBar > 0.0 ? beatsPerBar : 4.0, std::memory_order_release);
    pendingCountInBeats.store(juce::jmax(0, beats), std::memory_order_release);
}

bool Metronome::isCountingIn() const noexcept
{
    return countingIn.load(std::memory_order_acquire)
        || pendingCountInBeats.load(std::memory_order_acquire) > 0;
}

void Metronome::cancelCountIn() noexcept
{
    pendingCountInBeats.store(0, std::memory_order_release);
    countingIn.store(false, std::memory_order_release);
}

bool Metronome::processCountIn(juce::AudioBuffer<float>& output) noexcept
{
    if (const auto pending = pendingCountInBeats.exchange(0, std::memory_order_acq_rel); pending > 0)
    {
        countInBeatsLeft = pending;
        countInBeatIndex = 0;
        countInBarBeats = pendingBeatsPerBar.load(std::memory_order_acquire);
        countInSamplesPerBeat = preparedSampleRate * 60.0
                              / pendingTempoBpm.load(std::memory_order_acquire);
        countInSamplesToNext = 0.0;   // click immediately on the first beat
        countingIn.store(true, std::memory_order_release);
    }

    if (! countingIn.load(std::memory_order_acquire))
        return false;

    const auto numSamples = output.getNumSamples();
    auto writtenTo = 0;

    while (writtenTo < numSamples)
    {
        if (countInSamplesToNext <= 0.0 && countInBeatsLeft > 0)
        {
            // The first beat of the count and every bar line get the accent.
            const auto isDownbeat = std::fmod(static_cast<double>(countInBeatIndex),
                                              countInBarBeats) < 1.0e-6;
            trigger(isDownbeat);

            ++countInBeatIndex;
            --countInBeatsLeft;
            countInSamplesToNext = countInSamplesPerBeat;
        }

        const auto chunk = countInBeatsLeft > 0
            ? juce::jmin(numSamples - writtenTo, juce::jmax(1, juce::roundToInt(countInSamplesToNext)))
            : numSamples - writtenTo;

        renderInto(output, writtenTo, chunk);
        writtenTo += chunk;
        countInSamplesToNext -= chunk;

        // Done once the last beat has been counted and its click has rung out.
        if (countInBeatsLeft <= 0 && remainingSamples <= 0)
        {
            countingIn.store(false, std::memory_order_release);
            break;
        }
    }

    return true;
}

void Metronome::process(juce::AudioBuffer<float>& output,
                        double startBeat,
                        double endBeat,
                        double beatsPerBar) noexcept
{
    const auto numSamples = output.getNumSamples();

    if (numSamples <= 0)
        return;

    // A count-in owns the click until it finishes, whatever the transport does.
    if (processCountIn(output))
        return;

    if (! isEnabled())
    {
        reset();
        return;
    }

    const auto beatsInBlock = endBeat - startBeat;

    // Stopped, or moving backwards after a loop wrap: just let any tail ring out.
    if (beatsInBlock <= 0.0)
    {
        renderInto(output, 0, numSamples);
        return;
    }

    const auto samplesPerBeat = static_cast<double>(numSamples) / beatsInBlock;
    const auto bar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;

    // Walk every whole beat that lands inside this block.
    auto beat = std::ceil(startBeat - 1.0e-9);
    auto writtenTo = 0;

    while (beat < endBeat - 1.0e-9)
    {
        const auto offset = juce::jlimit(0, numSamples - 1,
                                         juce::roundToInt((beat - startBeat) * samplesPerBeat));

        // Fill up to the click, then start it: a click already ringing must not
        // be cut off by the next one arriving.
        if (offset > writtenTo)
        {
            renderInto(output, writtenTo, offset - writtenTo);
            writtenTo = offset;
        }

        // The downbeat of each bar is accented.
        const auto positionInBar = std::fmod(beat, bar);
        const auto isDownbeat = positionInBar < 1.0e-6 || std::abs(positionInBar - bar) < 1.0e-6;

        trigger(isDownbeat);
        beat += 1.0;
    }

    if (writtenTo < numSamples)
        renderInto(output, writtenTo, numSamples - writtenTo);
}

} // namespace djr
