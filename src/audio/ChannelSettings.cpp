#include "ChannelSettings.h"

#include <algorithm>
#include <cmath>

namespace djr
{

namespace
{
    /** Knob to seconds. Squared rather than linear so the short times - the
        ones a percussive envelope lives in - get most of the knob's travel.
    */
    double secondsFrom(float normalised, double longest) noexcept
    {
        const auto clamped = juce::jlimit(0.0f, 1.0f, normalised);
        return static_cast<double>(clamped) * static_cast<double>(clamped) * longest;
    }

    constexpr double delayLongest = 2.0;
    constexpr double attackLongest = 4.0;
    constexpr double holdLongest = 4.0;
    constexpr double decayLongest = 4.0;
    constexpr double releaseLongest = 8.0;
    constexpr double lfoDelayLongest = 4.0;
    constexpr double lfoAttackLongest = 4.0;

    /** The whole audible range, so the knob reads like a filter and not like a
        linear sweep that spends half its travel above anything you can hear.
    */
    double cutoffHzFrom(float normalised) noexcept
    {
        const auto clamped = juce::jlimit(0.0f, 1.0f, normalised);
        return 20.0 * std::pow(1000.0, static_cast<double>(clamped));
    }

    double lfoHzFrom(float normalised) noexcept
    {
        const auto clamped = juce::jlimit(0.0f, 1.0f, normalised);
        return 0.1 * std::pow(200.0, static_cast<double>(clamped));
    }

    /** How far the mod envelopes and LFOs can push the filter, in octaves and
        in resonance. Enough to be unmistakable, not enough to be unusable.
    */
    constexpr float cutoffModOctaves = 4.0f;
    constexpr float resonanceModRange = 0.8f;

