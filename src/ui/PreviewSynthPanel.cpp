#include "PreviewSynthPanel.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int panelWidth = 460;
    constexpr int panelHeight = 250;
    constexpr int padding = 14;
    constexpr int sectionHeader = 15;
    constexpr int chipHeight = 22;
    constexpr int knobWidth = 46;
    /** The longest attack, decay or release a knob can ask for. Past a few
        seconds a preview note stops being a preview.
    */
    constexpr float maxSeconds = 4.0f;
    constexpr float minSeconds = 0.001f;

    /** The knobs, in the order they are built and read. */
    enum EnvelopeKnob { attackKnob = 0, decayKnob, sustainKnob, releaseKnob, numKnobs };

    const char* const waveformNames[] = { "Sine", "Triangle", "Saw", "Square", "Noise" };
}

PreviewSynthPanel::PreviewSynthPanel(SimpleSynth& synthToEdit, bool drumKit)
    : synth(synthToEdit), isDrumKit(drumKit)
{
    for (auto* name : waveformNames)
    {
        auto* chip = new TabChip(TRANS(name));
        chip->setClickingTogglesState(true);
        chip->setRadioGroupId(0xD12);
        chip->addListener(this);
        chip->setEnabled(! isDrumKit);
        addAndMakeVisible(chip);
        waveformChips.add(chip);
    }

    for (auto* caption : { "ATT", "DEC", "SUS", "REL" })
    {
        auto knob = std::make_unique<Knob>(caption);
        knob->setRange(0.0, 1.0, 0.001);
        knob->addListener(this);
        // The kit's hits carry their own shape, so these have nothing to act on
        // here. Greyed rather than hidden: the page is the same page either way.
        knob->setAwaitingEngine(isDrumKit);
        addAndMakeVisible(knob.get());
        knobs.push_back(std::move(knob));
    }

    resetButton.setEnabled(! isDrumKit);
    resetButton.addListener(this);
    resetButton.setTooltip(TRANS("Put the waveform and envelope back to their starting values"));
    addAndMakeVisible(resetButton);

    loadFromSynth();

    // Last, not first: sizing is what lays the page out, and a resize before
    // the controls exist lays out nothing. The holder only ever moves this
    // panel afterwards, so that first layout is the only one it gets.
    setSize(panelWidth, panelHeight);
}

PreviewSynthPanel::~PreviewSynthPanel()
{
    for (auto* chip : waveformChips)
        chip->removeListener(this);

    for (auto& knob : knobs)
        knob->removeListener(this);

    resetButton.removeListener(this);
}

float PreviewSynthPanel::secondsFromKnob(double position) noexcept
{
    const auto travel = juce::jlimit(0.0, 1.0, position);
    return minSeconds + static_cast<float>(travel * travel * travel) * maxSeconds;
}

double PreviewSynthPanel::knobFromSeconds(float seconds) noexcept
{
    const auto above = juce::jlimit(0.0f, maxSeconds, seconds - minSeconds);
    return std::cbrt(static_cast<double>(above / maxSeconds));
}

void PreviewSynthPanel::loadFromSynth()
{
    const juce::ScopedValueSetter<bool> guard(loadingControls, true);

    const auto index = static_cast<int>(synth.getWaveform());

    if (auto* chip = waveformChips[index])
        chip->setToggleState(true, juce::dontSendNotification);

    const auto envelope = synth.getEnvelope();
    knobs[attackKnob]->setValue(knobFromSeconds(envelope.attack), juce::dontSendNotification);
    knobs[decayKnob]->setValue(knobFromSeconds(envelope.decay), juce::dontSendNotification);
    knobs[sustainKnob]->setValue(envelope.sustain, juce::dontSendNotification);
    knobs[releaseKnob]->setValue(knobFromSeconds(envelope.release), juce::dontSendNotification);
}

void PreviewSynthPanel::writeToSynth()
{
    if (loadingControls)
        return;

    SimpleSynth::Envelope envelope;
    envelope.attack = secondsFromKnob(knobs[attackKnob]->getValue());
    envelope.decay = secondsFromKnob(knobs[decayKnob]->getValue());
    envelope.sustain = static_cast<float>(knobs[sustainKnob]->getValue());
    envelope.release = secondsFromKnob(knobs[releaseKnob]->getValue());
    synth.setEnvelope(envelope);
}

void PreviewSynthPanel::buttonClicked(juce::Button* button)
{
    if (button == &resetButton)
    {
        synth.resetToDefault();
        loadFromSynth();
        repaint();
        return;
    }

    // A radio group also tells the chip that lost, and it tells it in whatever
    // order it likes: acting on that one too would set the waveform back to
    // whatever was showing before.
    if (loadingControls || ! button->getToggleState())
        return;

    for (int i = 0; i < waveformChips.size(); ++i)
    {
        if (waveformChips[i] != button)
            continue;

        synth.setWaveform(static_cast<SimpleSynth::Waveform>(i));
        return;
    }
}

