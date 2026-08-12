#include "EditHistory.h"

#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"

namespace djr
{

EditHistory::EditHistory(Mixer& mixerToWatch,
                         Transport* transportToWatch,
                         SessionState* sessionToWatch)
    : mixer(mixerToWatch), transport(transportToWatch), session(sessionToWatch)
{
}

EditHistory::Snapshot EditHistory::capture(const juce::String& actionName) const
{
    Snapshot snapshot;
    snapshot.name = actionName;
    snapshot.tracks.reserve(static_cast<size_t>(mixer.getNumTracks()));

    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        TrackState state;

        if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(i)))
        {
            state.placements = midiTrack->getPlacements();
            state.patterns.reserve(static_cast<size_t>(MidiTrack::maxPatterns));

            for (int pattern = 0; pattern < MidiTrack::maxPatterns; ++pattern)
                state.patterns.push_back(midiTrack->getClip(pattern).getNotesSnapshot());
        }
        else if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(i)))
        {
            state.audioClips = audioTrack->cloneClips();
        }

        snapshot.tracks.push_back(std::move(state));
    }

    if (transport != nullptr)
    {
        snapshot.session.timeSigNumerator = transport->getTimeSignatureNumerator();
        snapshot.session.timeSigDenominator = transport->getTimeSignatureDenominator();
    }

    if (session != nullptr)
    {
        for (int i = 0; i < SessionState::maxPatterns; ++i)
        {
            snapshot.session.patternNames[static_cast<size_t>(i)] = session->getCustomPatternName(i);
            snapshot.session.patternLengths[static_cast<size_t>(i)] = session->getPatternLengthBeats(i);
        }
    }

    return snapshot;
}

void EditHistory::restore(const Snapshot& snapshot)
{
    for (int i = 0; i < mixer.getNumTracks(); ++i)
    {
        if (! juce::isPositiveAndBelow(i, static_cast<int>(snapshot.tracks.size())))
            break;

        const auto& state = snapshot.tracks[static_cast<size_t>(i)];

        if (auto* midiTrack = dynamic_cast<MidiTrack*>(mixer.getTrack(i)))
        {
            midiTrack->setPlacements(state.placements);

            for (int pattern = 0; pattern < MidiTrack::maxPatterns
                                  && pattern < static_cast<int>(state.patterns.size()); ++pattern)
                midiTrack->getClip(pattern).setNotes(state.patterns[static_cast<size_t>(pattern)]);
        }
        else if (auto* audioTrack = dynamic_cast<AudioTrack*>(mixer.getTrack(i)))
        {
            // Hand the track fresh copies: the snapshot has to stay intact for
            // the next undo or redo.
            std::vector<std::unique_ptr<AudioClip>> copies;
            copies.reserve(state.audioClips.size());

            for (const auto& clip : state.audioClips)
                if (auto copy = clip->duplicate())
                    copies.push_back(std::move(copy));

            audioTrack->replaceClips(std::move(copies));
        }
    }

    if (transport != nullptr)
    {
        transport->setTimeSignature(snapshot.session.timeSigNumerator,
                                    snapshot.session.timeSigDenominator);
    }

    if (session != nullptr)
    {
        for (int i = 0; i < SessionState::maxPatterns; ++i)
        {
            session->setPatternName(i, snapshot.session.patternNames[static_cast<size_t>(i)]);
            session->setPatternLengthBeats(i, snapshot.session.patternLengths[static_cast<size_t>(i)]);
        }
    }
}

void EditHistory::beginGesture(const juce::String& actionName)
{
    juce::ignoreUnused(actionName);
    gestureOpen = true;
    gestureCaptured = false;
}

void EditHistory::endGesture()
{
    gestureOpen = false;
    gestureCaptured = false;
}

void EditHistory::pushSnapshot(const juce::String& actionName)
{
    // Inside a gesture only the first push counts, so a drag that fires on every
    // mouse move still collapses to a single step.
    if (gestureOpen)
    {
        if (gestureCaptured)
            return;

        gestureCaptured = true;
    }

    undoStack.push_back(capture(actionName));

    if (static_cast<int>(undoStack.size()) > maxDepth)
        undoStack.erase(undoStack.begin());

    // A fresh edit makes any redo branch unreachable.
    redoStack.clear();
}

void EditHistory::abandonSnapshot()
{
    if (! undoStack.empty())
        undoStack.pop_back();
}

bool EditHistory::canUndo() const noexcept
{
    return ! undoStack.empty();
}

bool EditHistory::canRedo() const noexcept
{
    return ! redoStack.empty();
}

juce::String EditHistory::getUndoName() const
{
    return undoStack.empty() ? juce::String() : undoStack.back().name;
}

juce::String EditHistory::getRedoName() const
{
    return redoStack.empty() ? juce::String() : redoStack.back().name;
}

bool EditHistory::undo()
{
    if (undoStack.empty())
        return false;

    // Remember where we are now so redo can come back to it.
    redoStack.push_back(capture(undoStack.back().name));

    restore(undoStack.back());
    undoStack.pop_back();

    if (onChanged)
        onChanged();

    return true;
}

bool EditHistory::redo()
{
    if (redoStack.empty())
        return false;

    undoStack.push_back(capture(redoStack.back().name));

    restore(redoStack.back());
    redoStack.pop_back();

    if (onChanged)
        onChanged();

    return true;
}

void EditHistory::clear()
{
    undoStack.clear();
    redoStack.clear();
}

} // namespace djr
