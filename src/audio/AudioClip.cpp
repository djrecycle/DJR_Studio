#include "AudioClip.h"

#include "TimeStretch.h"

#include <cmath>

namespace djr
{

namespace
{
    /** How fine the drawing summary is.

        One number for every source is the wrong shape: 512 buckets is plenty
        of detail for a two second loop and almost none for a four minute take,
        where each bucket would cover nearly half a second and the playlist
        draws it as a solid block. So the bucket is sized in samples instead,
        and the count follows the audio - with a ceiling, because the summary
        exists to be cheap.
    */
    constexpr int minPeakBuckets = 512;
    constexpr int maxPeakBuckets = 32768;
    constexpr int minSamplesPerBucket = 64;
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

std::unique_ptr<AudioClip> AudioClip::createFromBuffer(const juce::String& name,
                                                       const juce::AudioBuffer<float>& source,
                                                       int numSamples,
                                                       double sampleRate)
{
    const auto numChannels = juce::jlimit(1, 2, source.getNumChannels());
    const auto length = juce::jmin(numSamples, source.getNumSamples());

    if (length <= 0 || sampleRate <= 0.0)
        return nullptr;

    juce::AudioBuffer<float> copied(numChannels, length);

    for (int channel = 0; channel < numChannels; ++channel)
        copied.copyFrom(channel, 0, source, channel, 0, length);

    std::unique_ptr<AudioClip> clip(new AudioClip());
    clip->name = name;
    clip->clipSampleRate = sampleRate;
    clip->samples = std::make_shared<const juce::AudioBuffer<float>>(std::move(copied));
    clip->sourceLengthSeconds = length / sampleRate;
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
    clampFadesToLength();
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
    clampFadesToLength();
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

void AudioClip::clampFadesToLength() noexcept
{
    // Called after anything that shortens the clip: the fades were clamped
    // against the length it had then, not the one it has now.
    setFadeInSeconds(getFadeInSeconds());
    setFadeOutSeconds(getFadeOutSeconds());
}

void AudioClip::setWarpMode(WarpMode mode) noexcept
{
    warpMode.store(mode, std::memory_order_release);
}

AudioClip::WarpMode AudioClip::getWarpMode() const noexcept
{
    return warpMode.load(std::memory_order_acquire);
}

bool AudioClip::isWarpPrepared(double tempoBpm) const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(stretchLock);
    return stretched != nullptr
        && std::abs(stretchedForTempo - tempoBpm) < 1.0e-9
        && std::abs(stretchedForFactor - getPreparedFactor(tempoBpm)) < 1.0e-9;
}

void AudioClip::setPitchSemitones(int semitones) noexcept
{
    pitchSemitones.store(juce::jlimit(-maxPitchSemitones, maxPitchSemitones, semitones),
                         std::memory_order_release);
}

int AudioClip::getPitchSemitones() const noexcept
{
    return pitchSemitones.load(std::memory_order_acquire);
}

double AudioClip::getPitchRatio() const noexcept
{
    const auto semitones = getPitchSemitones();
    return semitones == 0 ? 1.0 : std::pow(2.0, semitones / 12.0);
}

bool AudioClip::needsPreparedCopy() const noexcept
{
    return (isWarpEnabled() && getWarpMode() == WarpMode::stretch) || getPitchSemitones() != 0;
}

double AudioClip::getPreparedFactor(double tempoBpm) const noexcept
{
    // Two jobs, one copy. Stretching for the tempo shortens it; pitching reads
    // it faster afterwards, so it has to be lengthened by the same amount first
    // or the clip would come out short as well as high.
    const auto tempoPart = isWarpEnabled() && getWarpMode() == WarpMode::stretch
        ? getPlaybackRate(tempoBpm)
        : 1.0;

    return tempoPart / getPitchRatio();
}

void AudioClip::prepareWarp(double tempoBpm)
{
    if (samples == nullptr)
        return;

    if (! needsPreparedCopy())
    {
        // Nothing to prepare any more - drop the copy rather than leave a stale
        // one for the audio thread to find.
        const juce::SpinLock::ScopedLockType scoped(stretchLock);
        stretched = nullptr;
        stretchedForFactor = 1.0;
        return;
    }

    if (isWarpPrepared(tempoBpm))
        return;

    const auto factor = getPreparedFactor(tempoBpm);

    // At a factor of one there is nothing to do, and running it through the
    // stretcher anyway would only cost quality.
    auto built = std::abs(factor - 1.0) < 1.0e-9
        ? std::make_shared<const juce::AudioBuffer<float>>(*samples)
        : std::make_shared<const juce::AudioBuffer<float>>(TimeStretch::process(*samples, factor));

    const juce::SpinLock::ScopedLockType scoped(stretchLock);
    stretched = std::move(built);
    stretchedForTempo = tempoBpm;
    stretchedForFactor = factor;
}

double AudioClip::getFadeInSeconds() const noexcept
{
    return fadeInSeconds.load(std::memory_order_acquire);
}

void AudioClip::setFadeInSeconds(double seconds) noexcept
{
    // Never longer than what plays: a fade that outruns the clip would still be
    // climbing when the audio stops, which is the click it exists to remove.
    fadeInSeconds.store(juce::jlimit(0.0, getPlayLengthSeconds(), seconds),
                        std::memory_order_release);
}

double AudioClip::getFadeOutSeconds() const noexcept
{
    return fadeOutSeconds.load(std::memory_order_acquire);
}

void AudioClip::setFadeOutSeconds(double seconds) noexcept
{
    fadeOutSeconds.store(juce::jlimit(0.0, getPlayLengthSeconds(), seconds),
                         std::memory_order_release);
}

void AudioClip::addToBuffer(juce::AudioBuffer<float>& destination,
                            double blockStartBeat,
                            double tempoBpm,
                            double sampleRate) const
{
    const auto numSamples = destination.getNumSamples();

    if (numSamples <= 0 || tempoBpm <= 0.0 || sampleRate <= 0.0)
        return;

    if (isMuted())
        return;

    // A destructive edit swaps this pointer from the message thread. Held only
    // long enough to take a reference: a lost race costs one block, the same
    // trade the plugin chain and the stretch cache already make.
    std::shared_ptr<const juce::AudioBuffer<float>> playing;

    {
        const juce::SpinLock::ScopedTryLockType scoped(sampleLock);

        if (! scoped.isLocked())
            return;

        playing = samples;
    }

    if (playing == nullptr)
        return;

    auto rate = getPlaybackRate(tempoBpm);

    // In stretch mode the tempo has already been applied, once, to a copy of
    // the audio. Playing that copy straight through is what keeps the pitch
    // still: the resampling path below is exactly what moves it.
    auto sourceScale = 1.0;

    if (needsPreparedCopy())
    {
        const juce::SpinLock::ScopedTryLockType scoped(stretchLock);

        // A failed try-lock, or a copy built for another tempo or another
        // pitch, falls back to resampling for this block rather than dropping
        // the clip.
        if (scoped.isLocked() && stretched != nullptr
            && std::abs(stretchedForTempo - tempoBpm) < 1.0e-9
            && std::abs(stretchedForFactor - getPreparedFactor(tempoBpm)) < 1.0e-9)
        {
            playing = stretched;

            // Everything below measures in source samples; the copy holds the
            // same audio at a different length, so the trim points move with it.
            sourceScale = 1.0 / stretchedForFactor;

            if (isWarpEnabled() && getWarpMode() == WarpMode::stretch)
                rate = 1.0;
        }
    }

    // Reading faster is what raises the pitch; the copy above was lengthened by
    // the same amount so the clip still ends where it did.
    rate *= getPitchRatio();

    const auto totalSourceSamples = playing != nullptr ? playing->getNumSamples() : 0;

    if (totalSourceSamples <= 0)
        return;

    const auto secondsPerBeat = 60.0 / tempoBpm;

    // Timeline seconds elapsed inside the clip, converted to source seconds.
    const auto timelineSeconds = (blockStartBeat - getStartBeat()) * secondsPerBeat;
    const auto offsetSamples = getSourceOffsetSeconds() * sampleRate * sourceScale;
    auto readPosition = offsetSamples + timelineSeconds * rate * sampleRate;

    const auto firstSample = offsetSamples;
    const auto lastSample = juce::jmin(static_cast<double>(totalSourceSamples),
                                       offsetSamples + getPlayLengthSeconds() * sampleRate * sourceScale);

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

    // Capped against what actually plays as well as clamped when set: trimming
    // a clip shorter afterwards must not leave a fade hanging off the end.
    const auto playSamples = juce::jmax(1.0, lastSample - firstSample);
    const auto fadeInSamples = juce::jmin(getFadeInSeconds() * sampleRate, playSamples);
    const auto fadeOutSamples = juce::jmin(getFadeOutSeconds() * sampleRate, playSamples);

    for (int sample = writeOffset; sample < numSamples; ++sample)
    {
        if (readPosition >= lastSample)
            break;

        auto gainHere = clipGain;

        // Linear, and multiplied where they overlap on a very short clip: that
        // dips the middle but never steps, which is the whole point.
        if (fadeInSamples > 0.0)
        {
            const auto into = readPosition - firstSample;

            if (into < fadeInSamples)
                gainHere *= static_cast<float>(juce::jmax(0.0, into) / fadeInSamples);
        }

        if (fadeOutSamples > 0.0)
        {
            const auto remaining = lastSample - readPosition;

            if (remaining < fadeOutSamples)
                gainHere *= static_cast<float>(juce::jmax(0.0, remaining) / fadeOutSamples);
        }

        const auto index = static_cast<int>(readPosition);
        const auto fraction = static_cast<float>(readPosition - index);
        const auto nextIndex = juce::jmin(index + 1, totalSourceSamples - 1);

        if (index < 0 || index >= totalSourceSamples)
            break;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto sourceChannel = juce::jmin(channel, playing->getNumChannels() - 1);
            const auto* data = playing->getReadPointer(sourceChannel);

            // Linear interpolation: enough for varispeed playback of a clip.
            const auto value = data[index] + (data[nextIndex] - data[index]) * fraction;
            destination.addSample(channel, sample, value * gainHere);
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
    object->setProperty("fadeIn", getFadeInSeconds());
    object->setProperty("fadeOut", getFadeOutSeconds());
    object->setProperty("warpMode", getWarpMode() == WarpMode::stretch ? "stretch" : "resample");
    object->setProperty("pitchSemitones", getPitchSemitones());

    // The edits, not the edited audio: the file on disk is the original, and
    // replaying a short list over it is cheaper than saving a second copy of
    // every take that was ever normalised.
    juce::Array<juce::var> editArray;

    for (const auto& record : getSampleEdits())
    {
        auto* editObject = new juce::DynamicObject();
        editObject->setProperty("edit", record.edit == SampleEdit::reverse ? "reverse" : "normalise");
        editObject->setProperty("offset", record.offsetSeconds);
        editObject->setProperty("length", record.lengthSeconds);
        editArray.add(juce::var(editObject));
    }

    if (! editArray.isEmpty())
        object->setProperty("sampleEdits", editArray);

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

    // Absent in files written before there was a choice, and those clips were
    // all resampling - so that is what the fallback has to be.
    setPitchSemitones(static_cast<int>(object->getProperty("pitchSemitones")));
    setWarpMode(object->getProperty("warpMode").toString() == "stretch"
                    ? WarpMode::stretch
                    : WarpMode::resample);
    setMuted(static_cast<bool>(object->getProperty("muted")));

    // Before the trim and the fades: replaying an edit does not touch either,
    // and the fades that were saved are the ones a reverse had already swapped.
    if (auto* editArray = object->getProperty("sampleEdits").getArray())
    {
        for (const auto& entry : *editArray)
        {
            auto* editObject = entry.getDynamicObject();

            if (editObject == nullptr)
                continue;

            SampleEditRecord record;
            record.edit = editObject->getProperty("edit").toString() == "reverse"
                ? SampleEdit::reverse
                : SampleEdit::normalise;
            record.offsetSeconds = static_cast<double>(editObject->getProperty("offset"));
            record.lengthSeconds = static_cast<double>(editObject->getProperty("length"));

            editSamples(record.edit, record.offsetSeconds, record.lengthSeconds);

            const juce::SpinLock::ScopedLockType scoped(sampleLock);
            sampleEdits.push_back(record);
        }
    }

    // Trim last, and clamped to what the decoded file actually holds.
    const auto offset = juce::jlimit(0.0,
                                     juce::jmax(0.0, sourceLengthSeconds - minimumLengthSeconds),
                                     static_cast<double>(object->getProperty("sourceOffset")));
    const auto length = juce::jlimit(minimumLengthSeconds,
                                     juce::jmax(minimumLengthSeconds, sourceLengthSeconds - offset),
                                     static_cast<double>(object->getProperty("playLength")));

    sourceOffsetSeconds.store(offset, std::memory_order_release);
    playLengthSeconds.store(length, std::memory_order_release);

    // After the trim, so the setters clamp against the length this clip really
    // has rather than the one it had a moment ago.
    setFadeInSeconds(static_cast<double>(object->getProperty("fadeIn")));
    setFadeOutSeconds(static_cast<double>(object->getProperty("fadeOut")));
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
    copy->sampleEdits = sampleEdits;
    copy->clipSampleRate = clipSampleRate;
    copy->sourceLengthSeconds = sourceLengthSeconds;

    copy->startBeat.store(getStartBeat(), std::memory_order_release);
    copy->sourceOffsetSeconds.store(getSourceOffsetSeconds(), std::memory_order_release);
    copy->playLengthSeconds.store(getPlayLengthSeconds(), std::memory_order_release);
    copy->originalTempo.store(getOriginalTempo(), std::memory_order_release);
    copy->warpEnabled.store(isWarpEnabled(), std::memory_order_release);
    copy->muted.store(isMuted(), std::memory_order_release);
    copy->gain.store(getGain(), std::memory_order_release);
    copy->fadeInSeconds.store(getFadeInSeconds(), std::memory_order_release);
    copy->fadeOutSeconds.store(getFadeOutSeconds(), std::memory_order_release);
    copy->warpMode.store(getWarpMode(), std::memory_order_release);

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

    // The cut makes two new edges in the middle of what was one clip. Each
    // piece keeps the fade on the edge it still owns and loses the one that is
    // now an internal join, where a fade would carve a hole in the audio.
    setFadeOutSeconds(0.0);
    right->setFadeInSeconds(0.0);

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

int AudioClip::getNumSourceSamples() const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);
    return samples != nullptr ? samples->getNumSamples() : 0;
}

double AudioClip::getClipSampleRate() const noexcept
{
    return clipSampleRate;
}

int AudioClip::getNumSourceChannels() const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);
    return samples != nullptr ? samples->getNumChannels() : 0;
}

