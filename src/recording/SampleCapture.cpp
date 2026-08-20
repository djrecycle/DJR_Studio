#include "SampleCapture.h"

namespace djr
{

void SampleCapture::prepareRing(double sampleRate, int channels)
{
    // Before the resize, not after: a block that arrives during it is one we
    // would rather drop than write into a buffer that is moving.
    capturing.store(false, std::memory_order_release);

    const auto rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto numChannels = juce::jlimit(1, 2, channels);

    deviceSampleRate.store(rate, std::memory_order_release);
    ringChannels.store(numChannels, std::memory_order_release);

    const juce::SpinLock::ScopedLockType lock(ringLock);
    ring.prepare(numChannels, juce::jmax(1, static_cast<int>(rate * ringSeconds)));
    // Sized here so a drain never allocates while holding the ring.
    drained.setSize(numChannels, juce::jmax(1, static_cast<int>(rate * ringSeconds)), false, false, true);
}

void SampleCapture::start()
{
    if (capturing.load(std::memory_order_acquire))
        return;

    const auto rate = deviceSampleRate.load(std::memory_order_acquire);

    if (! juce::approximatelyEqual(rate, storeSampleRate))
    {
        // The device changed under a capture that is still on screen. Its audio
        // is still true, but it cannot be continued into: appending 48 kHz to
        // 44.1 kHz makes a recording that is wrong in the middle.
        store.setSize(store.getNumChannels(), 0);
        capturedSamples = 0;
        storeSampleRate = rate;
    }

    droppedSamples = 0;
    reachedLimit = false;

    {
        // Whatever is still in the ring belongs to the last capture; starting
        // with it would splice two recordings together with no seam to see.
        const juce::SpinLock::ScopedLockType lock(ringLock);
        ring.reset();
    }

    lockedOutSamples.store(0, std::memory_order_release);
    capturing.store(true, std::memory_order_release);
}

void SampleCapture::stop()
{
    capturing.store(false, std::memory_order_release);
}

bool SampleCapture::isCapturing() const noexcept
{
    return capturing.load(std::memory_order_acquire);
}

void SampleCapture::clear()
{
    stop();

    {
        const juce::SpinLock::ScopedLockType lock(ringLock);
        ring.reset();
    }

    lockedOutSamples.store(0, std::memory_order_release);
    store.setSize(store.getNumChannels(), 0);
    capturedSamples = 0;
    droppedSamples = 0;
    reachedLimit = false;
}

void SampleCapture::adopt(const juce::AudioBuffer<float>& source, int numSamples, double sourceSampleRate)
{
    stop();

    {
        // Whatever was mid-flight belongs to the audio that is being replaced.
        const juce::SpinLock::ScopedLockType lock(ringLock);
        ring.reset();
    }

    lockedOutSamples.store(0, std::memory_order_release);
    droppedSamples = 0;
    reachedLimit = false;

    const auto length = juce::jlimit(0, source.getNumSamples(), numSamples);
    const auto numChannels = juce::jlimit(1, 2, source.getNumChannels());

    if (length <= 0 || sourceSampleRate <= 0.0)
    {
        store.setSize(store.getNumChannels(), 0);
        capturedSamples = 0;
        return;
    }

    // The rate comes with the audio rather than from the device: a 48 kHz file
    // opened on a 44.1 kHz device is still a 48 kHz file, and saying otherwise
    // would draw it at the wrong length.
    storeSampleRate = sourceSampleRate;
    store.setSize(numChannels, length, false, true, false);

    for (int channel = 0; channel < numChannels; ++channel)
        store.copyFrom(channel, 0, source, channel, 0, length);

    capturedSamples = length;
}

void SampleCapture::captureBlock(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (! capturing.load(std::memory_order_acquire))
        return;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    // Tried, never taken: the audio thread is not allowed to wait for a resize
    // or a drain. A lost race costs this block, and the count says so.
    const juce::SpinLock::ScopedTryLockType lock(ringLock);

    if (! lock.isLocked())
    {
        lockedOutSamples.fetch_add(numSamples, std::memory_order_relaxed);
        return;
    }

    ring.push(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), numSamples);
}

int SampleCapture::capacityLimitSamples() const noexcept
{
    return juce::jmax(1, static_cast<int>(storeSampleRate * maxCaptureSeconds));
}

int SampleCapture::drain()
{
    droppedSamples += lockedOutSamples.exchange(0, std::memory_order_acquire);

    const auto numChannels = juce::jmax(1, ringChannels.load(std::memory_order_acquire));

    if (store.getNumChannels() != numChannels)
    {
        store.setSize(numChannels, 0);
        capturedSamples = 0;
    }

    const auto limit = capacityLimitSamples();
    auto arrived = 0;

    // Loops because one pop only reaches the end of the ring's own wrap: a
    // drain that ran a little late has two runs waiting, not one.
    for (;;)
    {
        const auto room = limit - capturedSamples;

        if (room <= 0)
            break;

        int popped = 0;

        {
            // Held across the pop alone. Growing the store is the slow part,
            // and doing it under this lock would cost the audio thread blocks
            // every time the capture got longer.
            const juce::SpinLock::ScopedLockType lock(ringLock);
            droppedSamples += ring.getAndClearDroppedSamples();
            popped = ring.pop(drained, room);
        }

        if (popped <= 0)
            break;

        if (store.getNumSamples() < capturedSamples + popped)
        {
            const auto growth = juce::jmax(popped, static_cast<int>(storeSampleRate * growthSeconds));
            store.setSize(numChannels, juce::jmin(limit, capturedSamples + growth), true, true, false);
        }

        const auto writable = juce::jmin(popped, store.getNumSamples() - capturedSamples);

        for (int channel = 0; channel < numChannels; ++channel)
            store.copyFrom(channel, capturedSamples,
                           drained, juce::jmin(channel, drained.getNumChannels() - 1), 0, writable);

        capturedSamples += writable;
        arrived += writable;
    }

    if (capturedSamples >= limit && isCapturing())
    {
        stop();
        reachedLimit = true;
    }

    return arrived;
}

const juce::AudioBuffer<float>& SampleCapture::getAudio() const noexcept
{
    return store;
}

int SampleCapture::getNumCapturedSamples() const noexcept
{
    return capturedSamples;
}

double SampleCapture::getCapturedSeconds() const noexcept
{
    return storeSampleRate > 0.0 ? capturedSamples / storeSampleRate : 0.0;
}

double SampleCapture::getSampleRate() const noexcept
{
    return storeSampleRate;
}

int SampleCapture::getDroppedSamples() const noexcept
{
    return droppedSamples;
}

bool SampleCapture::hasReachedLimit() const noexcept
{
    return reachedLimit;
}

} // namespace djr
