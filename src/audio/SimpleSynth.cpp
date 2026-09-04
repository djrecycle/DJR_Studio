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

    /** One period of a discontinuous wave lands between two samples, and the
        step it leaves behind is what aliases. PolyBLEP subtracts a small
        polynomial around the jump, which is the cheapest correction that keeps
        a saw playable at the top of the keyboard.
    */
    float polyBlep(double phase, double increment) noexcept
    {
        const auto t = phase / juce::MathConstants<double>::twoPi;
        const auto dt = increment / juce::MathConstants<double>::twoPi;

        if (dt <= 0.0)
            return 0.0f;

        if (t < dt)
        {
            const auto x = t / dt;
            return static_cast<float>(x + x - x * x - 1.0);
        }

        if (t > 1.0 - dt)
        {
            const auto x = (t - 1.0) / dt;
            return static_cast<float>(x * x + x + x + 1.0);
        }

        return 0.0f;
    }

    /** How loud each shape has to be to sit at the level the sine always had.
        A saw at the same peak is a good deal louder to the ear, and switching
        waveform should change the colour, not the volume.
    */
    float levelFor(SimpleSynth::Waveform waveform) noexcept
    {
        switch (waveform)
        {
            case SimpleSynth::Waveform::triangle: return 1.0f;
            case SimpleSynth::Waveform::saw:      return 0.55f;
            case SimpleSynth::Waveform::square:   return 0.45f;
            case SimpleSynth::Waveform::noise:    return 0.40f;

            case SimpleSynth::Waveform::sine:
            case SimpleSynth::Waveform::numWaveforms:
            default:                              return 1.0f;
        }
    }

    /** Sine plus a soft second harmonic, or one of the plainer shapes. */
    class PreviewVoice final : public juce::SynthesiserVoice
    {
    public:
        explicit PreviewVoice(const SimpleSynth& owner)
            : synth(owner)
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

            // Read once, at the start: a note keeps the envelope it began with,
            // the way a generator's own envelope does. The waveform is read per
            // block instead, so turning that knob is heard while a note holds.
            const auto shaping = synth.getEnvelope();
            envelope.setParameters({ juce::jmax(0.001f, shaping.attack),
                                     juce::jmax(0.001f, shaping.decay),
                                     juce::jlimit(0.0f, 1.0f, shaping.sustain),
                                     juce::jmax(0.001f, shaping.release) });

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

            const auto waveform = synth.getWaveform();
            const auto shapeLevel = levelFor(waveform);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto gain = envelope.getNextSample();
                const auto value = waveValue(waveform) * level * shapeLevel * gain;

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
        /** One sample of the chosen shape at the phase the voice is at. */
        float waveValue(SimpleSynth::Waveform waveform) noexcept
        {
            const auto twoPi = juce::MathConstants<double>::twoPi;

            switch (waveform)
            {
                case SimpleSynth::Waveform::triangle:
                {
                    // Straight from the phase: a triangle's corners are gentle
                    // enough that it needs no band limiting to stay usable.
                    const auto t = phase / twoPi;
                    return static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0);
                }

                case SimpleSynth::Waveform::saw:
                {
                    const auto t = phase / twoPi;
                    return static_cast<float>(2.0 * t - 1.0) - polyBlep(phase, phaseIncrement);
                }

                case SimpleSynth::Waveform::square:
                {
                    const auto t = phase / twoPi;
                    auto value = t < 0.5 ? 1.0f : -1.0f;

                    // Two edges per period, and they jump opposite ways: the
                    // correction is added at the one going up and subtracted at
                    // the one going down, half a period later.
                    value += polyBlep(phase, phaseIncrement);
                    value -= polyBlep(std::fmod(phase + juce::MathConstants<double>::pi, twoPi),
                                      phaseIncrement);
                    return value;
                }

                case SimpleSynth::Waveform::noise:
                    return random.nextFloat() * 2.0f - 1.0f;

                case SimpleSynth::Waveform::sine:
                case SimpleSynth::Waveform::numWaveforms:
                default:
                    // The second harmonic is what this synth has always sounded
                    // like, so it stays part of the sine rather than a shape of
                    // its own: an old project has to reopen sounding the same.
                    return static_cast<float>(std::sin(phase) + 0.25 * std::sin(phase * 2.0));
            }
        }

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

        const SimpleSynth& synth;
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
        synth.addVoice(new PreviewVoice(*this));
}

