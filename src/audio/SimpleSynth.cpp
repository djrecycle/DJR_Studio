#include "SimpleSynth.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int numVoices = 16;

    struct PreviewSound final : public juce::SynthesiserSound
    {
        bool appliesToNote(int) override    { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    /** Which synthesised hit a General MIDI percussion pitch maps to. */
    enum class DrumKind
    {
        kick,
        snare,
        closedHat,
        openHat,
        clap,
        tom
    };

    DrumKind drumKindForPitch(int pitch) noexcept
    {
        switch (pitch)
        {
            case 35: case 36:                     return DrumKind::kick;
            case 38: case 40:                     return DrumKind::snare;
            case 39:                              return DrumKind::clap;
            case 42: case 44:                     return DrumKind::closedHat;
            case 46:                              return DrumKind::openHat;
            default:                              return DrumKind::tom;
        }
    }

    struct DrumShape
    {
        double startFrequency;
        double endFrequency;
        double pitchDecaySeconds;
        double amplitudeDecaySeconds;
        float noiseAmount;
        float toneAmount;
    };

    DrumShape shapeFor(DrumKind kind, int pitch) noexcept
    {
        switch (kind)
        {
            case DrumKind::kick:      return { 150.0,  48.0, 0.045, 0.34, 0.02f, 1.00f };
            case DrumKind::snare:     return { 210.0, 170.0, 0.030, 0.18, 0.80f, 0.45f };
            case DrumKind::clap:      return { 900.0, 700.0, 0.020, 0.22, 1.00f, 0.05f };
            case DrumKind::closedHat: return { 8000.0, 7000.0, 0.010, 0.045, 1.00f, 0.0f };
            case DrumKind::openHat:   return { 8000.0, 7000.0, 0.010, 0.32, 1.00f, 0.0f };

            case DrumKind::tom:
            default:
            {
                // Toms follow the note, so the remaining pads stay musically useful.
                const auto base = juce::MidiMessage::getMidiNoteInHertz(juce::jlimit(36, 72, pitch));
                return { base * 1.6, base, 0.05, 0.28, 0.05f, 1.0f };
            }
        }
    }

    /** Sine plus a soft second harmonic, so held notes are audible but not harsh. */
    class PreviewVoice final : public juce::SynthesiserVoice
    {
    public:
        PreviewVoice()
        {
            envelope.setParameters({ 0.005f, 0.12f, 0.75f, 0.18f });
        }

        void setDrumMode(bool shouldUseDrums) noexcept { drumMode = shouldUseDrums; }

        bool canPlaySound(juce::SynthesiserSound* sound) override
        {
            return dynamic_cast<PreviewSound*>(sound) != nullptr;
        }

        void setCurrentPlaybackSampleRate(double newRate) override
        {
            juce::SynthesiserVoice::setCurrentPlaybackSampleRate(newRate);

            if (newRate > 0.0)
                envelope.setSampleRate(newRate);
        }

        void startNote(int midiNoteNumber,
                       float velocity,
                       juce::SynthesiserSound*,
                       int) override
        {
            phase = 0.0;
            level = juce::jlimit(0.0f, 1.0f, velocity);
            const auto sampleRate = getSampleRate();

            if (drumMode)
            {
                playingDrum = true;
                shape = shapeFor(drumKindForPitch(midiNoteNumber), midiNoteNumber);
                samplesElapsed = 0;
                lastNoise = 0.0f;
                level *= 0.5f;
                return;
            }

            playingDrum = false;
            phaseIncrement = sampleRate > 0.0
                ? juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber) / sampleRate
                : 0.0;

            level *= 0.22f;
            envelope.noteOn();
        }

        void stopNote(float, bool allowTailOff) override
        {
            // A drum hit owns its own length; a note-off must not cut it short.
            if (playingDrum)
                return;

            if (allowTailOff)
            {
                envelope.noteOff();
                return;
            }

            envelope.reset();
            clearCurrentNote();
        }

        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}

        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
        {
            if (playingDrum)
            {
                renderDrum(outputBuffer, startSample, numSamples);
                return;
            }

            if (! envelope.isActive())
                return;

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto gain = envelope.getNextSample();
                const auto value = static_cast<float>(std::sin(phase) + 0.25 * std::sin(phase * 2.0))
                                 * level * gain;

                for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                    outputBuffer.addSample(channel, startSample + sample, value);

                phase += phaseIncrement;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;
            }

            if (! envelope.isActive())
                clearCurrentNote();
        }

    private:
        void renderDrum(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
        {
            const auto sampleRate = getSampleRate();

            if (sampleRate <= 0.0)
            {
                clearCurrentNote();
                return;
            }

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto seconds = samplesElapsed / sampleRate;
                const auto amplitude = std::exp(-seconds / shape.amplitudeDecaySeconds);

                if (amplitude < 0.0005)
                {
                    clearCurrentNote();
                    return;
                }

                // Pitch sweep gives the kick and toms their punch.
                const auto sweep = std::exp(-seconds / shape.pitchDecaySeconds);
                const auto frequency = shape.endFrequency
                                     + (shape.startFrequency - shape.endFrequency) * sweep;

                phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
                if (phase >= juce::MathConstants<double>::twoPi)
                    phase -= juce::MathConstants<double>::twoPi;

                const auto tone = static_cast<float>(std::sin(phase)) * shape.toneAmount;

                // Differentiated white noise: a cheap high pass that makes hats
                // and claps sit above the tonal parts.
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                const auto noise = (white - lastNoise) * 0.5f * shape.noiseAmount;
                lastNoise = white;

                const auto value = (tone + noise) * level * static_cast<float>(amplitude);

                for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                    outputBuffer.addSample(channel, startSample + sample, value);

                ++samplesElapsed;
            }
        }

        juce::ADSR envelope;
        juce::Random random;
        DrumShape shape {};
        double phase = 0.0;
        double phaseIncrement = 0.0;
        double samplesElapsed = 0.0;
        float level = 0.0f;
        float lastNoise = 0.0f;
        bool drumMode = false;
        bool playingDrum = false;
    };
}

SimpleSynth::SimpleSynth()
{
    synth.addSound(new PreviewSound());

    for (int i = 0; i < numVoices; ++i)
        synth.addVoice(new PreviewVoice());
}

void SimpleSynth::prepare(double sampleRate)
{
    if (sampleRate > 0.0)
        synth.setCurrentPlaybackSampleRate(sampleRate);
}

void SimpleSynth::render(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
}

void SimpleSynth::allNotesOff()
{
    synth.allNotesOff(0, false);
}

void SimpleSynth::setDrumMode(bool shouldUseDrums)
{
    drumMode = shouldUseDrums;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<PreviewVoice*>(synth.getVoice(i)))
            voice->setDrumMode(shouldUseDrums);
}

bool SimpleSynth::isDrumMode() const noexcept
{
    return drumMode;
}

} // namespace djr
