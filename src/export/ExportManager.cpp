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
    auto peak = 0.0f;
    return renderWithPeak(mixer, outputFile, options, errorOut, peak, std::move(onProgress));
}

std::vector<int> ExportManager::stemTrackIndices(const Mixer& mixer)
{
    std::vector<int> indices;
    const auto count = mixer.getNumTracks();

    // A track is heard if any of its routes - the main output or an active
    // send - reaches master without passing through a muted track: a track
    // whose group is muted but which still sends to an open reverb bus is in
    // the mix through that reverb. Each bus is visited once, so a bad file
    // with a routing loop cannot hang the export.
    const auto reachesMasterUnmuted = [&mixer, count] (int start)
    {
        std::vector<bool> seen(static_cast<size_t>(count), false);
        std::vector<int> pending { start };

        while (! pending.empty())
        {
            const auto index = pending.back();
            pending.pop_back();

            if (! juce::isPositiveAndBelow(index, count))
                return true;            // past the last bus: master

            if (seen[static_cast<size_t>(index)])
                continue;

            seen[static_cast<size_t>(index)] = true;
            const auto* track = mixer.getTrack(index);

            if (track == nullptr || track->isMuted())
                continue;

            pending.push_back(track->getOutputDestination());

            for (int slot = 0; slot < Track::maxSends; ++slot)
                // A send only ever feeds a bus; one pointing nowhere is dropped
                // by the mixer, not played to master.
                if (const auto send = track->getSend(slot);
                    send.isActive() && juce::isPositiveAndBelow(send.destination, count))
                    pending.push_back(send.destination);
        }

        return false;
    };

    for (int index = 0; index < count; ++index)
    {
        const auto* track = mixer.getTrack(index);

        if (track != nullptr && track->getKind() != TrackKind::bus && reachesMasterUnmuted(index))
            indices.push_back(index);
    }

    return indices;
}

juce::String ExportManager::stemFileName(int trackIndex, const juce::String& trackName, Format format)
{
    auto name = juce::File::createLegalFileName(trackName.trim());

    if (name.isEmpty())
        name = "Track";

    return juce::String(trackIndex + 1).paddedLeft('0', 2) + " " + name + extensionFor(format);
}

bool ExportManager::renderStems(Mixer& mixer,
                                const juce::File& folder,
                                const Options& options,
                                std::vector<Stem>& stemsOut,
                                juce::StringArray& skippedOut,
                                juce::String& errorOut,
                                std::function<void(double)> onProgress) const
{
    stemsOut.clear();
    skippedOut.clear();

    const auto indices = stemTrackIndices(mixer);

    if (indices.empty())
    {
        errorOut = TRANS("There is no audible track to export as a stem.");
        return false;
    }

    // Created only now there is something to put in it, and removed again
    // below if every stem turns out silent - but only if this call made it.
    const auto folderExisted = folder.isDirectory();

    if (! folder.createDirectory())
    {
        errorOut = TRANS("The destination folder could not be created: ") + folder.getFullPathName();
        return false;
    }

    auto stemOptions = options;
    stemOptions.normalise = false;
    stemOptions.trimSilentTail = false;

    // Whatever happens below, the mixer must not be left playing one track.
    struct IsolationReset
    {
        Mixer& mixer;
        ~IsolationReset() { mixer.setIsolatedTrack(-1); }
    } isolationReset { mixer };

    const auto total = static_cast<double>(indices.size());

    for (size_t i = 0; i < indices.size(); ++i)
    {
        const auto index = indices[i];
        const auto* track = mixer.getTrack(index);

        if (track == nullptr)
            continue;

        const auto name = track->getName();
        const auto file = folder.getChildFile(stemFileName(index, name, options.format));

        mixer.setIsolatedTrack(index);

        auto peak = 0.0f;
        juce::String error;
        const auto rendered = renderWithPeak(mixer, file, stemOptions, error, peak,
            [&onProgress, i, total] (double p)
            {
                if (onProgress)
                    onProgress((static_cast<double>(i) + p) / total);
            });

        if (! rendered)
        {
            errorOut = name + ": " + error;
            return false;
        }

        // An empty track in the mixer would otherwise turn up as a file of
        // nothing, which is clutter at best and confusing at worst.
        if (peak <= 1.0e-5f)
        {
            file.deleteFile();
            skippedOut.add(name);
            continue;
        }

        stemsOut.push_back({ index, name, file });
    }

    if (onProgress)
        onProgress(1.0);

    if (stemsOut.empty())
    {
        if (! folderExisted && folder.getNumberOfChildFiles(juce::File::findFilesAndDirectories
                                                             | juce::File::ignoreHiddenFiles) == 0)
            folder.deleteRecursively();

        errorOut = TRANS("Every track rendered silent, so no stem was written.");
        return false;
    }

    return true;
}

bool ExportManager::renderWithPeak(Mixer& mixer,
                                   const juce::File& outputFile,
                                   const Options& options,
                                   juce::String& errorOut,
                                   float& peakOut,
                                   std::function<void(double)> onProgress) const
{
    peakOut = 0.0f;

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

        if (options.trimSilentTail && blockPeak <= 1.0e-5f)
            break;
    }

    scratchWriter.reset();
    peakOut = peak;

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
