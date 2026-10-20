#include "ExportManager.h"

#include <cmath>

namespace djr
{

namespace
{
    std::unique_ptr<juce::AudioFormat> makeFormat(ExportManager::Format format)
    {
        switch (format)
        {
            case ExportManager::Format::flac: return std::make_unique<juce::FlacAudioFormat>();
            case ExportManager::Format::aiff: return std::make_unique<juce::AiffAudioFormat>();
            case ExportManager::Format::wav:  break;
        }

        return std::make_unique<juce::WavAudioFormat>();
    }

    /** Opens a writer for `file`. The writer owns the stream once it exists.
        32 bits means float: that is how JUCE's wav writer reads it.
    */
    std::unique_ptr<juce::AudioFormatWriter> createWriter(juce::AudioFormat& format,
                                                          const juce::File& file,
                                                          double sampleRate,
                                                          int channels,
                                                          int bitDepth)
    {
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());

        if (stream == nullptr)
            return {};

        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), sampleRate, static_cast<unsigned int>(channels),
                                   bitDepth, {}, 0));

        if (writer != nullptr)
            stream.release();

        return writer;
    }
}

juce::String ExportManager::extensionFor(Format format)
{
    switch (format)
    {
        case Format::flac: return ".flac";
        case Format::aiff: return ".aiff";
        case Format::wav:  break;
    }

    return ".wav";
}

juce::Array<int> ExportManager::bitDepthsFor(Format format)
{
    // Float is the one depth worth having that FLAC and AIFF cannot hold.
    if (format == Format::wav)
        return { 16, 24, 32 };

    return { 16, 24 };
}

int ExportManager::resolveBitDepth(Format format, int requested)
{
    const auto depths = bitDepthsFor(format);
    auto chosen = depths.getFirst();

    for (const auto depth : depths)
        if (depth <= requested)
            chosen = depth;

    return chosen;
}