bool AudioClip::getSampleRange(int channel, int firstSample, int numSamples,
                               float& minOut, float& maxOut) const
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);

    if (samples == nullptr || numSamples <= 0
        || channel < 0 || channel >= samples->getNumChannels())
        return false;

    const auto total = samples->getNumSamples();
    const auto first = juce::jlimit(0, juce::jmax(0, total - 1), firstSample);
    const auto length = juce::jmin(numSamples, total - first);

    if (first >= total || length <= 0)
        return false;

    const auto* read = samples->getReadPointer(channel);
    auto lowest = read[first];
    auto highest = read[first];

    for (int sample = first + 1; sample < first + length; ++sample)
    {
        lowest = juce::jmin(lowest, read[sample]);
        highest = juce::jmax(highest, read[sample]);
    }

    minOut = lowest;
    maxOut = highest;
    return true;
}

std::vector<AudioClip::SampleEditRecord> AudioClip::getSampleEdits() const
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);
    return sampleEdits;
}

bool AudioClip::hasSampleEdits() const noexcept
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);
    return ! sampleEdits.empty();
}

void AudioClip::getPlayedRegion(int& firstSampleOut, int& numSamplesOut) const
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);

    const auto total = samples != nullptr ? samples->getNumSamples() : 0;

    if (total <= 0 || clipSampleRate <= 0.0)
    {
        firstSampleOut = 0;
        numSamplesOut = 0;
        return;
    }

    firstSampleOut = juce::jlimit(0, total,
                                  static_cast<int>(std::round(getSourceOffsetSeconds() * clipSampleRate)));
    numSamplesOut = juce::jlimit(0, total - firstSampleOut,
                                 static_cast<int>(std::round(getPlayLengthSeconds() * clipSampleRate)));
}

