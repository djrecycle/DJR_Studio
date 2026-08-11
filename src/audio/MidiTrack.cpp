#include "MidiTrack.h"

#include <cmath>
#include <limits>

namespace djr
{

MidiTrack::MidiTrack(juce::String trackName)
    : Track(std::move(trackName), TrackKind::midi)
{
    for (auto& clip : patternClips)
        clip = std::make_unique<MidiClip>();

    placements.reserve(static_cast<size_t>(maxPlacements));
}

MidiClip& MidiTrack::getClip() noexcept
{
    return getClip(getActivePattern());
}

const MidiClip& MidiTrack::getClip() const noexcept
{
    return getClip(getActivePattern());
}

MidiClip& MidiTrack::getClip(int patternIndex) noexcept
{
    return *patternClips[static_cast<size_t>(juce::jlimit(0, maxPatterns - 1, patternIndex))];
}

const MidiClip& MidiTrack::getClip(int patternIndex) const noexcept
{
    return *patternClips[static_cast<size_t>(juce::jlimit(0, maxPatterns - 1, patternIndex))];
}

void MidiTrack::setClipNotes(const juce::Array<MidiNote>& notes)
{
    getClip().setNotes(notes);
}

void MidiTrack::setActivePattern(int patternIndex) noexcept
{
    activePattern.store(juce::jlimit(0, maxPatterns - 1, patternIndex), std::memory_order_release);
}

int MidiTrack::getActivePattern() const noexcept
{
    return activePattern.load(std::memory_order_acquire);
}

bool MidiTrack::patternHasContent(int patternIndex) const
{
    return getClip(patternIndex).getNumNotes() > 0;
}

void MidiTrack::addPlacement(PatternPlacement placement)
{
    placement.patternIndex = juce::jlimit(0, maxPatterns - 1, placement.patternIndex);
    placement.startBeat = juce::jmax(0.0, placement.startBeat);
    placement.lengthBeats = juce::jmax(0.25, placement.lengthBeats);

    const juce::SpinLock::ScopedLockType scoped(placementLock);

    if (static_cast<int>(placements.size()) >= maxPlacements)
        return;

    placements.push_back(placement);
}

bool MidiTrack::removePlacementAt(int index)
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(placements.size())))
        return false;

    placements.erase(placements.begin() + index);
    return true;
}

void MidiTrack::clearPlacements()
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);
    placements.clear();
    placements.reserve(static_cast<size_t>(maxPlacements));
}

std::vector<PatternPlacement> MidiTrack::getPlacements() const
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);
    return placements;
}

void MidiTrack::setPlacements(const std::vector<PatternPlacement>& newPlacements)
{
    // Build the copy outside the lock so the audio thread waits as little as
    // possible, then swap it in.
    std::vector<PatternPlacement> sanitised;
    sanitised.reserve(static_cast<size_t>(maxPlacements));

    for (const auto& placement : newPlacements)
    {
        if (static_cast<int>(sanitised.size()) >= maxPlacements)
            break;

        auto copy = placement;
        copy.patternIndex = juce::jlimit(0, maxPatterns - 1, copy.patternIndex);
        copy.startBeat = juce::jmax(0.0, copy.startBeat);
        copy.lengthBeats = juce::jmax(PatternPlacement::minimumLengthBeats, copy.lengthBeats);
        sanitised.push_back(copy);
    }

    const juce::SpinLock::ScopedLockType scoped(placementLock);
    placements.swap(sanitised);
}

int MidiTrack::findPlacementAt(double beat) const
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);

    for (int i = 0; i < static_cast<int>(placements.size()); ++i)
    {
        const auto& placement = placements[static_cast<size_t>(i)];

        if (beat >= placement.startBeat && beat < placement.startBeat + placement.lengthBeats)
            return i;
    }

    return -1;
}

double MidiTrack::getFreeSpanFrom(double startBeat) const
{
    auto span = std::numeric_limits<double>::max();

    const juce::SpinLock::ScopedLockType scoped(placementLock);

    for (const auto& placement : placements)
    {
        if (startBeat >= placement.startBeat && startBeat < placement.getEndBeat())
            return 0.0;

        if (placement.startBeat > startBeat)
            span = juce::jmin(span, placement.startBeat - startBeat);
    }

    return span;
}

PatternPlacement MidiTrack::getPlacement(int index) const
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);

    return juce::isPositiveAndBelow(index, static_cast<int>(placements.size()))
        ? placements[static_cast<size_t>(index)]
        : PatternPlacement{};
}

bool MidiTrack::updatePlacement(int index, const PatternPlacement& placement)
{
    const juce::SpinLock::ScopedLockType scoped(placementLock);

    if (! juce::isPositiveAndBelow(index, static_cast<int>(placements.size())))
        return false;

    auto sanitised = placement;
    sanitised.patternIndex = juce::jlimit(0, maxPatterns - 1, sanitised.patternIndex);
    sanitised.startBeat = juce::jmax(0.0, sanitised.startBeat);
    sanitised.lengthBeats = juce::jmax(PatternPlacement::minimumLengthBeats, sanitised.lengthBeats);
    sanitised.sourceOffsetBeats = juce::jmax(0.0, sanitised.sourceOffsetBeats);

    placements[static_cast<size_t>(index)] = sanitised;
    return true;
}

void MidiTrack::setPreviewDrumKit(bool shouldUseDrums)
{
    previewSynth.setDrumMode(shouldUseDrums);
}

bool MidiTrack::isPreviewDrumKit() const noexcept
{
    return previewSynth.isDrumMode();
}