bool ExportManager::render(Mixer& mixer,
                           const juce::File& outputFile,
                           const Options& options,
                           juce::String& errorOut,
                           std::function<void(double)> onProgress) const
{
    if (options.sampleRate <= 0.0 || options.blockSize <= 0 || options.tempoBpm <= 0.0)
    {
        errorOut = TRANS("The export settings are not valid.");
        return false;
    }

    if (options.lengthBeats <= 0.0)
    {
        errorOut = TRANS("Export length is zero - there is nothing to render.");
        return false;
    }

    if (! outputFile.getParentDirectory().createDirectory())
    {
        errorOut = TRANS("The destination folder could not be created: ") + outputFile.getParentDirectory().getFullPathName();
        return false;
    }

    const auto channels = juce::jlimit(1, 2, options.channels);

    // --- Pass 1: the mix, at full float precision, into a hidden scratch
    // file. TemporaryFile removes it again however this function returns.
    juce::TemporaryFile scratch(outputFile.withFileExtension(".rendering.wav"),
                                juce::TemporaryFile::useHiddenFile);
    juce::WavAudioFormat wav;
    auto scratchWriter = createWriter(wav, scratch.getFile(), options.sampleRate, channels, 32);

    if (scratchWriter == nullptr)
    {
        errorOut = TRANS("The file could not be written: ") + outputFile.getFullPathName();
        return false;
    }

    mixer.prepare(options.sampleRate, options.blockSize, channels);

    const auto beatsPerBlock = (static_cast<double>(options.blockSize) / options.sampleRate)
                             * (options.tempoBpm / 60.0);
    const auto totalSamples = static_cast<juce::int64>(
        options.lengthBeats * (60.0 / options.tempoBpm) * options.sampleRate);

    juce::AudioBuffer<float> block(channels, options.blockSize);
    auto beat = options.startBeat;
    auto peak = 0.0f;
    juce::int64 written = 0;

    // The render is most of the work; the conversion pass after it is quick,
    // so it only gets the last tenth of the progress bar.
    constexpr double renderShare = 0.9;

    // Renders and writes one block, returning its peak - or -1 when the
    // scratch file could not take it.
    const auto renderBlock = [&] (int samplesThisBlock, bool playing)
    {
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), channels, samplesThisBlock);
        view.clear();

        TrackPlaybackContext context;
        context.sampleRate = options.sampleRate;
        context.tempoBpm = options.tempoBpm;
        context.startBeat = beat;
        context.endBeat = beat + beatsPerBlock;
        context.isPlaying = playing;
        context.songMode = options.songMode;

        mixer.process(view, context);
        beat = context.endBeat;

        auto blockPeak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            blockPeak = juce::jmax(blockPeak, view.getMagnitude(channel, 0, samplesThisBlock));

        peak = juce::jmax(peak, blockPeak);

        if (! scratchWriter->writeFromAudioSampleBuffer(view, 0, samplesThisBlock))
            return -1.0f;

        return blockPeak;
    };

    while (written < totalSamples)
    {
        const auto samplesThisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(options.blockSize), totalSamples - written));

        if (renderBlock(samplesThisBlock, true) < 0.0f)
        {
            errorOut = TRANS("Writing the file failed part way through the render.");
            return false;
        }

        written += samplesThisBlock;

        if (onProgress)
            onProgress(renderShare * static_cast<double>(written) / static_cast<double>(totalSamples));
    }

    // Let anything still ringing decay instead of ending on a click.
    const auto tailSamples = static_cast<juce::int64>(options.sampleRate * 1.5);
    juce::int64 tailWritten = 0;

    while (tailWritten < tailSamples)
    {
        const auto samplesThisBlock = static_cast<int>(
            juce::jmin(static_cast<juce::int64>(options.blockSize), tailSamples - tailWritten));

        const auto blockPeak = renderBlock(samplesThisBlock, false);

        if (blockPeak < 0.0f)
        {
            errorOut = TRANS("Writing the file failed while rendering the tail.");
            return false;
        }

        tailWritten += samplesThisBlock;

        if (blockPeak <= 1.0e-5f)
            break;
    }

    scratchWriter.reset();

    // --- Pass 2: the scratch file, scaled if asked, in the chosen format.
    std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(scratch.getFile().createInputStream().release(), true));

    if (reader == nullptr)
    {
        errorOut = TRANS("The rendered audio could not be read back.");
        return false;
    }

    // Silence stays silence: there is no level to scale towards.
    const auto gain = options.normalise && peak > 1.0e-6f
        ? juce::Decibels::decibelsToGain(options.normaliseTargetDb) / peak
        : 1.0f;

    auto format = makeFormat(options.format);
    const auto bitDepth = resolveBitDepth(options.format, options.bitDepth);

    outputFile.deleteFile();
    auto writer = createWriter(*format, outputFile, options.sampleRate, channels, bitDepth);

    if (writer == nullptr)
    {
        errorOut = TRANS("The file could not be written: ") + outputFile.getFullPathName();
        return false;
    }

    // A write failure leaves a truncated, unplayable file behind; better to
    // remove it than let the user find a file that looks done but isn't.
    const auto failWithPartialFile = [&] (const juce::String& message)
    {
        writer.reset();
        outputFile.deleteFile();
        errorOut = message;
        return false;
    };

    constexpr int chunk = 65536;
    juce::AudioBuffer<float> buffer(channels, chunk);
    const auto length = reader->lengthInSamples;

    for (juce::int64 position = 0; position < length; position += chunk)
    {
        const auto count = static_cast<int>(juce::jmin(static_cast<juce::int64>(chunk), length - position));

        if (! reader->read(&buffer, 0, count, position, true, channels > 1))
            return failWithPartialFile(TRANS("The rendered audio could not be read back."));

        if (gain != 1.0f)
            buffer.applyGain(0, count, gain);

        if (! writer->writeFromAudioSampleBuffer(buffer, 0, count))
            return failWithPartialFile(TRANS("Writing the file failed part way through the render."));

        if (onProgress)
            onProgress(renderShare + (1.0 - renderShare) * static_cast<double>(position + count)
                                         / static_cast<double>(length));
    }

    writer.reset();

    if (onProgress)
        onProgress(1.0);

    return true;
}

} // namespace djr