void PreviewSynthPanel::sliderValueChanged(juce::Slider*)
{
    writeToSynth();
    // Only the envelope picture follows the knobs; the rest of the page is
    // static, and repainting all of it on every drag step would be waste.
    repaint(envelopeBox);
}

juce::Path PreviewSynthPanel::buildEnvelopePath(juce::Rectangle<float> area) const
{
    juce::Path shape;

    if (knobs.size() < numKnobs)
        return shape;

    // Drawn in stage proportions, not in seconds: a four-second release beside
    // a five-millisecond attack would leave the attack invisible, and what the
    // picture is for is the shape.
    const auto travel = [this] (int index)
    {
        return static_cast<float>(knobs[static_cast<size_t>(index)]->getValue());
    };

    const auto sustain = juce::jlimit(0.0f, 1.0f, travel(sustainKnob));
    const auto held = 0.5f;

    auto total = travel(attackKnob) + travel(decayKnob) + held + travel(releaseKnob) + 4.0f * 0.08f;
    total = juce::jmax(0.001f, total);

    const auto plot = area.reduced(6.0f);
    auto x = plot.getX();
    const auto step = [&] (float length) { return (length + 0.08f) / total * plot.getWidth(); };
    const auto levelY = [&] (float level) { return plot.getBottom() - level * plot.getHeight(); };

    shape.startNewSubPath(x, plot.getBottom());
    x += step(travel(attackKnob));
    shape.lineTo(x, plot.getY());
    x += step(travel(decayKnob));
    shape.lineTo(x, levelY(sustain));
    x += step(held);
    shape.lineTo(x, levelY(sustain));
    x += step(travel(releaseKnob));
    shape.lineTo(juce::jmin(x, plot.getRight()), plot.getBottom());

    return shape;
}

void PreviewSynthPanel::paint(juce::Graphics& g)
{
    auto header = getLocalBounds().reduced(padding, 0).withHeight(padding + 20).withTrimmedTop(padding);

    g.setColour(Theme::text());
    g.setFont(Theme::ui(13.0f, true));
    g.drawText(TRANS("Preview instrument"), header, juce::Justification::centredLeft, false);

    g.setColour(Theme::mutedText());
    g.setFont(Theme::ui(11.0f));
    g.drawText(isDrumKit ? TRANS("Drum kit channel - each pad carries its own shape")
                         : TRANS("Playing until an instrument is loaded"),
               header, juce::Justification::centredRight, true);

    // Waveform ---------------------------------------------------------------
    g.setColour(Theme::mutedText());
    g.setFont(Theme::caps(8.5f));
    g.drawText(TRANS("Waveform"), waveRow.withHeight(sectionHeader).translated(0, -sectionHeader),
               juce::Justification::centredLeft, false);

    g.setColour(isDrumKit ? Theme::faintText() : Theme::mutedText());
    g.setFont(Theme::ui(10.5f));
    g.drawText(TRANS("Reset"), resetButton.getBounds(), juce::Justification::centred, false);

    // Envelope ---------------------------------------------------------------
    g.setColour(Theme::panel());
    g.fillRoundedRectangle(envelopeBox.toFloat(), 5.0f);
    g.setColour(Theme::outline());
    g.drawRoundedRectangle(envelopeBox.toFloat(), 5.0f, 1.0f);

    g.setColour(Theme::mutedText());
    g.setFont(Theme::caps(8.5f));
    g.drawText(TRANS("Envelope"),
               envelopeBox.reduced(8, 0).withHeight(sectionHeader).translated(0, 5),
               juce::Justification::centredLeft, false);

    g.setColour(Theme::inset());
    g.fillRoundedRectangle(envelopeDisplay.toFloat(), 3.0f);

    const auto curve = buildEnvelopePath(envelopeDisplay.toFloat());

    g.setColour(isDrumKit ? Theme::mutedText() : Theme::accent());
    g.strokePath(curve, juce::PathStrokeType(1.6f));
}

void PreviewSynthPanel::resized()
{
    auto area = getLocalBounds().reduced(padding);
    area.removeFromTop(padding + 20);

    area.removeFromTop(sectionHeader);
    waveRow = area.removeFromTop(chipHeight);

    auto chips = waveRow;
    resetButton.setBounds(chips.removeFromRight(60));

    for (auto* chip : waveformChips)
    {
        chip->setBounds(chips.removeFromLeft(juce::jmax(56, chip->getPreferredWidth())));
        chips.removeFromLeft(4);
    }

    area.removeFromTop(12);
    envelopeBox = area;

    auto body = envelopeBox.reduced(8, 6).withTrimmedTop(sectionHeader);
    auto knobRow = body.removeFromBottom(Knob::preferredSize);

    for (auto& knob : knobs)
    {
        knob->setBounds(knobRow.removeFromLeft(knobWidth));
        knobRow.removeFromLeft(4);
    }

    envelopeDisplay = body.withTrimmedBottom(6);
}

} // namespace djr
