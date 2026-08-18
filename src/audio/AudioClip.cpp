#include "AudioClip.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int peakBuckets = 512;
    constexpr juce::int64 maxSourceSamples = 44100LL * 60LL * 10LL; // 10 minutes
}

std::unique_ptr<AudioClip> AudioClip::createFromFile(const juce::File& file,
                                                     double targetSampleRate,
                                                     juce::AudioFormatManager& formats,
                                                     juce::String& errorOut)
{
    if (! file.existsAsFile())
    {
        errorOut = TRANS("File not found: ") + file.getFileName();
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        errorOut = TRANS("Unsupported format: ") + file.getFileName();
        return nullptr;
    }

    if (reader->lengthInSamples <= 0)
    {
        errorOut = TRANS("Empty file: ") + file.getFileName();
        return nullptr;
    }

    if (reader->lengthInSamples > maxSourceSamples)
    {
        errorOut = TRANS("File too long to load into memory: ") + file.getFileName();
        return nullptr;
    }

    const auto numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const auto numSourceSamples = static_cast<int>(reader->lengthInSamples);

    juce::AudioBuffer<float> source(numChannels, numSourceSamples);
    reader->read(&source, 0, numSourceSamples, 0, true, true);

    std::unique_ptr<AudioClip> clip(new AudioClip());
    clip->name = file.getFileNameWithoutExtension();
    clip->sourceFile = file;
    clip->clipSampleRate = targetSampleRate;

    const auto sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : targetSampleRate;

    if (std::abs(sourceRate - targetSampleRate) < 1.0)
    {
        clip->samples = std::make_shared<const juce::AudioBuffer<float>>(std::move(source));
    }
    else
    {
        // Resample once here so the audio thread only ever reads straight through.
        const auto ratio = sourceRate / targetSampleRate;
        const auto numOutputSamples = juce::jmax(1, static_cast<int>(std::floor(numSourceSamples / ratio)));

        juce::AudioBuffer<float> resampled(numChannels, numOutputSamples);
        resampled.clear();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process(ratio,
                                 source.getReadPointer(channel),
                                 resampled.getWritePointer(channel),
                                 numOutputSamples);
        }

        clip->samples = std::make_shared<const juce::AudioBuffer<float>>(std::move(resampled));
    }

    clip->sourceLengthSeconds = targetSampleRate > 0.0
        ? clip->samples->getNumSamples() / targetSampleRate
        : 0.0;
    clip->playLengthSeconds.store(clip->sourceLengthSeconds, std::memory_order_release);

    clip->buildPeaks();
    return clip;
}

const juce::String& AudioClip::getName() const noexcept
{
    return name;
}

const juce::File& AudioClip::getFile() const noexcept
{
    return sourceFile;
}

double AudioClip::getStartBeat() const noexcept
{
    return startBeat.load(std::memory_order_acquire);
}

void AudioClip::setStartBeat(double beat) noexcept
{
    startBeat.store(juce::jmax(0.0, beat), std::memory_order_release);
}

double AudioClip::getSourceOffsetSeconds() const noexcept
{
    return sourceOffsetSeconds.load(std::memory_order_acquire);
}

double AudioClip::getPlayLengthSeconds() const noexcept
{
    return playLengthSeconds.load(std::memory_order_acquire);
}

double AudioClip::getSourceLengthSeconds() const noexcept
{
    return sourceLengthSeconds;
}

void AudioClip::setSourceOffsetSeconds(double seconds) noexcept
{
    const auto maximum = juce::jmax(0.0, sourceLengthSeconds - getPlayLengthSeconds());
    sourceOffsetSeconds.store(juce::jlimit(0.0, maximum, seconds), std::memory_order_release);
}

void AudioClip::trimStart(double newStartBeat, double tempoBpm) noexcept
{
    if (tempoBpm <= 0.0)
        return;

    const auto currentStart = getStartBeat();
    const auto rate = getPlaybackRate(tempoBpm);
    const auto clamped = juce::jmax(0.0, newStartBeat);

    // Moving the left edge later eats into the head of the source, so the audio
    // underneath stays put on the timeline.
    const auto deltaBeats = clamped - currentStart;
    const auto deltaSourceSeconds = deltaBeats * (60.0 / tempoBpm) * rate;

    const auto offset = getSourceOffsetSeconds();
    const auto length = getPlayLengthSeconds();

    const auto newOffset = juce::jlimit(0.0,
                                        juce::jmax(0.0, offset + length - minimumLengthSeconds),
                                        offset + deltaSourceSeconds);
    const auto appliedSeconds = newOffset - offset;
    const auto newLength = juce::jmax(minimumLengthSeconds, length - appliedSeconds);
    const auto appliedBeats = rate > 0.0 ? appliedSeconds / rate * (tempoBpm / 60.0) : 0.0;

    sourceOffsetSeconds.store(newOffset, std::memory_order_release);
    playLengthSeconds.store(newLength, std::memory_order_release);
    setStartBeat(currentStart + appliedBeats);
}

