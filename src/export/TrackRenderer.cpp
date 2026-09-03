#include "TrackRenderer.h"

namespace djr
{

bool TrackRenderer::render(Track& track,
                           const juce::File& outputFile,
                           const Options& options,
                           juce::String& errorOut,
                           std::function<void(double)> onProgress) const
{
    if (options.sampleRate <= 0.0 || options.blockSize <= 0 || options.tempoBpm <= 0.0)
    {
        errorOut = TRANS("The render settings are not valid.");
        return false;
    }

    if (options.lengthBeats <= 0.0)
    {
        errorOut = TRANS("Render length is zero - there is nothing to render.");
        return false;
    }

    if (! outputFile.getParentDirectory().createDirectory())
    {
        errorOut = TRANS("The destination folder could not be created: ") + outputFile.getParentDirectory().getFullPathName();
        return false;
    }

    outputFile.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());

    if (stream == nullptr)
    {
        errorOut = TRANS("The file could not be written: ") + outputFile.getFullPathName();
        return false;
    }

    const auto channels = juce::jlimit(1, 2, options.channels);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(),
                            options.sampleRate,
                            static_cast<unsigned int>(channels),
                            options.bitDepth,
                            {},
                            0));

    if (writer == nullptr)
    {
        errorOut = TRANS("The wav writer could not be created.");
        return false;
    }

    stream.release();

    track.prepare(options.sampleRate, options.blockSize);

    const auto beatsPerBlock = (static_cast<double>(options.blockSize) / options.sampleRate)
                             * (options.tempoBpm / 60.0);
    const auto totalSamples = static_cast<juce::int64>(
        options.lengthBeats * (60.0 / options.tempoBpm) * options.sampleRate);

    juce::AudioBuffer<float> block(channels, options.blockSize);
    juce::AudioBuffer<float> preFader(channels, options.blockSize);
    juce::MidiBuffer midi;

    auto beat = options.startBeat;
    juce::int64 written = 0;

    // The pre-fader tap is what gets written; `block` is only there because
    // processAudio needs somewhere to build the post-fader signal.
    const auto renderBlock = [&] (int samplesThisBlock, bool playing)
    {
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), channels, samplesThisBlock);
        juce::AudioBuffer<float> tap(preFader.getArrayOfWritePointers(), channels, samplesThisBlock);

        view.clear();
        tap.clear();
        midi.clear();

        TrackPlaybackContext context;
        context.sampleRate = options.sampleRate;
        context.tempoBpm = options.tempoBpm;
        context.startBeat = beat;
        context.endBeat = beat + beatsPerBlock;
        context.isPlaying = playing;
        context.songMode = options.songMode;

        track.processAudio(view, midi, context, &tap);
        beat = context.endBeat;

        return writer->writeFromAudioSampleBuffer(tap, 0, samplesThisBlock);
    };

    while (written < totalSamples)
    {
        const auto samplesThisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(options.blockSize), totalSamples - written));

        if (! renderBlock(samplesThisBlock, true))
        {
            errorOut = TRANS("Writing the file failed part way through the render.");
            return false;
        }

        written += samplesThisBlock;

        if (onProgress)
            onProgress(static_cast<double>(written) / static_cast<double>(totalSamples));
    }

    // Let anything still ringing decay instead of ending on a click. The
    // transport is stopped for this, so no new notes start during the tail.
    const auto tailSamples = static_cast<juce::int64>(options.sampleRate * juce::jmax(0.0, options.tailSeconds));
    juce::int64 tailWritten = 0;

    while (tailWritten < tailSamples)
    {
        const auto samplesThisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(options.blockSize), tailSamples - tailWritten));

        if (! renderBlock(samplesThisBlock, false))
            break;

        tailWritten += samplesThisBlock;

        juce::AudioBuffer<float> tap(preFader.getArrayOfWritePointers(), channels, samplesThisBlock);
        auto peak = 0.0f;

        for (int channel = 0; channel < tap.getNumChannels(); ++channel)
            peak = juce::jmax(peak, tap.getMagnitude(channel, 0, samplesThisBlock));

        if (peak <= 1.0e-5f)
            break;
    }

    writer.reset();
    return true;
}

} // namespace djr
