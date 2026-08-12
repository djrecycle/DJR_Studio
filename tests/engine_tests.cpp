// Offline checks for the audio graph. No audio device is involved: the mixer is
// driven by hand so the assertions are deterministic.

#include "audio/AudioClip.h"
#include "audio/Transport.h"
#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"
#include "audio/Mixer.h"
#include "app/SessionState.h"
#include "midi/MidiEngine.h"
#include "midi/PianoRollModel.h"
#include "export/ExportManager.h"
#include "project/Project.h"
#include "app/EditHistory.h"
#include "audio/Metronome.h"
#include "recording/Recorder.h"

#include <algorithm>
#include <iostream>

namespace
{
    int failures = 0;

    void check(bool condition, const juce::String& what)
    {
        if (condition)
        {
            std::cout << "ok   : " << what << "\n";
            return;
        }

        std::cout << "FAIL : " << what << "\n";
        ++failures;
    }

    bool isSilent(float peak) noexcept
    {
        return peak <= 1.0e-6f;
    }

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    constexpr double tempoBpm = 120.0;

    /** Runs the mixer for a few blocks and returns the loudest sample seen. */
    float renderPeak(djr::Mixer& mixer,
                     bool playing,
                     int numBlocks,
                     const juce::AudioBuffer<float>* input = nullptr,
                     double startBeat = 0.0,
                     const juce::MidiBuffer* liveMidi = nullptr)
    {
        juce::AudioBuffer<float> output(2, blockSize);
        const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (tempoBpm / 60.0);

        auto beat = startBeat;
        auto peak = 0.0f;

        for (int block = 0; block < numBlocks; ++block)
        {
            djr::TrackPlaybackContext context;
            context.sampleRate = sampleRate;
            context.tempoBpm = tempoBpm;
            context.startBeat = beat;
            context.endBeat = beat + beatsPerBlock;
            context.isPlaying = playing;
            context.inputBuffer = input;
            // Live notes belong to the first block only, like a real key press.
            context.liveMidi = block == 0 ? liveMidi : nullptr;

            output.clear();
            mixer.process(output, context);

            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                peak = std::max(peak, output.getMagnitude(channel, 0, output.getNumSamples()));

            if (playing)
                beat = context.endBeat;
        }

        return peak;
    }

    /** Runs stopped blocks so the preview synth's release tail can decay before
        an assertion about silence.
    */
    void settle(djr::Mixer& mixer)
    {
        renderPeak(mixer, false, 90);
    }

    juce::Array<djr::MidiNote> makeFourBarChord()
    {
        juce::Array<djr::MidiNote> notes;

        for (int i = 0; i < 4; ++i)
        {
            djr::MidiNote note;
            note.pitch = 60 + i * 2;
            note.velocity = 0.9f;
            note.startBeat = static_cast<double>(i);
            note.lengthBeats = 1.0;
            notes.add(note);
        }

        return notes;
    }

    /** First melodic MIDI track. The drum track answers with hits that ignore
        note-off, so the tonal assertions need a track that does not.
    */
    djr::MidiTrack* findFirstMidiTrack(djr::Mixer& mixer)
    {
        for (int i = 0; i < mixer.getNumTracks(); ++i)
            if (auto* midiTrack = dynamic_cast<djr::MidiTrack*>(mixer.getTrack(i)))
                if (! midiTrack->isPreviewDrumKit())
                    return midiTrack;

        return nullptr;
    }

    djr::AudioTrack* findFirstAudioTrack(djr::Mixer& mixer)
    {
        for (int i = 0; i < mixer.getNumTracks(); ++i)
            if (auto* audioTrack = dynamic_cast<djr::AudioTrack*>(mixer.getTrack(i)))
                return audioTrack;

        return nullptr;
    }