    /** Step lengths the arpeggiator's TIME knob picks between, in beats. */
    constexpr double arpStepBeats[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
}

ChannelSettings::ChannelSettings()
{
    resetToDefaults();
}

//==============================================================================
void ChannelSettings::setEnvelope(Target target, const Envelope& envelope) noexcept
{
    auto& stored = envelopes[static_cast<size_t>(target)];
    stored.enabled.store(envelope.enabled, std::memory_order_release);
    stored.delay.store(envelope.delay, std::memory_order_release);
    stored.attack.store(envelope.attack, std::memory_order_release);
    stored.hold.store(envelope.hold, std::memory_order_release);
    stored.decay.store(envelope.decay, std::memory_order_release);
    stored.sustain.store(envelope.sustain, std::memory_order_release);
    stored.release.store(envelope.release, std::memory_order_release);
}

ChannelSettings::Envelope ChannelSettings::getEnvelope(Target target) const noexcept
{
    const auto& stored = envelopes[static_cast<size_t>(target)];

    Envelope envelope;
    envelope.enabled = stored.enabled.load(std::memory_order_acquire);
    envelope.delay = stored.delay.load(std::memory_order_acquire);
    envelope.attack = stored.attack.load(std::memory_order_acquire);
    envelope.hold = stored.hold.load(std::memory_order_acquire);
    envelope.decay = stored.decay.load(std::memory_order_acquire);
    envelope.sustain = stored.sustain.load(std::memory_order_acquire);
    envelope.release = stored.release.load(std::memory_order_acquire);
    return envelope;
}

void ChannelSettings::setLfo(Target target, const Lfo& lfo) noexcept
{
    auto& stored = lfos[static_cast<size_t>(target)];
    stored.enabled.store(lfo.enabled, std::memory_order_release);
    stored.delay.store(lfo.delay, std::memory_order_release);
    stored.attack.store(lfo.attack, std::memory_order_release);
    stored.amount.store(lfo.amount, std::memory_order_release);
    stored.speed.store(lfo.speed, std::memory_order_release);
}

ChannelSettings::Lfo ChannelSettings::getLfo(Target target) const noexcept
{
    const auto& stored = lfos[static_cast<size_t>(target)];

    Lfo lfo;
    lfo.enabled = stored.enabled.load(std::memory_order_acquire);
    lfo.delay = stored.delay.load(std::memory_order_acquire);
    lfo.attack = stored.attack.load(std::memory_order_acquire);
    lfo.amount = stored.amount.load(std::memory_order_acquire);
    lfo.speed = stored.speed.load(std::memory_order_acquire);
    return lfo;
}

void ChannelSettings::setFilterEnabled(bool shouldBeEnabled) noexcept
{
    filterEnabled.store(shouldBeEnabled, std::memory_order_release);
}

bool ChannelSettings::isFilterEnabled() const noexcept
{
    return filterEnabled.load(std::memory_order_acquire);
}

void ChannelSettings::setFilterCutoff(float normalised) noexcept
{
    filterCutoff.store(juce::jlimit(0.0f, 1.0f, normalised), std::memory_order_release);
}

float ChannelSettings::getFilterCutoff() const noexcept
{
    return filterCutoff.load(std::memory_order_acquire);
}

void ChannelSettings::setFilterResonance(float normalised) noexcept
{
    filterResonance.store(juce::jlimit(0.0f, 1.0f, normalised), std::memory_order_release);
}

float ChannelSettings::getFilterResonance() const noexcept
{
    return filterResonance.load(std::memory_order_acquire);
}

void ChannelSettings::setArpDirection(ArpDirection direction) noexcept
{
    arpDirection.store(static_cast<int>(direction), std::memory_order_release);
}

ChannelSettings::ArpDirection ChannelSettings::getArpDirection() const noexcept
{
    return static_cast<ArpDirection>(arpDirection.load(std::memory_order_acquire));
}

void ChannelSettings::setArpRange(int octaves) noexcept
{
    arpRange.store(juce::jlimit(1, 4, octaves), std::memory_order_release);
}

int ChannelSettings::getArpRange() const noexcept
{
    return arpRange.load(std::memory_order_acquire);
}

void ChannelSettings::setArpTime(float normalised) noexcept
{
    arpTime.store(juce::jlimit(0.0f, 1.0f, normalised), std::memory_order_release);
}

float ChannelSettings::getArpTime() const noexcept
{
    return arpTime.load(std::memory_order_acquire);
}

void ChannelSettings::setArpGate(float normalised) noexcept
{
    arpGate.store(juce::jlimit(0.0f, 1.0f, normalised), std::memory_order_release);
}

float ChannelSettings::getArpGate() const noexcept
{
    return arpGate.load(std::memory_order_acquire);
}

bool ChannelSettings::isArpActive() const noexcept
{
    return getArpDirection() != ArpDirection::off;
}

bool ChannelSettings::isActive() const noexcept
{
    if (isFilterEnabled() || isArpActive())
        return true;

    for (int i = 0; i < numTargets; ++i)
    {
        if (envelopes[static_cast<size_t>(i)].enabled.load(std::memory_order_acquire))
            return true;

        if (lfos[static_cast<size_t>(i)].enabled.load(std::memory_order_acquire))
            return true;
    }

    return false;
}

void ChannelSettings::resetToDefaults() noexcept
{
    for (int i = 0; i < numTargets; ++i)
    {
        setEnvelope(static_cast<Target>(i), {});
        setLfo(static_cast<Target>(i), {});
    }

    setFilterEnabled(false);
    setFilterCutoff(1.0f);
    setFilterResonance(0.0f);
    setArpDirection(ArpDirection::off);
    setArpRange(1);
    setArpTime(0.25f);
    setArpGate(0.6f);
}

//==============================================================================
juce::var ChannelSettings::toVar() const
{
    auto* object = new juce::DynamicObject();

    juce::Array<juce::var> envelopeList;
    juce::Array<juce::var> lfoList;

    for (int i = 0; i < numTargets; ++i)
    {
        const auto envelope = getEnvelope(static_cast<Target>(i));
        auto* envelopeObject = new juce::DynamicObject();
        envelopeObject->setProperty("enabled", envelope.enabled);
        envelopeObject->setProperty("delay", envelope.delay);
        envelopeObject->setProperty("attack", envelope.attack);
        envelopeObject->setProperty("hold", envelope.hold);
        envelopeObject->setProperty("decay", envelope.decay);
        envelopeObject->setProperty("sustain", envelope.sustain);
        envelopeObject->setProperty("release", envelope.release);
        envelopeList.add(juce::var(envelopeObject));

        const auto lfo = getLfo(static_cast<Target>(i));
        auto* lfoObject = new juce::DynamicObject();
        lfoObject->setProperty("enabled", lfo.enabled);
        lfoObject->setProperty("delay", lfo.delay);
        lfoObject->setProperty("attack", lfo.attack);
        lfoObject->setProperty("amount", lfo.amount);
        lfoObject->setProperty("speed", lfo.speed);
        lfoList.add(juce::var(lfoObject));
    }

    object->setProperty("envelopes", envelopeList);
    object->setProperty("lfos", lfoList);
    object->setProperty("filterEnabled", isFilterEnabled());
    object->setProperty("filterCutoff", getFilterCutoff());
    object->setProperty("filterResonance", getFilterResonance());
    object->setProperty("arpDirection", static_cast<int>(getArpDirection()));
    object->setProperty("arpRange", getArpRange());
    object->setProperty("arpTime", getArpTime());
    object->setProperty("arpGate", getArpGate());

    return juce::var(object);
}

void ChannelSettings::fromVar(const juce::var& value)
{
    resetToDefaults();

    auto* object = value.getDynamicObject();

    if (object == nullptr)
        return;

    const auto number = [] (const juce::var& source, float fallback)
    {
        return source.isVoid() ? fallback : static_cast<float>(static_cast<double>(source));
    };

    if (const auto* envelopeList = object->getProperty("envelopes").getArray())
    {
        for (int i = 0; i < envelopeList->size() && i < numTargets; ++i)
        {
            auto* envelopeObject = envelopeList->getReference(i).getDynamicObject();

            if (envelopeObject == nullptr)
                continue;

            Envelope envelope;
            envelope.enabled = static_cast<bool>(envelopeObject->getProperty("enabled"));
            envelope.delay = number(envelopeObject->getProperty("delay"), envelope.delay);
            envelope.attack = number(envelopeObject->getProperty("attack"), envelope.attack);
            envelope.hold = number(envelopeObject->getProperty("hold"), envelope.hold);
            envelope.decay = number(envelopeObject->getProperty("decay"), envelope.decay);
            envelope.sustain = number(envelopeObject->getProperty("sustain"), envelope.sustain);
            envelope.release = number(envelopeObject->getProperty("release"), envelope.release);
            setEnvelope(static_cast<Target>(i), envelope);
        }
    }

    if (const auto* lfoList = object->getProperty("lfos").getArray())
    {
        for (int i = 0; i < lfoList->size() && i < numTargets; ++i)
        {
            auto* lfoObject = lfoList->getReference(i).getDynamicObject();

            if (lfoObject == nullptr)
                continue;

            Lfo lfo;
            lfo.enabled = static_cast<bool>(lfoObject->getProperty("enabled"));
            lfo.delay = number(lfoObject->getProperty("delay"), lfo.delay);
            lfo.attack = number(lfoObject->getProperty("attack"), lfo.attack);
            lfo.amount = number(lfoObject->getProperty("amount"), lfo.amount);
            lfo.speed = number(lfoObject->getProperty("speed"), lfo.speed);
            setLfo(static_cast<Target>(i), lfo);
        }
    }

    setFilterEnabled(static_cast<bool>(object->getProperty("filterEnabled")));
    setFilterCutoff(number(object->getProperty("filterCutoff"), 1.0f));
    setFilterResonance(number(object->getProperty("filterResonance"), 0.0f));

    const auto direction = object->getProperty("arpDirection");
    if (! direction.isVoid())
        setArpDirection(static_cast<ArpDirection>(
            juce::jlimit(0, static_cast<int>(ArpDirection::random), static_cast<int>(direction))));

    const auto range = object->getProperty("arpRange");
    if (! range.isVoid())
        setArpRange(static_cast<int>(range));

    setArpTime(number(object->getProperty("arpTime"), 0.25f));
    setArpGate(number(object->getProperty("arpGate"), 0.6f));
}

//==============================================================================
void ChannelSettings::prepare(double newSampleRate)
{
    sampleRate.store(newSampleRate > 0.0 ? newSampleRate : 44100.0, std::memory_order_release);
    reset();
}

void ChannelSettings::reset() noexcept
{
    envelopeStates = {};
    lfoStates = {};
    filterStates = {};
    lfoValues = {};
    gateEventCount = 0;
    soundingNotes = 0;
    gateOpen = false;
    renderGate = false;
    arp = {};
}

void ChannelSettings::pushGateEvent(int sampleOffset, bool opening) noexcept
{
    if (gateEventCount >= maxGateEvents)
        return;

    gateEvents[static_cast<size_t>(gateEventCount++)] = { sampleOffset, opening };
}

void ChannelSettings::trackGate(const juce::MidiBuffer& midi) noexcept
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            // The gate opens on the first note and stays open until the last one
            // goes: retriggering under a held chord would chop the notes that
            // are already sounding.
            if (soundingNotes == 0)
            {
                gateOpen = true;
                pushGateEvent(metadata.samplePosition, true);
            }