bool AudioClip::copySamples(int firstSample, int numSamples, juce::AudioBuffer<float>& destination) const
{
    const juce::SpinLock::ScopedLockType scoped(sampleLock);

    if (samples == nullptr || numSamples <= 0)
        return false;

    const auto total = samples->getNumSamples();
    const auto first = juce::jlimit(0, juce::jmax(0, total), firstSample);
    const auto length = juce::jmin(numSamples, total - first);

    if (length <= 0)
        return false;

    destination.setSize(samples->getNumChannels(), length, false, false, true);

    for (int channel = 0; channel < samples->getNumChannels(); ++channel)
        destination.copyFrom(channel, 0, *samples, channel, first, length);

    return true;
}

juce::File AudioClip::exportPlayedRegion(const juce::File& file,
                                         juce::AudioFormatManager& formats,
                                         juce::String& errorOut) const
{
    // A file with no extension, or one nothing here can write, becomes a wav:
    // that is the format every other program in the room can open.
    auto target = file;
    auto* format = formats.findFormatForFileExtension(target.getFileExtension());

    if (format == nullptr || ! format->canHandleFile(target))
    {
        target = target.withFileExtension("wav");
        format = formats.findFormatForFileExtension("wav");
    }

    if (format == nullptr)
    {
        errorOut = TRANS("No audio formats are available to write with.");
        return {};
    }

    int first = 0;
    int length = 0;
    getPlayedRegion(first, length);

    juce::AudioBuffer<float> audio;

    if (! copySamples(first, length, audio) || audio.getNumSamples() <= 0)
    {
        errorOut = TRANS("There is no sample to export.");
        return {};
    }

    if (! target.getParentDirectory().createDirectory())
    {
        errorOut = TRANS("Cannot write to that folder.");
        return {};
    }

    target.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(target.createOutputStream());

    if (stream == nullptr)
    {
        errorOut = TRANS("Cannot write to that file.");
        return {};
    }

    // 24 bit where the format allows it. The audio was normalised and reversed
    // in floating point, and 16 bit would throw that away on the way out for no
    // reason anyone asked for.
    const auto depths = format->getPossibleBitDepths();
    const auto depth = depths.contains(24) ? 24
                     : depths.contains(16) ? 16
                     : depths.isEmpty() ? 16 : depths[depths.size() - 1];

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format->createWriterFor(stream.get(), clipSampleRate,
                                static_cast<unsigned int>(audio.getNumChannels()),
                                depth, {}, 0));

    if (writer == nullptr)
    {
        errorOut = TRANS("That format cannot be written.");
        return {};
    }

    // The writer owns the stream from here, and only from here: releasing it
    // before the writer exists would leak it on the failure above.
    stream.release();

    const auto written = writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    writer.reset();

    if (! written)
    {
        target.deleteFile();
        errorOut = TRANS("Writing the sample failed.");
        return {};
    }

    errorOut.clear();
    return target;
}

