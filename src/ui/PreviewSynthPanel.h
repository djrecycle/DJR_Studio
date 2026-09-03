#pragma once

#include "Knob.h"
#include "UiControls.h"

#include "audio/SimpleSynth.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace djr
{

/** The Generator page of a channel that has no plugin in it.

    A MIDI channel is not silent before an instrument is loaded - it plays
    through the preview synth - so this is the page that says what that sounds
    like and lets it be changed: one waveform and one amplitude envelope, which
    is what FL gives a channel before a real generator takes the slot.

    Deliberately not a plugin: the preview synth is part of the track, and
    wrapping it in an AudioProcessor only to reach it through the same window
    would buy nothing but a parameter list nobody asked for.
*/
class PreviewSynthPanel final : public juce::Component,
                                private juce::Button::Listener,
                                private juce::Slider::Listener
{
public:
    /** `drumKit` is the step sequencer's channel: its pads each carry their own
        shape, so the controls here would have nothing to act on.
    */
    PreviewSynthPanel(SimpleSynth& synthToEdit, bool drumKit);
    ~PreviewSynthPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void buttonClicked(juce::Button* button) override;
    void sliderValueChanged(juce::Slider* slider) override;

    /** Knob travel to seconds. Cubed, so the short times a note actually needs
        are spread over most of the turn instead of crowding the first degree.
    */
    static float secondsFromKnob(double position) noexcept;
    static double knobFromSeconds(float seconds) noexcept;

    void loadFromSynth();
    void writeToSynth();
    /** The envelope the knobs currently describe, drawn over the display area. */
    juce::Path buildEnvelopePath(juce::Rectangle<float> area) const;

    SimpleSynth& synth;
    const bool isDrumKit;

    juce::OwnedArray<TabChip> waveformChips;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::Rectangle<int> waveRow;
    juce::Rectangle<int> envelopeBox;
    juce::Rectangle<int> envelopeDisplay;
    /** Set while the controls are being filled in, so writing them back does
        not answer its own change.
    */
    bool loadingControls = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreviewSynthPanel)
};

} // namespace djr
