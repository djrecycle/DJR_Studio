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
    threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), writerThread, 32768);
    firstInputChannel.store(juce::jmax(0, firstChannel), std::memory_order_release);
    writerChannels.store(channels, std::memory_order_release);
    activeWriter.store(threadedWriter.get(), std::memory_order_release);
    Logger::write("Recording started: " + wavFile.getFullPathName());
    return true;
}

void Recorder::stop()
{
    activeWriter.store(nullptr, std::memory_order_release);
    threadedWriter.reset();
}

bool Recorder::isRecording() const noexcept
{
    return activeWriter.load(std::memory_order_acquire) != nullptr;
}

void Recorder::processInputBlock(const float* const* inputData, int numChannels, int numSamples) noexcept
{
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
