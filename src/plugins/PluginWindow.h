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
                          private juce::ComponentListener,
                          private juce::Timer
{
public:
    /** How long a plugin editor that came back at zero size is given to
        report a real one before it is treated as broken - see the
        constructor and timerCallback(). Public so tests can pump exactly
        this long rather than guess at (or duplicate) the real value.
    */
    static constexpr int editorSizeGracePeriodMs = 2000;

    /** `processor` is the generator this channel holds, or nullptr when the
        track has none and is still playing through its preview synth. The
        channel's own pages work either way - they belong to the track, not to
        the plugin.
    */
    PluginShell(juce::AudioProcessor* processor, Track* track);
    ~PluginShell() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Whether `description`'s own editor should open as its own separate,
        top-level window rather than embedded in this shell - the per-plugin
        choice the detach toggle in the top strip makes. Persisted across
        runs, keyed by format and file/identifier, for a plugin that gives
        real trouble embedded: a Qt-based LV2 UI that pops its own extra
        top-level windows regardless of embedding (QMidiArp's "Pattern"
        editor, among others) gets no worse for it, and one whose embedding
        itself is what is fragile - a native GUI reparented into a window
        with a mismatched GL visual, say - sidesteps that reparenting
        altogether.
    */
    static bool prefersDetachedWindow(const juce::PluginDescription& description);
    static void setPrefersDetachedWindow(const juce::PluginDescription& description, bool detached);

    /** Switches this shell's own plugin editor between embedded (the normal
        case) and living in its own top-level window - what the detach
        toggle does, exposed here too so it can be driven without simulating
        a click. Persists the choice via setPrefersDetachedWindow() when this
        editor came from a real, identifiable plugin instance; a no-op when
        there is no native editor to detach in the first place (the generic
        parameter panel, the no-controls notice, or nothing at all), or when
        `detached` already matches the current state.
    */
    void setDetachedWindowActive(bool detached);
    bool isDetachedWindowActive() const noexcept { return detachedWindowActive; }

    /** The generator page's own plain-text notice, when it is showing one
        (a plugin with no editor and no automatable parameters - see
        buildFallbackEditor()) - empty otherwise (a real editor, the generic
        parameter panel, or nothing was ever built). Exposed so a test can
        tell which specific notice came up (a plugin known to need its own
        explanation, like Carla, versus the generic one) without a general
        way to read arbitrary Component text.
    */
    juce::String getFallbackNoticeText() const;

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
    /** Fires once editorSizeGracePeriodMs has passed since a zero-size
        editor was embedded, unless componentMovedOrResized() already
        cancelled it by then - swaps to the fallback if the editor is still
        zero-size, otherwise does nothing (it sized itself in time).
    */
    void timerCallback() override;
    /** The generic parameter panel, or (for a plugin with no automatable
        parameters at all) a short notice explaining there is nothing to
        show - what an editor that never showed up, or never sized itself,
        is replaced by.
    */
    std::unique_ptr<juce::Component> buildFallbackEditor() const;
    /** Takes the plugin's own editor out of generatorHolder and gives it its
        own top-level window instead, then puts detachedNotice in its place -
        the other half of setDetachedWindowActive(true).
    */
    void showGeneratorEditorDetached();
    /** The reverse: pulls the editor back off the desktop and into
        generatorHolder, and drops detachedNotice - setDetachedWindowActive(false).
    */
    void showGeneratorEditorEmbedded();
    /** Raises the plugin's own window, for detachedNotice's button - easy to
        lose behind this one or minimise by accident, with no taskbar entry
        of its own to recover it from.
    */
    void bringDetachedEditorToFront();
    /** Whichever of generatorEditor or detachedNotice is the thing actually
        meant to be visible in generatorHolder right now - what the sizing
        and centring in getPreferredBounds(), getMinimumBounds() and
        resized() all care about, regardless of which one it is.
    */
    juce::Component* embeddedGeneratorContent() const noexcept;
    /** The description behind `processor`'s identity, for the detach
        preference to be keyed and persisted by - only an AudioPluginInstance
        (a plugin the format manager created) has one; a built-in processor
        does not, and gets no detach toggle at all as a result.
    */
    static bool identityOf(juce::AudioProcessor* processor, juce::PluginDescription& out);
    void sliderValueChanged(juce::Slider* slider) override;
    void comboBoxChanged(juce::ComboBox* box) override;
    void showPage(Page page);
    void refreshPageVisibility();

    void buildTopStrip();
    /** Fills the preset list from the plugin, and hides it when there is
        nothing to fill it with. Most plugins report one nameless program,
        which is a list of nothing worth showing.
    */
    void refreshPresetList();
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

    juce::AudioProcessor* audioProcessor = nullptr;
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
    /** The plugin's own editor, the generic panel when it has none, or the
        preview-synth placeholder when there is no plugin at all.
    */
    std::unique_ptr<juce::Component> generatorEditor;
    /** True when generatorEditor is the plugin's own native GUI rather than
        the generic panel, the no-controls notice, or the preview synth's own
        panel - only that one is ever worth the detach toggle.
    */
    bool hasNativeEditor = false;
    /** Set from identityOf() at construction: whether generatorEditor's
        processor is one the detach preference can actually be keyed and
        persisted by.
    */
    bool hasPluginIdentity = false;
    juce::PluginDescription pluginIdentity;
    /** Whether generatorEditor currently lives on its own, off the desktop,
        rather than embedded in generatorHolder - see setDetachedWindowActive().
    */
    bool detachedWindowActive = false;
    /** Stands in for generatorEditor in generatorHolder while it is
        detached - explains where the plugin's own window went, with a way
        back to it. Never both this and generatorEditor embedded at once.
    */
    std::unique_ptr<juce::Component> detachedNotice;
    /** Toggles setDetachedWindowActive() - shown only when hasNativeEditor
        and hasPluginIdentity both hold, since anything else has no native
        window worth detaching, or no stable identity to remember the choice
        by.
    */
    IconChipButton detachButton { "detach window", Icon::popOut };

    juce::OwnedArray<IconChipButton> tabButtons;
    juce::OwnedArray<TabChip> envelopeTabs;

    /** The plugin's own presets, which JUCE hands over as programs - LV2
        calls them presets, VST3 calls them programs, and a reader only wants
        the list either way.

        Worth having on its own, and worth more than it looks: a plugin whose
        GUI cannot open a file dialog inside a host that is not its own toolkit
        - which is every Qt plugin here - can still be given a preset from
        outside its window.
    */
    juce::ComboBox presetBox;

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
    PluginWindow(juce::AudioProcessor* processor, Track* track);
    ~PluginWindow() override;

    /** The generator this window was opened for, or nullptr for a channel that
        has none. Together with getTrack() it is what identifies a window.
    */
    juce::AudioProcessor* getProcessor() noexcept;
    Track* getTrack() noexcept;
    void closeButtonPressed() override;

private:
    /** Keeps the window from being dragged smaller than the page can survive. */
    void applyResizeLimits();

    juce::AudioProcessor* audioProcessor = nullptr;
    Track* channelTrack = nullptr;
    PluginShell* shell = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace djr
