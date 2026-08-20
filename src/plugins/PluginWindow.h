#pragma once

#include "audio/ChannelSettings.h"
#include "ui/Knob.h"
#include "ui/UiControls.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace djr
{

class Track;

/** The channel shell FL wraps every generator in, ours in DJR colours.

    FL does not open a plugin's GUI on its own: it opens a channel window whose
    first tab holds the generator - its own sampler, or a third-party plugin's
    editor - and whose other tabs are the channel's own envelope and playback
    settings, the same for every generator. The strip along the top belongs to
    the channel too, not to the plugin.

    Built the same way here, so a VST and the built-in synth are framed
    identically. The controls whose engine side does not exist yet are drawn
    and greyed rather than left out: the layout is what is being built, and a
    control that is missing cannot say "not yet".
*/
class PluginShell final : public juce::Component,
                          private juce::Button::Listener,
                          private juce::Slider::Listener,
                          private juce::ComboBox::Listener,
                          private juce::ComponentListener
{
public:
    PluginShell(juce::AudioProcessor& processor, Track* track);
    ~PluginShell() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** The size this shell wants for the page currently showing. */
    juce::Rectangle<int> getPreferredBounds() const;
    /** The smallest this shell may be without breaking what it holds. A plugin
        GUI drawn into a native X11 child window is not clipped by anything
        JUCE owns, so shrinking past it does not scroll - it spills.
    */
    juce::Rectangle<int> getMinimumBounds() const;

    std::function<void()> onPageChanged;

private:
    enum class Page { generator = 0, envelope, misc };

    /** A titled box on one of the settings pages. FL groups its channel
        controls this way, and without the grouping the knobs read as one long
        undifferentiated row.
    */
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
        /** What FL puts in this box, for the ones whose controls are not knobs.
            Named rather than left blank so the layout still says what belongs
            there while the engine side is missing.
        */
        juce::String hint;
    };

    void buttonClicked(juce::Button* button) override;
    /** A plugin's own editor often has no size until well after it is created,
        and some resize themselves later on. The window follows it.
    */
    void componentMovedOrResized(juce::Component& component, bool moved, bool resized) override;
    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* box) override;
    void showPage(Page page);
    void refreshPageVisibility();

    void buildTopStrip();
    void drawSections(juce::Graphics& g, const std::vector<Section>& sections) const;
    void buildEnvelopePage();
    void buildMiscPage();
    /** Every knob on the pages that have no engine behind them yet. */
    Knob* addPending(std::vector<std::unique_ptr<Knob>>& into,
                     juce::Component& parent,
                     const juce::String& caption,
                     Knob::Style style = Knob::Style::unipolar);
    /** A knob the channel actually listens to. */
    Knob* addLive(std::vector<std::unique_ptr<Knob>>& into,
                  juce::Component& parent,
                  const juce::String& caption,
                  Knob::Style style = Knob::Style::unipolar);

    /** The channel this window's settings belong to, or nullptr when the window
        was opened for something that has no track behind it.
    */
    ChannelSettings* channel() const noexcept;
    /** Which envelope the page is showing, from the sub-tab that is lit. */
    ChannelSettings::Target selectedTarget() const noexcept;
    /** Puts the selected target's stored values back on the knobs. Called when
        the sub-tab changes, so each curve keeps its own settings.
    */
    void loadTargetIntoControls();
    /** Sends the envelope and LFO knobs back to the channel. */
    void writeTargetFromControls();
    /** Greys out what this target has no engine for, and what is switched off. */
    void refreshControlStates();
    /** Draws the DAHDSR the knobs currently describe. */
    juce::Path buildEnvelopePath(juce::Rectangle<float> area) const;
    juce::Path buildLfoPath(juce::Rectangle<float> area) const;

    juce::AudioProcessor& audioProcessor;
    Track* channelTrack = nullptr;
    Page currentPage = Page::generator;

    juce::Component generatorPage;
    /** A plugin GUI is a fixed-size picture, not a layout: it is never
        stretched to the window. The viewport lets one bigger than the screen
        be reached, and the holder centres one smaller than the window.
    */
    juce::Viewport generatorViewport;
    juce::Component generatorHolder;
    juce::Component envelopePage;
    juce::Component miscPage;
    /** The plugin's own editor, or the generic panel when it has none. */
    std::unique_ptr<juce::Component> generatorEditor;

    juce::OwnedArray<IconChipButton> tabButtons;
    juce::OwnedArray<TabChip> envelopeTabs;

    SwitchButton onSwitch { "channel on" };
    /** The ON switch's own column, so its caption lines up with the knobs'. */
    juce::Rectangle<int> onColumn;
    juce::Rectangle<int> trackBox;
    Knob panKnob { "PAN", Knob::Style::bipolar };
    Knob volKnob { "VOL" };
    Knob pitchKnob { "PITCH", Knob::Style::bipolar };

    /** Envelope page knobs, in order: the six DAHDSR, the four LFO, then the
        filter's two. The indices are used by the engine wiring, so a knob
        cannot be inserted here without moving them.
    */
    enum EnvelopeKnob
    {
        envDelay = 0, envAttack, envHold, envDecay, envSustain, envRelease,
        lfoDelay, lfoAttack, lfoAmount, lfoSpeed,
        filterCut, filterRes, numEnvelopeKnobs
    };

    /** Misc page knobs, in the order they are built. */
    enum MiscKnob
    {
        levelPan = 0, levelVol, levelModX, levelModY,
        timeGate, timeShift, timeSwing,
        arpTime, arpGate,
        echoFeed, echoPan, echoPitch, echoTime, numMiscKnobs
    };

    SwitchButton envelopeSwitch { "envelope on" };
    SwitchButton lfoSwitch { "lfo on" };
    SwitchButton filterSwitch { "filter on" };
    juce::ComboBox arpDirectionBox;
    juce::ComboBox arpRangeBox;
    /** Set while the controls are being loaded, so writing them back does not
        answer its own change and overwrite what was just read.
    */
    bool loadingControls = false;

    std::vector<std::unique_ptr<Knob>> envelopeKnobs;
    std::vector<std::unique_ptr<Knob>> miscKnobs;

    std::vector<Section> envelopeSections;
    std::vector<Section> miscSections;
    /** Where the envelope curve and the LFO shape are drawn. */
    juce::Rectangle<int> envelopeDisplay;
    juce::Rectangle<int> lfoDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginShell)
};

class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(juce::AudioProcessor& processor, Track* track);
    ~PluginWindow() override;

    juce::AudioProcessor& getProcessor() noexcept;
    void closeButtonPressed() override;

private:
    /** Keeps the window from being dragged smaller than the page can survive. */
    void applyResizeLimits();

    juce::AudioProcessor& audioProcessor;
    PluginShell* shell = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace djr