            ++soundingNotes;
        }
        else if (message.isNoteOff())
        {
            if (soundingNotes > 0 && --soundingNotes == 0)
            {
                gateOpen = false;
                pushGateEvent(metadata.samplePosition, false);
            }
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            if (soundingNotes > 0)
            {
                soundingNotes = 0;
                gateOpen = false;
                pushGateEvent(metadata.samplePosition, false);
            }
        }
    }
}

int ChannelSettings::nextArpNote() noexcept
{
    std::array<int, 128> sorted {};
    int count = 0;

    for (int note = 0; note < 128; ++note)
        if (arp.held[static_cast<size_t>(note)])
            sorted[static_cast<size_t>(count++)] = note;

    if (count == 0)
        return -1;

    const auto range = juce::jlimit(1, 4, getArpRange());
    const auto total = count * range;
    const auto direction = getArpDirection();

    int index = 0;

    switch (direction)
    {
        case ArpDirection::down:
            index = total - 1 - (arp.stepIndex % total);
            break;

        case ArpDirection::upDown:
        {
            // Turning at the ends without repeating them, which is what makes
            // it read as one line rather than two runs stitched together.
            const auto cycle = juce::jmax(1, total * 2 - 2);
            const auto position = arp.stepIndex % cycle;
            index = position < total ? position : cycle - position;
            break;
        }

        case ArpDirection::random:
            index = arpRandom.nextInt(total);
            break;

        case ArpDirection::off:
        case ArpDirection::up:
        default:
            index = arp.stepIndex % total;
            break;
    }

    ++arp.stepIndex;

    const auto octave = index / count;
    const auto note = sorted[static_cast<size_t>(index % count)] + 12 * octave;
    return juce::jlimit(0, 127, note);
}

