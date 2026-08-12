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

bool Recorder::startRecording(const juce::File& wavFile, double sampleRate, int channels)
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
    if (auto* writer = activeWriter.load(std::memory_order_acquire))
        writer->write(inputData, numSamples);

    juce::ignoreUnused(numChannels);
}

} // namespace djr