void AudioClip::trimEnd(double newEndBeat, double tempoBpm) noexcept
{
    if (tempoBpm <= 0.0)
        return;

    const auto lengthBeats = juce::jmax(0.0, newEndBeat - getStartBeat());
    const auto rate = getPlaybackRate(tempoBpm);
    const auto requestedSeconds = lengthBeats * (60.0 / tempoBpm) * rate;
    const auto available = juce::jmax(minimumLengthSeconds,
                                      sourceLengthSeconds - getSourceOffsetSeconds());

    playLengthSeconds.store(juce::jlimit(minimumLengthSeconds, available, requestedSeconds),
                            std::memory_order_release);
}

void AudioClip::setWarpEnabled(bool shouldWarp) noexcept
{
    warpEnabled.store(shouldWarp, std::memory_order_release);
}

bool AudioClip::isWarpEnabled() const noexcept
{
    return warpEnabled.load(std::memory_order_acquire);
}

void AudioClip::setOriginalTempo(double tempoBpm) noexcept
{
    if (tempoBpm > 0.0)
        originalTempo.store(tempoBpm, std::memory_order_release);
}

double AudioClip::getOriginalTempo() const noexcept
{
    return originalTempo.load(std::memory_order_acquire);
}

double AudioClip::getLengthBeats(double tempoBpm) const noexcept
{
    const auto seconds = getPlayLengthSeconds();

    // Warped, the clip owns a fixed slice of the bar and stretches to fill it.
    // Unwarped, it holds its real duration and covers fewer beats as tempo rises.
    if (isWarpEnabled())
        return seconds * (getOriginalTempo() / 60.0);

    return seconds * (tempoBpm / 60.0);
}

float AudioClip::getGain() const noexcept
{
    return gain.load(std::memory_order_acquire);
}

void AudioClip::setGain(float newGain) noexcept
{
    gain.store(juce::jlimit(0.0f, 2.0f, newGain), std::memory_order_release);
}

double AudioClip::getPlaybackRate(double tempoBpm) const noexcept
{
    if (! isWarpEnabled())
        return 1.0;

    const auto original = getOriginalTempo();
    return original > 0.0 ? tempoBpm / original : 1.0;
}

void AudioClip::addToBuffer(juce::AudioBuffer<float>& destination,
                            double blockStartBeat,
                            double tempoBpm,
                            double sampleRate) const
{
    const auto numSamples = destination.getNumSamples();
    const auto totalSourceSamples = samples != nullptr ? samples->getNumSamples() : 0;

    if (numSamples <= 0 || totalSourceSamples <= 0 || tempoBpm <= 0.0 || sampleRate <= 0.0)
        return;

    if (isMuted())
        return;

    const auto rate = getPlaybackRate(tempoBpm);
    const auto secondsPerBeat = 60.0 / tempoBpm;

    // Timeline seconds elapsed inside the clip, converted to source seconds.
    const auto timelineSeconds = (blockStartBeat - getStartBeat()) * secondsPerBeat;
    auto readPosition = (getSourceOffsetSeconds() + timelineSeconds * rate) * sampleRate;

    const auto firstSample = getSourceOffsetSeconds() * sampleRate;
    const auto lastSample = juce::jmin(static_cast<double>(totalSourceSamples),
                                       (getSourceOffsetSeconds() + getPlayLengthSeconds()) * sampleRate);

    auto writeOffset = 0;

    // Skip the part of the block that falls before the clip begins.
    if (readPosition < firstSample)
    {
        const auto samplesBefore = rate > 0.0 ? (firstSample - readPosition) / rate : 0.0;

        if (samplesBefore >= numSamples)
            return;

        writeOffset = static_cast<int>(std::ceil(samplesBefore));
        readPosition += writeOffset * rate;
    }

    if (readPosition >= lastSample)
        return;

    const auto numChannels = destination.getNumChannels();
    const auto clipGain = getGain();

    for (int sample = writeOffset; sample < numSamples; ++sample)
    {
        if (readPosition >= lastSample)
            break;

        const auto index = static_cast<int>(readPosition);
        const auto fraction = static_cast<float>(readPosition - index);
        const auto nextIndex = juce::jmin(index + 1, totalSourceSamples - 1);

        if (index < 0 || index >= totalSourceSamples)
            break;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, samples->getNumChannels() - 1);
            const auto* data = samples->getReadPointer(sourceChannel);

            // Linear interpolation: enough for varispeed playback of a clip.
            const auto value = data[index] + (data[nextIndex] - data[index]) * fraction;
            destination.addSample(channel, sample, value * clipGain);
        }

        readPosition += rate;
    }
}

juce::var AudioClip::toVar() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty("file", sourceFile.getFullPathName());
    object->setProperty("startBeat", getStartBeat());
    object->setProperty("sourceOffset", getSourceOffsetSeconds());
    object->setProperty("playLength", getPlayLengthSeconds());
    object->setProperty("originalTempo", getOriginalTempo());
    object->setProperty("gain", static_cast<double>(getGain()));
    object->setProperty("warp", isWarpEnabled());
    object->setProperty("muted", isMuted());
    return object;
}