void ChannelSettings::runArpeggiator(juce::MidiBuffer& midi, int numSamples, double tempoBpm)
{
    const auto rate = sampleRate.load(std::memory_order_acquire);
    const auto steps = static_cast<int>(std::size(arpStepBeats));
    const auto beats = arpStepBeats[juce::jlimit(0, steps - 1,
                                                 static_cast<int>(getArpTime() * static_cast<float>(steps)))];
    const auto stepSamples = juce::jmax(16.0, 60.0 / juce::jmax(1.0, tempoBpm) * beats * rate);
    // Never the whole step: a note ending exactly where the next one starts
    // leaves the instrument no gap to retrigger in.
    const auto gateSamples = stepSamples * juce::jlimit(0.05, 0.95, static_cast<double>(getArpGate()));

    juce::MidiBuffer output;
    int cursor = 0;

    const auto stopNote = [&] (int offset)
    {
        if (! arp.noteIsSounding)
            return;

        output.addEvent(juce::MidiMessage::noteOff(arp.playingChannel, arp.playingNote), offset);
        arp.noteIsSounding = false;
    };

    // Walks the pattern up to `until`, emitting whatever falls in between. One
    // sample at a time: the whole loop is two comparisons, and stepping it any
    // coarser would drag every note onto a block boundary.
    const auto advanceTo = [&] (int until)
    {
        for (int sample = cursor; sample < until; ++sample)
        {
            if (arp.noteIsSounding && arp.stepPosition >= gateSamples)
                stopNote(sample);

            if (arp.heldCount > 0 && arp.stepPosition >= stepSamples)
            {
                arp.stepPosition -= stepSamples;
                stopNote(sample);

                const auto note = nextArpNote();

                if (note >= 0)
                {
                    output.addEvent(juce::MidiMessage::noteOn(arp.playingChannel, note,
                                                              static_cast<juce::uint8>(arp.playingVelocity)),
                                    sample);
                    arp.playingNote = note;
                    arp.noteIsSounding = true;
                }
            }

            arp.stepPosition += 1.0;
        }

        cursor = juce::jmax(cursor, until);
    };

    const auto releaseAll = [&] (int offset)
    {
        arp.held.fill(false);
        arp.heldCount = 0;
        stopNote(offset);
    };

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto offset = juce::jlimit(0, numSamples, metadata.samplePosition);
        advanceTo(offset);

        if (message.isNoteOn())
        {
            const auto note = juce::jlimit(0, 127, message.getNoteNumber());
            const auto wasSilent = arp.heldCount == 0;

            if (! arp.held[static_cast<size_t>(note)])
            {
                arp.held[static_cast<size_t>(note)] = true;
                ++arp.heldCount;
            }

            // The pattern speaks with the last voice played, and starts from the
            // bottom when the chord is a new one rather than mid-run.
            arp.playingChannel = message.getChannel();
            arp.playingVelocity = juce::jmax(1, static_cast<int>(message.getVelocity()));

            if (wasSilent)
            {
                arp.stepIndex = 0;
                arp.stepPosition = stepSamples;
            }
        }
        else if (message.isNoteOff())
        {
            const auto note = juce::jlimit(0, 127, message.getNoteNumber());

            if (arp.held[static_cast<size_t>(note)])
            {
                arp.held[static_cast<size_t>(note)] = false;
                arp.heldCount = juce::jmax(0, arp.heldCount - 1);
            }

            if (arp.heldCount == 0)
                stopNote(offset);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            releaseAll(offset);
            output.addEvent(message, offset);
        }
        else
        {
            // Everything that is not a note - sustain, wheels, program changes -
            // belongs to the instrument and goes through untouched.
            output.addEvent(message, offset);
        }
    }

    advanceTo(numSamples);
    midi.swapWith(output);
}