void MidiTrack::prepare(double sampleRate, int blockSize)
{
    Track::prepare(sampleRate, blockSize);
    previewSynth.prepare(sampleRate);
    resetTransportState();
}

void MidiTrack::renderAudio(juce::AudioBuffer<float>& buffer,
                            juce::MidiBuffer& midi,
                            const TrackPlaybackContext& context)
{
    if (context.isPlaying)
    {
        wasPlaying = true;

        if (context.songMode)
            renderSongMode(midi, context, buffer.getNumSamples());
        else
            renderPatternMode(midi, context, buffer.getNumSamples());
    }
    else if (wasPlaying)
    {
        // The graph keeps running while stopped, so release anything the
        // sequence left hanging instead of letting it drone.
        sendAllNotesOff(midi);
        resetTransportState();
        wasPlaying = false;
    }

    // Live playing is merged by Track before the instrument runs, so the preview
    // synth has to see it too - render after the sequence has been written.
    if (! hasInstrument())
    {
        juce::MidiBuffer forSynth(midi);
        previewSynth.render(buffer, forSynth);
    }
}

void MidiTrack::renderPatternMode(juce::MidiBuffer& midi, const TrackPlaybackContext& context, int numSamples)
{
    const auto jumpedBackward = context.startBeat + 0.0001 < lastBlockStartBeat;

    if (! transportPrepared || jumpedBackward)
        resetTransportState();

    emitNotes(midi,
              getClip().getNotesSnapshot(),
              context.startBeat,
              context.endBeat,
              0.0,
              std::numeric_limits<double>::max(),
              numSamples);

    lastBlockStartBeat = context.startBeat;
    transportPrepared = true;
}

void MidiTrack::renderSongMode(juce::MidiBuffer& midi, const TrackPlaybackContext& context, int numSamples)
{
    const auto jumpedBackward = context.startBeat + 0.0001 < lastBlockStartBeat;

    if (! transportPrepared || jumpedBackward)
        resetTransportState();

    // Skipping a block beats blocking the device while the playlist is edited.
    const juce::SpinLock::ScopedTryLockType scoped(placementLock);

    if (scoped.isLocked())
    {
        for (const auto& placement : placements)
        {
            const auto placementEnd = placement.startBeat + placement.lengthBeats;

            if (placement.muted || placementEnd <= context.startBeat || placement.startBeat >= context.endBeat)
                continue;

            // Clip time = timeline time shifted by the placement, then offset to
            // wherever the left edge was trimmed to.
            const auto shift = placement.sourceOffsetBeats - placement.startBeat;

            emitNotes(midi,
                      getClip(placement.patternIndex).getNotesSnapshot(),
                      context.startBeat + shift,
                      context.endBeat + shift,
                      placement.sourceOffsetBeats,
                      placement.sourceOffsetBeats + placement.lengthBeats,
                      numSamples);
        }
    }

    lastBlockStartBeat = context.startBeat;
    transportPrepared = true;
}

void MidiTrack::emitNotes(juce::MidiBuffer& midi,
                          const juce::Array<MidiNote>& notes,
                          double localStartBeat,
                          double localEndBeat,
                          double windowStartBeat,
                          double windowEndBeat,
                          int numSamples)
{
    const auto beatSpan = juce::jmax(0.000001, localEndBeat - localStartBeat);
    numSamples = juce::jmax(1, numSamples);

    for (const auto& note : notes)
    {
        // A muted note is still drawn and editable, it just never sounds.
        if (note.muted)
            continue;

        auto noteStartBeat = note.startBeat;
        auto noteEndBeat = note.startBeat + note.lengthBeats;

        // Trimming hides the notes outside the placement's slice and clips the
        // ones that straddle its edges.
        if (noteStartBeat >= windowEndBeat || noteEndBeat <= windowStartBeat)
            continue;

        noteStartBeat = juce::jmax(noteStartBeat, windowStartBeat);
        noteEndBeat = juce::jmin(noteEndBeat, windowEndBeat);

        const auto pitch = juce::jlimit(0, 127, note.pitch);
        const auto velocity = static_cast<juce::uint8>(juce::jlimit(1, 127, juce::roundToInt(note.velocity * 127.0f)));

        const auto offsetFor = [&] (double beat)
        {
            return juce::jlimit(0,
                                numSamples - 1,
                                juce::roundToInt(((beat - localStartBeat) / beatSpan) * numSamples));
        };

        if (noteStartBeat < localStartBeat && noteEndBeat > localStartBeat && ! activeNotes[static_cast<size_t>(pitch)])
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, pitch, velocity), 0);
            activeNotes[static_cast<size_t>(pitch)] = true;
        }

        if (noteStartBeat >= localStartBeat && noteStartBeat < localEndBeat)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, pitch, velocity), offsetFor(noteStartBeat));
            activeNotes[static_cast<size_t>(pitch)] = true;
        }

        if (noteEndBeat >= localStartBeat && noteEndBeat < localEndBeat)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, pitch), offsetFor(noteEndBeat));
            activeNotes[static_cast<size_t>(pitch)] = false;
        }
    }
}

void MidiTrack::sendAllNotesOff(juce::MidiBuffer& midi)
{
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        if (! activeNotes[static_cast<size_t>(pitch)])
            continue;

        midi.addEvent(juce::MidiMessage::noteOff(1, pitch), 0);
        activeNotes[static_cast<size_t>(pitch)] = false;
    }

    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
}

void MidiTrack::resetTransportState()
{
    activeNotes.fill(false);
    lastBlockStartBeat = 0.0;
    transportPrepared = true;
}

} // namespace djr
