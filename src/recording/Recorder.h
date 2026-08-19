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

    /** `firstChannel` is where in the device's inputs this take starts, so a
        track can capture the one socket its instrument is plugged into instead
        of whatever happens to be first.
    */
    bool startRecording(const juce::File& wavFile, double sampleRate, int channels, int firstChannel = 0);
    void stop();
    bool isRecording() const noexcept;
    void processInputBlock(const float* const* inputData, int numChannels, int numSamples) noexcept;

private:
    juce::TimeSliceThread writerThread { "DJR Recorder Writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
    std::atomic<int> firstInputChannel { 0 };
    std::atomic<int> writerChannels { 0 };
};

} // namespace djr
