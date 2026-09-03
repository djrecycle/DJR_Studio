#include "Recorder.h"

#include "utils/Logger.h"

namespace djr
{

Recorder::Recorder()
{
    writerThread.startThread();
}

Recorder::~Recorder()
{
    stop();
}

bool Recorder::startRecording(const juce::File& wavFile, double sampleRate, int channels, int firstChannel)
{
    stop();

    if (! wavFile.getParentDirectory().createDirectory())
        return false;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> stream(wavFile.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(stream.get(), sampleRate, static_cast<unsigned int>(channels), 24, {}, 0));

    if (writer == nullptr)
        return false;

    stream.release();

    // Built before the lock is taken: allocating the writer and its 32k queue is
    // not something the audio thread should be made to wait for.
    auto prepared = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), writerThread, 32768);

    {
        // Published under the lock, so the audio thread cannot pick up the
        // pointer before the channel layout that goes with it.
        const juce::SpinLock::ScopedLockType scoped(writerLock);
        threadedWriter = std::move(prepared);
        firstInputChannel.store(juce::jmax(0, firstChannel), std::memory_order_release);
        writerChannels.store(channels, std::memory_order_release);
        activeWriter.store(threadedWriter.get(), std::memory_order_release);
    }

    Logger::write("Recording started: " + wavFile.getFullPathName());
    return true;
}

void Recorder::stop()
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> detached;

    {
        const juce::SpinLock::ScopedLockType scoped(writerLock);

        // Clearing this under the lock is what makes the take safe to destroy:
        // a callback already inside processInputBlock holds the lock, so we wait
        // for it here, and every callback after this one sees nothing to write.
        activeWriter.store(nullptr, std::memory_order_release);
        detached = std::move(threadedWriter);
    }

    // Destroying the writer flushes the take and closes the file, which blocks
    // on disk - do it outside the lock the audio thread contends for.
    detached.reset();
}

bool Recorder::isRecording() const noexcept
{
    return activeWriter.load(std::memory_order_acquire) != nullptr;
}

void Recorder::processInputBlock(const float* const* inputData, int numChannels, int numSamples) noexcept
{
    // Try rather than wait: the lock is only ever held while a take is being
    // started or stopped, so the block this can cost is one at the very edge of
    // a recording - never worth blocking the device for.
    const juce::SpinLock::ScopedTryLockType scoped(writerLock);

    if (! scoped.isLocked())
        return;

    // Read inside the lock. Outside it the writer could be freed between this
    // load and the write below.
    auto* writer = activeWriter.load(std::memory_order_acquire);

    if (writer == nullptr || inputData == nullptr || numChannels <= 0)
        return;

    // The writer was made for this track's channels, which are not necessarily
    // the device's first ones. Handing it inputData straight through wrote
    // whatever happened to be plugged into socket one.
    const auto first = firstInputChannel.load(std::memory_order_acquire);
    const auto wanted = juce::jlimit(1, 2, writerChannels.load(std::memory_order_acquire));

    const float* channels[2] = { nullptr, nullptr };

    for (int i = 0; i < wanted; ++i)
    {
        // Clamped rather than dropped: a channel the device does not have
        // records the nearest one it does, and never reads past the block.
        const auto source = juce::jlimit(0, numChannels - 1, first + i);
        channels[i] = inputData[source];
    }

    writer->write(channels, numSamples);
}

} // namespace djr