//==============================================================================
void ChannelSettings::advanceEnvelope(EnvelopeState& state,
                                      const Envelope& shape,
                                      double seconds,
                                      bool isGateOpen) const noexcept
{
    using Stage = EnvelopeState::Stage;

    if (isGateOpen && (state.stage == Stage::idle || state.stage == Stage::release))
    {
        state.stage = Stage::delay;
        state.elapsed = 0.0;
    }
    else if (! isGateOpen && state.stage != Stage::idle && state.stage != Stage::release)
    {
        state.stage = Stage::release;
        state.elapsed = 0.0;
        state.releaseFrom = state.level;
    }

    state.elapsed += seconds;

    const auto delayTime = secondsFrom(shape.delay, delayLongest);
    const auto attackTime = secondsFrom(shape.attack, attackLongest);
    const auto holdTime = secondsFrom(shape.hold, holdLongest);
    const auto decayTime = secondsFrom(shape.decay, decayLongest);
    const auto releaseTime = secondsFrom(shape.release, releaseLongest);
    const auto sustain = juce::jlimit(0.0f, 1.0f, shape.sustain);

    // Stages roll over one at a time, so a block long enough to cross two of
    // them - or a stage of zero length - still lands in the right place.
    for (int guard = 0; guard < 8; ++guard)
    {
        switch (state.stage)
        {
            case Stage::idle:
                state.level = 0.0f;
                return;

            case Stage::delay:
                state.level = 0.0f;

                if (state.elapsed < delayTime)
                    return;

                state.elapsed -= delayTime;
                state.stage = Stage::attack;
                break;

            case Stage::attack:
                if (state.elapsed < attackTime)
                {
                    state.level = attackTime > 0.0
                        ? static_cast<float>(state.elapsed / attackTime)
                        : 1.0f;
                    return;
                }

                state.elapsed -= attackTime;
                state.stage = Stage::hold;
                break;

            case Stage::hold:
                state.level = 1.0f;

                if (state.elapsed < holdTime)
                    return;

                state.elapsed -= holdTime;
                state.stage = Stage::decay;
                break;

            case Stage::decay:
                if (state.elapsed < decayTime)
                {
                    const auto through = decayTime > 0.0
                        ? static_cast<float>(state.elapsed / decayTime)
                        : 1.0f;
                    state.level = 1.0f - (1.0f - sustain) * through;
                    return;
                }

                state.elapsed -= decayTime;
                state.stage = Stage::sustain;
                break;

            case Stage::sustain:
                state.level = sustain;
                return;

            case Stage::release:
                if (state.elapsed < releaseTime)
                {
                    const auto through = releaseTime > 0.0
                        ? static_cast<float>(state.elapsed / releaseTime)
                        : 1.0f;
                    state.level = state.releaseFrom * (1.0f - through);
                    return;
                }

                state.level = 0.0f;
                state.stage = Stage::idle;
                return;
        }
    }
}

