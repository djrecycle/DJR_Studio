#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

namespace djr
{

class Recorder
{
public:
    Recorder();
    ~Recorder();

    bool startRecording(const juce::File& wavFile, double sampleRate, int channels);
    void stop();
    bool isRecording() const noexcept;
    void processInputBlock(const float* const* inputData, int numChannels, int numSamples) noexcept;

private:
    juce::TimeSliceThread writerThread { "DJR Recorder Writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
};

} // namespace djr