bool AudioClip::canApplySampleEdit(SampleEdit edit) const
{
    std::shared_ptr<const juce::AudioBuffer<float>> current;

    {
        const juce::SpinLock::ScopedLockType scoped(sampleLock);
        current = samples;
    }

    if (current == nullptr || clipSampleRate <= 0.0)
        return false;

    int first = 0;
    int length = 0;
    getPlayedRegion(first, length);

    if (length <= 0)
        return false;

    if (edit == SampleEdit::reverse)
        return length > 1;

    auto peak = 0.0f;

    for (int channel = 0; channel < current->getNumChannels(); ++channel)
        peak = juce::jmax(peak, current->getMagnitude(channel, first, length));

    return peak > 1.0e-6f && std::abs(peak - 1.0f) >= 1.0e-4f;
}

bool AudioClip::editSamples(SampleEdit edit, double offsetSeconds, double lengthSeconds)
{
    if (clipSampleRate <= 0.0)
        return false;

    // The copy is made outside the lock: it is the expensive part, and the
    // audio thread is reading the old buffer the whole time it runs.
    std::shared_ptr<const juce::AudioBuffer<float>> current;

    {
        const juce::SpinLock::ScopedLockType scoped(sampleLock);
        current = samples;
    }

    if (current == nullptr)
        return false;

    const auto total = current->getNumSamples();
    const auto first = juce::jlimit(0, juce::jmax(0, total),
                                    static_cast<int>(std::round(offsetSeconds * clipSampleRate)));
    const auto length = juce::jlimit(0, total - first,
                                     static_cast<int>(std::round(lengthSeconds * clipSampleRate)));

    if (length <= 0)
        return false;

    juce::AudioBuffer<float> edited(*current);

    if (edit == SampleEdit::normalise)
    {
        auto peak = 0.0f;

        for (int channel = 0; channel < edited.getNumChannels(); ++channel)
            peak = juce::jmax(peak, edited.getMagnitude(channel, first, length));

        // Silence has no peak to lift, and audio already at full scale has
        // nowhere to go. Both would be an undo step that changes nothing.
        if (peak <= 1.0e-6f || std::abs(peak - 1.0f) < 1.0e-4f)
            return false;

        // Every channel by the same amount: scaling them apart would move the
        // stereo image, which is not what normalising is for.
        edited.applyGain(first, length, 1.0f / peak);
    }
    else
    {
        for (int channel = 0; channel < edited.getNumChannels(); ++channel)
            edited.reverse(channel, first, length);
    }

    auto replacement = std::make_shared<const juce::AudioBuffer<float>>(std::move(edited));

    {
        const juce::SpinLock::ScopedLockType scoped(sampleLock);
        samples = std::move(replacement);
    }

    // The stretched copy was built from the audio that has just been replaced,
    // so it is now a picture of something that no longer exists.
    {
        const juce::SpinLock::ScopedLockType scoped(stretchLock);
        stretched = nullptr;
        stretchedForTempo = 0.0;
    }

    buildPeaks();
    return true;
}