void SimpleSynth::prepare(double sampleRate)
{
    if (sampleRate > 0.0)
        synth.setCurrentPlaybackSampleRate(sampleRate);
}

void SimpleSynth::render(juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi)
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

void SimpleSynth::setWaveform(Waveform newWaveform) noexcept
{
    const auto index = static_cast<int>(newWaveform);

    if (index < 0 || index >= static_cast<int>(Waveform::numWaveforms))
        return;

    waveform.store(index, std::memory_order_relaxed);
}

SimpleSynth::Waveform SimpleSynth::getWaveform() const noexcept
{
    return static_cast<Waveform>(waveform.load(std::memory_order_relaxed));
}

void SimpleSynth::setEnvelope(const Envelope& newEnvelope) noexcept
{
    // Clamped here rather than at every caller: the window, a project file and
    // a test all reach this, and a zero-length attack is a click.
    attack.store(juce::jlimit(0.001f, 10.0f, newEnvelope.attack), std::memory_order_relaxed);
    decay.store(juce::jlimit(0.001f, 10.0f, newEnvelope.decay), std::memory_order_relaxed);
    sustain.store(juce::jlimit(0.0f, 1.0f, newEnvelope.sustain), std::memory_order_relaxed);
    release.store(juce::jlimit(0.001f, 10.0f, newEnvelope.release), std::memory_order_relaxed);
}

SimpleSynth::Envelope SimpleSynth::getEnvelope() const noexcept
{
    Envelope shaping;
    shaping.attack = attack.load(std::memory_order_relaxed);
    shaping.decay = decay.load(std::memory_order_relaxed);
    shaping.sustain = sustain.load(std::memory_order_relaxed);
    shaping.release = release.load(std::memory_order_relaxed);
    return shaping;
}

bool SimpleSynth::isDefault() const noexcept
{
    const Envelope defaults;
    const auto shaping = getEnvelope();

    const auto same = [] (float a, float b) { return std::abs(a - b) < 1.0e-6f; };

    return getWaveform() == Waveform::sine
        && same(shaping.attack, defaults.attack)
        && same(shaping.decay, defaults.decay)
        && same(shaping.sustain, defaults.sustain)
        && same(shaping.release, defaults.release);
}

void SimpleSynth::resetToDefault() noexcept
{
    setWaveform(Waveform::sine);
    setEnvelope(Envelope {});
}

juce::var SimpleSynth::toVar() const
{
    auto* object = new juce::DynamicObject();
    const auto shaping = getEnvelope();

    object->setProperty("waveform", static_cast<int>(getWaveform()));
    object->setProperty("attack", shaping.attack);
    object->setProperty("decay", shaping.decay);
    object->setProperty("sustain", shaping.sustain);
    object->setProperty("release", shaping.release);

    return juce::var(object);
}

void SimpleSynth::fromVar(const juce::var& value)
{
    auto* object = value.getDynamicObject();

    if (object == nullptr)
        return;

    const Envelope defaults;

    const auto number = [object] (const char* name, float fallback)
    {
        const auto stored = object->getProperty(name);
        return stored.isVoid() ? fallback : static_cast<float>(static_cast<double>(stored));
    };

    setWaveform(static_cast<Waveform>(static_cast<int>(object->getProperty("waveform"))));
    setEnvelope({ number("attack", defaults.attack),
                  number("decay", defaults.decay),
                  number("sustain", defaults.sustain),
                  number("release", defaults.release) });
}

} // namespace djr
