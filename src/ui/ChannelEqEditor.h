#pragma once

#include "Knob.h"
#include "UiControls.h"
#include "audio/ChannelEq.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** The editor a mixer strip's EQ row opens in a call-out: the response curve,
    Low / Mid / Freq / High knobs, an on switch and a reset.

    Writes straight to the track's ChannelEq - the audio thread picks changes
    up on its next block - and tells the owner, so the strip redraws its mini
    curve and the project is marked unsaved.
*/
class ChannelEqEditor final : public juce::Component,
                              private juce::Button::Listener
{
public:
    ChannelEqEditor(ChannelEq& eq, const juce::String& trackName, juce::Colour accent, double sampleRate);
    ~ChannelEqEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void()> onChange;

    /** The response of `settings`, drawn into `area` with a 0 dB line and
        +/-15 dB of range - the editor's curve and the strip's miniature.
        Log frequency, 20 Hz to 20 kHz.
    */
    static void drawCurve(juce::Graphics& g,
                          juce::Rectangle<float> area,
                          const ChannelEq::Settings& settings,
                          double sampleRate,
                          juce::Colour colour,
                          bool withGrid);

    static constexpr int preferredWidth = 286;
    static constexpr int preferredHeight = 196;

private:
    void buttonClicked(juce::Button* button) override;
    void pushSettings();
    void pullSettings();
    juce::Rectangle<int> getCurveBounds() const;

    ChannelEq& eq;
    juce::String trackName;
    juce::Colour accent;
    double sampleRate;

    SwitchButton onSwitch { "EQ on" };
    PillButton resetButton { TRANS("Reset") };
    Knob lowKnob { "Low", Knob::Style::bipolar };
    Knob midKnob { "Mid", Knob::Style::bipolar };
    Knob freqKnob { "Freq" };
    Knob highKnob { "High", Knob::Style::bipolar };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelEqEditor)
};

} // namespace djr