bool AudioClip::applySampleEdit(SampleEdit edit)
{
    const auto offset = getSourceOffsetSeconds();
    const auto length = getPlayLengthSeconds();

    if (! editSamples(edit, offset, length))
        return false;

    // Playing the region backwards puts its end at its start, so the fade that
    // was easing it in is now what eases it out.
    if (edit == SampleEdit::reverse)
    {
        const auto fadeIn = getFadeInSeconds();
        setFadeInSeconds(getFadeOutSeconds());
        setFadeOutSeconds(fadeIn);
    }

    const juce::SpinLock::ScopedLockType scoped(sampleLock);
    sampleEdits.push_back({ edit, offset, length });
    return true;
}

void AudioClip::buildPeaks()
{
    const auto total = samples != nullptr ? samples->getNumSamples() : 0;

    if (total <= 0)
    {
        peaks = std::make_shared<const std::vector<float>>(std::vector<float>(minPeakBuckets, 0.0f));
        return;
    }

    const auto perBucket = juce::jmax(minSamplesPerBucket,
                                      (total + maxPeakBuckets - 1) / maxPeakBuckets);
    const auto bucketCount = juce::jlimit(minPeakBuckets, maxPeakBuckets,
                                          (total + perBucket - 1) / perBucket);

    std::vector<float> built(static_cast<size_t>(bucketCount), 0.0f);

    for (int bucket = 0; bucket < bucketCount; ++bucket)
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