void AudioClip::applyStateFromVar(const juce::var& value)
{
    auto* object = value.getDynamicObject();

    if (object == nullptr)
        return;

    setStartBeat(static_cast<double>(object->getProperty("startBeat")));
    setOriginalTempo(static_cast<double>(object->getProperty("originalTempo")));
    setGain(static_cast<float>(static_cast<double>(object->getProperty("gain"))));
    setWarpEnabled(static_cast<bool>(object->getProperty("warp")));
    setMuted(static_cast<bool>(object->getProperty("muted")));

    // Trim last, and clamped to what the decoded file actually holds.
    const auto offset = juce::jlimit(0.0,
                                     juce::jmax(0.0, sourceLengthSeconds - minimumLengthSeconds),
                                     static_cast<double>(object->getProperty("sourceOffset")));
    const auto length = juce::jlimit(minimumLengthSeconds,
                                     juce::jmax(minimumLengthSeconds, sourceLengthSeconds - offset),
                                     static_cast<double>(object->getProperty("playLength")));

    sourceOffsetSeconds.store(offset, std::memory_order_release);
    playLengthSeconds.store(length, std::memory_order_release);
}

juce::File AudioClip::getFileFromVar(const juce::var& value)
{
    if (auto* object = value.getDynamicObject())
        return juce::File(object->getProperty("file").toString());

    return {};
}

const std::vector<float>& AudioClip::getPeaks() const noexcept
{
    static const std::vector<float> none;
    return peaks != nullptr ? *peaks : none;
}

void AudioClip::setMuted(bool shouldMute) noexcept
{
    muted.store(shouldMute, std::memory_order_release);
}

bool AudioClip::isMuted() const noexcept
{
    return muted.load(std::memory_order_acquire);
}

std::unique_ptr<AudioClip> AudioClip::duplicate() const
{
    std::unique_ptr<AudioClip> copy(new AudioClip());

    copy->name = name;
    copy->sourceFile = sourceFile;
    copy->samples = samples;   // shared, not copied
    copy->peaks = peaks;
    copy->clipSampleRate = clipSampleRate;
    copy->sourceLengthSeconds = sourceLengthSeconds;

    copy->startBeat.store(getStartBeat(), std::memory_order_release);
    copy->sourceOffsetSeconds.store(getSourceOffsetSeconds(), std::memory_order_release);
    copy->playLengthSeconds.store(getPlayLengthSeconds(), std::memory_order_release);
    copy->originalTempo.store(getOriginalTempo(), std::memory_order_release);
    copy->warpEnabled.store(isWarpEnabled(), std::memory_order_release);
    copy->muted.store(isMuted(), std::memory_order_release);
    copy->gain.store(getGain(), std::memory_order_release);

    return copy;
}

bool AudioClip::canSplitAt(double beat, double tempoBpm) const
{
    if (tempoBpm <= 0.0)
        return false;

    const auto start = getStartBeat();
    const auto end = start + getLengthBeats(tempoBpm);

    // Refuse cuts that would leave a sliver on either side.
    const auto minimumBeats = minimumLengthSeconds * (tempoBpm / 60.0);

    return beat > start + minimumBeats && beat < end - minimumBeats;
}

std::unique_ptr<AudioClip> AudioClip::splitAt(double beat, double tempoBpm)
{
    if (! canSplitAt(beat, tempoBpm))
        return nullptr;

    auto right = duplicate();

    if (right == nullptr)
        return nullptr;

    // The right piece starts at the cut and reveals the source from there.
    right->trimStart(beat, tempoBpm);
    trimEnd(beat, tempoBpm);

    return right;
}

double AudioClip::getTrimStartFraction() const noexcept
{
    return sourceLengthSeconds > 0.0 ? getSourceOffsetSeconds() / sourceLengthSeconds : 0.0;
}

double AudioClip::getTrimEndFraction() const noexcept
{
    if (sourceLengthSeconds <= 0.0)
        return 1.0;

    return juce::jlimit(0.0, 1.0, (getSourceOffsetSeconds() + getPlayLengthSeconds()) / sourceLengthSeconds);
}

void AudioClip::buildPeaks()
{
    std::vector<float> built(peakBuckets, 0.0f);

    const auto total = samples != nullptr ? samples->getNumSamples() : 0;

    if (total <= 0)
    {
        peaks = std::make_shared<const std::vector<float>>(std::move(built));
        return;
    }

    const auto perBucket = juce::jmax(1, total / peakBuckets);

    for (int bucket = 0; bucket < peakBuckets; ++bucket)
    {
        const auto start = bucket * perBucket;

        if (start >= total)
            break;

        const auto length = juce::jmin(perBucket, total - start);
        auto peak = 0.0f;

        for (int channel = 0; channel < samples->getNumChannels(); ++channel)
            peak = juce::jmax(peak, samples->getMagnitude(channel, start, length));

        built[static_cast<size_t>(bucket)] = juce::jlimit(0.0f, 1.0f, peak);
    }

    peaks = std::make_shared<const std::vector<float>>(std::move(built));
}

} // namespace djr