float ChannelSettings::advanceLfo(LfoState& state, const Lfo& shape, double seconds) const noexcept
{
    state.elapsed += seconds;

    const auto delayTime = secondsFrom(shape.delay, lfoDelayLongest);

    if (state.elapsed < delayTime)
        return 0.0f;

    const auto attackTime = secondsFrom(shape.attack, lfoAttackLongest);
    const auto sinceDelay = state.elapsed - delayTime;
    const auto ramp = attackTime > 0.0
        ? juce::jlimit(0.0, 1.0, sinceDelay / attackTime)
        : 1.0;

    state.phase += lfoHzFrom(shape.speed) * seconds;

    if (state.phase >= 1.0)
        state.phase -= std::floor(state.phase);

    return static_cast<float>(std::sin(state.phase * juce::MathConstants<double>::twoPi) * ramp);
}

ChannelSettings::Modulation ChannelSettings::currentModulation() const noexcept
{
    Modulation modulation;

    const auto envelopeLevel = [this] (Target target)
    {
        return envelopeStates[static_cast<size_t>(target)].level;
    };

    const auto lfoValue = [this] (Target target)
    {
        return lfoValues[static_cast<size_t>(target)];
    };

    const auto& volumeEnvelope = envelopes[static_cast<size_t>(Target::volume)];
    const auto& volumeLfo = lfos[static_cast<size_t>(Target::volume)];

    if (volumeEnvelope.enabled.load(std::memory_order_acquire))
        modulation.gain *= envelopeLevel(Target::volume);

    if (volumeLfo.enabled.load(std::memory_order_acquire))
        modulation.gain *= juce::jlimit(0.0f, 2.0f,
                                        1.0f + volumeLfo.amount.load(std::memory_order_acquire)
                                                   * lfoValue(Target::volume) * 0.5f);

    const auto& panEnvelope = envelopes[static_cast<size_t>(Target::panning)];
    const auto& panLfo = lfos[static_cast<size_t>(Target::panning)];

    // An envelope reads 0..1 and panning is two-sided, so the curve sweeps the
    // whole width: silent is hard left, peak is hard right, as FL does it.
    if (panEnvelope.enabled.load(std::memory_order_acquire))
        modulation.pan += envelopeLevel(Target::panning) * 2.0f - 1.0f;

    if (panLfo.enabled.load(std::memory_order_acquire))
        modulation.pan += panLfo.amount.load(std::memory_order_acquire) * lfoValue(Target::panning);

    const auto& cutoffEnvelope = envelopes[static_cast<size_t>(Target::modX)];
    const auto& cutoffLfo = lfos[static_cast<size_t>(Target::modX)];

    if (cutoffEnvelope.enabled.load(std::memory_order_acquire))
        modulation.cutoffOctaves += envelopeLevel(Target::modX) * cutoffModOctaves;

    if (cutoffLfo.enabled.load(std::memory_order_acquire))
        modulation.cutoffOctaves += cutoffLfo.amount.load(std::memory_order_acquire)
                                  * lfoValue(Target::modX) * cutoffModOctaves;

    const auto& resonanceEnvelope = envelopes[static_cast<size_t>(Target::modY)];
    const auto& resonanceLfo = lfos[static_cast<size_t>(Target::modY)];

    if (resonanceEnvelope.enabled.load(std::memory_order_acquire))
        modulation.resonance += envelopeLevel(Target::modY) * resonanceModRange;

    if (resonanceLfo.enabled.load(std::memory_order_acquire))
        modulation.resonance += resonanceLfo.amount.load(std::memory_order_acquire)
                              * lfoValue(Target::modY) * resonanceModRange;

    modulation.pan = juce::jlimit(-1.0f, 1.0f, modulation.pan);
    return modulation;
}

