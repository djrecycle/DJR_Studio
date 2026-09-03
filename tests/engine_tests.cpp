// Offline checks for the audio graph. No audio device is involved: the mixer is
// driven by hand so the assertions are deterministic.

#include "audio/AlignmentDelay.h"
#include "audio/TimeStretch.h"
#include "audio/AudioAnalysis.h"
#include "audio/AudioClip.h"
#include "audio/AutomationLane.h"
#include "audio/ChannelSettings.h"
#include "audio/BusTrack.h"
#include "audio/Transport.h"
#include "audio/AudioTrack.h"
#include "audio/MidiTrack.h"
#include "audio/Mixer.h"
#include "audio/SimpleSynth.h"
#include "app/SessionState.h"
#include "midi/Chord.h"
#include "midi/MidiEngine.h"
#include "midi/PianoRollModel.h"
#include "export/ExportManager.h"
#include "export/TrackRenderer.h"
#include "project/Project.h"
#include "project/ProjectTrackLayout.h"
#include "app/EditHistory.h"
#include "ui/Theme.h"
#include "audio/Metronome.h"
#include "recording/Recorder.h"
#include "recording/SampleCapture.h"

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
                     const juce::MidiBuffer* liveMidi = nullptr,
                     // Clips on the timeline only play in Song mode, the same
                     // way patterns only play in Pattern mode. The default is
                     // Pattern because that is what most of these tests drive.
                     bool songMode = false)
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
            context.songMode = songMode;
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

            // Fades -----------------------------------------------------------
            {
                // A tone starting at full amplitude is exactly the click a fade
                // exists to remove, so the first block is where it shows.
                const auto blockSeconds = static_cast<double>(blockSize) / sampleRate;

                juce::AudioBuffer<float> plain(2, blockSize);
                plain.clear();
                clip->setStartBeat(0.0);
                clip->addToBuffer(plain, 0.0, 120.0, sampleRate);
                const auto openingWithoutFade = plain.getMagnitude(0, 0, blockSize);

                auto faded = clip->duplicate();
                check(faded != nullptr, "a clip can be duplicated to fade separately");

                if (faded != nullptr)
                {
                    // Long enough that the whole first block sits inside it.
                    faded->setFadeInSeconds(blockSeconds * 4.0);
                    check(std::abs(faded->getFadeInSeconds() - blockSeconds * 4.0) < 1.0e-9,
                          "a fade in is remembered");

                    juce::AudioBuffer<float> ramped(2, blockSize);
                    ramped.clear();
                    faded->addToBuffer(ramped, 0.0, 120.0, sampleRate);

                    check(ramped.getMagnitude(0, 0, blockSize) < openingWithoutFade * 0.5f,
                          "a fade in makes the clip's opening quieter");
                    check(std::abs(ramped.getSample(0, 0)) < 1.0e-4f,
                          "and starts it from silence");

                    // Asking for more than the clip holds is clamped, not obeyed.
                    faded->setFadeOutSeconds(9999.0);
                    check(faded->getFadeOutSeconds() <= faded->getPlayLengthSeconds() + 1.0e-9,
                          "a fade never outruns the clip it is on");

                    // The other edge. The last block of a two second clip at
                    // 120 BPM sits just under four beats in.
                    const auto endBeat = (2.0 - blockSeconds) * 2.0;

                    juce::AudioBuffer<float> tail(2, blockSize);
                    tail.clear();
                    clip->addToBuffer(tail, endBeat, 120.0, sampleRate);
                    const auto endingWithoutFade = tail.getMagnitude(0, 0, blockSize);

                    auto fadedOut = clip->duplicate();

                    if (fadedOut != nullptr)
                    {
                        fadedOut->setFadeOutSeconds(blockSeconds * 4.0);

                        juce::AudioBuffer<float> quietTail(2, blockSize);
                        quietTail.clear();
                        fadedOut->addToBuffer(quietTail, endBeat, 120.0, sampleRate);

                        check(endingWithoutFade > 0.0f
                                  && quietTail.getMagnitude(0, 0, blockSize) < endingWithoutFade * 0.5f,
                              "a fade out makes the clip's ending quieter");
                    }
                }

                // A fade that vanishes when the project is reopened is worse
                // than no fade: the click comes back and nothing says why.
                {
                    clip->setFadeInSeconds(0.05);
                    clip->setFadeOutSeconds(0.2);

                    juce::String reloadError;
                    auto reopened = djr::AudioClip::createFromFile(wav, sampleRate, formats, reloadError);
                    check(reopened != nullptr, "the clip's file loads a second time");

                    if (reopened != nullptr)
                    {
                        reopened->applyStateFromVar(clip->toVar());
                        check(std::abs(reopened->getFadeInSeconds() - 0.05) < 1.0e-6,
                              "a fade in survives saving and reopening");
                        check(std::abs(reopened->getFadeOutSeconds() - 0.2) < 1.0e-6,
                              "a fade out survives saving and reopening");
                    }

                    // The rest of this block expects the clip as it was.
                    clip->setFadeInSeconds(0.0);
                    clip->setFadeOutSeconds(0.0);
                }

                // A project saved before gain existed has no "gain" key at
                // all. getProperty on a missing key returns a void var,
                // which casts to 0.0 - silencing every clip from an older
                // project unless the fallback is unity instead.
                {
                    auto stateVar = clip->toVar();

                    if (auto* stateObject = stateVar.getDynamicObject())
                        stateObject->removeProperty("gain");

                    juce::String reloadError;
                    auto reopened = djr::AudioClip::createFromFile(wav, sampleRate, formats, reloadError);
                    check(reopened != nullptr, "the clip's file loads a third time");

                    if (reopened != nullptr)
                    {
                        reopened->setGain(0.4f);
                        reopened->applyStateFromVar(stateVar);
                        check(std::abs(reopened->getGain() - 1.0f) < 1.0e-6f,
                              "state missing gain falls back to unity, not silence");
                    }
                }

                // Warp modes, on the clip rather than on the stretcher alone.
                // The source is a 220 Hz tone recorded at 120 BPM; played at
                // 240 the two modes must disagree about the pitch.
                {
                    const auto readPitch = [&] (double tempo)
                    {
                        // Enough blocks to cover a good stretch of the clip.
                        const auto blocks = 60;
                        juce::AudioBuffer<float> rendered(2, blockSize * blocks);
                        rendered.clear();

                        const auto beatsPerBlock = static_cast<double>(blockSize) / sampleRate
                                                 * (tempo / 60.0);

                        for (int block = 0; block < blocks; ++block)
                        {
                            juce::AudioBuffer<float> one(2, blockSize);
                            one.clear();
                            clip->addToBuffer(one, block * beatsPerBlock, tempo, sampleRate);

                            for (int channel = 0; channel < 2; ++channel)
                                rendered.copyFrom(channel, block * blockSize, one, channel, 0, blockSize);
                        }

                        auto crossings = 0;

                        for (int i = 1; i < rendered.getNumSamples(); ++i)
                            if (rendered.getSample(0, i - 1) <= 0.0f && rendered.getSample(0, i) > 0.0f)
                                ++crossings;

                        return crossings / (rendered.getNumSamples() / sampleRate);
                    };

                    clip->setStartBeat(0.0);
                    clip->setOriginalTempo(120.0);
                    clip->setWarpEnabled(true);

                    clip->setWarpMode(djr::AudioClip::WarpMode::resample);
                    const auto resampledPitch = readPitch(240.0);
                    check(resampledPitch > 300.0,
                          "resampling warp drags the pitch up with the tempo");

                    clip->setWarpMode(djr::AudioClip::WarpMode::stretch);
                    clip->prepareWarp(240.0);
                    check(clip->isWarpPrepared(240.0),
                          "the stretched copy is built for the tempo asked for");

                    const auto stretchedPitch = readPitch(240.0);
                    check(std::abs(stretchedPitch - 220.0) < 25.0,
                          "stretch warp plays the same clip at the same pitch");

                    // Back to how the rest of this block expects to find it.
                    clip->setWarpMode(djr::AudioClip::WarpMode::resample);
                    clip->setWarpEnabled(true);
                }
            }

            djr::Mixer audioMixer;
            audioMixer.prepare(sampleRate, blockSize);

            auto* onlyAudio = findFirstAudioTrack(audioMixer);
            check(onlyAudio != nullptr, "the session has an audio track to drop a clip on");

            if (onlyAudio != nullptr)
            {
                check(isSilent(renderPeak(audioMixer, true, 8, nullptr, 0.0, nullptr, true)), "an empty audio track is silent");

                clip->setStartBeat(4.0);
                onlyAudio->addClip(std::move(clip));
                check(onlyAudio->getNumClips() == 1, "the clip is on the track");

                // Before the clip starts there must be nothing.
                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 0.0, nullptr, true)),
                      "nothing plays before the clip's start beat");

                check(! isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0, nullptr, true)),
                      "the clip plays once the playhead reaches it");

                // The transport has two meanings and the timeline belongs to
                // one of them. Audio used to ignore that and play under both,
                // which made Pattern mode play half a song.
                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0, nullptr, false)),
                      "and stays quiet in Pattern mode, where the timeline is not what plays");

                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 40.0, nullptr, true)),
                      "nothing plays past the end of the clip");

                auto* placed = onlyAudio->getClip(0);
                check(placed != nullptr, "the placed clip can be edited");

                if (placed != nullptr)
                {
                    // --- Moving -------------------------------------------
                    placed->setStartBeat(12.0);
                    check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0, nullptr, true)),
                          "moving a clip takes the audio with it");
                    check(! isSilent(renderPeak(audioMixer, true, 6, nullptr, 12.0, nullptr, true)),
                          "the clip now plays at its new position");
                    placed->setStartBeat(4.0);

                    // --- Trimming the end ---------------------------------
                    const auto fullLength = placed->getPlayLengthSeconds();
                    placed->trimEnd(5.0, 120.0);
                    check(placed->getPlayLengthSeconds() < fullLength,
                          "trimming the end shortens the clip");
                    check(std::abs(placed->getLengthBeats(120.0) - 1.0) < 0.05,
                          "the trimmed clip covers the beats it was dragged to");
                    check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 6.0, nullptr, true)),
                          "the trimmed-off tail no longer sounds");

                    // --- Trimming the start -------------------------------
                    placed->trimEnd(8.0, 120.0);
                    placed->trimStart(5.0, 120.0);
                    check(placed->getSourceOffsetSeconds() > 0.0,
                          "trimming the start skips into the source");
                    check(std::abs(placed->getStartBeat() - 5.0) < 0.05,
                          "the clip now begins where the edge was dragged");
                    check(isSilent(renderPeak(audioMixer, true, 4, nullptr, 4.0, nullptr, true)),
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
                        check(! isSilent(renderPeak(audioMixer, true, 4, nullptr, 3.5, nullptr, true)),
                              "a warped clip is still sounding at the end of its span");
                    }
                }

                onlyAudio->clearClips();
                check(onlyAudio->getNumClips() == 0, "clips can be cleared");
                check(isSilent(renderPeak(audioMixer, true, 6, nullptr, 4.0, nullptr, true)),
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

    // --- The export tail keeps advancing, not stuck replaying one block -----
    // The bug: the render loop lost `beat = context.endBeat` in its tail
    // phase, so every tail block re-read the exact same source position. A
    // frozen track reads straight from its render at `context.startBeat`, so
    // freezing a source whose loudness ramps up over time makes a frozen
    // beat directly audible: the whole tail would stay as quiet as its very
    // first instant instead of following the ramp.
    {
        djr::Mixer tailMixer;
        tailMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(tailMixer);
        check(track != nullptr, "there is a track to freeze for the tail check");

        if (track != nullptr)
        {
            const auto sourceFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getChildFile("djr_engine_test_tail_source.wav");
            sourceFile.deleteFile();

            // Quiet for the first two seconds - what the main render plays -
            // then a steady amplitude ramp for two more, long enough to cover
            // the export's 1.5 second tail with room to spare.
            {
                juce::WavAudioFormat wavFormat;
                std::unique_ptr<juce::FileOutputStream> stream(sourceFile.createOutputStream());
                std::unique_ptr<juce::AudioFormatWriter> writer(
                    wavFormat.createWriterFor(stream.get(), sampleRate, 2, 16, {}, 0));

                check(writer != nullptr, "tail source writer opens");

                if (writer != nullptr)
                {
                    stream.release();

                    const auto totalSamples = static_cast<int>(sampleRate * 4.0);
                    juce::AudioBuffer<float> source(2, totalSamples);
                    double phase = 0.0;

                    for (int sample = 0; sample < totalSamples; ++sample)
                    {
                        const auto seconds = sample / sampleRate;
                        const auto amplitude = seconds < 2.0 ? 0.4 : 0.05 + (seconds - 2.0) / 2.0 * 0.85;
                        const auto value = static_cast<float>(std::sin(phase) * amplitude);
                        phase += juce::MathConstants<double>::twoPi * 220.0 / sampleRate;

                        for (int channel = 0; channel < 2; ++channel)
                            source.setSample(channel, sample, value);
                    }

                    writer->writeFromAudioSampleBuffer(source, 0, totalSamples);
                    writer.reset();
                }
            }

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::String loadError;
            auto tailClip = djr::AudioClip::createFromFile(sourceFile, sampleRate, formats, loadError);
            check(tailClip != nullptr, "the tail source loads back as a clip: " + loadError);

            if (tailClip != nullptr)
            {
                tailClip->setWarpEnabled(false);
                track->setFrozenAudio(std::move(tailClip));

                const auto renderFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                            .getChildFile("djr_engine_test_tail_export.wav");
                renderFile.deleteFile();

                djr::ExportManager tailExporter;
                djr::ExportManager::Options tailOptions;
                tailOptions.sampleRate = sampleRate;
                tailOptions.blockSize = blockSize;
                tailOptions.tempoBpm = tempoBpm;
                tailOptions.lengthBeats = 4.0; // two seconds at 120 BPM
                tailOptions.songMode = false;

                juce::String tailError;
                const auto tailRendered = tailExporter.render(tailMixer, renderFile, tailOptions, tailError);
                check(tailRendered, "the tail export renders: " + tailError);

                if (tailRendered)
                {
                    juce::AudioFormatManager readFormats;
                    readFormats.registerBasicFormats();
                    std::unique_ptr<juce::AudioFormatReader> tailReader(readFormats.createReaderFor(renderFile));
                    check(tailReader != nullptr, "the tail export can be read back");

                    if (tailReader != nullptr)
                    {
                        const auto mainSamples = static_cast<juce::int64>(sampleRate * 2.0);
                        check(tailReader->lengthInSamples > mainSamples,
                              "the export has a tail past the main render");

                        const auto tailLength = tailReader->lengthInSamples - mainSamples;
                        const auto probeLength = static_cast<int>(juce::jmin<juce::int64>(
                            tailLength / 4, static_cast<juce::int64>(sampleRate * 0.2)));

                        if (probeLength > 0)
                        {
                            juce::AudioBuffer<float> earlyTail(static_cast<int>(tailReader->numChannels), probeLength);
                            tailReader->read(&earlyTail, 0, probeLength, mainSamples, true, true);

                            juce::AudioBuffer<float> lateTail(static_cast<int>(tailReader->numChannels), probeLength);
                            tailReader->read(&lateTail, 0, probeLength,
                                             tailReader->lengthInSamples - probeLength, true, true);

                            auto earlyPeak = 0.0f;
                            auto latePeak = 0.0f;

                            for (int channel = 0; channel < earlyTail.getNumChannels(); ++channel)
                            {
                                earlyPeak = std::max(earlyPeak, earlyTail.getMagnitude(channel, 0, probeLength));
                                latePeak = std::max(latePeak, lateTail.getMagnitude(channel, 0, probeLength));
                            }

                            check(latePeak > earlyPeak * 1.3f,
                                  "the tail keeps advancing through the frozen source instead of "
                                  "repeating its first block");
                        }
                    }

                    tailReader.reset();
                }

                renderFile.deleteFile();
            }

            sourceFile.deleteFile();
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

    // --- Time stretch: the tempo moves, the pitch does not ------------------
    {
        const auto stretchRate = 44100.0;
        const auto seconds = 2.0;
        const auto toneHz = 440.0;
        const auto length = static_cast<int>(stretchRate * seconds);

        juce::AudioBuffer<float> tone(1, length);

        for (int i = 0; i < length; ++i)
            tone.setSample(0, i, std::sin(juce::MathConstants<double>::twoPi * toneHz
                                              * static_cast<double>(i) / stretchRate));

        // Counting rising zero crossings is enough to read a sine's pitch, and
        // needs nothing a test would have to pull in.
        const auto measureHz = [stretchRate] (const juce::AudioBuffer<float>& buffer)
        {
            auto crossings = 0;

            for (int i = 1; i < buffer.getNumSamples(); ++i)
                if (buffer.getSample(0, i - 1) <= 0.0f && buffer.getSample(0, i) > 0.0f)
                    ++crossings;

            const auto duration = buffer.getNumSamples() / stretchRate;
            return duration > 0.0 ? crossings / duration : 0.0;
        };

        check(std::abs(measureHz(tone) - toneHz) < 2.0, "the test tone reads as its own pitch");

        // Slower: the same audio spread over more time.
        const auto slower = djr::TimeStretch::process(tone, 0.5);
        check(std::abs(slower.getNumSamples() - length * 2) < stretchRate * 0.05,
              "stretching to half speed makes the audio about twice as long");
        check(std::abs(measureHz(slower) - toneHz) < 10.0,
              "and leaves the pitch where it was");

        // Faster, the direction a warped loop usually goes.
        const auto faster = djr::TimeStretch::process(tone, 2.0);
        check(std::abs(faster.getNumSamples() - length / 2) < stretchRate * 0.05,
              "stretching to double speed makes it about half as long");
        check(std::abs(measureHz(faster) - toneHz) < 10.0,
              "and still leaves the pitch alone");

        // The comparison that gives the feature its name: resampling, which is
        // what the old warp did, moves the pitch by exactly the rate.
        juce::AudioBuffer<float> resampled(1, length / 2);

        for (int i = 0; i < resampled.getNumSamples(); ++i)
            resampled.setSample(0, i, tone.getSample(0, juce::jmin(length - 1, i * 2)));

        check(measureHz(resampled) > toneHz * 1.5,
              "resampling the same tone does move its pitch, which is the difference");

        const auto unchanged = djr::TimeStretch::process(tone, 1.0);
        check(unchanged.getNumSamples() == length,
              "a rate of one returns the audio at its own length");
    }

    // --- Latency compensation: the delay that lines tracks up ---------------
    {
        const auto pdcRate = 48000.0;
        const auto pdcBlock = 64;

        djr::AlignmentDelay delay;
        delay.prepare(1, pdcRate);

        const auto impulseAt = [&delay, pdcBlock] (int blocks)
        {
            // One impulse in the very first sample, then silence. Where it comes
            // out is exactly how many samples the line is holding.
            for (int block = 0; block < blocks; ++block)
            {
                juce::AudioBuffer<float> buffer(1, pdcBlock);
                buffer.clear();

                if (block == 0)
                    buffer.setSample(0, 0, 1.0f);

                delay.process(buffer);

                for (int sample = 0; sample < pdcBlock; ++sample)
                    if (buffer.getSample(0, sample) > 0.5f)
                        return block * pdcBlock + sample;
            }

            return -1;
        };

        delay.setDelaySamples(0);
        delay.reset();
        check(impulseAt(1) == 0, "no compensation leaves the audio where it was");

        delay.setDelaySamples(17);
        delay.reset();
        check(impulseAt(1) == 17, "a delay shorter than a block lands on the right sample");

        // The case that catches an off-by-one in the wrap: the impulse has to
        // survive being written in one block and read back in a later one.
        delay.setDelaySamples(pdcBlock + 5);
        delay.reset();
        check(impulseAt(4) == pdcBlock + 5, "a delay longer than a block still lands right");

        delay.setDelaySamples(1000000);
        check(delay.getDelaySamples() <= static_cast<int>(pdcRate * djr::AlignmentDelay::maxDelaySeconds),
              "an absurd delay is clamped to what the line can hold");

        // Preparing again at a lower rate makes the line shorter. A delay left
        // over from the faster one would send process() reading at a negative
        // index - writePosition - delay + capacity drops below zero, and C++
        // leaves that remainder negative - so it has to be clamped again there.
        delay.setDelaySamples(1000000);
        delay.prepare(1, 8000.0);

        check(delay.getDelaySamples()
                  <= static_cast<int>(8000.0 * djr::AlignmentDelay::maxDelaySeconds),
              "and clamped again when a slower rate shortens the line");

        // Actually read it back at that delay: this is what a sanitizer build
        // has to walk over if the clamp ever goes missing again.
        juce::AudioBuffer<float> shortened(1, pdcBlock);
        shortened.clear();
        delay.process(shortened);
    }

    // --- Latency compensation: what the mixer works out ---------------------
    {
        djr::Mixer pdcMixer;
        pdcMixer.prepare(48000.0, 256);
        pdcMixer.refreshLatencyCompensation();

        // Nothing in the session reports latency, so nobody waits for anybody.
        check(pdcMixer.getReportedLatencySamples() == 0,
              "a session with no plugins reports no latency to compensate");
        check(pdcMixer.getLatencyCompensationSamples(0) == 0,
              "and holds no track back");

        // The arithmetic, without needing a plugin that reports latency.
        // Two sources straight to master, one of them slow.
        {
            auto longest = 0;
            const auto holds = djr::Mixer::computeLatencyHolds({ 0, 100 },
                                                               { false, false },
                                                               { -1, -1 },
                                                               longest);
            check(longest == 100, "the slowest track sets what everything waits for");
            check(holds.size() == 2 && holds[0] == 100,
                  "the track with no latency is held back to match it");
            check(holds.size() == 2 && holds[1] == 0,
                  "and the slow one is not held back at all");
        }

        // Track 0 -> bus 2 -> master, track 1 straight to master. The bus adds
        // fifty of its own, so track 1 has to wait for the whole path.
        {
            auto longest = 0;
            const auto holds = djr::Mixer::computeLatencyHolds({ 0, 0, 50 },
                                                               { false, false, true },
                                                               { 2, -1, -1 },
                                                               longest);
            check(longest == 50, "a bus's own latency counts towards the path through it");
            check(holds.size() == 3 && holds[0] == 0,
                  "the track feeding the bus waits for nothing");
            check(holds.size() == 3 && holds[1] == 50,
                  "the track going straight out waits for the bus");
            check(holds.size() == 3 && holds[2] == 0,
                  "and the bus is never held back itself");
        }

        // Two sources into the same bus, one slow. They have to meet at the
        // bus input, not merely at the master.
        {
            auto longest = 0;
            const auto holds = djr::Mixer::computeLatencyHolds({ 20, 0, 50 },
                                                               { false, false, true },
                                                               { 2, 2, -1 },
                                                               longest);
            check(longest == 70, "the longest path runs through the slower source");
            check(holds.size() == 3 && holds[0] == 0 && holds[1] == 20,
                  "two sources into one bus are lined up with each other");
        }
    }

    // --- The mixer is sized for the device's real channel count -------------
    // The bug: prepare() always sized the summing buffers and the alignment
    // delays for two channels, no matter what was asked for. A device with
    // more outputs than that would grow the buffers on the audio thread on
    // first use, and the alignment delay - never resized - would go on
    // compensating only its first two channels forever.
    {
        djr::Mixer channelMixer;
        channelMixer.prepare(sampleRate, blockSize, 6);
        check(channelMixer.getPreparedChannelCount() == 6,
              "prepare sizes the mixer for the channel count it is given");

        // The default keeps working for every call site that never mentions one.
        djr::Mixer defaultMixer;
        defaultMixer.prepare(sampleRate, blockSize);
        check(defaultMixer.getPreparedChannelCount() == 2,
              "leaving numChannels out still prepares a stereo mixer");

        // A count past what a track's own plugin chain can carry would just add
        // channels nothing downstream can fill - clamped, not taken as asked.
        djr::Mixer clampedMixer;
        clampedMixer.prepare(sampleRate, blockSize, 64);
        check(clampedMixer.getPreparedChannelCount() == djr::PluginChain::maxPluginChannels,
              "an unreasonable channel count is clamped to what plugins can carry");
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
        drums.inputChannel = 1;              // the second socket, on its own
        drums.inputStereo = false;
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
        check(loaded.tracks.size() == 2 && loaded.tracks.getReference(0).inputChannel == 1,
              "a track remembers which input it records from");
        check(loaded.tracks.size() == 2 && ! loaded.tracks.getReference(0).inputStereo,
              "and remembers that the input is mono");
        check(loaded.tracks.size() == 2 && loaded.tracks.getReference(1).inputChannel == 0
                  && loaded.tracks.getReference(1).inputStereo,
              "a track that was never pointed anywhere keeps the old default");

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

    // --- Automation: the curve model ---------------------------------------
    {
        djr::AutomationTarget volumeTarget;
        volumeTarget.kind = djr::AutomationTarget::Kind::trackVolume;

        djr::AutomationLane lane(volumeTarget);
        check(lane.isEmpty(), "a new automation lane has no points");

        // Deliberately out of order: the lane is responsible for sorting.
        lane.addPoint(8.0, 0.0);
        lane.addPoint(0.0, 1.0);

        const auto points = lane.getPoints();
        check(points.size() == 2 && points[0].beat < points[1].beat,
              "points are kept in beat order however they arrive");

        check(std::abs(lane.getValueAtBeat(-4.0) - 1.0) < 1.0e-9,
              "before the first point the curve holds its value");
        check(std::abs(lane.getValueAtBeat(99.0) - 0.0) < 1.0e-9,
              "and after the last point it holds too");
        check(std::abs(lane.getValueAtBeat(4.0) - 0.5) < 1.0e-9,
              "a straight segment reads linearly");

        lane.addPoint(0.0, 0.25);
        check(lane.getNumPoints() == 2, "dropping a point onto an existing one moves it");
        check(std::abs(lane.getValueAtBeat(0.0) - 0.25) < 1.0e-9, "and it takes the new value");

        lane.setPoints({ { 0.0, 0.0, 0.0 }, { 8.0, 1.0, 0.0 } });
        lane.setPointCurve(1, 1.0);
        check(lane.getValueAtBeat(4.0) < 0.5, "positive tension holds the previous value longer");

        lane.setPointCurve(1, -1.0);
        check(lane.getValueAtBeat(4.0) > 0.5, "negative tension races towards the next one");

        lane.setPointCurve(1, 0.0);
        check(std::abs(lane.getValueAtBeat(4.0) - 0.5) < 1.0e-9, "and zero is a straight line again");

        // Sub-block sampling: the audio thread asks for a whole block's worth of
        // values under one lock rather than one lock per sub-block.
        double sampled[5] = {};
        check(lane.sampleRange(0.0, 8.0, sampled, 5), "a range can be sampled in one go");
        check(std::abs(sampled[0] - 0.0) < 1.0e-9 && std::abs(sampled[4] - 1.0) < 1.0e-9,
              "the ends of the range land on the ends of the curve");
        check(std::abs(sampled[2] - 0.5) < 1.0e-9, "and the middle sample sits in the middle");

        double single = -1.0;
        check(lane.sampleRange(2.0, 8.0, &single, 1) && std::abs(single - 0.25) < 1.0e-9,
              "asking for one value reads the start of the range");

        lane.setEnabled(false);
        check(! lane.sampleRange(0.0, 8.0, sampled, 5), "a bypassed lane refuses to be sampled");
        lane.setEnabled(true);
    }

    // --- Automation: the curve read from a snapshot, with no lane ------------
    {
        // Drawing uses this so a repaint never contends with the audio thread.
        const std::vector<djr::AutomationPoint> points { { 0.0, 0.2, 0.0 }, { 4.0, 0.8, 0.0 } };

        check(std::abs(djr::AutomationLane::valueAt(points, -1.0) - 0.2) < 1.0e-9,
              "a snapshot holds before its first point");
        check(std::abs(djr::AutomationLane::valueAt(points, 2.0) - 0.5) < 1.0e-9,
              "and interpolates the same way the lane does");
        check(std::abs(djr::AutomationLane::valueAt({}, 2.0)) < 1.0e-9,
              "an empty snapshot answers zero rather than reading off the end");
    }

    // --- Automation: parameter ranges --------------------------------------
    {
        djr::AutomationTarget volumeTarget;
        volumeTarget.kind = djr::AutomationTarget::Kind::trackVolume;
        check(std::abs(volumeTarget.toParameterValue(0.5) - 1.0) < 1.0e-9, "track volume runs 0..2");
        check(std::abs(volumeTarget.fromParameterValue(0.8) - 0.4) < 1.0e-9, "and maps back again");

        djr::AutomationTarget panTarget;
        panTarget.kind = djr::AutomationTarget::Kind::trackPan;
        check(std::abs(panTarget.toParameterValue(0.5)) < 1.0e-9, "half way up a pan lane is centre");
        check(std::abs(panTarget.toParameterValue(1.0) - 1.0) < 1.0e-9, "and the top is hard right");

        djr::AutomationTarget pluginTarget;
        pluginTarget.kind = djr::AutomationTarget::Kind::pluginParameter;
        check(std::abs(pluginTarget.toParameterValue(0.35) - 0.35) < 1.0e-9,
              "plugin parameters are already normalised");
    }

    // --- Automation: project round trip ------------------------------------
    {
        djr::AutomationTarget target;
        target.kind = djr::AutomationTarget::Kind::pluginParameter;
        target.pluginSlot = 2;
        target.parameterIndex = 7;
        target.label = "Reverb: Mix";

        djr::AutomationLane lane(target);
        lane.setPoints({ { 0.0, 0.2, 0.0 }, { 4.0, 0.9, 0.5 } });
        lane.setEnabled(false);
        lane.setLaneHeight(70);

        auto restored = djr::AutomationLane::fromVar(lane.toVar());
        check(restored != nullptr, "a lane survives a var round trip");

        if (restored != nullptr)
        {
            check(restored->getTarget().pluginSlot == 2 && restored->getTarget().parameterIndex == 7,
                  "the target comes back pointing at the same parameter");
            check(restored->getTarget().label == "Reverb: Mix", "and keeps the name it was given");
            check(! restored->isEnabled(), "a bypassed lane reopens bypassed");
            check(restored->getLaneHeight() == 70, "the lane height is kept");

            const auto restoredPoints = restored->getPoints();
            check(restoredPoints.size() == 2 && std::abs(restoredPoints[1].curve - 0.5) < 1.0e-9,
                  "and the curve comes back with its tension");
        }
    }

    // --- Automation: it really drives the track ----------------------------
    {
        djr::Mixer automationMixer;
        automationMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(automationMixer);
        check(track != nullptr, "there is a MIDI track for the automation checks");

        if (track != nullptr)
        {
            juce::Array<djr::MidiNote> notes;
            djr::MidiNote held;
            held.pitch = 60;
            held.velocity = 0.9f;
            held.startBeat = 0.0;
            held.lengthBeats = 16.0;
            notes.add(held);
            track->getClip(0).setNotes(notes);

            check(! isSilent(renderPeak(automationMixer, true, 8)),
                  "the held note sounds with no automation on the track");

            settle(automationMixer);

            djr::AutomationTarget target;
            target.kind = djr::AutomationTarget::Kind::trackVolume;
            target.label = "Volume";

            auto* lane = track->addAutomationLane(target);
            check(lane != nullptr, "a volume lane can be added to a track");

            if (lane != nullptr)
            {
                lane->setPoints({ { 0.0, 0.0, 0.0 } });
                check(isSilent(renderPeak(automationMixer, true, 8)),
                      "a curve pinned at zero silences the same note");

                settle(automationMixer);
                lane->setPoints({ { 0.0, 1.0, 0.0 }, { 8.0, 0.0, 0.0 } });

                renderPeak(automationMixer, true, 1, nullptr, 0.0);
                check(track->isVolumeAutomated(), "the track reports its volume as automated");
                check(std::abs(track->getEffectiveVolume() - 2.0f) < 0.05f,
                      "at the top of the curve the fader reads full");

                renderPeak(automationMixer, true, 1, nullptr, 4.0);
                check(std::abs(track->getEffectiveVolume() - 1.0f) < 0.1f,
                      "half way along it reads half way down");

                renderPeak(automationMixer, true, 1, nullptr, 8.0);
                check(track->getEffectiveVolume() < 0.05f, "and at the end it reads silence");

                // Bypass hands the fader back to whoever was holding it.
                const auto stored = track->getVolume();
                lane->setEnabled(false);
                renderPeak(automationMixer, true, 1, nullptr, 8.0);
                check(! track->isVolumeAutomated(), "bypassing a lane gives the fader back");
                check(std::abs(track->getEffectiveVolume() - stored) < 1.0e-6f,
                      "and the stored level is what applies again");
            }
        }
    }

    // --- Automation: undo ---------------------------------------------------
    {
        djr::Mixer undoAutomationMixer;
        undoAutomationMixer.prepare(sampleRate, blockSize);

        auto* track = undoAutomationMixer.getTrack(0);
        check(track != nullptr, "there is a track for the automation undo checks");

        if (track != nullptr)
        {
            djr::EditHistory history(undoAutomationMixer);

            djr::AutomationTarget target;
            target.kind = djr::AutomationTarget::Kind::trackPan;
            target.label = "Pan";

            history.pushSnapshot("Tambah lane automation");

            if (auto* lane = track->addAutomationLane(target))
                lane->addPoint(2.0, 0.75);

            check(track->getNumAutomationLanes() == 1, "the lane is on the track");
            check(history.undo(), "undo runs for an automation edit");
            check(track->getNumAutomationLanes() == 0, "undo takes the lane away again");

            check(history.redo(), "redo runs");
            check(track->getNumAutomationLanes() == 1, "redo puts the lane back");

            if (const auto* restored = track->getAutomationLane(0))
            {
                check(restored->getNumPoints() == 1, "with the point that was on it");
                check(std::abs(restored->getPoints()[0].value - 0.75) < 1.0e-9, "at the value it had");
                check(restored->getTarget().kind == djr::AutomationTarget::Kind::trackPan,
                      "and still aimed at pan");
            }
        }
    }

    // --- Renaming a track ---------------------------------------------------
    {
        djr::Mixer renameMixer;
        renameMixer.prepare(sampleRate, blockSize);

        auto* track = renameMixer.getTrack(0);
        check(track != nullptr, "there is a track to rename");

        if (track != nullptr)
        {
            const auto original = track->getName();

            track->setName("Lead");
            check(track->getName() == "Lead", "a track takes a new name");

            track->setName("  Sub Bass  ");
            check(track->getName() == "Sub Bass", "and the name is trimmed");

            track->setName("   ");
            check(track->getName() == "Sub Bass", "a blank name is refused, not stored");

            djr::EditHistory history(renameMixer);
            history.pushSnapshot("Ganti nama track");
            track->setName("Pluck");

            check(track->getName() == "Pluck", "the rename went through");
            check(history.undo() && track->getName() == "Sub Bass", "undo puts the old name back");
            check(history.redo() && track->getName() == "Pluck", "and redo returns the new one");

            // The name has to be recorded on every snapshot, not just on a
            // rename, or an unrelated undo would drag it backwards.
            history.pushSnapshot("Taruh clip");
            track->setName("Keys");
            check(history.undo() && track->getName() == "Pluck",
                  "a snapshot taken for another edit still carries the name");

            track->setName(original);
        }
    }

    // --- A project round trip keeps the track name --------------------------
    {
        djr::Project project;
        djr::ProjectTrackState state;
        state.name = "Sub Bass";
        state.type = "midi";
        project.tracks.add(state);

        djr::Project reopened;
        reopened.fromVar(project.toVar());

        check(reopened.tracks.size() == 1 && reopened.tracks.getReference(0).name == "Sub Bass",
              "a renamed track survives being saved and reopened");
    }

    // --- Opening a project rebuilds the track list ---------------------------
    {
        djr::Project saved;

        const auto addTrackState = [&saved] (const juce::String& name, const juce::String& type)
        {
            djr::ProjectTrackState state;
            state.name = name;
            state.type = type;
            saved.tracks.add(state);
        };

        // Seven tracks, and not the kinds the mixer starts with: both the count
        // and the kinds have to be made to match.
        addTrackState("Kick", "midi");
        addTrackState("Gitar", "audio");   // the mixer has a MIDI track here
        addTrackState("Sub", "midi");
        addTrackState("Vocal", "audio");
        addTrackState("Room", "audio");
        addTrackState("Lead", "midi");
        addTrackState("Tape", "audio");    // the seventh, silently dropped before

        djr::Project reopened;
        reopened.fromVar(saved.toVar());
        check(reopened.tracks.size() == 7, "all seven tracks survive the file");

        djr::Mixer layoutMixer;
        layoutMixer.prepare(sampleRate, blockSize);
        check(layoutMixer.getNumTracks() == 6, "the mixer starts with its six default tracks");

        // A note on a track the project keeps as MIDI: rebuilding the list must
        // not throw that track away, or its clips and plugins would be lost on
        // every load.
        auto* kept = dynamic_cast<djr::MidiTrack*>(layoutMixer.getTrack(0));
        check(kept != nullptr, "the first default track is a MIDI track");

        if (kept != nullptr)
        {
            juce::Array<djr::MidiNote> notes;
            notes.add({ 60, 0.9f, 0.0, 1.0, false });
            kept->setClipNotes(notes);
        }

        check(djr::applyProjectTrackLayout(layoutMixer, reopened.tracks),
              "the track list reports that it changed");
        check(layoutMixer.getNumTracks() == 7, "the seventh track exists to be loaded into");
        check(layoutMixer.getTrack(0) == kept, "a track of the right kind is kept, not rebuilt");
        check(kept != nullptr && kept->getClip(0).getNumNotes() == 1, "so its clip is still there");
        check(dynamic_cast<djr::AudioTrack*>(layoutMixer.getTrack(1)) != nullptr,
              "a MIDI slot the project calls audio becomes an audio track");
        check(dynamic_cast<djr::MidiTrack*>(layoutMixer.getTrack(5)) != nullptr,
              "and a MIDI one stays MIDI");
        check(dynamic_cast<djr::AudioTrack*>(layoutMixer.getTrack(6)) != nullptr,
              "the seventh track is built as the kind the project asks for");

        // The session applies the saved state on top; the point is that there is
        // now something at index 6 to apply it to.
        for (int i = 0; i < reopened.tracks.size(); ++i)
            if (auto* track = layoutMixer.getTrack(i))
                track->setName(reopened.tracks.getReference(i).name);

        check(layoutMixer.getTrack(6) != nullptr && layoutMixer.getTrack(6)->getName() == "Tape",
              "the seventh track's state is no longer dropped");

        check(! djr::applyProjectTrackLayout(layoutMixer, reopened.tracks),
              "a list that already matches is left alone");

        // Fewer tracks than the mixer holds: the leftovers have to go, or a small
        // project opens with the previous song's tracks still hanging off it.
        djr::Project smaller;
        djr::ProjectTrackState single;
        single.name = "Solo";
        single.type = "midi";
        smaller.tracks.add(single);

        check(djr::applyProjectTrackLayout(layoutMixer, smaller.tracks), "a shorter list changes the mixer");
        check(layoutMixer.getNumTracks() == 1, "a smaller project leaves no stale tracks behind");
        check(layoutMixer.getTrack(0) == kept, "and the track that survives is the one that fit");

        // A file written before tracks were saved names none at all; that says
        // nothing about the list, so it must not empty the mixer.
        djr::Project untracked;
        check(! djr::applyProjectTrackLayout(layoutMixer, untracked.tracks),
              "a project without any tracks reports no change");
        check(layoutMixer.getNumTracks() == 1, "and leaves the session's tracks alone");

        // More tracks than the mixer can hold: it fills up and stops.
        djr::Project overfull;
        for (int i = 0; i < djr::Mixer::maxTracks + 8; ++i)
        {
            djr::ProjectTrackState state;
            state.name = "Track " + juce::String(i + 1);
            state.type = i % 2 == 0 ? "midi" : "audio";
            overfull.tracks.add(state);
        }

        djr::applyProjectTrackLayout(layoutMixer, overfull.tracks);
        check(layoutMixer.getNumTracks() == djr::Mixer::maxTracks,
              "a project with more tracks than the mixer holds stops at the limit");
        check(dynamic_cast<djr::AudioTrack*>(layoutMixer.getTrack(djr::Mixer::maxTracks - 1)) != nullptr,
              "the last track it did fit in still gets the right kind");
    }

    // --- A project from before the track type was saved ----------------------
    {
        djr::Mixer legacyMixer;
        legacyMixer.prepare(sampleRate, blockSize);

        djr::Project legacy;
        for (int i = 0; i < 4; ++i)
        {
            djr::ProjectTrackState state;     // type left empty, as an old file has it
            state.name = "Old " + juce::String(i + 1);
            legacy.tracks.add(state);
        }

        djr::applyProjectTrackLayout(legacyMixer, legacy.tracks);
        check(legacyMixer.getNumTracks() == 4, "an old project still sets the number of tracks");
        check(dynamic_cast<djr::MidiTrack*>(legacyMixer.getTrack(0)) != nullptr,
              "a slot without a saved type keeps the kind it had");
        check(dynamic_cast<djr::AudioTrack*>(legacyMixer.getTrack(3)) != nullptr,
              "including the audio one");
    }

    // --- Bus routing: audio reaches master through a bus ---------------------
    {
        djr::Mixer busMixer;
        busMixer.prepare(sampleRate, blockSize);

        auto* source = findFirstMidiTrack(busMixer);
        check(source != nullptr, "there is a MIDI track to route");

        auto* bus = dynamic_cast<djr::BusTrack*>(busMixer.addTrack(std::make_unique<djr::BusTrack>("Reverb")));
        check(bus != nullptr, "a bus track can be added to the mixer");

        if (source != nullptr && bus != nullptr)
        {
            const auto busIndex = busMixer.indexOf(bus);
            const auto sourceIndex = busMixer.indexOf(source);
            check(busIndex > sourceIndex, "the bus lands after the track in the list");

            source->getClip(0).setNotes(makeFourBarChord());

            const auto direct = renderPeak(busMixer, true, 8);
            check(! isSilent(direct), "the track is audible straight into master");

            settle(busMixer);

            // Route the track through the bus instead of to master.
            check(busMixer.setTrackOutput(sourceIndex, busIndex), "the track can be routed to the bus");
            check(source->getOutputDestination() == busIndex, "and it remembers where it points");

            const auto throughBus = renderPeak(busMixer, true, 8);
            check(! isSilent(throughBus), "audio still reaches master through the bus");

            settle(busMixer);

            // A muted bus takes everything routed into it with it.
            bus->setMuted(true);
            check(isSilent(renderPeak(busMixer, true, 8)), "muting the bus silences what feeds it");
            bus->setMuted(false);
            settle(busMixer);

            // The bus is processed after the track that feeds it, always.
            const auto order = busMixer.getProcessOrder();
            const auto positionOf = [&order] (int trackIndex)
            {
                return static_cast<int>(std::distance(order.begin(),
                                                      std::find(order.begin(), order.end(), trackIndex)));
            };

            check(order.size() == static_cast<size_t>(busMixer.getNumTracks()),
                  "every track appears in the process order exactly once");
            check(positionOf(sourceIndex) < positionOf(busIndex),
                  "and a feeder is always processed before its bus");
        }
    }

    // --- Routing refuses to feed back ---------------------------------------
    {
        djr::Mixer loopMixer;
        loopMixer.prepare(sampleRate, blockSize);

        auto* busA = loopMixer.addTrack(std::make_unique<djr::BusTrack>("Bus A"));
        auto* busB = loopMixer.addTrack(std::make_unique<djr::BusTrack>("Bus B"));
        check(busA != nullptr && busB != nullptr, "two buses can be added");

        if (busA != nullptr && busB != nullptr)
        {
            const auto a = loopMixer.indexOf(busA);
            const auto b = loopMixer.indexOf(busB);

            check(! loopMixer.canRoute(a, a), "a bus cannot feed itself");
            check(loopMixer.canRoute(a, b), "but it can feed another bus");
            check(loopMixer.setTrackOutput(a, b), "and that route is accepted");

            // B now feeds off A, so sending B back into A would close the loop.
            check(! loopMixer.canRoute(b, a), "the route back round is refused");
            check(! loopMixer.setTrackOutput(b, a), "and setting it changes nothing");
            check(busB->getOutputDestination() == djr::Track::masterDestination,
                  "the refused track still points at master");

            // A send closes a loop just as surely as a main output does.
            check(! loopMixer.setTrackSend(b, 0, { a, 0.5f, false }),
                  "a send that would loop is refused too");

            check(! loopMixer.canRoute(0, 1), "a track that is not a bus cannot receive");
        }
    }

    // --- Sends: level, pre-fader, and what the fader does to them ------------
    {
        djr::Mixer sendMixer;
        sendMixer.prepare(sampleRate, blockSize);

        auto* source = findFirstMidiTrack(sendMixer);
        auto* bus = sendMixer.addTrack(std::make_unique<djr::BusTrack>("Send bus"));

        if (source != nullptr && bus != nullptr)
        {
            const auto sourceIndex = sendMixer.indexOf(source);
            const auto busIndex = sendMixer.indexOf(bus);

            source->getClip(0).setNotes(makeFourBarChord());

            // Only the send reaches master, so what arrives is the send alone.
            check(sendMixer.setTrackOutput(sourceIndex, busIndex), "route the track into the bus");
            check(sendMixer.setTrackSend(sourceIndex, 0, { busIndex, 0.0f, false }),
                  "a send at zero is still a legal route");

            bus->setMuted(true);
            check(isSilent(renderPeak(sendMixer, true, 8)), "with the bus muted nothing gets out");
            bus->setMuted(false);
            settle(sendMixer);

            // Back to master, and measure the send on its own into a muted bus.
            check(sendMixer.setTrackOutput(sourceIndex, djr::Track::masterDestination),
                  "the track goes back to master");

            source->setVolume(1.0f);
            check(sendMixer.setTrackSend(sourceIndex, 0, { busIndex, 1.0f, true }),
                  "a pre-fader send can be set");

            const auto loudFader = renderPeak(sendMixer, true, 8);
            check(! isSilent(loudFader), "the track is heard with the fader up");

            settle(sendMixer);

            // Pre-fader: pulling the fader right down must not take the send
            // with it, so the bus still carries the signal.
            source->setVolume(0.0f);
            const auto preFaderOnly = renderPeak(sendMixer, true, 8);
            check(! isSilent(preFaderOnly),
                  "a pre-fader send survives the fader being pulled to silence");

            settle(sendMixer);

            // Post-fader: the same fader now takes the send down with it.
            check(sendMixer.setTrackSend(sourceIndex, 0, { busIndex, 1.0f, false }),
                  "the same slot can be made post-fader");
            check(isSilent(renderPeak(sendMixer, true, 8)),
                  "a post-fader send follows the fader down to silence");

            source->setVolume(0.8f);
        }
    }

    // --- Removing a track takes the routing with it --------------------------
    {
        djr::Mixer removeMixer;
        removeMixer.prepare(sampleRate, blockSize);

        auto* first = removeMixer.addTrack(std::make_unique<djr::BusTrack>("Bus one"));
        auto* second = removeMixer.addTrack(std::make_unique<djr::BusTrack>("Bus two"));

        if (first != nullptr && second != nullptr)
        {
            const auto firstIndex = removeMixer.indexOf(first);
            const auto secondIndex = removeMixer.indexOf(second);

            check(removeMixer.setTrackOutput(0, firstIndex), "track 0 feeds the first bus");
            check(removeMixer.setTrackSend(1, 0, { secondIndex, 0.5f, false }), "track 1 sends to the second");

            // Drop the first bus: track 0 loses its destination, and track 1's
            // send has to follow the second bus down to its new index.
            check(removeMixer.removeTrack(firstIndex), "the first bus is removed");

            check(removeMixer.getTrack(0)->getOutputDestination() == djr::Track::masterDestination,
                  "a track pointing at the removed bus falls back to master");
            check(removeMixer.getTrack(1)->getSend(0).destination == secondIndex - 1,
                  "and a send above the hole slides down with it");
            check(removeMixer.getTrack(1)->getSend(0).destination == removeMixer.indexOf(second),
                  "which is exactly where that bus now lives");
        }
    }

    // --- Routing survives undo and a project round trip ----------------------
    {
        djr::Mixer routingMixer;
        routingMixer.prepare(sampleRate, blockSize);

        auto* bus = routingMixer.addTrack(std::make_unique<djr::BusTrack>("Bus"));

        if (bus != nullptr)
        {
            const auto busIndex = routingMixer.indexOf(bus);
            djr::EditHistory history(routingMixer);

            routingMixer.setTrackOutput(0, busIndex);
            routingMixer.setTrackSend(0, 0, { busIndex, 0.4f, true });

            history.pushSnapshot("Routing");
            routingMixer.setTrackOutput(0, djr::Track::masterDestination);
            routingMixer.setTrackSend(0, 0, {});

            check(routingMixer.getTrack(0)->getOutputDestination() == djr::Track::masterDestination,
                  "the route was changed");
            check(history.undo(), "undo runs for routing");
            check(routingMixer.getTrack(0)->getOutputDestination() == busIndex,
                  "undo puts the output back on the bus");

            const auto restored = routingMixer.getTrack(0)->getSend(0);
            check(restored.destination == busIndex && restored.preFader,
                  "and restores the send with its pre-fader flag");
            check(std::abs(restored.level - 0.4f) < 1.0e-6f, "at the level it had");
        }
    }

    {
        djr::Project project;
        djr::ProjectTrackState state;
        state.name = "Reverb";
        state.type = "bus";
        state.outputDestination = 3;

        auto* sendObject = new juce::DynamicObject();
        sendObject->setProperty("destination", 2);
        sendObject->setProperty("level", 0.65);
        sendObject->setProperty("preFader", true);
        state.sends.add(juce::var(sendObject));

        project.tracks.add(state);

        djr::Project reopened;
        reopened.fromVar(project.toVar());

        check(reopened.tracks.size() == 1, "the bus track is written to the project");

        if (reopened.tracks.size() == 1)
        {
            const auto& loaded = reopened.tracks.getReference(0);
            check(loaded.type == "bus", "a bus reopens as a bus");
            check(loaded.outputDestination == 3, "its output destination survives");
            check(loaded.sends.size() == 1, "and so does its send");

            if (auto* loadedSend = loaded.sends[0].getDynamicObject())
            {
                check(static_cast<int>(loadedSend->getProperty("destination")) == 2,
                      "the send still points where it did");
                check(static_cast<bool>(loadedSend->getProperty("preFader")),
                      "and is still pre-fader");
            }
        }

        // Freeze is remembered by the path of its render, so reopening a project
        // does not have to render everything again.
        djr::Project frozenProject;
        djr::ProjectTrackState frozenState;
        frozenState.name = "Keys";
        frozenState.type = "midi";
        frozenState.frozenFile = "/tmp/djr-freeze-keys.wav";
        frozenProject.tracks.add(frozenState);

        djr::Project frozenReopened;
        frozenReopened.fromVar(frozenProject.toVar());
        check(frozenReopened.tracks.size() == 1
                  && frozenReopened.tracks.getReference(0).frozenFile == "/tmp/djr-freeze-keys.wav",
              "a frozen track reopens still pointing at its render");

        // A file written before routing existed must still open, aimed at master.
        djr::Project legacy;
        djr::ProjectTrackState old;
        old.name = "Bass";
        old.type = "midi";
        legacy.tracks.add(old);

        auto legacyVar = legacy.toVar();
        legacyVar.getDynamicObject()->getProperty("tracks").getArray()
            ->getReference(0).getDynamicObject()->removeProperty("outputDestination");

        djr::Project legacyReopened;
        legacyReopened.fromVar(legacyVar);
        check(legacyReopened.tracks.size() == 1
                  && legacyReopened.tracks.getReference(0).outputDestination == -1,
              "a project from before routing opens pointing at master");
    }

    // --- Freeze: render one track, then play the render instead --------------
    {
        djr::Mixer freezeMixer;
        freezeMixer.prepare(sampleRate, blockSize);

        auto* track = findFirstMidiTrack(freezeMixer);
        check(track != nullptr, "there is a MIDI track to freeze");

        if (track != nullptr)
        {
            track->getClip(0).setNotes(makeFourBarChord());

            const auto renderFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getChildFile("djr_freeze_test.wav");

            djr::TrackRenderer renderer;
            djr::TrackRenderer::Options options;
            options.sampleRate = sampleRate;
            options.blockSize = blockSize;
            options.tempoBpm = tempoBpm;
            options.lengthBeats = 4.0;
            options.songMode = false;
            options.tailSeconds = 0.5;

            juce::String error;
            const auto rendered = renderer.render(*track, renderFile, options, error);
            check(rendered, "a single track renders to a file: " + error);

            if (rendered)
            {
                check(renderFile.getSize() > 1000, "and the file has audio in it, not just a header");

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();

                juce::String loadError;
                auto clip = djr::AudioClip::createFromFile(renderFile, sampleRate, formats, loadError);
                check(clip != nullptr, "the render loads back as a clip: " + loadError);

                if (clip != nullptr)
                {
                    // The render must actually contain the chord, not silence.
                    juce::AudioBuffer<float> probe(2, blockSize);
                    probe.clear();
                    clip->setWarpEnabled(false);
                    clip->addToBuffer(probe, 0.5, tempoBpm, sampleRate);

                    auto renderPeakLevel = 0.0f;
                    for (int channel = 0; channel < probe.getNumChannels(); ++channel)
                        renderPeakLevel = std::max(renderPeakLevel,
                                                   probe.getMagnitude(channel, 0, probe.getNumSamples()));

                    check(! isSilent(renderPeakLevel), "the rendered audio is not silence");

                    settle(freezeMixer);

                    // Frozen: the notes are still in the clip, but they must not
                    // be what is heard - the render is.
                    track->setFrozenAudio(std::move(clip));
                    check(track->isFrozen(), "the track reports itself frozen");
                    check(track->getFrozenFile() == renderFile, "and remembers where the render came from");
                    check(track->getClip(0).getNumNotes() > 0,
                          "freezing leaves the notes alone, so it can be undone");

                    const auto frozenPeak = renderPeak(freezeMixer, true, 8);
                    check(! isSilent(frozenPeak), "a frozen track is still audible");

                    settle(freezeMixer);

                    // The fader stays live: the render is pre-fader, so pulling
                    // the fader down still silences the track.
                    const auto stored = track->getVolume();
                    track->setVolume(0.0f);
                    // The first block ramps down to the new gain rather than
                    // snapping to it - that ramp is itself audio, and would
                    // otherwise read as the fader not having taken effect.
                    renderPeak(freezeMixer, true, 1);
                    check(isSilent(renderPeak(freezeMixer, true, 8)),
                          "the fader still works on a frozen track");
                    track->setVolume(stored);

                    settle(freezeMixer);

                    track->setFrozenAudio(nullptr);
                    check(! track->isFrozen(), "unfreezing puts the track back");
                    check(! isSilent(renderPeak(freezeMixer, true, 8)),
                          "and it makes its own sound again");
                }
            }

            renderFile.deleteFile();
        }
    }

    // Destructive sample edits: the two things the timeline cannot do to a clip.
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("djr_sample_edit_test.wav");
        wav.deleteFile();

        // A ramp rather than a tone: every sample is different, so reversing it
        // is something an assertion can actually see.
        const auto totalSamples = static_cast<int>(sampleRate);

        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));

            if (writer != nullptr)
            {
                stream.release();

                juce::AudioBuffer<float> ramp(1, totalSamples);

                for (int sample = 0; sample < totalSamples; ++sample)
                    ramp.setSample(0, sample,
                                   0.5f * static_cast<float>(sample) / static_cast<float>(totalSamples - 1));

                writer->writeFromAudioSampleBuffer(ramp, 0, totalSamples);
                writer.reset();
            }
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        juce::String error;
        auto clip = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);
        check(clip != nullptr, "the ramp loads as a clip: " + error);

        if (clip != nullptr)
        {
            const auto sampleAt = [] (const djr::AudioClip& source, int index)
            {
                float lowest = 0.0f;
                float highest = 0.0f;
                source.getSampleRange(0, index, 1, lowest, highest);
                return lowest;
            };

            const auto peakOf = [] (const djr::AudioClip& source)
            {
                float lowest = 0.0f;
                float highest = 0.0f;
                source.getSampleRange(0, 0, source.getNumSourceSamples(), lowest, highest);
                return std::max(std::abs(lowest), std::abs(highest));
            };

            check(clip->getNumSourceSamples() == totalSamples,
                  "the clip can say how many samples it holds");
            check(std::abs(peakOf(*clip) - 0.5f) < 0.01f, "and the ramp peaks where it was written");

            // A clip made before the edit must not hear about it -------------
            auto sibling = clip->duplicate();

            check(clip->canApplySampleEdit(djr::AudioClip::SampleEdit::normalise),
                  "a clip at half scale has room to be normalised");
            check(clip->applySampleEdit(djr::AudioClip::SampleEdit::normalise),
                  "and normalising it succeeds");
            check(std::abs(peakOf(*clip) - 1.0f) < 0.01f, "normalising lifts the peak to full scale");
            check(clip->getNumSourceSamples() == totalSamples,
                  "and does not change how long the audio is");
            check(sibling != nullptr && std::abs(peakOf(*sibling) - 0.5f) < 0.01f,
                  "a clip that shared the samples is left alone");

            check(! clip->canApplySampleEdit(djr::AudioClip::SampleEdit::normalise),
                  "normalising again would change nothing, and says so");

            // Reverse ---------------------------------------------------------
            const auto firstBefore = sampleAt(*clip, 0);
            const auto lastBefore = sampleAt(*clip, totalSamples - 1);

            check(clip->applySampleEdit(djr::AudioClip::SampleEdit::reverse), "reversing succeeds");
            check(std::abs(sampleAt(*clip, 0) - lastBefore) < 1.0e-4f
                      && std::abs(sampleAt(*clip, totalSamples - 1) - firstBefore) < 1.0e-4f,
                  "reversing puts the end of the audio at its start");

            // Only the part the clip plays ------------------------------------
            {
                auto trimmed = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);

                if (trimmed != nullptr)
                {
                    // The back half only, so the front half is a control. Trimmed
                    // rather than offset: sliding the offset alone is clamped so
                    // the clip still fits, which on a full-length clip is no move
                    // at all.
                    trimmed->trimStart(1.0, 120.0);
                    trimmed->applySampleEdit(djr::AudioClip::SampleEdit::normalise);

                    const auto untouched = sampleAt(*trimmed, totalSamples / 4);
                    const auto expected = 0.5f * static_cast<float>(totalSamples / 4)
                                        / static_cast<float>(totalSamples - 1);

                    check(std::abs(untouched - expected) < 0.01f,
                          "an edit leaves the part of the source the clip does not play alone");
                    check(std::abs(peakOf(*trimmed) - 1.0f) < 0.01f,
                          "and does reach full scale inside the part it does");
                }
            }

            // Exporting -------------------------------------------------------
            {
                auto exported = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);

                if (exported != nullptr)
                {
                    // The back half, normalised, so the file has to differ from
                    // the source in both length and level to be right.
                    exported->trimStart(1.0, 120.0);
                    exported->applySampleEdit(djr::AudioClip::SampleEdit::normalise);

                    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("djr_sample_export.wav");
                    out.deleteFile();

                    juce::String exportError;
                    const auto written = exported->exportPlayedRegion(out, formats, exportError);

                    check(written == out, "the sample exports to the file it was asked for: " + exportError);

                    auto readBack = djr::AudioClip::createFromFile(written, sampleRate, formats, error);
                    check(readBack != nullptr, "and the export loads back as audio");

                    if (readBack != nullptr)
                    {
                        check(std::abs(readBack->getSourceLengthSeconds() - 0.5) < 0.01,
                              "the export holds the part the clip plays, not the whole source");
                        check(std::abs(peakOf(*readBack) - 1.0f) < 0.02f,
                              "and the edit is baked into it rather than replayed at load");
                        check(! readBack->hasSampleEdits(),
                              "an exported file carries no edit list: it is already the audio");
                    }

                    // An extension nothing can write --------------------------
                    const auto odd = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                         .getChildFile("djr_sample_export.qqq");
                    odd.deleteFile();

                    const auto fallback = exported->exportPlayedRegion(odd, formats, exportError);
                    check(fallback.hasFileExtension("wav"),
                          "an extension nothing can write falls back to wav rather than failing");

                    fallback.deleteFile();
                    out.deleteFile();
                }
            }

            // Reopening -------------------------------------------------------
            {
                const auto saved = clip->toVar();
                auto reopened = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);

                if (reopened != nullptr)
                {
                    reopened->applyStateFromVar(saved);

                    check(std::abs(peakOf(*reopened) - 1.0f) < 0.01f,
                          "a reopened project replays the normalise over the original file");
                    check(std::abs(sampleAt(*reopened, 0) - sampleAt(*clip, 0)) < 1.0e-4f,
                          "and the reverse with it, in the order they were made");
                    check(reopened->getSampleEdits().size() == 2,
                          "both edits are remembered, not just their result");
                }

                auto plain = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);

                if (plain != nullptr)
                {
                    check(! plain->hasSampleEdits(), "a freshly loaded clip has no edits on it");
                    check(std::abs(peakOf(*plain) - 0.5f) < 0.01f,
                          "and the file on disk was never written to");
                }
            }
        }

        wav.deleteFile();
    }

    // --- A checked-out sample buffer is released off the audio thread -------
    // The bug: addToBuffer copies the samples shared_ptr under a try-lock,
    // but if a destructive edit swaps the pointer while that copy is still
    // alive, the audio thread's copy can end up the last reference - and
    // its destructor, which may call free(), has no business running there.
    // hasPendingRelease exposes the handoff this is fixed with: the audio
    // thread parks what it used instead of releasing it inline, and only a
    // message-thread edit actually clears the parked reference.
    {
        const auto wav = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("djr_deferred_release_test.wav");
        wav.deleteFile();

        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> stream(wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(stream.get(), sampleRate, 1, 16, {}, 0));

            if (writer != nullptr)
            {
                stream.release();

                const auto totalSamples = static_cast<int>(sampleRate);
                juce::AudioBuffer<float> tone(1, totalSamples);
                tone.clear();

                writer->writeFromAudioSampleBuffer(tone, 0, totalSamples);
                writer.reset();
            }
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        juce::String error;
        auto clip = djr::AudioClip::createFromFile(wav, sampleRate, formats, error);
        check(clip != nullptr, "the deferred-release clip loads: " + error);

        if (clip != nullptr)
        {
            check(! clip->hasPendingRelease(), "a freshly loaded clip has nothing parked");

            juce::AudioBuffer<float> probe(1, blockSize);
            probe.clear();
            clip->addToBuffer(probe, 0.0, tempoBpm, sampleRate);

            check(clip->hasPendingRelease(),
                  "addToBuffer parks the buffer it used instead of releasing it inline");

            // A destructive edit is the one place that swaps the pointer out
            // from under a concurrent block - and the one place that has to
            // sweep the parked reference before it does.
            check(clip->applySampleEdit(djr::AudioClip::SampleEdit::reverse),
                  "the edit that provokes the race succeeds");

            check(! clip->hasPendingRelease(),
                  "the edit sweeps the parked buffer before swapping in its own");
        }

        wav.deleteFile();
    }

    // The channel's own stage: the envelope, the LFO, the filter and the
    // arpeggiator, driven directly so the assertions do not depend on whatever
    // the preview synth happens to sound like.
    {
        using Channel = djr::ChannelSettings;

        // A block of MIDI holding one note down, and a block of steady signal
        // to hear what the channel does to it.
        const auto silenceGate = [] (Channel& channel, int numSamples)
        {
            juce::MidiBuffer empty;
            channel.processMidi(empty, numSamples, tempoBpm);
        };

        const auto renderGain = [] (Channel& channel, int numSamples)
        {
            juce::AudioBuffer<float> buffer(2, numSamples);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 1.0f, numSamples);

            channel.processAudio(buffer);
            return buffer.getMagnitude(0, 0, numSamples);
        };

        {
            Channel channel;
            channel.prepare(sampleRate);

            Channel::Envelope envelope;
            envelope.enabled = true;
            envelope.delay = 0.0f;
            // Squared knobs: 0.5 is a quarter of the four-second longest, so a
            // second of attack - long enough to be unambiguous in one block.
            envelope.attack = 0.5f;
            envelope.hold = 0.0f;
            envelope.decay = 0.0f;
            envelope.sustain = 1.0f;
            envelope.release = 0.5f;
            channel.setEnvelope(Channel::Target::volume, envelope);

            check(isSilent(renderGain(channel, blockSize)),
                  "a volume envelope with no note holds the channel silent");

            juce::MidiBuffer noteOn;
            noteOn.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            channel.processMidi(noteOn, blockSize, tempoBpm);

            const auto opening = renderGain(channel, blockSize);
            check(! isSilent(opening) && opening < 0.2f,
                  "the note opens the envelope, and a slow attack starts quietly");

            auto climbing = opening;
            auto everFell = false;

            for (int block = 0; block < 20; ++block)
            {
                silenceGate(channel, blockSize);
                const auto next = renderGain(channel, blockSize);
                everFell = everFell || next < climbing - 1.0e-5f;
                climbing = next;
            }

            check(! everFell, "the attack keeps climbing while the note is held");

            // Twenty-one blocks is about a quarter of a second, and the attack
            // is a second long, so it should be about a quarter of the way up.
            check(climbing > 0.15f && climbing < 0.4f,
                  "and a second-long attack is a quarter of the way up after a quarter second");

            juce::MidiBuffer noteOff;
            noteOff.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            channel.processMidi(noteOff, blockSize, tempoBpm);

            // The first block of the release still peaks where the envelope was
            // when the note went, so the fall shows a block or two later.
            renderGain(channel, blockSize);

            for (int block = 0; block < 3; ++block)
            {
                silenceGate(channel, blockSize);
                renderGain(channel, blockSize);
            }

            silenceGate(channel, blockSize);
            const auto releasing = renderGain(channel, blockSize);
            check(releasing < climbing, "letting go starts the release");

            for (int block = 0; block < 200; ++block)
            {
                silenceGate(channel, blockSize);
                renderGain(channel, blockSize);
            }

            check(isSilent(renderGain(channel, blockSize)),
                  "and the release runs all the way down to silence");
        }

        {
            // A filter that is nearly shut takes the top off a signal that is
            // nothing but top: alternating samples are the highest frequency
            // the sample rate can carry.
            Channel channel;
            channel.prepare(sampleRate);
            channel.setFilterEnabled(true);
            channel.setFilterCutoff(0.1f);
            channel.setFilterResonance(0.0f);

            juce::AudioBuffer<float> buffer(2, blockSize);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int sample = 0; sample < blockSize; ++sample)
                    buffer.setSample(ch, sample, sample % 2 == 0 ? 1.0f : -1.0f);

            channel.processAudio(buffer);
            check(buffer.getMagnitude(0, blockSize / 2, blockSize / 2) < 0.1f,
                  "a low cutoff takes the top off the channel");
        }

        {
            Channel channel;
            channel.prepare(sampleRate);
            channel.setArpDirection(Channel::ArpDirection::up);
            channel.setArpRange(2);
            channel.setArpTime(0.0f);
            channel.setArpGate(0.5f);

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 64, 0.9f), 0);
            channel.processMidi(midi, blockSize, tempoBpm);

            juce::Array<int> played;

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    played.add(metadata.getMessage().getNoteNumber());

            // A sixteenth at 120bpm is ~5500 samples, so one 512-sample block
            // holds the first step and nothing else.
            check(played.size() == 1 && played[0] == 60,
                  "the arpeggiator plays the bottom of the chord first");

            for (int block = 0; block < 40; ++block)
            {
                juce::MidiBuffer next;
                channel.processMidi(next, blockSize, tempoBpm);

                for (const auto metadata : next)
                    if (metadata.getMessage().isNoteOn())
                        played.add(metadata.getMessage().getNoteNumber());
            }

            check(played.size() >= 4, "and keeps stepping while the chord is held");
            check(played.contains(64), "it reaches the notes above the root");
            check(played.contains(72), "and the range takes it into the next octave");

            juce::MidiBuffer release;
            release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
            release.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
            channel.processMidi(release, blockSize, tempoBpm);

            auto stillPlaying = false;

            for (int block = 0; block < 40; ++block)
            {
                juce::MidiBuffer next;
                channel.processMidi(next, blockSize, tempoBpm);

                for (const auto metadata : next)
                    if (metadata.getMessage().isNoteOn())
                        stillPlaying = true;
            }

            check(! stillPlaying, "and it stops when the chord is let go");
        }

        {
            Channel channel;
            Channel::Envelope envelope;
            envelope.enabled = true;
            envelope.attack = 0.42f;
            envelope.sustain = 0.3f;
            channel.setEnvelope(Channel::Target::modX, envelope);
            channel.setFilterEnabled(true);
            channel.setFilterCutoff(0.33f);
            channel.setArpDirection(Channel::ArpDirection::upDown);
            channel.setArpRange(3);

            Channel reopened;
            reopened.fromVar(channel.toVar());

            const auto restored = reopened.getEnvelope(Channel::Target::modX);
            check(restored.enabled
                      && juce::approximatelyEqual(restored.attack, 0.42f)
                      && juce::approximatelyEqual(restored.sustain, 0.3f),
                  "the channel's envelope survives a save and a reload");
            check(reopened.isFilterEnabled()
                      && juce::approximatelyEqual(reopened.getFilterCutoff(), 0.33f),
                  "so does the filter");
            check(reopened.getArpDirection() == Channel::ArpDirection::upDown
                      && reopened.getArpRange() == 3,
                  "and so does the arpeggiator");

            Channel untouched;
            untouched.fromVar({});
            check(! untouched.isActive(),
                  "a project from before any of this reads back as a channel that does nothing");
        }
    }

    // The sample editor's capture: the audio thread's half only ever copies
    // into the ring, and the growing half is checked here because that is where
    // the mistakes would be silent - a hole in a recording that nothing reports.
    {
        djr::SampleCapture capture;
        capture.prepareRing(48000.0, 2);

        juce::AudioBuffer<float> block(2, 512);

        const auto pushBlocks = [&capture, &block] (int count, float value)
        {
            for (int i = 0; i < count; ++i)
            {
                for (int channel = 0; channel < block.getNumChannels(); ++channel)
                    juce::FloatVectorOperations::fill(block.getWritePointer(channel), value, block.getNumSamples());

                capture.captureBlock(block);
            }
        };

        pushBlocks(4, 0.5f);
        check(capture.drain() == 0 && capture.getNumCapturedSamples() == 0,
              "audio that arrives before Record is not captured");

        capture.start();
        pushBlocks(4, 0.5f);
        const auto arrived = capture.drain();
        check(arrived == 4 * 512 && capture.getNumCapturedSamples() == 4 * 512,
              "everything pushed while armed comes back out of the ring");
        check(juce::approximatelyEqual(capture.getAudio().getSample(0, 0), 0.5f)
                  && juce::approximatelyEqual(capture.getAudio().getSample(1, 4 * 512 - 1), 0.5f),
              "and it comes back as the samples that went in");

        // Two drains rather than one long push: the store has to grow without
        // losing what it already held.
        pushBlocks(4, -0.25f);
        capture.drain();
        check(capture.getNumCapturedSamples() == 8 * 512,
              "a second drain appends rather than replaces");
        check(juce::approximatelyEqual(capture.getAudio().getSample(0, 0), 0.5f)
                  && juce::approximatelyEqual(capture.getAudio().getSample(0, 4 * 512), -0.25f),
              "and the two takes sit end to end in the order they arrived");

        capture.stop();
        pushBlocks(2, 1.0f);
        capture.drain();
        check(capture.getNumCapturedSamples() == 8 * 512,
              "Stop means stop: nothing after it is captured");

        auto clip = djr::AudioClip::createFromBuffer("captured",
                                                     capture.getAudio(),
                                                     capture.getNumCapturedSamples(),
                                                     capture.getSampleRate());
        check(clip != nullptr && clip->getNumSourceSamples() == 8 * 512,
              "the capture becomes a clip the sample editor can draw");
        check(clip != nullptr && clip->getFile() == juce::File(),
              "a captured clip has no file behind it, and does not pretend to");

        capture.clear();
        check(capture.getNumCapturedSamples() == 0 && ! capture.isCapturing(),
              "Clear leaves nothing behind");
    }

    // The ring is fixed and the drain is not guaranteed to keep up. What must
    // not happen is losing audio quietly.
    {
        djr::SampleCapture capture;
        capture.prepareRing(48000.0, 2);
        capture.start();

        juce::AudioBuffer<float> block(2, 4096);
        block.clear();

        // Three seconds pushed into a two second ring with nobody draining.
        for (int i = 0; i < 36; ++i)
            capture.captureBlock(block);

        capture.drain();
        check(capture.getDroppedSamples() > 0,
              "a ring nobody drained says how much audio it lost");
    }

    // The channel's echo. Audible behaviour nobody can check by looking, and
    // the delay time is derived from the tempo, so it is worth pinning down.
    {
        djr::ChannelSettings channel;
        channel.prepare(48000.0);
        channel.setTempo(120.0);
        channel.setEchoFeedback(0.5f);
        channel.setEchoTime(0.0f);   // the short end of the knob: a sixteenth
        channel.setEchoPan(0.0f);

        // A sixteenth at 120 BPM is an eighth of a second: 6000 samples at 48k.
        juce::AudioBuffer<float> block(2, 16000);
        block.clear();
        block.setSample(0, 0, 1.0f);
        block.setSample(1, 0, 1.0f);

        channel.processAudio(block);

        check(juce::approximatelyEqual(block.getSample(0, 0), 1.0f),
              "the echo leaves the dry signal where it was");
        check(std::abs(block.getSample(0, 6000) - 0.5f) < 1.0e-4f,
              "the first repeat lands a sixteenth later, at the feedback level");
        check(std::abs(block.getSample(0, 12000) - 0.25f) < 1.0e-4f,
              "and the second is the first fed back through again");

        djr::ChannelSettings silent;
        silent.prepare(48000.0);
        silent.setTempo(120.0);
        silent.setEchoFeedback(0.0f);

        juce::AudioBuffer<float> untouched(2, 16000);
        untouched.clear();
        untouched.setSample(0, 0, 1.0f);
        silent.processAudio(untouched);

        check(juce::approximatelyEqual(untouched.getSample(0, 6000), 0.0f),
              "no feedback means no repeats at all");

        // Tempo decides where the repeat lands, so a slower one must move it.
        djr::ChannelSettings slower;
        slower.prepare(48000.0);
        slower.setTempo(60.0);
        slower.setEchoFeedback(0.5f);
        slower.setEchoTime(0.0f);

        juce::AudioBuffer<float> slowBlock(2, 16000);
        slowBlock.clear();
        slowBlock.setSample(0, 0, 1.0f);
        slower.processAudio(slowBlock);

        check(std::abs(slowBlock.getSample(0, 12000) - 0.5f) < 1.0e-4f,
              "at half the tempo the repeat waits twice as long");

        // Pitch reads the line faster than it is written, so the repeat comes
        // back early as well as high - the two are the same fact about tape.
        djr::ChannelSettings pitched;
        pitched.prepare(48000.0);
        pitched.setTempo(120.0);
        pitched.setEchoFeedback(0.5f);
        pitched.setEchoTime(0.0f);
        pitched.setEchoPitch(1.0f);   // an octave up: read twice as fast

        juce::AudioBuffer<float> pitchedBlock(2, 16000);
        pitchedBlock.clear();

        for (int i = 0; i < 200; ++i)
            pitchedBlock.setSample(0, i, 1.0f);

        pitched.processAudio(pitchedBlock);

        const auto energyBetween = [] (const juce::AudioBuffer<float>& b, int from, int to)
        {
            auto sum = 0.0f;

            for (int i = from; i < to; ++i)
                sum += std::abs(b.getSample(0, i));

            return sum;
        };

        // Half the length at twice the speed, and the second repeat arrives
        // sooner still - both follow from reading the line faster.
        check(energyBetween(pitchedBlock, 5900, 6120) > 0.0f,
              "a detuned echo still repeats");
        check(energyBetween(pitchedBlock, 6000, 6100) > energyBetween(pitchedBlock, 6100, 6200),
              "an octave up returns the repeat in half the time it went in");
    }

    // The channel's pitch, which is done to the notes rather than to the audio.
    {
        const auto notesFrom = [] (const juce::MidiBuffer& buffer)
        {
            std::vector<int> notes;

            for (const auto metadata : buffer)
                if (metadata.getMessage().isNoteOn())
                    notes.push_back(metadata.getMessage().getNoteNumber());

            return notes;
        };

        djr::ChannelSettings channel;
        channel.prepare(48000.0);
        channel.setPitchSemitones(5);

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 200);
        channel.processMidi(midi, 512, 120.0);

        const auto shifted = notesFrom(midi);
        check(shifted.size() == 1 && shifted[0] == 65,
              "a channel pitched up five semitones plays five semitones up");

        djr::ChannelSettings straight;
        straight.prepare(48000.0);

        juce::MidiBuffer untouched;
        untouched.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        straight.processMidi(untouched, 512, 120.0);

        check(notesFrom(untouched) == std::vector<int> { 60 },
              "and a channel at zero leaves the notes where they were written");

        // Off the end of the keyboard is dropped, not wrapped: a wrapped note
        // would sound ten octaves from where it was written.
        djr::ChannelSettings high;
        high.prepare(48000.0);
        high.setPitchSemitones(12);

        juce::MidiBuffer edge;
        edge.addEvent(juce::MidiMessage::noteOn(1, 120, 1.0f), 0);
        high.processMidi(edge, 512, 120.0);

        check(notesFrom(edge).empty(),
              "a note pushed past the top of the keyboard is dropped rather than wrapped");
    }

    // A clip's own pitch: audio has no notes to move, so it is read faster and
    // stretched back. What must not move is where the clip sits or how long it
    // lasts - only the pitch.
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getChildFile("djr_engine_test_pitch.wav");
        file.deleteFile();

        // Two seconds of tone, the same way the clip tests above make one.
        {
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(stream.get(), 48000.0, 2, 16, {}, 0));

            if (writer != nullptr)
            {
                stream.release();

                const auto totalSamples = 96000;
                juce::AudioBuffer<float> tone(2, totalSamples);
                double phase = 0.0;

                for (int i = 0; i < totalSamples; ++i)
                {
                    const auto value = static_cast<float>(std::sin(phase)) * 0.5f;
                    tone.setSample(0, i, value);
                    tone.setSample(1, i, value);
                    phase += juce::MathConstants<double>::twoPi * 220.0 / 48000.0;
                }

                writer->writeFromAudioSampleBuffer(tone, 0, totalSamples);
            }
        }

        juce::String error;
        auto clip = djr::AudioClip::createFromFile(file, 48000.0, formats, error);

        check(clip != nullptr, "the pitch test clip loads");

        if (clip != nullptr)
        {
            clip->setWarpEnabled(false);
            const auto beforeLength = clip->getLengthBeats(120.0);
            const auto beforeSource = clip->getNumSourceSamples();

            clip->setPitchSemitones(12);
            clip->prepareWarp(120.0);

            check(clip->getPitchSemitones() == 12, "the clip keeps the pitch it was given");
            check(std::abs(clip->getLengthBeats(120.0) - beforeLength) < 1.0e-9,
                  "pitching a clip does not change how long it lasts on the timeline");
            check(clip->getNumSourceSamples() == beforeSource,
                  "and does not rewrite the audio it came from");

            // An octave up is read twice as fast, so the copy behind it has to
            // be twice as long for the clip to still end where it did.
            juce::AudioBuffer<float> block(2, 4096);
            block.clear();
            clip->addToBuffer(block, 0.0, 120.0, 48000.0);

            check(block.getMagnitude(0, 0, block.getNumSamples()) > 0.01f,
                  "a pitched clip still plays");

            clip->setPitchSemitones(0);
            clip->prepareWarp(120.0);

            check(clip->getPitchSemitones() == 0,
                  "and it can be put back where it was");
        }

        file.deleteFile();
    }

    // Reading a tempo and a pitch back out of audio. Both are guesses, so what
    // is pinned down is that they are the right guess for material where the
    // answer is known, and that they refuse rather than invent for material
    // where there is none.
    {
        constexpr double rate = 44100.0;

        // Four seconds of A440.
        juce::AudioBuffer<float> tone(1, static_cast<int>(rate * 4.0));
        double phase = 0.0;

        for (int i = 0; i < tone.getNumSamples(); ++i)
        {
            tone.setSample(0, i, static_cast<float>(std::sin(phase)) * 0.5f);
            phase += juce::MathConstants<double>::twoPi * 440.0 / rate;
        }

        const auto pitch = djr::AudioAnalysis::detectPitch(tone, tone.getNumSamples(), rate);

        check(std::abs(pitch.frequencyHz - 440.0) < 5.0,
              "a sine at 440 Hz is heard as 440 Hz");
        check(pitch.midiNote == 69, "which is the A above middle C");

        // Eight seconds of clicks at 120 BPM: one every half second.
        juce::AudioBuffer<float> clicks(1, static_cast<int>(rate * 8.0));
        clicks.clear();

        for (int beat = 0; beat < 16; ++beat)
        {
            const auto at = static_cast<int>(beat * rate * 0.5);

            for (int i = 0; i < 400 && at + i < clicks.getNumSamples(); ++i)
                clicks.setSample(0, at + i, (i % 2 == 0 ? 0.8f : -0.8f) * (1.0f - i / 400.0f));
        }

        const auto tempo = djr::AudioAnalysis::detectTempo(clicks, clicks.getNumSamples(), rate);

        check(std::abs(tempo.bpm - 120.0) < 3.0,
              "clicks every half second are heard as 120 BPM");
        check(tempo.confidence > 0.15, "and the answer is confident enough to show");

        // Silence has no tempo and no pitch, and saying so is the point.
        juce::AudioBuffer<float> silence(1, static_cast<int>(rate * 8.0));
        silence.clear();

        check(djr::AudioAnalysis::detectPitch(silence, silence.getNumSamples(), rate).midiNote < 0,
              "silence is not given a pitch");
        check(djr::AudioAnalysis::detectTempo(silence, silence.getNumSamples(), rate).bpm <= 0.0,
              "and not given a tempo either");
    }

    // The preview synth's own controls. What a channel sounds like before an
    // instrument is loaded is now settable, so the settings have to be proved
    // to reach the sound rather than only the window.
    {
        using Synth = djr::SimpleSynth;

        // One held note rendered through a synth, so two shapes can be compared.
        const auto renderNote = [] (Synth& synth, int numSamples)
        {
            juce::AudioBuffer<float> buffer(2, numSamples);
            buffer.clear();

            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);

            synth.render(buffer, midi);
            return buffer;
        };

        Synth sine;
        sine.prepare(sampleRate);
        const auto sineBuffer = renderNote(sine, 4096);

        check(sineBuffer.getMagnitude(0, sineBuffer.getNumSamples()) > 0.01f,
              "the preview synth answers a note with sound");

        Synth square;
        square.prepare(sampleRate);
        square.setWaveform(Synth::Waveform::square);
        const auto squareBuffer = renderNote(square, 4096);

        auto sameShape = true;
        for (int i = 1000; i < 4096 && sameShape; ++i)
            sameShape = std::abs(sineBuffer.getSample(0, i) - squareBuffer.getSample(0, i)) < 1.0e-4f;

        check(! sameShape, "and a different waveform is a different sound");

        // A square holds its level between edges, a sine spends most of its
        // period away from the peak - the crest factor is what separates them.
        const auto crest = [] (const juce::AudioBuffer<float>& buffer)
        {
            auto peak = 0.0f;
            auto sum = 0.0;

            for (int i = 1000; i < buffer.getNumSamples(); ++i)
            {
                const auto value = buffer.getSample(0, i);
                peak = juce::jmax(peak, std::abs(value));
                sum += static_cast<double>(value) * value;
            }

            const auto rms = std::sqrt(sum / (buffer.getNumSamples() - 1000));
            return rms > 0.0 ? peak / static_cast<float>(rms) : 0.0f;
        };

        check(crest(squareBuffer) < crest(sineBuffer),
              "a square sits closer to its peak than a sine does");

        // A long attack has to still be climbing where a short one has arrived.
        Synth slow;
        slow.prepare(sampleRate);
        slow.setEnvelope({ 2.0f, 0.12f, 0.75f, 0.18f });
        const auto slowBuffer = renderNote(slow, 4096);

        check(slowBuffer.getMagnitude(0, 4096) < sineBuffer.getMagnitude(0, 4096) * 0.5f,
              "a two second attack is still quiet where the default has opened");

        // The envelope is read when the note starts, so a knob turned during a
        // note must not retune the note already sounding.
        Synth changed;
        changed.prepare(sampleRate);
        juce::AudioBuffer<float> holding(2, 4096);
        holding.clear();
        juce::MidiBuffer noteOn;
        noteOn.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        changed.render(holding, noteOn);
        changed.setEnvelope({ 2.0f, 0.12f, 0.75f, 0.18f });

        juce::AudioBuffer<float> after(2, 4096);
        after.clear();
        juce::MidiBuffer empty;
        changed.render(after, empty);

        check(after.getMagnitude(0, 4096) > 0.01f,
              "a note already sounding keeps the envelope it began with");

        // Round trip: what the window sets is what a project reads back.
        Synth saved;
        saved.setWaveform(Synth::Waveform::saw);
        saved.setEnvelope({ 0.3f, 0.4f, 0.5f, 0.6f });

        check(! saved.isDefault(), "a shaped preview synth knows it is not the default");

        Synth loaded;
        loaded.fromVar(saved.toVar());

        check(loaded.getWaveform() == Synth::Waveform::saw,
              "the waveform survives a save and an open");
        check(std::abs(loaded.getEnvelope().attack - 0.3f) < 1.0e-4f
                  && std::abs(loaded.getEnvelope().release - 0.6f) < 1.0e-4f,
              "and so does the envelope");

        Synth untouched;
        untouched.fromVar(juce::var());

        check(untouched.isDefault(),
              "a project saved before any of this existed reads back as the old sine");

        // Nothing but the shape: a zero attack would click, so it is clamped.
        Synth clamped;
        clamped.setEnvelope({ -1.0f, 0.0f, 4.0f, -2.0f });

        check(clamped.getEnvelope().attack > 0.0f
                  && juce::approximatelyEqual(clamped.getEnvelope().sustain, 1.0f),
              "an impossible envelope is clamped rather than played");
    }

    // Meters are read in decibels. Drawn linearly, a note at a perfectly normal
    // level sat in the bottom fifth of the bar and riding the fader barely
    // moved it - which is what this arithmetic is here to keep fixed.
    {
        check(juce::approximatelyEqual(djr::Theme::meterPosition(0.0f), 0.0f),
              "silence leaves the meter empty");
        check(juce::approximatelyEqual(djr::Theme::meterPosition(1.0f), 1.0f),
              "full scale fills it");
        check(juce::approximatelyEqual(djr::Theme::meterPosition(2.0f), 1.0f),
              "and past full scale it stays full rather than overflowing");

        // -6 dBFS is half the amplitude and a tenth of the way down the meter.
        const auto half = djr::Theme::meterPosition(0.5f);
        check(half > 0.88f && half < 0.92f, "half the amplitude is 6 dB down, not half way down");

        // The level a preview note actually plays at.
        const auto normal = djr::Theme::meterPosition(0.14f);
        check(normal > 0.6f && normal < 0.75f, "a note at -17 dBFS fills most of the meter");

        check(djr::Theme::meterPosition(0.14f) < djr::Theme::meterPosition(0.26f),
              "and raising the fader moves it up");
        check(juce::approximatelyEqual(djr::Theme::meterPosition(0.001f), 0.0f),
              "-60 dBFS is the floor");
    }

    // --- A loop coming round must not leave a note held down -----------------
    // The note-off a sustained note is waiting for sits at the end of the clip.
    // When the transport wraps back to the top, that note-off is behind the
    // playhead and never gets written, so the instrument goes on holding the
    // note while the next pass strikes it again. The release has to be sent at
    // the wrap instead, which is what this checks.
    {
        djr::MidiTrack track("Loop");
        track.prepare(sampleRate, blockSize);

        // One note long enough to still be sounding when the wrap happens.
        juce::Array<djr::MidiNote> notes;
        djr::MidiNote held;
        held.pitch = 60;
        held.velocity = 0.9f;
        held.startBeat = 0.0;
        held.lengthBeats = 4.0;
        notes.add(held);
        track.setClipNotes(notes);

        const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (tempoBpm / 60.0);

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;

        const auto renderAt = [&] (double startBeat)
        {
            djr::TrackPlaybackContext context;
            context.sampleRate = sampleRate;
            context.tempoBpm = tempoBpm;
            context.startBeat = startBeat;
            context.endBeat = startBeat + beatsPerBlock;
            context.isPlaying = true;
            context.songMode = false;

            buffer.clear();
            track.processAudio(buffer, midi, context);
        };

        // Where pitch 60 is struck or released, counted in the order the
        // instrument reads the block. -1 when the block does not mention it.
        // Position rather than sample offset: both land on sample 0 at a wrap,
        // so only the order tells them apart.
        const auto positionOf = [&] (bool wantNoteOn)
        {
            auto position = 0;

            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();

                if (message.getNoteNumber() == 60
                    && (wantNoteOn ? message.isNoteOn() : message.isNoteOff()))
                    return position;

                ++position;
            }

            return -1;
        };

        renderAt(0.0);
        check(positionOf(true) >= 0, "a note at the top of the pattern is struck");
        check(positionOf(false) < 0, "and not released in the same block");

        // Halfway through the note: nothing to say, it is simply still held.
        renderAt(2.0);
        check(positionOf(true) < 0 && positionOf(false) < 0,
              "a block in the middle of a held note emits nothing");

        // The wrap: the playhead goes back to the top while the note is sounding.
        renderAt(0.0);

        const auto released = positionOf(false);
        const auto struck = positionOf(true);

        check(released >= 0, "a loop coming round releases the note it was holding");
        check(struck >= 0, "and strikes it again for the new pass");
        check(released >= 0 && struck >= 0 && released < struck,
              "in that order, so the instrument is not left holding two of them");
    }

    // --- Switching pattern mid-note must not leave it held down --------------
    // Sibling to the loop-wrap bug above, through a different door: emitNotes
    // tracks activeNotes by pitch alone, with no idea the notes it just read
    // came from a different pattern than the block before. Switch pattern
    // while a note is sounding and the new one owes that pitch nothing - no
    // note-off is ever coming for it unless the switch itself releases it.
    {
        djr::MidiTrack track("Switch");
        track.prepare(sampleRate, blockSize);

        juce::Array<djr::MidiNote> held;
        djr::MidiNote note;
        note.pitch = 60;
        note.velocity = 0.9f;
        note.startBeat = 0.0;
        note.lengthBeats = 4.0;
        held.add(note);
        track.getClip(0).setNotes(held);

        // Empty, so nothing it emits could coincidentally supply the
        // note-off pattern 0 owed.
        track.getClip(1).setNotes({});

        const auto beatsPerBlock = (static_cast<double>(blockSize) / sampleRate) * (tempoBpm / 60.0);

        juce::AudioBuffer<float> buffer(2, blockSize);
        juce::MidiBuffer midi;

        const auto renderAt = [&] (double startBeat)
        {
            djr::TrackPlaybackContext context;
            context.sampleRate = sampleRate;
            context.tempoBpm = tempoBpm;
            context.startBeat = startBeat;
            context.endBeat = startBeat + beatsPerBlock;
            context.isPlaying = true;
            context.songMode = false;

            buffer.clear();
            track.processAudio(buffer, midi, context);
        };

        const auto releasesPitch60 = [&]
        {
            for (const auto metadata : midi)
            {
                const auto message = metadata.getMessage();

                if (message.getNoteNumber() == 60 && message.isNoteOff())
                    return true;
            }

            return false;
        };

        track.setActivePattern(0);
        renderAt(0.0);
        check(! releasesPitch60(), "the note is freshly struck, not released, on its first block");

        // Halfway through the held note, switch to the empty pattern.
        track.setActivePattern(1);
        renderAt(2.0);

        check(releasesPitch60(),
              "switching pattern mid-note releases what the old one left holding");
    }

    // --- Chord: the intervals the piano roll's chord stamp writes -----------
    {
        using namespace djr::Chord;
        using djr::ChordType;

        const auto contains = [] (const std::vector<int>& intervals, int value)
        {
            return std::find(intervals.begin(), intervals.end(), value) != intervals.end();
        };

        // Spot-check the shapes a reader would actually recognise.
        {
            const auto& major = intervalsFor(ChordType::major);
            check(major.size() == 3 && contains(major, 0) && contains(major, 4) && contains(major, 7),
                  "major is a plain root-third-fifth triad");

            const auto& minor = intervalsFor(ChordType::minor);
            check(minor.size() == 3 && contains(minor, 0) && contains(minor, 3) && contains(minor, 7),
                  "minor flattens only the third");

            const auto& dominant7 = intervalsFor(ChordType::dominant7);
            check(dominant7.size() == 4 && contains(dominant7, 4) && contains(dominant7, 10),
                  "a dominant 7th keeps the major third and flattens the seventh");

            check(intervalsFor(ChordType::major) != intervalsFor(ChordType::minor),
                  "major and minor are not secretly the same shape");
        }

        // Every chord type: root present, every interval inside an octave and
        // a bit, and both name functions have something to say.
        for (int i = 0; i < static_cast<int>(ChordType::count); ++i)
        {
            const auto type = static_cast<ChordType>(i);
            const auto& intervals = intervalsFor(type);

            check(! intervals.empty(), "chord type " + juce::String(i) + " has at least one interval");
            check(contains(intervals, 0), "chord type " + juce::String(i) + " includes its own root");

            for (const auto interval : intervals)
                check(interval >= 0 && interval <= 11,
                      "chord type " + juce::String(i) + " keeps every interval within an octave of the root");

            check(nameFor(type).isNotEmpty(), "chord type " + juce::String(i) + " has a menu name");
            check(shortNameFor(type).isNotEmpty(), "chord type " + juce::String(i) + " has a badge name");
        }
    }

    std::cout << (failures == 0 ? "\nAll engine tests passed\n"
                                : "\n" + std::to_string(failures) + " engine test(s) failed\n");
    return failures == 0 ? 0 : 1;
}