    /** Loads the first installed VST3 effect, or nullptr when none can be used.
        Opportunistic: a machine with no plugins simply skips the check.
    */
    std::unique_ptr<juce::AudioPluginInstance> loadFirstVst3Effect(juce::AudioPluginFormatManager& manager,
                                                                   juce::String& nameOut)
    {
        juce::VST3PluginFormat format;

        juce::FileSearchPath searchPath;
        searchPath.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3"));
        searchPath.add(juce::File("/usr/lib/vst3"));
        searchPath.add(juce::File("/usr/local/lib/vst3"));

        juce::KnownPluginList list;
        juce::PluginDirectoryScanner scanner(list, format, searchPath, true, juce::File());

        juce::String scanning;
        while (scanner.scanNextFile(true, scanning))
        {
            for (const auto& description : list.getTypes())
            {
                if (description.isInstrument)
                    continue;

                juce::String error;
                if (auto instance = manager.createPluginInstance(description, sampleRate, blockSize, error))
                {
                    nameOut = description.name;
                    return instance;
                }
            }
        }

        return nullptr;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    djr::Mixer mixer;
    mixer.prepare(sampleRate, blockSize);

    auto* midiTrack = findFirstMidiTrack(mixer);
    check(midiTrack != nullptr, "mixer ships with a MIDI track");

    if (midiTrack == nullptr)
        return 1;

    midiTrack->setClipNotes(makeFourBarChord());

    // --- The graph runs whether or not the transport does -------------------
    check(! isSilent(renderPeak(mixer, true, 12)),
          "MIDI track is audible while the transport plays");

    settle(mixer);
    check(isSilent(renderPeak(mixer, false, 4)),
          "sequenced notes stay silent while the transport is stopped");

    // Playing and then stopping must not leave a note droning once the release
    // envelope has run its course.
    renderPeak(mixer, true, 8);
    settle(mixer);
    check(isSilent(renderPeak(mixer, false, 4)),
          "stopping after playback releases hanging notes");

    // --- No instrument loaded means the preview synth still speaks ----------
    check(! midiTrack->hasInstrument(), "a fresh track has no instrument");

    // --- Live input reaches the track even when stopped ---------------------
    juce::AudioBuffer<float> input(2, blockSize);
    for (int channel = 0; channel < input.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(input.getWritePointer(channel), 0.5f, blockSize);

    auto* audioTrack = findFirstAudioTrack(mixer);
    check(audioTrack != nullptr && audioTrack->getKind() == djr::TrackKind::audio,
          "the session has an audio track");

    settle(mixer);
    check(isSilent(renderPeak(mixer, false, 2, &input)),
          "input is ignored until monitoring is armed");

    if (audioTrack != nullptr)
    {
        audioTrack->setInputMonitoring(true);
        check(! isSilent(renderPeak(mixer, false, 2, &input)),
              "input monitoring is audible with the transport stopped");
        audioTrack->setInputMonitoring(false);
    }

    // --- Live MIDI plays without the transport running -----------------------
    juce::MidiBuffer liveNote;
    liveNote.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);

    settle(mixer);
    mixer.setLiveMidiTarget(mixer.indexOf(midiTrack));
    check(! isSilent(renderPeak(mixer, false, 6, nullptr, 0.0, &liveNote)),
          "a live note sounds on the target track with the transport stopped");

    juce::MidiBuffer releaseNote;
    releaseNote.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
    renderPeak(mixer, false, 2, nullptr, 0.0, &releaseNote);
    settle(mixer);
    check(isSilent(renderPeak(mixer, false, 2)), "a released live note stops sounding");

    // Routing away from the track means it must stay quiet.
    mixer.setLiveMidiTarget(mixer.indexOf(audioTrack));
    check(isSilent(renderPeak(mixer, false, 6, nullptr, 0.0, &liveNote)),
          "live MIDI does not leak into untargeted tracks");
    renderPeak(mixer, false, 2, nullptr, 0.0, &releaseNote);
    settle(mixer);

    // --- Mute and solo ------------------------------------------------------
    midiTrack->setMuted(true);
    check(isSilent(renderPeak(mixer, true, 12)), "muting a track silences it");
    midiTrack->setMuted(false);

    if (audioTrack != nullptr)
    {
        audioTrack->setSoloed(true);
        check(isSilent(renderPeak(mixer, true, 12)),
              "soloing a silent track excludes the others");
        audioTrack->setSoloed(false);
    }

    check(! isSilent(renderPeak(mixer, true, 12)), "clearing solo restores playback");

    // --- Tracks can be added and removed at runtime -------------------------
    const auto before = mixer.getNumTracks();
    auto* added = mixer.addTrack(std::make_unique<djr::MidiTrack>("Test"));
    check(added != nullptr, "addTrack returns the new track");
    check(mixer.getNumTracks() == before + 1, "addTrack grows the mixer");
    check(mixer.indexOf(added) == before, "the new track lands at the end");

    if (added != nullptr)
    {
        static_cast<djr::MidiTrack*>(added)->setClipNotes(makeFourBarChord());
        check(! isSilent(renderPeak(mixer, true, 12)), "a runtime track plays");
    }

    check(mixer.removeTrack(before), "removeTrack succeeds");
    check(mixer.getNumTracks() == before, "removeTrack shrinks the mixer");
    check(! mixer.removeTrack(999), "removing an out-of-range track is refused");

    // --- An insert effect must never silence the track ----------------------
    // The format manager has to outlive any instance it created.
    juce::AudioPluginFormatManager pluginFormats;
    pluginFormats.addDefaultFormats();

    juce::String effectName;
    if (auto effect = loadFirstVst3Effect(pluginFormats, effectName))
    {
        midiTrack->addPlugin(std::move(effect));
        check(midiTrack->getPluginCount() == 1, "insert effect is added to the chain");
        check(! midiTrack->hasInstrument(), "an effect does not claim the instrument slot");
        check(! isSilent(renderPeak(mixer, true, 12)),
              "track stays audible with insert effect " + effectName);

        midiTrack->clearPlugins();
        check(midiTrack->getPluginCount() == 0, "clearPlugins empties the chain");
    }
    else
    {
        std::cout << "skip : no usable VST3 effect installed, insert check skipped\n";
    }

    // --- Audio clips play from the timeline ---------------------------------
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("djr_engine_test_clip.wav");
        wav.deleteFile();

        // Write two seconds of tone to load back as a clip.
        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());

            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(stream.get(), sampleRate, 2, 16, {}, 0));

            check(writer != nullptr, "test tone writer opens");

            if (writer != nullptr)
            {
                stream.release();

                const auto totalSamples = static_cast<int>(sampleRate * 2.0);
                juce::AudioBuffer<float> tone(2, totalSamples);
                double phase = 0.0;

                for (int sample = 0; sample < totalSamples; ++sample)
                {
                    const auto value = static_cast<float>(std::sin(phase)) * 0.6f;
                    phase += juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

                    for (int channel = 0; channel < 2; ++channel)
                        tone.setSample(channel, sample, value);
                }

                writer->writeFromAudioSampleBuffer(tone, 0, totalSamples);
                writer.reset();
            }
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        juce::String clipError;
        auto clip = djr::AudioClip::createFromFile(wav, sampleRate, formats, clipError);
        check(clip != nullptr, "an audio clip loads from a wav file");

        if (clip != nullptr)
        {
            check(std::abs(clip->getPlayLengthSeconds() - 2.0) < 0.05, "the clip knows its own duration");
            check(std::abs(clip->getLengthBeats(120.0) - 4.0) < 0.1,
                  "clip length in beats follows the tempo");
            check(! clip->getPeaks().empty(), "the clip builds a waveform envelope");

            djr::Mixer audioMixer;
            audioMixer.prepare(sampleRate, blockSize);

            auto* onlyAudio = findFirstAudioTrack(audioMixer);
            check(onlyAudio != nullptr, "the session has an audio track to drop a clip on");

            if (onlyAudio != nullptr)
            {
                check(isSilent(renderPeak(audioMixer, true, 8)), "an empty audio track is silent");

                clip->setStartBeat(4.0);
                onlyAudio->addClip(std::move(clip));
                check(onlyAudio->getNumClips() == 1, "the clip is on the track");

                // Before the clip starts there must be nothing.
                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 0.0)),
                      "nothing plays before the clip's start beat");

                check(! isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0)),
                      "the clip plays once the playhead reaches it");

                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 40.0)),
                      "nothing plays past the end of the clip");

                auto* placed = onlyAudio->getClip(0);
                check(placed != nullptr, "the placed clip can be edited");

                if (placed != nullptr)
                {
                    // --- Moving -------------------------------------------
                    placed->setStartBeat(12.0);
                    check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0)),
                          "moving a clip takes the audio with it");
                    check(! isSilent(renderPeak(audioMixer, true, 6, nullptr, 12.0)),
                          "the clip now plays at its new position");
                    placed->setStartBeat(4.0);

                    // --- Trimming the end ---------------------------------
                    const auto fullLength = placed->getPlayLengthSeconds();
                    placed->trimEnd(5.0, 120.0);
                    check(placed->getPlayLengthSeconds() < fullLength,
                          "trimming the end shortens the clip");
                    check(std::abs(placed->getLengthBeats(120.0) - 1.0) < 0.05,
                          "the trimmed clip covers the beats it was dragged to");
                    check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 6.0)),
                          "the trimmed-off tail no longer sounds");

                    // --- Trimming the start -------------------------------
                    placed->trimEnd(8.0, 120.0);
                    placed->trimStart(5.0, 120.0);
                    check(placed->getSourceOffsetSeconds() > 0.0,
                          "trimming the start skips into the source");
                    check(std::abs(placed->getStartBeat() - 5.0) < 0.05,
                          "the clip now begins where the edge was dragged");
                    check(isSilent(renderPeak(audioMixer, true, 4, nullptr, 4.0)),
                          "nothing plays in the trimmed-off head");

                    // Trimming can never invert or erase the clip.
                    placed->trimStart(999.0, 120.0);
                    check(placed->getPlayLengthSeconds() >= djr::AudioClip::minimumLengthSeconds,
                          "over-trimming stops at the minimum length");

                    // --- Warp follows the tempo ---------------------------
                    onlyAudio->clearClips();

                    juce::String warpError;
                    auto warpClip = djr::AudioClip::createFromFile(wav, sampleRate, formats, warpError);
                    check(warpClip != nullptr, "a second clip loads for the warp check");

                    if (warpClip != nullptr)
                    {
                        warpClip->setOriginalTempo(120.0);
                        warpClip->setStartBeat(0.0);

                        warpClip->setWarpEnabled(true);
                        const auto warpedAt120 = warpClip->getLengthBeats(120.0);
                        const auto warpedAt240 = warpClip->getLengthBeats(240.0);
                        check(std::abs(warpedAt120 - warpedAt240) < 1.0e-6,
                              "a warped clip keeps the same length in beats at any tempo");

                        warpClip->setWarpEnabled(false);
                        const auto rawAt120 = warpClip->getLengthBeats(120.0);
                        const auto rawAt240 = warpClip->getLengthBeats(240.0);
                        check(std::abs(rawAt240 - rawAt120 * 2.0) < 1.0e-6,
                              "an unwarped clip covers more beats as the tempo rises");

                        warpClip->setWarpEnabled(true);
                        onlyAudio->addClip(std::move(warpClip));

                        // At double tempo the warped clip still fills its four beats.
                        check(! isSilent(renderPeak(audioMixer, true, 4, nullptr, 3.5)),
                              "a warped clip is still sounding at the end of its span");
                    }
                }

                onlyAudio->clearClips();
                check(onlyAudio->getNumClips() == 0, "clips can be cleared");
                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0)),
                      "a cleared track goes quiet again");
            }
        }

        wav.deleteFile();
    }

    // --- Editors, track and engine share one clip ---------------------------
    {
        djr::Mixer shared;
        shared.prepare(sampleRate, blockSize);

        auto* target = findFirstMidiTrack(shared);
        auto* otherTrack = shared.getTrack(0);
        check(target != nullptr && otherTrack != nullptr, "shared mixer has both track kinds");

        djr::PianoRollModel model;
        check(! model.hasTargetClip(), "the editor starts with nothing to edit");
        check(model.getNotes().isEmpty(), "an unpointed editor reports no notes");

        model.addNote(60, 0.0, 1.0, 0.9f);
        check(model.getNotes().isEmpty(), "editing without a target is a no-op");

        // Selecting a MIDI track points every editor at that track's clip.
        model.setTargetClip(&target->getClip());
        model.addNote(60, 0.0, 1.0, 0.9f);

        check(target->getClip().getNumNotes() == 1,
              "a note added in the editor lands in the selected track's clip");
        check(! isSilent(renderPeak(shared, true, 12)),
              "the engine plays what the editor just wrote");

        // A second MIDI track must keep its own notes.
        auto* second = dynamic_cast<djr::MidiTrack*>(shared.addTrack(std::make_unique<djr::MidiTrack>("Second")));
        check(second != nullptr, "a second MIDI track can be added");

        if (second != nullptr)
        {
            model.setTargetClip(&second->getClip());
            check(model.getNotes().isEmpty(), "switching track shows that track's own clip");

            model.addNote(72, 0.0, 1.0, 0.9f);
            check(second->getClip().getNumNotes() == 1, "the new note goes to the newly selected track");
            check(target->getClip().getNumNotes() == 1, "the first track keeps its own note");
        }

        // Selecting an audio track blanks the editors rather than editing the wrong clip.
        model.setTargetClip(nullptr);
        model.addNote(64, 0.0, 1.0, 0.9f);
        check(model.getNotes().isEmpty(), "an audio track leaves the editors empty");
        check(target->getClip().getNumNotes() == 1, "no stray note leaks into another track");
    }

    // --- Patterns and the song timeline -------------------------------------
    {
        djr::Mixer patternMixer;
        patternMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(patternMixer);
        check(track != nullptr, "there is a MIDI track to hold patterns");

        if (track != nullptr)
        {
            // Each pattern keeps its own notes.
            track->setActivePattern(0);
            track->setClipNotes(makeFourBarChord());
            track->setActivePattern(1);
            check(track->getClip().getNumNotes() == 0, "a fresh pattern starts empty");

            juce::Array<djr::MidiNote> second;
            second.add({ 72, 0.9f, 0.0, 1.0 });
            track->setClipNotes(second);

            check(track->getClip(0).getNumNotes() == 4, "pattern 1 keeps its own notes");
            check(track->getClip(1).getNumNotes() == 1, "pattern 2 keeps its own notes");
            check(track->patternHasContent(0) && track->patternHasContent(1),
                  "both patterns report content");
            check(! track->patternHasContent(2), "an untouched pattern reports empty");

            // Pattern mode plays whichever pattern is active.
            track->setActivePattern(1);
            check(! isSilent(renderPeak(patternMixer, true, 8)), "pattern mode plays the active pattern");

            track->setActivePattern(2);
            settle(patternMixer);
            check(isSilent(renderPeak(patternMixer, true, 8)), "an empty active pattern is silent");

            // --- Song mode plays placements, not the active pattern ---------
            patternMixer.setLiveMidiTarget(-1);

            const auto renderSong = [&] (double startBeat, int blocks)
            {
                juce::AudioBuffer<float> output(2, blockSize);
                const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (tempoBpm / 60.0);
                auto beat = startBeat;
                auto peak = 0.0f;

                for (int block = 0; block < blocks; ++block)
                {
                    djr::TrackPlaybackContext context;
                    context.sampleRate = sampleRate;
                    context.tempoBpm = tempoBpm;
                    context.startBeat = beat;
                    context.endBeat = beat + beatsPerBlock;
                    context.isPlaying = true;
                    context.songMode = true;

                    output.clear();
                    patternMixer.process(output, context);

                    for (int channel = 0; channel < output.getNumChannels(); ++channel)
                        peak = std::max(peak, output.getMagnitude(channel, 0, output.getNumSamples()));

                    beat = context.endBeat;
                }

                return peak;
            };

            check(isSilent(renderSong(0.0, 6)), "song mode is silent with no placements");

            djr::PatternPlacement placement;
            placement.patternIndex = 0;   // the four bar chord
            placement.startBeat = 8.0;
            placement.lengthBeats = 4.0;
            track->addPlacement(placement);

            check(track->getPlacements().size() == 1u, "the placement is on the track");
            check(isSilent(renderSong(0.0, 6)), "nothing plays before the placement starts");
            check(! isSilent(renderSong(8.0, 8)), "the placement plays where it was put");

            check(track->findPlacementAt(9.0) == 0, "a beat inside the placement finds it");
            check(track->findPlacementAt(20.0) == -1, "a beat outside finds nothing");

            // A placement must not leak past its own length.
            settle(patternMixer);
            check(isSilent(renderSong(13.0, 6)), "the placement stops at its end");

            // --- Trimming a placement ---------------------------------------
            // Pattern 0 is the four bar chord: one note per beat, beats 0..3.
            {
                auto trimmed = track->getPlacement(0);
                check(std::abs(trimmed.startBeat - 8.0) < 1.0e-6, "the placement starts where it was put");

                // Right edge back to one beat: only the first note survives.
                trimmed.trimEnd(9.0);
                check(std::abs(trimmed.lengthBeats - 1.0) < 1.0e-6, "trimming the end shortens the placement");
                track->updatePlacement(0, trimmed);

                check(! isSilent(renderSong(8.0, 3)), "the surviving note still plays");
                settle(patternMixer);
                check(isSilent(renderSong(9.5, 6)), "the trimmed-off tail is gone");

                // Left edge in by one beat: the placement starts on the second note.
                trimmed = track->getPlacement(0);
                trimmed.trimEnd(12.0);
                trimmed.trimStart(9.0);
                track->updatePlacement(0, trimmed);

                const auto shifted = track->getPlacement(0);
                check(std::abs(shifted.startBeat - 9.0) < 1.0e-6, "the left edge moved");
                check(std::abs(shifted.sourceOffsetBeats - 1.0) < 1.0e-6,
                      "trimming the start skips into the pattern instead of dropping notes");
                check(std::abs(shifted.lengthBeats - 3.0) < 1.0e-6, "and the placement got shorter");

                settle(patternMixer);
                check(isSilent(renderSong(8.0, 2)), "nothing plays in the trimmed-off head");
                check(! isSilent(renderSong(9.0, 3)), "the placement plays from its new start");

                // Trimming can never invert or erase the placement.
                auto over = track->getPlacement(0);
                over.trimStart(999.0);
                check(over.lengthBeats >= djr::PatternPlacement::minimumLengthBeats,
                      "over-trimming the start stops at the minimum length");

                over = track->getPlacement(0);
                over.trimEnd(-50.0);
                check(over.lengthBeats >= djr::PatternPlacement::minimumLengthBeats,
                      "over-trimming the end stops at the minimum length");

                // The pattern itself must be untouched by any of this.
                check(track->getClip(0).getNumNotes() == 4,
                      "trimming a placement never edits the pattern's notes");
            }

            check(track->removePlacementAt(0), "a placement can be removed");
            check(track->getPlacements().empty(), "removing it empties the timeline");

            // --- What the paint tool asks before dropping a clip -------------
            {
                track->addPlacement({ 0, 4.0, 4.0, 0.0, false });

                check(track->getFreeSpanFrom(4.0) <= 0.0, "painting onto a clip is refused");
                check(track->getFreeSpanFrom(6.0) <= 0.0, "so is painting into its middle");
                check(std::abs(track->getFreeSpanFrom(2.0) - 2.0) < 1.0e-9,
                      "a two beat gap in front reports two beats");
                check(track->getFreeSpanFrom(8.0) > 1.0e6, "past the last clip the lane is wide open");
                check(std::abs(track->getFreeSpanFrom(0.0) - 4.0) < 1.0e-9,
                      "and the gap at the front is four beats");

                // The nearest placement wins, not the first one found.
                track->addPlacement({ 0, 12.0, 4.0, 0.0, false });
                check(std::abs(track->getFreeSpanFrom(8.0) - 4.0) < 1.0e-9,
                      "the gap between two clips measures to the nearer one");

                track->clearPlacements();
                check(track->getPlacements().empty(), "the paint check leaves nothing behind");
            }
        }
    }

    // --- Drum pads: the step sequencer's data path --------------------------
    {
        djr::Mixer drumMixer;
        drumMixer.prepare(sampleRate, blockSize);

        auto* drums = dynamic_cast<djr::MidiTrack*>(drumMixer.getTrack(0));
        check(drums != nullptr, "the default Drums track is a MIDI track");
        check(drums != nullptr && drums->getName() == "Drums", "and it is still called Drums");
        check(drums != nullptr && drums->isPreviewDrumKit(),
              "the Drums track answers with the drum kit, not the tonal voice");

        if (drums != nullptr)
        {
            djr::PianoRollModel pads;
            pads.setTargetClip(&drums->getClip());

            // A pad writes a 1/16 note at the step it sits on.
            juce::Array<djr::MidiNote> pattern;
            pattern.add({ 36, 0.9f, 0.0,  0.25 });   // kick, step 1
            pattern.add({ 42, 0.9f, 0.25, 0.25 });   // hat,  step 2
            pattern.add({ 38, 0.9f, 1.0,  0.25 });   // snare, step 5
            pads.setNotes(pattern);

            check(drums->getClip().getNumNotes() == 3, "pads land in the track's clip");
            check(! isSilent(renderPeak(drumMixer, true, 10)), "the drum pattern is audible");

            // Each kit piece must actually make a different sound.
            const auto renderOne = [&] (int pitch)
            {
                juce::Array<djr::MidiNote> single;
                single.add({ pitch, 1.0f, 0.0, 0.25 });
                pads.setNotes(single);
                settle(drumMixer);
                return renderPeak(drumMixer, true, 6);
            };

            const auto kick = renderOne(36);
            const auto hat = renderOne(42);
            check(! isSilent(kick), "the kick pad sounds");
            check(! isSilent(hat), "the hat pad sounds");
            check(std::abs(kick - hat) > 1.0e-4f, "kick and hat are not the same sound");

            // A tonal track must be unaffected by the drum mapping.
            auto* bass = dynamic_cast<djr::MidiTrack*>(drumMixer.getTrack(1));
            check(bass != nullptr && ! bass->isPreviewDrumKit(),
                  "other MIDI tracks stay tonal");
        }
    }

    // --- Mute and slice, the model behind the playlist tools -----------------
    {
        djr::Mixer toolMixer;
        toolMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(toolMixer);
        check(track != nullptr, "there is a MIDI track for the tool checks");

        if (track != nullptr)
        {
            track->setActivePattern(0);
            track->setClipNotes(makeFourBarChord());

            const auto renderSong = [&] (double startBeat, int blocks)
            {
                juce::AudioBuffer<float> output(2, blockSize);
                const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (tempoBpm / 60.0);
                auto beat = startBeat;
                auto peak = 0.0f;

                for (int block = 0; block < blocks; ++block)
                {
                    djr::TrackPlaybackContext context;
                    context.sampleRate = sampleRate;
                    context.tempoBpm = tempoBpm;
                    context.startBeat = beat;
                    context.endBeat = beat + beatsPerBlock;
                    context.isPlaying = true;
                    context.songMode = true;

                    output.clear();
                    toolMixer.process(output, context);

                    for (int channel = 0; channel < output.getNumChannels(); ++channel)
                        peak = std::max(peak, output.getMagnitude(channel, 0, output.getNumSamples()));

                    beat = context.endBeat;
                }

                return peak;
            };

            track->addPlacement({ 0, 0.0, 4.0, 0.0, false });
            check(! isSilent(renderSong(0.0, 8)), "the placement plays before muting");

            // --- Mute tool ------------------------------------------------
            auto placement = track->getPlacement(0);
            placement.muted = true;
            track->updatePlacement(0, placement);

            settle(toolMixer);
            check(isSilent(renderSong(0.0, 8)), "a muted placement makes no sound");
            check(track->getPlacements().size() == 1u, "but it is still on the timeline");

            placement.muted = false;
            track->updatePlacement(0, placement);
            check(! isSilent(renderSong(0.0, 8)), "unmuting brings it back");

            // --- Slice tool -----------------------------------------------
            auto left = track->getPlacement(0);
            djr::PatternPlacement right;

            check(left.splitAt(2.0, right), "a placement can be sliced in the middle");
            check(std::abs(left.lengthBeats - 2.0) < 1.0e-6, "the left piece ends at the cut");
            check(std::abs(right.startBeat - 2.0) < 1.0e-6, "the right piece starts at the cut");
            check(std::abs(right.sourceOffsetBeats - 2.0) < 1.0e-6,
                  "the right piece continues the pattern where the left one stopped");
            check(std::abs(right.lengthBeats - 2.0) < 1.0e-6, "and covers the rest");

            // A cut outside the clip must be refused, not silently accepted.
            auto untouched = track->getPlacement(0);
            djr::PatternPlacement ignored;
            check(! untouched.splitAt(0.0, ignored), "a cut at the very start is refused");
            check(! untouched.splitAt(99.0, ignored), "a cut past the end is refused");
            check(std::abs(untouched.lengthBeats - 4.0) < 1.0e-6, "a refused cut changes nothing");
        }
    }

    // --- Pattern length: manual, or following the notes ----------------------
    {
        djr::SessionState session;

        check(std::abs(session.getPatternLengthBeats(0)) < 1.0e-9,
              "a pattern starts on auto length");

        session.setPatternLengthBeats(0, 8.0);
        session.dispatchPendingMessages();
        check(std::abs(session.getPatternLengthBeats(0) - 8.0) < 1.0e-9,
              "a pattern length can be pinned");

        session.setPatternLengthBeats(0, 0.0);
        check(std::abs(session.getPatternLengthBeats(0)) < 1.0e-9,
              "zero puts it back on auto");

        // Lengths are per pattern, not shared.
        session.setPatternLengthBeats(3, 16.0);
        check(std::abs(session.getPatternLengthBeats(3) - 16.0) < 1.0e-9,
              "another pattern keeps its own length");
        check(std::abs(session.getPatternLengthBeats(0)) < 1.0e-9,
              "and does not disturb the first");

        // Out of range must be ignored rather than corrupting anything.
        session.setPatternLengthBeats(-1, 8.0);
        session.setPatternLengthBeats(999, 8.0);
        check(std::abs(session.getPatternLengthBeats(3) - 16.0) < 1.0e-9,
              "an out-of-range pattern index is ignored");
    }

    // --- The loop wraps at the pattern's length, not a fixed 16 beats --------
    {
        djr::Transport loopTransport;
        loopTransport.setTempoBpm(120.0);
        loopTransport.setLoopEnabled(true);

        // This is the bug that was fixed: a one bar pattern used to be followed
        // by three bars of silence because the loop was pinned at 16 beats.
        loopTransport.setLoopRangeBeats(0.0, 4.0);
        loopTransport.setPositionBeats(3.9);
        loopTransport.play();

        // Advance a little over a tenth of a beat at 120 bpm.
        loopTransport.advanceSamples(4410, sampleRate);

        check(loopTransport.getPositionBeats() < 4.0,
              "a one bar loop wraps at bar 1, not bar 4");
        check(loopTransport.getPositionBeats() >= 0.0,
              "and it wraps forwards, not into negative time");

        loopTransport.setLoopRangeBeats(0.0, 16.0);
        loopTransport.setPositionBeats(3.9);
        loopTransport.advanceSamples(4410, sampleRate);
        check(loopTransport.getPositionBeats() > 4.0,
              "a four bar loop keeps playing past bar 1");

        loopTransport.stop();
    }

    // --- Selection is announced once, to everyone ---------------------------
    {
        djr::SessionState session;

        struct Watcher final : public juce::ChangeListener
        {
            void changeListenerCallback(juce::ChangeBroadcaster*) override { ++hits; }
            int hits = 0;
        };

        Watcher first;
        Watcher second;
        session.addChangeListener(&first);
        session.addChangeListener(&second);

        session.setSelectedTrack(3);
        // ChangeBroadcaster queues its callbacks; deliver them now.
        session.dispatchPendingMessages();

        check(session.getSelectedTrack() == 3, "the session remembers the selection");
        check(first.hits == 1 && second.hits == 1, "every listener is told exactly once");

        session.setSelectedTrack(3);
        session.dispatchPendingMessages();
        check(first.hits == 1, "re-selecting the same track does not re-broadcast");

        session.removeChangeListener(&first);
        session.removeChangeListener(&second);
    }

    // --- Export renders real audio, not silence -----------------------------
    {
        djr::Mixer exportMixer;
        exportMixer.prepare(sampleRate, blockSize);

        auto* voice = findFirstMidiTrack(exportMixer);
        check(voice != nullptr, "there is a track to export");

        if (voice != nullptr)
        {
            voice->setClipNotes(makeFourBarChord());

            const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile("djr_engine_test_export.wav");
            wav.deleteFile();

            djr::ExportManager exporter;
            djr::ExportManager::Options options;
            options.sampleRate = sampleRate;
            options.blockSize = blockSize;
            options.tempoBpm = tempoBpm;
            options.lengthBeats = 4.0;
            options.songMode = false;

            juce::String error;
            auto lastProgress = 0.0;
            const auto rendered = exporter.render(exportMixer, wav, options, error,
                                                  [&lastProgress] (double p) { lastProgress = p; });

            check(rendered, "export reports success" + (error.isEmpty() ? juce::String() : ": " + error));
            check(wav.existsAsFile(), "the export file exists");
            check(lastProgress > 0.99, "progress reaches the end");

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));

            check(reader != nullptr, "the export can be read back");

            if (reader != nullptr)
            {
                check(reader->numChannels == 2, "the export is stereo");
                check(std::abs(reader->sampleRate - sampleRate) < 1.0, "the export keeps the sample rate");

                // Two bars at 120 BPM is 2 seconds, plus the decay tail.
                check(reader->lengthInSamples > static_cast<juce::int64>(sampleRate * 1.9),
                      "the export is at least as long as the song");

                juce::AudioBuffer<float> readBack(static_cast<int>(reader->numChannels),
                                                  static_cast<int>(reader->lengthInSamples));
                reader->read(&readBack, 0, readBack.getNumSamples(), 0, true, true);

                auto peak = 0.0f;
                for (int channel = 0; channel < readBack.getNumChannels(); ++channel)
                    peak = std::max(peak, readBack.getMagnitude(channel, 0, readBack.getNumSamples()));

                check(! isSilent(peak), "the export contains audio, not silence");
            }

            reader.reset();
            wav.deleteFile();

            // A zero length request must be refused rather than writing junk.
            options.lengthBeats = 0.0;
            juce::String zeroError;
            check(! exporter.render(exportMixer, wav, options, zeroError),
                  "a zero length export is refused");
            check(zeroError.isNotEmpty(), "and it says why");
        }
    }

    // --- Recording writes a real file ---------------------------------------
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("djr_engine_test_take.wav");
        wav.deleteFile();

        djr::Recorder recorder;
        check(recorder.startRecording(wav, sampleRate, 2), "recorder opens a wav file");

        juce::AudioBuffer<float> tone(2, blockSize);
        double phase = 0.0;

        for (int block = 0; block < 90; ++block)
        {
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto value = static_cast<float>(std::sin(phase)) * 0.5f;
                phase += juce::MathConstants<double>::twoPi * 440.0 / sampleRate;

                for (int channel = 0; channel < tone.getNumChannels(); ++channel)
                    tone.setSample(channel, sample, value);
            }

            recorder.processInputBlock(tone.getArrayOfReadPointers(), 2, blockSize);
        }

        check(recorder.isRecording(), "recorder reports itself active");
        recorder.stop();
        check(! recorder.isRecording(), "stop closes the recorder");

        check(wav.existsAsFile() && wav.getSize() > 1024, "the take is written to disk");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        if (auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(wav)))
        {
            check(reader->numChannels == 2, "the take is stereo");
            check(std::abs(reader->sampleRate - sampleRate) < 1.0, "the take keeps the device sample rate");

            juce::AudioBuffer<float> readBack(static_cast<int>(reader->numChannels), blockSize);
            reader->read(&readBack, 0, blockSize, blockSize * 10, true, true);
            check(! isSilent(readBack.getMagnitude(0, 0, blockSize)), "the take contains the audio we fed in");
        }
        else
        {
            check(false, "the take can be read back");
        }

        wav.deleteFile();
    }

    // --- MIDI recording captures what was played ----------------------------
    {
        juce::AudioDeviceManager devices;
        djr::Transport recordTransport;
        djr::MidiEngine midi(devices, recordTransport);

        midi.setRecordingArmed(true);
        recordTransport.setPositionBeats(2.0);
        midi.postLiveMessage(juce::MidiMessage::noteOn(1, 60, 0.8f));

        recordTransport.setPositionBeats(3.0);
        midi.postLiveMessage(juce::MidiMessage::noteOff(1, 60));

        const auto captured = midi.takeRecordedNotes();
        check(captured.size() == 1, "a played note is captured while armed");

        if (! captured.isEmpty())
        {
            const auto& note = captured.getReference(0);
            check(note.pitch == 60, "the captured note keeps its pitch");
            check(std::abs(note.startBeat - 2.0) < 1.0e-6, "the note starts where the transport was");
            check(std::abs(note.lengthBeats - 1.0) < 1.0e-6, "the note length matches how long it was held");
        }

        check(midi.takeRecordedNotes().isEmpty(), "collecting drains the queue");

        midi.setRecordingArmed(false);
        midi.discardRecordedNotes();
        midi.postLiveMessage(juce::MidiMessage::noteOn(1, 62, 0.8f));
        midi.postLiveMessage(juce::MidiMessage::noteOff(1, 62));
        check(midi.takeRecordedNotes().isEmpty(), "nothing is captured once disarmed");
    }

    // --- Project round trip: what survives closing the app ------------------
    {
        djr::Project saved;
        saved.tempo = 128.0;

        saved.patternNames.add("Intro");
        saved.patternNames.add("");          // never renamed, keeps its default
        saved.patternLengths.add(8.0);       // pinned to two bars
        saved.patternLengths.add(0.0);       // follows its notes

        djr::ProjectTrackState drums;
        drums.name = "Drums";
        drums.type = "midi";
        drums.laneHeight = 72;
        saved.tracks.add(drums);

        djr::ProjectTrackState bass;
        bass.name = "Bass";
        bass.type = "midi";
        bass.laneHeight = 0;                 // never resized
        saved.tracks.add(bass);

        djr::Project loaded;
        loaded.fromVar(saved.toVar());

        check(loaded.patternNames.size() >= 2 && loaded.patternNames[0] == "Intro",
              "a pattern name survives a save and load");
        check(loaded.patternNames.size() >= 2 && loaded.patternNames[1].isEmpty(),
              "an unnamed pattern stays unnamed");
        check(loaded.patternLengths.size() >= 2 && std::abs(loaded.patternLengths[0] - 8.0) < 1.0e-9,
              "a pinned pattern length survives");
        check(loaded.patternLengths.size() >= 2 && loaded.patternLengths[1] <= 0.0,
              "an auto length stays auto");
        check(loaded.tracks.size() == 2, "both tracks come back");
        check(loaded.tracks.size() == 2 && loaded.tracks.getReference(0).laneHeight == 72,
              "a resized lane keeps its height");
        check(loaded.tracks.size() == 2 && loaded.tracks.getReference(1).laneHeight == 0,
              "an untouched lane stays on the default");

        // A file written before these fields existed must still open.
        auto* legacy = new juce::DynamicObject();
        legacy->setProperty("name", "Old Project");
        legacy->setProperty("tempo", 120.0);
        djr::Project old;
        old.fromVar(juce::var(legacy));
        check(old.patternNames.isEmpty() && old.patternLengths.isEmpty(),
              "an older project without the new fields still loads");
    }

    // --- A muted note is drawn but never played -----------------------------
    {
        djr::Mixer muteMixer;
        muteMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(muteMixer);
        check(track != nullptr, "there is a MIDI track for the mute check");

        if (track != nullptr)
        {
            juce::Array<djr::MidiNote> notes;
            notes.add({ 60, 0.9f, 0.0, 2.0, false });
            track->setClipNotes(notes);

            settle(muteMixer);
            check(! isSilent(renderPeak(muteMixer, true, 8)), "an unmuted note sounds");

            notes.getReference(0).muted = true;
            track->setClipNotes(notes);

            settle(muteMixer);
            check(isSilent(renderPeak(muteMixer, true, 8)), "a muted note makes no sound");
            check(track->getClip().getNumNotes() == 1, "but it is still in the clip");

            // And it survives a save and load as muted.
            const auto restored = djr::MidiNote::fromVar(notes.getReference(0).toVar());
            check(restored.muted, "the muted flag survives a save and load");
        }
    }

    // --- Metronome and count-in ---------------------------------------------
    {
        djr::Metronome metronome;
        metronome.prepare(sampleRate);

        juce::AudioBuffer<float> clickBuffer(2, blockSize);

        // One block long enough to contain a beat at 120 bpm: half a second.
        const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (120.0 / 60.0);

        const auto renderBlock = [&] (double startBeat)
        {
            clickBuffer.clear();
            metronome.process(clickBuffer, startBeat, startBeat + beatsPerBlock, 4.0);
            return clickBuffer.getMagnitude(0, blockSize);
        };

        check(! metronome.isEnabled(), "the click starts switched off");
        check(isSilent(renderBlock(0.0)), "a disabled metronome writes nothing");

        metronome.setEnabled(true);
        check(metronome.isEnabled(), "the click can be switched on");

        // Beat zero falls inside this block, so it must click.
        check(renderBlock(0.0) > 0.0f, "a beat inside the block produces a click");

        // A block that contains no whole beat, with nothing still ringing.
        metronome.reset();
        check(isSilent(renderBlock(0.30)), "a block between beats stays silent");

        // --- Count-in ----------------------------------------------------
        metronome.setEnabled(false);
        metronome.reset();
        check(! metronome.isCountingIn(), "nothing is counting in yet");

        metronome.startCountIn(4, 120.0, 4.0);
        check(metronome.isCountingIn(), "a requested count-in is pending");

        // The count-in clicks even though the metronome itself is off and the
        // transport is parked: same start and end beat.
        clickBuffer.clear();
        metronome.process(clickBuffer, 0.0, 0.0, 4.0);
        check(clickBuffer.getMagnitude(0, blockSize) > 0.0f,
              "the count-in clicks with the transport stopped and the click off");

        // Drive it to the end; four beats at 120 bpm is two seconds.
        auto guard = 0;
        while (metronome.isCountingIn() && guard < 1000)
        {
            clickBuffer.clear();
            metronome.process(clickBuffer, 0.0, 0.0, 4.0);
            ++guard;
        }

        check(! metronome.isCountingIn(), "the count-in finishes on its own");
        check(guard > 1 && guard < 1000, "and it takes a sensible number of blocks");

        metronome.startCountIn(4, 120.0, 4.0);
        metronome.cancelCountIn();
        check(! metronome.isCountingIn(), "a count-in can be cancelled");
    }

    // --- Time signature -----------------------------------------------------
    {
        djr::Transport sigTransport;

        check(sigTransport.getTimeSignatureNumerator() == 4
              && sigTransport.getTimeSignatureDenominator() == 4,
              "a new transport starts in 4/4");
        check(std::abs(sigTransport.getBeatsPerBar() - 4.0) < 1.0e-9, "4/4 is four beats to the bar");

        sigTransport.setTimeSignature(3, 4);
        check(std::abs(sigTransport.getBeatsPerBar() - 3.0) < 1.0e-9, "3/4 is three beats");

        // Beats are quarter notes, so eighth note signatures are not just the
        // numerator: 6/8 is six eighths, which is three beats.
        sigTransport.setTimeSignature(6, 8);
        check(std::abs(sigTransport.getBeatsPerBar() - 3.0) < 1.0e-9, "6/8 is three beats, not six");

        sigTransport.setTimeSignature(7, 8);
        check(std::abs(sigTransport.getBeatsPerBar() - 3.5) < 1.0e-9, "7/8 is three and a half beats");

        sigTransport.setTimeSignature(5, 4);
        check(std::abs(sigTransport.getBeatsPerBar() - 5.0) < 1.0e-9, "5/4 is five beats");

        // A denominator that is not a power of two would give a nonsense bar.
        sigTransport.setTimeSignature(4, 5);
        check(sigTransport.getTimeSignatureDenominator() == 4, "an odd denominator falls back to quarters");

        sigTransport.setTimeSignature(0, 4);
        check(sigTransport.getTimeSignatureNumerator() >= 1, "a bar always has at least one beat");

        // --- It survives a save and load ---------------------------------
        djr::Project saved;
        saved.timeSigNumerator = 7;
        saved.timeSigDenominator = 8;

        djr::Project loaded;
        loaded.fromVar(saved.toVar());
        check(loaded.timeSigNumerator == 7 && loaded.timeSigDenominator == 8,
              "the time signature survives a save and load");

        auto* legacy = new juce::DynamicObject();
        legacy->setProperty("tempo", 120.0);
        djr::Project old;
        old.fromVar(juce::var(legacy));
        check(old.timeSigNumerator == 4 && old.timeSigDenominator == 4,
              "a project saved before signatures existed opens as 4/4");
    }

    // --- Undo and redo ------------------------------------------------------
    {
        djr::Mixer undoMixer;
        undoMixer.prepare(sampleRate, blockSize);

        auto* track = dynamic_cast<djr::MidiTrack*>(undoMixer.getTrack(0));
        check(track != nullptr, "there is a MIDI track for the undo checks");

        if (track != nullptr)
        {
            djr::EditHistory history(undoMixer);

            check(! history.canUndo(), "a fresh history has nothing to undo");
            check(! history.canRedo(), "and nothing to redo");
            check(! history.undo(), "undo on an empty history is refused");

            // --- Placements -------------------------------------------------
            track->clearPlacements();
            history.pushSnapshot("Taruh clip");
            track->addPlacement({ 0, 4.0, 4.0, 0.0, false });
            check(track->getPlacements().size() == 1u, "the clip is placed");
            check(history.canUndo(), "and there is something to undo");
            check(history.getUndoName() == "Taruh clip", "the step remembers its name");

            check(history.undo(), "undo runs");
            check(track->getPlacements().empty(), "undo takes the clip away again");
            check(history.canRedo(), "redo becomes available");

            check(history.redo(), "redo runs");
            check(track->getPlacements().size() == 1u, "redo puts the clip back");

            // --- Notes, and that undo does not disturb its neighbours --------
            juce::Array<djr::MidiNote> notes;
            notes.add({ 60, 0.8f, 0.0, 1.0 });
            track->getClip(0).setNotes(notes);

            history.pushSnapshot("Tambah note");
            notes.add({ 64, 0.8f, 1.0, 1.0 });
            track->getClip(0).setNotes(notes);
            check(track->getClip(0).getNumNotes() == 2, "the second note is in");

            check(history.undo(), "undo runs for notes");
            check(track->getClip(0).getNumNotes() == 1, "undo removes only the new note");
            check(track->getPlacements().size() == 1u, "and leaves the placement alone");

            // --- A new edit throws away the redo branch ---------------------
            check(history.canRedo(), "the note edit can be redone");
            history.pushSnapshot("Hapus clip");
            track->clearPlacements();
            check(! history.canRedo(), "a fresh edit drops the redo branch");
            check(history.undo() && track->getPlacements().size() == 1u,
                  "the clip comes back after undoing its removal");

            // --- Depth is bounded -------------------------------------------
            history.clear();
            check(! history.canUndo(), "clear empties the history");

            for (int i = 0; i < djr::EditHistory::maxDepth + 20; ++i)
            {
                history.pushSnapshot("Geser clip");
                auto placement = track->getPlacement(0);
                placement.startBeat += 1.0;
                track->updatePlacement(0, placement);
            }

            auto steps = 0;
            while (history.undo())
                ++steps;

            check(steps == djr::EditHistory::maxDepth,
                  "the history stops at its depth limit instead of growing forever");
        }
    }

    // --- Undo covers song settings, but only what it also records -----------
    {
        djr::Mixer settingsMixer;
        settingsMixer.prepare(sampleRate, blockSize);

        djr::Transport settingsTransport;
        djr::SessionState settingsSession;
        djr::EditHistory history(settingsMixer, &settingsTransport, &settingsSession);

        // --- Time signature ---------------------------------------------
        settingsTransport.setTimeSignature(4, 4);
        history.pushSnapshot("Time signature");
        settingsTransport.setTimeSignature(7, 8);
        check(settingsTransport.getTimeSignatureNumerator() == 7, "the signature changed to 7/8");

        check(history.undo(), "undo runs for the signature");
        check(settingsTransport.getTimeSignatureNumerator() == 4
              && settingsTransport.getTimeSignatureDenominator() == 4,
              "undo puts the time signature back");

        // --- Pattern name and length -------------------------------------
        settingsSession.setPatternName(0, "Intro");
        settingsSession.setPatternLengthBeats(0, 8.0);

        history.pushSnapshot("Ganti nama pattern");
        settingsSession.setPatternName(0, "Verse");
        check(settingsSession.getPatternName(0) == "Verse", "the pattern was renamed");

        check(history.undo(), "undo runs for the rename");
        check(settingsSession.getPatternName(0) == "Intro", "undo restores the old name");
        check(std::abs(settingsSession.getPatternLengthBeats(0) - 8.0) < 1.0e-9,
              "and leaves the pattern length alone");

        // --- Tempo is deliberately NOT restored ---------------------------
        // Restoring a value that is never recorded would let an unrelated undo
        // drag it back without the user asking.
        settingsTransport.setTempoBpm(140.0);
        history.pushSnapshot("Taruh clip");
        settingsTransport.setTempoBpm(90.0);

        check(history.undo(), "undo runs after a tempo change");
        check(std::abs(settingsTransport.getTempoBpm() - 90.0) < 1.0e-9,
              "undo leaves tempo exactly where the user put it");
    }

    std::cout << (failures == 0 ? "\nAll engine tests passed\n"
                                : "\n" + std::to_string(failures) + " engine test(s) failed\n");
    return failures == 0 ? 0 : 1;
}