void ChannelSettings::advanceModulation(double seconds) noexcept
{
    for (int i = 0; i < numTargets; ++i)
    {
        const auto target = static_cast<Target>(i);
        const auto index = static_cast<size_t>(i);

        if (envelopes[index].enabled.load(std::memory_order_acquire))
            advanceEnvelope(envelopeStates[index], getEnvelope(target), seconds, renderGate);

        if (lfos[index].enabled.load(std::memory_order_acquire))
            lfoValues[index] = advanceLfo(lfoStates[index], getLfo(target), seconds);
    }
}

void ChannelSettings::processAudio(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0 || ! isActive())
    {
        gateEventCount = 0;
        return;
    }

    const auto rate = sampleRate.load(std::memory_order_acquire);
    const auto filtering = isFilterEnabled();
    const auto baseCutoff = cutoffHzFrom(getFilterCutoff());
    const auto baseResonance = getFilterResonance();

    int nextEvent = 0;

    for (int start = 0; start < numSamples; start += modulationChunk)
    {
        const auto end = juce::jmin(start + modulationChunk, numSamples);
        const auto length = end - start;

        // A gate change lands on its chunk rather than its exact sample. At
        // ~0.7ms that is inaudible, and it keeps the modulation to one pass per
        // chunk instead of one per sample.
        while (nextEvent < gateEventCount && gateEvents[static_cast<size_t>(nextEvent)].sampleOffset < end)
            renderGate = gateEvents[static_cast<size_t>(nextEvent++)].opening;

        const auto from = currentModulation();
        advanceModulation(static_cast<double>(length) / rate);
        const auto to = currentModulation();

        const auto gains = [] (const Modulation& modulation)
        {
            return std::pair<float, float> {
                modulation.gain * (modulation.pan > 0.0f ? 1.0f - modulation.pan : 1.0f),
                modulation.gain * (modulation.pan < 0.0f ? 1.0f + modulation.pan : 1.0f)
            };
        };

        const auto [leftFrom, rightFrom] = gains(from);
        const auto [leftTo, rightTo] = gains(to);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            // Anything past a stereo pair follows the right-hand side rather
            // than being left unprocessed and louder than the rest.
            const auto startGain = channel == 0 ? leftFrom : rightFrom;
            const auto endGain = channel == 0 ? leftTo : rightTo;

            if (juce::approximatelyEqual(startGain, endGain))
                buffer.applyGain(channel, start, length, startGain);
            else
                buffer.applyGainRamp(channel, start, length, startGain, endGain);
        }

        if (! filtering)
            continue;

        // Coefficients once per chunk: tan() per sample would cost more than
        // the filter itself, and the cutoff cannot move far in 32 samples.
        const auto cutoffHz = juce::jlimit(20.0, rate * 0.45,
                                           baseCutoff * std::pow(2.0, static_cast<double>(to.cutoffOctaves)));
        const auto resonance = juce::jlimit(0.0f, 1.0f, baseResonance + to.resonance);
        const auto q = 0.707 + static_cast<double>(resonance) * 11.0;
        const auto g = std::tan(juce::MathConstants<double>::pi * cutoffHz / rate);
        const auto k = 1.0 / q;
        const auto a1 = 1.0 / (1.0 + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        for (int channel = 0; channel < juce::jmin(numChannels, 2); ++channel)
        {
            auto& state = filterStates[static_cast<size_t>(channel)];
            auto* samples = buffer.getWritePointer(channel);

            for (int sample = start; sample < end; ++sample)
            {
                const auto input = static_cast<double>(samples[sample]);
                const auto v3 = input - state.ic2;
                const auto v1 = a1 * state.ic1 + a2 * v3;
                const auto v2 = state.ic2 + a2 * state.ic1 + a3 * v3;

                state.ic1 = 2.0 * v1 - state.ic1;
                state.ic2 = 2.0 * v2 - state.ic2;

                samples[sample] = static_cast<float>(v2);
            }
        }
    }

    // Whatever the events did not say, the block-level gate did: a block whose
    // MIDI ran without audio behind it would otherwise leave the render holding
    // a gate that has already changed.
    renderGate = gateOpen;
    gateEventCount = 0;
}

void ChannelSettings::processMidi(juce::MidiBuffer& midi, int numSamples, double tempoBpm)
{
    gateEventCount = 0;

    if (isArpActive())
        runArpeggiator(midi, numSamples, tempoBpm);

    // After the arpeggiator, so the envelopes follow the notes that are
    // actually played rather than the ones that were held down.
    trackGate(midi);
}

} // namespace djr
