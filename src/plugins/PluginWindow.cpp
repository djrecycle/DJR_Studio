#include "PluginWindow.h"

#include "audio/Track.h"
#include "ui/Theme.h"

namespace djr
{

namespace
{
    /** Tall enough for a knob and its caption with room to breathe: a knob is
        Knob::preferredSize high, and anything less clips the caption off.
    */
    constexpr int stripHeight = Knob::preferredSize + 14;
    constexpr int pagePadding = 12;
    constexpr int knobWidth = 40;
    constexpr int knobHeight = Knob::preferredSize;
    /** The settings pages are fixed layouts, unlike the generator page which
        takes whatever size the plugin's editor asks for.
    */
    constexpr int settingsPageWidth = 600;
    constexpr int settingsPageHeight = 258;
    /** Room for a section's title above its contents. */
    constexpr int sectionHeader = 15;
    constexpr int knobPitch = knobWidth + 4;

    /** Lays a run of knobs left to right inside `area`, and returns what is
        left. Every section on these pages is built this way.
    */
    juce::Rectangle<int> placeKnobs(juce::Rectangle<int> area,
                                    const std::vector<std::unique_ptr<Knob>>& knobs,
                                    size_t first,
                                    size_t last)
    {
        for (auto i = first; i < last && i < knobs.size(); ++i)
        {
            knobs[i]->setBounds(area.removeFromLeft(knobWidth).withHeight(knobHeight));
            area.removeFromLeft(4);
        }

        return area;
    }

    /** The inside of a titled box: below the heading, inset from the border. */
    juce::Rectangle<int> sectionBody(juce::Rectangle<int> bounds)
    {
        return bounds.reduced(8, 6).withTrimmedTop(sectionHeader);
    }
}

PluginShell::PluginShell(juce::AudioProcessor& processor, Track* track)
    : audioProcessor(processor), channelTrack(track)
{
    addAndMakeVisible(generatorPage);
    addChildComponent(envelopePage);
    addChildComponent(miscPage);

    // The generator page is the plugin itself: its own editor when it has one,
    // and the generic parameter panel when it has none. hasEditor() can say yes
    // and still hand back nothing, so the result is what decides.
    if (audioProcessor.hasEditor())
        if (auto* editor = audioProcessor.createEditorIfNeeded())
            generatorEditor.reset(editor);

    if (generatorEditor == nullptr)
    {
        auto* generic = new juce::GenericAudioProcessorEditor(audioProcessor);
        const auto height = juce::jlimit(200, 560, generic->getHeight());
        generic->setSize(juce::jmax(460, generic->getWidth()), height);
        generatorEditor.reset(generic);
    }

    generatorViewport.setViewedComponent(&generatorHolder, false);
    generatorViewport.setScrollBarsShown(true, true);
    generatorPage.addAndMakeVisible(generatorViewport);

    generatorHolder.addAndMakeVisible(generatorEditor.get());
    generatorEditor->addComponentListener(this);

    buildTopStrip();
    buildEnvelopePage();
    buildMiscPage();
    loadTargetIntoControls();
    refreshControlStates();
    refreshPageVisibility();
}

PluginShell::~PluginShell()
{
    for (auto* button : tabButtons)
        button->removeListener(this);

    for (auto* chip : envelopeTabs)
        chip->removeListener(this);

    presetBox.removeListener(this);

    for (auto* toggle : { &onSwitch, &envelopeSwitch, &lfoSwitch, &filterSwitch })
        toggle->removeListener(this);

    for (auto& knob : envelopeKnobs)
        knob->removeListener(this);

    for (auto& knob : miscKnobs)
        knob->removeListener(this);

    arpDirectionBox.removeListener(this);
    arpRangeBox.removeListener(this);

    for (auto* knob : { &panKnob, &volKnob, &pitchKnob })
        knob->removeListener(this);

    // The processor has to be told before the editor goes, not after:
    // ~AudioProcessorEditor asserts if it is still the processor's active
    // editor, and a pointer read back after the delete is already dangling.
    // Only editors that came from createEditorIfNeeded are registered; the
    // generic panel is ours alone, so the check covers both cases.
    if (generatorEditor != nullptr)
    {
        generatorEditor->removeComponentListener(this);
        generatorHolder.removeChildComponent(generatorEditor.get());

        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*>(generatorEditor.get()))
            if (audioProcessor.getActiveEditor() == editor)
                audioProcessor.editorBeingDeleted(editor);
    }

    generatorEditor = nullptr;
}

void PluginShell::refreshPresetList()
{
    presetBox.clear(juce::dontSendNotification);

    const auto count = audioProcessor.getNumPrograms();

    // One program is what a plugin with no presets reports, and a list holding
    // only that is a control that promises a choice nobody has.
    if (count < 2)
    {
        presetBox.setVisible(false);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const auto name = audioProcessor.getProgramName(i);
        presetBox.addItem(name.isNotEmpty() ? name : TRANS("Preset ") + juce::String(i + 1), i + 1);
    }

    presetBox.setSelectedId(audioProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setVisible(true);
}

void PluginShell::buildTopStrip()
{
    // FL puts the three tabs at the left of the strip: the generator, the
    // envelopes, and everything else.
    const std::pair<const char*, Icon> tabs[] = {
        { "Generator", Icon::plug },
        { "Envelope and instrument settings", Icon::waveform },
        { "Miscellaneous functions", Icon::gear },
    };

    for (const auto& [tooltip, icon] : tabs)
    {
        auto* button = new IconChipButton(TRANS(tooltip), icon);
        button->setCornerSize(5.0f);
        button->addListener(this);
        addAndMakeVisible(button);
        tabButtons.add(button);
    }

    tabButtons[0]->setHighlighted(true);

    presetBox.addListener(this);
    presetBox.setTextWhenNothingSelected(TRANS("Preset"));
    addChildComponent(presetBox);
    refreshPresetList();

    // The strip is the channel's, not the plugin's, so these drive the track.
    onSwitch.addListener(this);
    onSwitch.setToggleState(channelTrack == nullptr || ! channelTrack->isMuted(),
                            juce::dontSendNotification);
    addAndMakeVisible(onSwitch);

    panKnob.setRange(-1.0, 1.0, 0.01);
    panKnob.setDoubleClickReturnValue(true, 0.0);
    panKnob.setValue(channelTrack != nullptr ? channelTrack->getPan() : 0.0,
                     juce::dontSendNotification);

    volKnob.setRange(0.0, 1.0, 0.01);
    volKnob.setDoubleClickReturnValue(true, 0.8);
    volKnob.setValue(channelTrack != nullptr ? channelTrack->getVolume() : 0.8,
                     juce::dontSendNotification);

    // Live on a channel driven by notes, where transposing means rewriting the
    // notes. On a track that has none it stays grey - see refreshControlStates.
    pitchKnob.setRange(-12.0, 12.0, 1.0);
    pitchKnob.setDoubleClickReturnValue(true, 0.0);

    for (auto* knob : { &panKnob, &volKnob, &pitchKnob })
    {
        knob->addListener(this);
        addAndMakeVisible(knob);
    }
}

Knob* PluginShell::addPending(std::vector<std::unique_ptr<Knob>>& into,
                              juce::Component& parent,
                              const juce::String& caption,
                              Knob::Style style)
{
    auto knob = std::make_unique<Knob>(caption, style);
    knob->setRange(0.0, 1.0, 0.01);
    knob->setAwaitingEngine(true);

    auto* raw = knob.get();
    parent.addAndMakeVisible(raw);
    into.push_back(std::move(knob));
    return raw;
}

Knob* PluginShell::addLive(std::vector<std::unique_ptr<Knob>>& into,
                           juce::Component& parent,
                           const juce::String& caption,
                           Knob::Style style)
{
    auto knob = std::make_unique<Knob>(caption, style);
    knob->setRange(style == Knob::Style::bipolar ? -1.0 : 0.0, 1.0, 0.01);
    knob->setDoubleClickReturnValue(true, style == Knob::Style::bipolar ? 0.0 : 0.0);
    knob->addListener(this);

    auto* raw = knob.get();
    parent.addAndMakeVisible(raw);
    into.push_back(std::move(knob));
    return raw;
}

ChannelSettings* PluginShell::channel() const noexcept
{
    // These settings are driven by notes, so they mean nothing on a track that
    // has none. The page is still drawn there - it is the same shell over every
    // generator - but nothing on it pretends to work.
    if (channelTrack == nullptr)
        return nullptr;

    const auto kind = channelTrack->getKind();

    if (kind != TrackKind::midi && kind != TrackKind::instrument)
        return nullptr;

    return &channelTrack->getChannelSettings();
}

ChannelSettings::Target PluginShell::selectedTarget() const noexcept
{
    for (int i = 0; i < envelopeTabs.size(); ++i)
        if (envelopeTabs[i]->getToggleState())
            return static_cast<ChannelSettings::Target>(i);

    return ChannelSettings::Target::volume;
}

void PluginShell::buildEnvelopePage()
{
    // The tab order is the target order the engine uses, so the lit chip is
    // the whole answer to "which envelope am I editing".
    for (auto* name : { "Panning", "Volume", "Mod X", "Mod Y", "Pitch" })
    {
        auto* chip = new TabChip(TRANS(name));
        chip->setClickingTogglesState(true);
        chip->setRadioGroupId(0xD11);
        chip->addListener(this);
        envelopePage.addAndMakeVisible(chip);
        envelopeTabs.add(chip);
    }

    envelopeTabs[static_cast<int>(ChannelSettings::Target::volume)]
        ->setToggleState(true, juce::dontSendNotification);

    for (auto* caption : { "DELAY", "ATT", "HOLD", "DEC", "SUS", "REL" })
        addLive(envelopeKnobs, envelopePage, caption);

    addLive(envelopeKnobs, envelopePage, "DELAY");
    addLive(envelopeKnobs, envelopePage, "ATT");
    addLive(envelopeKnobs, envelopePage, "AMT", Knob::Style::bipolar);
    addLive(envelopeKnobs, envelopePage, "SPEED");

    addLive(envelopeKnobs, envelopePage, "CUT");
    addLive(envelopeKnobs, envelopePage, "RES");

    // Each box switches on separately: an envelope drawn but not armed is how
    // FL leaves an untouched channel, and it is why loading one changes nothing
    // until you say so.
    for (auto* toggle : { &envelopeSwitch, &lfoSwitch, &filterSwitch })
    {
        toggle->addListener(this);
        envelopePage.addAndMakeVisible(toggle);
    }
}

void PluginShell::buildMiscPage()
{
    for (auto* caption : { "PAN", "VOL", "MOD X", "MOD Y" })
        addPending(miscKnobs, miscPage, caption,
                   juce::String(caption) == "VOL" ? Knob::Style::unipolar : Knob::Style::bipolar);

    for (auto* caption : { "GATE", "SHIFT", "SWING" })
        addPending(miscKnobs, miscPage, caption);

    // The arpeggiator's two, which the channel does listen to.
    addLive(miscKnobs, miscPage, "TIME");
    addLive(miscKnobs, miscPage, "GATE");

    // All four of the echo's. Pitch works here in a way it does not for the
    // pitch envelope: detuning a repeat only means reading the delay line at
    // another rate, where shifting a whole rendered channel would need a
    // proper resampler.
    addLive(miscKnobs, miscPage, "FEED", Knob::Style::unipolar);
    addLive(miscKnobs, miscPage, "PAN", Knob::Style::bipolar);
    addLive(miscKnobs, miscPage, "PITCH", Knob::Style::bipolar);
    addLive(miscKnobs, miscPage, "TIME", Knob::Style::bipolar);

    // Direction picks the pattern and doubles as the arpeggiator's on switch:
    // "off" is a direction in FL too, and one control is one less thing to
    // wonder about than a switch that has to agree with a menu.
    for (auto* name : { "Off", "Up", "Down", "Up / down", "Random" })
        arpDirectionBox.addItem(TRANS(name), arpDirectionBox.getNumItems() + 1);

    for (int octaves = 1; octaves <= 4; ++octaves)
        arpRangeBox.addItem(juce::String(octaves) + TRANS(" oct"), octaves);

    for (auto* box : { &arpDirectionBox, &arpRangeBox })
    {
        box->addListener(this);
        miscPage.addAndMakeVisible(box);
    }
}

void PluginShell::loadTargetIntoControls()
{
    auto* settings = channel();

    if (settings == nullptr)
        return;

    // Nothing written back while this runs: setValue answers with a change
    // callback, and that callback would push the half-loaded knobs at the
    // target it is in the middle of reading.
    const juce::ScopedValueSetter<bool> loading(loadingControls, true);

    const auto target = selectedTarget();
    const auto envelope = settings->getEnvelope(target);
    const auto lfo = settings->getLfo(target);

    const std::pair<int, float> envelopeValues[] = {
        { envDelay, envelope.delay }, { envAttack, envelope.attack },
        { envHold, envelope.hold },   { envDecay, envelope.decay },
        { envSustain, envelope.sustain }, { envRelease, envelope.release },
        { lfoDelay, lfo.delay }, { lfoAttack, lfo.attack },
        { lfoAmount, lfo.amount }, { lfoSpeed, lfo.speed },
        { filterCut, settings->getFilterCutoff() },
        { filterRes, settings->getFilterResonance() },
    };

    for (const auto& [index, value] : envelopeValues)
        if (static_cast<size_t>(index) < envelopeKnobs.size())
            envelopeKnobs[static_cast<size_t>(index)]->setValue(value, juce::dontSendNotification);

    envelopeSwitch.setToggleState(envelope.enabled, juce::dontSendNotification);
    lfoSwitch.setToggleState(lfo.enabled, juce::dontSendNotification);
    filterSwitch.setToggleState(settings->isFilterEnabled(), juce::dontSendNotification);
    pitchKnob.setValue(settings->getPitchSemitones(), juce::dontSendNotification);

    if (static_cast<size_t>(arpGate) < miscKnobs.size())
    {
        miscKnobs[static_cast<size_t>(arpTime)]->setValue(settings->getArpTime(), juce::dontSendNotification);
        miscKnobs[static_cast<size_t>(arpGate)]->setValue(settings->getArpGate(), juce::dontSendNotification);
    }

    if (static_cast<size_t>(echoTime) < miscKnobs.size())
    {
        miscKnobs[static_cast<size_t>(echoFeed)]->setValue(settings->getEchoFeedback(), juce::dontSendNotification);
        miscKnobs[static_cast<size_t>(echoPan)]->setValue(settings->getEchoPan(), juce::dontSendNotification);
        miscKnobs[static_cast<size_t>(echoPitch)]->setValue(settings->getEchoPitch(), juce::dontSendNotification);
        miscKnobs[static_cast<size_t>(echoTime)]->setValue(settings->getEchoTime(), juce::dontSendNotification);
    }

    arpDirectionBox.setSelectedId(static_cast<int>(settings->getArpDirection()) + 1,
                                 juce::dontSendNotification);
    arpRangeBox.setSelectedId(settings->getArpRange(), juce::dontSendNotification);
}

void PluginShell::writeTargetFromControls()
{
    auto* settings = channel();

    if (settings == nullptr || loadingControls || envelopeKnobs.size() < numEnvelopeKnobs)
        return;

    const auto value = [this] (int index)
    {
        return static_cast<float>(envelopeKnobs[static_cast<size_t>(index)]->getValue());
    };

    const auto target = selectedTarget();

    ChannelSettings::Envelope envelope;
    envelope.enabled = envelopeSwitch.getToggleState();
    envelope.delay = value(envDelay);
    envelope.attack = value(envAttack);
    envelope.hold = value(envHold);
    envelope.decay = value(envDecay);
    envelope.sustain = value(envSustain);
    envelope.release = value(envRelease);
    settings->setEnvelope(target, envelope);

    ChannelSettings::Lfo lfo;
    lfo.enabled = lfoSwitch.getToggleState();
    lfo.delay = value(lfoDelay);
    lfo.attack = value(lfoAttack);
    lfo.amount = value(lfoAmount);
    lfo.speed = value(lfoSpeed);
    settings->setLfo(target, lfo);

    // The filter and the arpeggiator belong to the channel rather than to one
    // envelope, so they are written whichever sub-tab is showing.
    settings->setFilterEnabled(filterSwitch.getToggleState());
    settings->setFilterCutoff(value(filterCut));
    settings->setFilterResonance(value(filterRes));
    settings->setPitchSemitones(juce::roundToInt(pitchKnob.getValue()));

    if (static_cast<size_t>(arpGate) < miscKnobs.size())
    {
        settings->setArpTime(static_cast<float>(miscKnobs[static_cast<size_t>(arpTime)]->getValue()));
        settings->setArpGate(static_cast<float>(miscKnobs[static_cast<size_t>(arpGate)]->getValue()));
    }

    if (static_cast<size_t>(echoTime) < miscKnobs.size())
    {
        settings->setEchoFeedback(static_cast<float>(miscKnobs[static_cast<size_t>(echoFeed)]->getValue()));
        settings->setEchoPan(static_cast<float>(miscKnobs[static_cast<size_t>(echoPan)]->getValue()));
        settings->setEchoPitch(static_cast<float>(miscKnobs[static_cast<size_t>(echoPitch)]->getValue()));
        settings->setEchoTime(static_cast<float>(miscKnobs[static_cast<size_t>(echoTime)]->getValue()));
    }
}

void PluginShell::refreshControlStates()
{
    const auto hasChannel = channel() != nullptr;
    // Nothing shifts the pitch of a channel that has already been rendered, so
    // that tab's knobs stay drawn and stay honest about it.
    const auto pitchTarget = selectedTarget() == ChannelSettings::Target::pitch;
    const auto live = hasChannel && ! pitchTarget;

    for (auto* toggle : { &envelopeSwitch, &lfoSwitch })
        toggle->setEnabled(live);

    filterSwitch.setEnabled(hasChannel);

    // The one knob on the strip that belongs to the channel rather than the
    // track. An audio track has no notes to transpose, so there it is drawn
    // and stays honest about doing nothing.
    pitchKnob.setAwaitingEngine(! hasChannel);

    const auto grey = [this] (int first, int last, bool awaiting)
    {
        for (auto i = first; i < last && static_cast<size_t>(i) < envelopeKnobs.size(); ++i)
            envelopeKnobs[static_cast<size_t>(i)]->setAwaitingEngine(awaiting);
    };

    grey(envDelay, lfoDelay, ! (live && envelopeSwitch.getToggleState()));
    grey(lfoDelay, filterCut, ! (live && lfoSwitch.getToggleState()));
    grey(filterCut, numEnvelopeKnobs, ! (hasChannel && filterSwitch.getToggleState()));

    const auto arpOn = hasChannel && arpDirectionBox.getSelectedId() > 1;

    for (auto i = static_cast<size_t>(arpTime); i <= static_cast<size_t>(arpGate) && i < miscKnobs.size(); ++i)
        miscKnobs[i]->setAwaitingEngine(! arpOn);

    // All four echo knobs together. Making the other three wait for feedback
    // was a rule nobody asked for: a knob that will not turn until another one
    // has been turned reads as broken, whatever the reason behind it.
    for (const auto i : { static_cast<size_t>(echoFeed), static_cast<size_t>(echoPan),
                          static_cast<size_t>(echoPitch), static_cast<size_t>(echoTime) })
        if (i < miscKnobs.size())
            miscKnobs[i]->setAwaitingEngine(! hasChannel);

    arpDirectionBox.setEnabled(hasChannel);
    arpRangeBox.setEnabled(arpOn);
}

juce::Path PluginShell::buildEnvelopePath(juce::Rectangle<float> area) const
{
    juce::Path shape;

    if (envelopeKnobs.size() < numEnvelopeKnobs)
        return shape;

    const auto value = [this] (int index)
    {
        return static_cast<float>(envelopeKnobs[static_cast<size_t>(index)]->getValue());
    };

    // Drawn in stage proportions rather than in seconds: a two-second attack
    // beside a one-millisecond decay would leave the decay invisible, and the
    // point of the picture is the shape.
    const auto stages = { value(envDelay), value(envAttack), value(envHold),
                          value(envDecay), 0.6f, value(envRelease) };

    auto total = 0.0f;
    for (const auto stage : stages)
        total += stage + 0.08f;

    const auto plot = area.reduced(6.0f);
    const auto sustain = juce::jlimit(0.0f, 1.0f, value(envSustain));

    auto x = plot.getX();
    const auto step = [&] (float length) { return (length + 0.08f) / total * plot.getWidth(); };
    const auto levelY = [&] (float level) { return plot.getBottom() - level * plot.getHeight(); };

    shape.startNewSubPath(x, plot.getBottom());
    x += step(value(envDelay));
    shape.lineTo(x, plot.getBottom());
    x += step(value(envAttack));
    shape.lineTo(x, plot.getY());
    x += step(value(envHold));
    shape.lineTo(x, plot.getY());
    x += step(value(envDecay));
    shape.lineTo(x, levelY(sustain));
    x += step(0.6f);
    shape.lineTo(x, levelY(sustain));
    x += step(value(envRelease));
    shape.lineTo(juce::jmin(x, plot.getRight()), plot.getBottom());

    return shape;
}

juce::Path PluginShell::buildLfoPath(juce::Rectangle<float> area) const
{
    juce::Path wave;

    if (envelopeKnobs.size() < numEnvelopeKnobs)
        return wave;

    const auto speed = static_cast<float>(envelopeKnobs[lfoSpeed]->getValue());
    const auto amount = static_cast<float>(envelopeKnobs[lfoAmount]->getValue());
    // One cycle at the slowest setting, six at the fastest: enough for the knob
    // to read as speed without the drawing turning into a solid block.
    const auto cycles = 0.5f + speed * 5.5f;
    const auto plot = area.reduced(4.0f);
    const auto steps = juce::jmax(2, static_cast<int>(plot.getWidth()));

    for (int i = 0; i < steps; ++i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(steps - 1);
        const auto x = plot.getX() + proportion * plot.getWidth();
        const auto y = plot.getCentreY()
                     - std::sin(proportion * cycles * juce::MathConstants<float>::twoPi)
                           * plot.getHeight() * 0.42f * amount;

        if (i == 0)
            wave.startNewSubPath(x, y);
        else
            wave.lineTo(x, y);
    }

    return wave;
}

void PluginShell::showPage(Page page)
{
    if (currentPage == page)
        return;

    currentPage = page;
    refreshPageVisibility();

    if (onPageChanged)
        onPageChanged();
}

void PluginShell::refreshPageVisibility()
{
    // Taken out of the hierarchy, not just hidden. A plugin GUI is an embedded
    // native X11 window, and XEmbedComponent watches only the component's
    // bounds and its peer - never its visibility. Hiding an ancestor left the
    // GUI mapped and drawing over whichever settings page was showing, so every
    // tab showed the plugin. Losing its peer is what unmaps it.
    if (generatorEditor != nullptr)
    {
        const auto wanted = currentPage == Page::generator;
        const auto attached = generatorEditor->getParentComponent() != nullptr;

        if (wanted && ! attached)
            generatorHolder.addAndMakeVisible(generatorEditor.get());
        else if (! wanted && attached)
            generatorHolder.removeChildComponent(generatorEditor.get());
    }

    generatorPage.setVisible(currentPage == Page::generator);
    envelopePage.setVisible(currentPage == Page::envelope);
    miscPage.setVisible(currentPage == Page::misc);

    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setHighlighted(i == static_cast<int>(currentPage));

    resized();
    repaint();
}

juce::Rectangle<int> PluginShell::getPreferredBounds() const
{
    auto wanted = juce::Rectangle<int>(settingsPageWidth, stripHeight + settingsPageHeight);

    if (currentPage == Page::generator && generatorEditor != nullptr)
    {
        // A plugin editor that has not sized itself yet reports nothing, and a
        // window the height of its own title bar is worse than a rough guess.
        const auto width = generatorEditor->getWidth() > 0 ? generatorEditor->getWidth() : 640;
        const auto height = generatorEditor->getHeight() > 0 ? generatorEditor->getHeight() : 420;

        wanted = { juce::jmax(460, width), stripHeight + height };
    }

    // A GUI taller than the screen is one nobody can reach the bottom of, and
    // the window manager will not save us from asking for it. Capped here; the
    // viewport gives the rest back as scrolling.
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto room = display->userArea;
        wanted.setWidth(juce::jmin(wanted.getWidth(), room.getWidth() - 40));
        wanted.setHeight(juce::jmin(wanted.getHeight(), room.getHeight() - 60));
    }

    return wanted;
}

juce::Rectangle<int> PluginShell::getMinimumBounds() const
{
    if (currentPage != Page::generator || generatorEditor == nullptr)
        return { 360, stripHeight + 160 };

    // The plugin's own size is the floor. Its GUI usually lives in a native
    // child window, which the viewport cannot clip: made any smaller, the GUI
    // spills over the window instead of scrolling inside it.
    return { juce::jmax(360, generatorEditor->getWidth()),
             stripHeight + juce::jmax(120, generatorEditor->getHeight()) };
}

void PluginShell::componentMovedOrResized(juce::Component& component, bool, bool wasResized)
{
    if (! wasResized || generatorEditor == nullptr || &component != generatorEditor.get())
        return;

    // Only while its page is the one on screen; a resize behind the settings
    // pages would yank the window out from under whoever is using them.
    if (currentPage != Page::generator)
        return;

    // Only grow. The editor is centred in a holder that is at least as big as
    // the viewport, so laying the page out resizes the holder, not the editor -
    // but a plugin that reports its size late must still be able to open the
    // window up. Shrinking here would fight a window the user made bigger.
    const auto wanted = getPreferredBounds();

    if (onPageChanged != nullptr
        && (wanted.getWidth() > getWidth() || wanted.getHeight() > getHeight()))
        onPageChanged();
}

void PluginShell::buttonClicked(juce::Button* button)
{
    for (int i = 0; i < tabButtons.size(); ++i)
    {
        if (button == tabButtons[i])
        {
            showPage(static_cast<Page>(i));
            return;
        }
    }

    if (button == &onSwitch)
    {
        if (channelTrack != nullptr)
            channelTrack->setMuted(! onSwitch.getToggleState());

        return;
    }

    if (button == &envelopeSwitch || button == &lfoSwitch || button == &filterSwitch)
    {
        writeTargetFromControls();
        refreshControlStates();
        repaint();
        return;
    }

    // The sub-tabs pick which curve is being edited. Each target keeps its own
    // values, so switching tab reloads rather than carrying the last one over.
    for (auto* chip : envelopeTabs)
    {
        if (button == chip)
        {
            loadTargetIntoControls();
            refreshControlStates();
            break;
        }
    }

    repaint();
}

void PluginShell::sliderValueChanged(juce::Slider* slider)
{
    if (channelTrack == nullptr)
        return;

    if (slider == &panKnob)
    {
        channelTrack->setPan(static_cast<float>(panKnob.getValue()));
        return;
    }

    if (slider == &volKnob)
    {
        channelTrack->setVolume(static_cast<float>(volKnob.getValue()));
        return;
    }

    // Everything else on these pages is one knob of the channel's own settings,
    // and they are written as a set: reading twelve atomics costs less than
    // working out which one moved.
    writeTargetFromControls();

    // The envelope and the LFO are drawn from these values, so the picture
    // follows the knob instead of contradicting it.
    if (currentPage == Page::envelope)
        repaint();
}

void PluginShell::comboBoxChanged(juce::ComboBox* box)
{
    auto* settings = channel();

    if (settings == nullptr || loadingControls)
        return;

    if (box == &presetBox)
    {
        const auto index = presetBox.getSelectedId() - 1;

        if (index >= 0 && index != audioProcessor.getCurrentProgram())
            audioProcessor.setCurrentProgram(index);

        return;
    }

    if (box == &arpDirectionBox)
        settings->setArpDirection(
            static_cast<ChannelSettings::ArpDirection>(juce::jmax(0, arpDirectionBox.getSelectedId() - 1)));
    else if (box == &arpRangeBox)
        settings->setArpRange(arpRangeBox.getSelectedId());

    refreshControlStates();
}

void PluginShell::paint(juce::Graphics& g)
{
    g.fillAll(Theme::windowBackground());

    // Top strip -------------------------------------------------------------
    auto strip = getLocalBounds().removeFromTop(stripHeight);
    g.setColour(Theme::panelHeader());
    g.fillRect(strip);
    g.setColour(Theme::divider());
    g.fillRect(strip.withHeight(1).withY(strip.getBottom() - 1));

    // The channel this window belongs to, where FL puts its TRACK readout.
    // Same caption band as a knob uses, so the whole row reads as one line.
    g.setColour(Theme::mutedText());
    g.setFont(Theme::caps(8.5f));
    // On a copy: removeFromBottom mutates what it is called on, and doing that
    // to the member shaved twelve more pixels off the column every repaint.
    auto onCaption = onColumn;
    g.drawText("ON", onCaption.removeFromBottom(Knob::captionHeight),
               juce::Justification::centred, false);

    g.setColour(Theme::panelDeep());
    g.fillRoundedRectangle(trackBox.toFloat(), 4.0f);
    g.setColour(Theme::mutedText());
    g.setFont(Theme::caps(8.0f));
    g.drawText("TRACK", trackBox.withHeight(12).translated(0, 4),
               juce::Justification::centred, false);
    g.setColour(channelTrack != nullptr ? Theme::accent() : Theme::faintText());
    g.setFont(Theme::ui(10.5f, true));
    g.drawText(channelTrack != nullptr ? channelTrack->getName() : juce::String("-"),
               trackBox.withTrimmedTop(14), juce::Justification::centred, true);

    if (currentPage == Page::generator)
        return;

    const auto page = currentPage == Page::envelope ? envelopePage.getBounds()
                                                    : miscPage.getBounds();

    if (currentPage == Page::envelope)
    {
        drawSections(g, envelopeSections);

        // The curve is drawn from the knobs themselves, so the picture is what
        // the channel will actually do rather than a decoration beside it.
        const auto curve = envelopeDisplay.translated(page.getX(), page.getY()).toFloat();
        g.setColour(Theme::panelDeep());
        g.fillRoundedRectangle(curve, 3.0f);

        const auto armed = envelopeSwitch.getToggleState() && envelopeSwitch.isEnabled();
        g.setColour(armed ? Theme::accent().withAlpha(0.85f) : Theme::mutedText().withAlpha(0.45f));
        g.strokePath(buildEnvelopePath(curve), juce::PathStrokeType(1.4f));

        const auto lfo = lfoDisplay.translated(page.getX(), page.getY()).toFloat();
        g.setColour(Theme::panelDeep());
        g.fillRoundedRectangle(lfo, 3.0f);

        const auto lfoArmed = lfoSwitch.getToggleState() && lfoSwitch.isEnabled();
        g.setColour(lfoArmed ? Theme::accent().withAlpha(0.85f) : Theme::mutedText().withAlpha(0.45f));
        g.strokePath(buildLfoPath(lfo), juce::PathStrokeType(1.4f));
    }
    else
    {
        drawSections(g, miscSections);
    }

    g.setColour(Theme::faintText());
    g.setFont(Theme::ui(10.0f));
    g.drawText(currentPage == Page::envelope
                   ? TRANS("The channel's own envelope, LFO and filter. A pitch envelope is drawn but not yet played.")
                   : TRANS("The arpeggiator and the echo are live; the rest of this page is drawn while its engine is built."),
               page.withHeight(14).translated(0, page.getHeight() - 14),
               juce::Justification::centredLeft, true);
}

void PluginShell::drawSections(juce::Graphics& g, const std::vector<Section>& sections) const
{
    const auto page = currentPage == Page::envelope ? envelopePage.getBounds()
                                                    : miscPage.getBounds();

    for (const auto& section : sections)
    {
        // Section rectangles are worked out in page coordinates; the shell
        // paints in its own, so they move by the page's origin.
        const auto bounds = section.bounds.translated(page.getX(), page.getY());

        g.setColour(Theme::panel());
        g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
        g.setColour(Theme::outline());
        g.drawRoundedRectangle(bounds.toFloat(), 5.0f, 1.0f);

        g.setColour(Theme::mutedText());
        g.setFont(Theme::caps(8.5f));
        g.drawText(section.title,
                   bounds.reduced(8, 0).withHeight(sectionHeader).translated(0, 5),
                   juce::Justification::centredLeft, false);

        if (section.hint.isEmpty())
            continue;

        g.setColour(Theme::faintText());
        g.setFont(Theme::ui(10.0f));
        g.drawFittedText(section.hint, bounds.reduced(8, 6).withTrimmedTop(sectionHeader),
                         juce::Justification::centred, 2);
    }
}

void PluginShell::resized()
{
    auto area = getLocalBounds();
    auto strip = area.removeFromTop(stripHeight);

    auto left = strip.reduced(pagePadding, 0);

    for (auto* button : tabButtons)
    {
        button->setBounds(left.removeFromLeft(26).withSizeKeepingCentre(24, 24));
        left.removeFromLeft(4);
    }

    // Right of the strip, in FL's order: ON, PAN, VOL, PITCH, then TRACK.
    // Every one of them gets the same column height so their captions sit on
    // one line; sizing a knob taller than the strip clipped it before.
    auto right = strip.reduced(pagePadding, 0);
    trackBox = right.removeFromRight(74).withSizeKeepingCentre(74, stripHeight - 16);
    right.removeFromRight(10);

    const auto column = [&right] (int width)
    {
        return right.removeFromRight(width).withSizeKeepingCentre(width, knobHeight);
    };

    // Between the tabs and the channel's own controls, because it belongs to
    // the plugin rather than to the channel.
    if (presetBox.isVisible())
    {
        const auto width = juce::jlimit(90, 190, right.getWidth() - 260);
        presetBox.setBounds(left.removeFromLeft(width).withSizeKeepingCentre(width, 20));
    }

    for (auto* knob : { &pitchKnob, &volKnob, &panKnob })
    {
        knob->setBounds(column(knobWidth));
        right.removeFromRight(2);
    }

    right.removeFromRight(6);
    onColumn = column(knobWidth);

    // The switch sits where a knob's dial would, so its caption band is free.
    onSwitch.setBounds(onColumn.withTrimmedBottom(Knob::captionHeight)
                               .withSizeKeepingCentre(34, 17));

    generatorPage.setBounds(area);
    envelopePage.setBounds(area.reduced(pagePadding));
    miscPage.setBounds(area.reduced(pagePadding));

    generatorViewport.setBounds(generatorPage.getLocalBounds());

    if (generatorEditor != nullptr && generatorEditor->getParentComponent() != nullptr)
    {
        // The holder is whichever is bigger, so a small GUI sits centred in the
        // window and a large one gets somewhere to scroll to.
        const auto editorSize = generatorEditor->getLocalBounds();
        const auto visible = generatorViewport.getMaximumVisibleWidth() > 0
            ? juce::Rectangle<int>(generatorViewport.getMaximumVisibleWidth(),
                                   generatorViewport.getMaximumVisibleHeight())
            : generatorPage.getLocalBounds();

        generatorHolder.setSize(juce::jmax(editorSize.getWidth(), visible.getWidth()),
                                juce::jmax(editorSize.getHeight(), visible.getHeight()));

        generatorEditor->setTopLeftPosition(
            (generatorHolder.getWidth() - editorSize.getWidth()) / 2,
            (generatorHolder.getHeight() - editorSize.getHeight()) / 2);
    }

    // Envelope page ----------------------------------------------------------
    {
        auto page = envelopePage.getLocalBounds();
        auto tabRow = page.removeFromTop(20);

        for (auto* chip : envelopeTabs)
        {
            chip->setBounds(tabRow.removeFromLeft(chip->getPreferredWidth())
                                  .withSizeKeepingCentre(chip->getPreferredWidth(), 18));
            tabRow.removeFromLeft(4);
        }

        page.removeFromTop(10);

        envelopeSections.clear();

        // Three boxes side by side, the way FL splits this page: the envelope
        // and its curve, the LFO and its shape, then the filter.
        auto row = page.removeFromTop(160);
        auto envelopeBox = row.removeFromLeft(6 * knobPitch + 16);
        row.removeFromLeft(8);
        auto lfoBox = row.removeFromLeft(4 * knobPitch + 16);
        row.removeFromLeft(8);
        auto filterBox = row;

        envelopeSections.push_back({ TRANS("Envelope"), envelopeBox });
        envelopeSections.push_back({ TRANS("LFO"), lfoBox });
        envelopeSections.push_back({ TRANS("Filter"), filterBox });

        // Each box's switch sits in its own heading, at the far end of it: that
        // is the only free space on the page, and it puts the switch where the
        // thing it arms is named.
        const auto headerSwitch = [] (juce::Rectangle<int> box)
        {
            return box.reduced(6, 0).withHeight(sectionHeader).translated(0, 5)
                      .removeFromRight(30).withSizeKeepingCentre(28, 14);
        };

        envelopeSwitch.setBounds(headerSwitch(envelopeBox));
        lfoSwitch.setBounds(headerSwitch(lfoBox));
        filterSwitch.setBounds(headerSwitch(filterBox));

        auto envelopeBody = sectionBody(envelopeBox);
        envelopeDisplay = envelopeBody.removeFromTop(envelopeBody.getHeight() - knobHeight - 6);
        envelopeBody.removeFromTop(6);
        placeKnobs(envelopeBody, envelopeKnobs, 0, 6);

        auto lfoBody = sectionBody(lfoBox);
        lfoDisplay = lfoBody.removeFromTop(lfoBody.getHeight() - knobHeight - 6);
        lfoBody.removeFromTop(6);
        placeKnobs(lfoBody, envelopeKnobs, 6, 10);

        // The filter's two knobs stack, so the box stays as narrow as FL's.
        auto filterBody = sectionBody(filterBox);

        for (size_t i = 10; i < envelopeKnobs.size(); ++i)
        {
            envelopeKnobs[i]->setBounds(filterBody.removeFromTop(knobHeight)
                                                  .withSizeKeepingCentre(knobWidth, knobHeight));
            filterBody.removeFromTop(6);
        }
    }

    // Misc page --------------------------------------------------------------
    {
        auto page = miscPage.getLocalBounds();
        miscSections.clear();

        auto firstRow = page.removeFromTop(knobHeight + sectionHeader + 14);

        auto levelsBox = firstRow.removeFromLeft(4 * knobPitch + 16);
        firstRow.removeFromLeft(8);
        auto polyBox = firstRow.removeFromLeft(96);
        firstRow.removeFromLeft(8);
        auto timeBox = firstRow.removeFromLeft(3 * knobPitch + 16);

        miscSections.push_back({ TRANS("Levels adjustment"), levelsBox });
        miscSections.push_back({ TRANS("Polyphony"), polyBox, "Max / Slide\nPorta / Mono" });
        miscSections.push_back({ TRANS("Time"), timeBox });

        placeKnobs(sectionBody(levelsBox), miscKnobs, 0, 4);
        placeKnobs(sectionBody(timeBox), miscKnobs, 4, 7);

        page.removeFromTop(10);
        auto secondRow = page.removeFromTop(knobHeight + sectionHeader + 14);

        auto groupBox = secondRow.removeFromLeft(96);
        secondRow.removeFromLeft(8);
        // Wider than the two knobs need: the direction and the range are the
        // arpeggiator's real controls, and they are lists rather than dials.
        auto arpBox = secondRow.removeFromLeft(2 * knobPitch + 130);
        secondRow.removeFromLeft(8);
        auto echoBox = secondRow.removeFromLeft(4 * knobPitch + 16);

        miscSections.push_back({ TRANS("Group"), groupBox, "Cut / By\nCut self" });
        miscSections.push_back({ TRANS("Arpeggiator"), arpBox });
        miscSections.push_back({ TRANS("Echo delay / fat mode"), echoBox });

        auto arpBody = placeKnobs(sectionBody(arpBox), miscKnobs, 7, 9);
        arpBody.removeFromLeft(6);
        arpDirectionBox.setBounds(arpBody.removeFromTop(20));
        arpBody.removeFromTop(4);
        arpRangeBox.setBounds(arpBody.removeFromTop(20));
        placeKnobs(sectionBody(echoBox), miscKnobs, 9, miscKnobs.size());
    }
}

//==============================================================================
PluginWindow::PluginWindow(juce::AudioProcessor& processor, Track* track)
    : DocumentWindow(processor.getName(), Theme::panel(), DocumentWindow::closeButton),
      audioProcessor(processor)
{
    setUsingNativeTitleBar(true);

    auto content = std::make_unique<PluginShell>(processor, track);
    shell = content.get();

    // Switching tabs changes what the window should be: the generator page is
    // as big as the plugin asked for, the settings pages are a fixed layout.
    shell->onPageChanged = [this]
    {
        if (shell == nullptr)
            return;

        // Limits before size: setContentComponentSize is clamped by them, and a
        // stale floor from the previous page would clamp to the wrong number.
        applyResizeLimits();

        const auto wanted = shell->getPreferredBounds();
        setContentComponentSize(wanted.getWidth(), wanted.getHeight());
    };

    const auto wanted = content->getPreferredBounds();
    setContentOwned(content.release(), false);
    setResizable(true, false);
    setVisible(true);

    // Order matters: the peer only exists once the window is on screen, and the
    // X11 peer publishes the limits as WM_NORMAL_HINTS the next time its bounds
    // change - so the sizing has to come after the limits, not before.
    applyResizeLimits();
    centreWithSize(wanted.getWidth(), wanted.getHeight());
}

PluginWindow::~PluginWindow()
{
    shell = nullptr;
    clearContentComponent();
}

void PluginWindow::applyResizeLimits()
{
    if (shell == nullptr)
        return;

    const auto minimum = shell->getMinimumBounds();
    const auto border = getBorderThickness().getTopAndBottom() + getTitleBarHeight();

    setResizeLimits(minimum.getWidth(), minimum.getHeight() + border, 4000, 3000);

    // Known gap: on this desktop the limits do not reach the window manager as
    // WM_NORMAL_HINTS, so dragging the frame smaller than a plugin's own GUI is
    // still possible, and that GUI - a native child window no JUCE viewport can
    // clip - spills instead of scrolling. The constrainer is set anyway: it is
    // what JUCE resizes by where the frame is not the window manager's, and it
    // costs nothing where the hints are honoured.
    if (auto* peer = getPeer())
        peer->setConstrainer(getConstrainer());
}

juce::AudioProcessor& PluginWindow::getProcessor() noexcept
{
    return audioProcessor;
}

void PluginWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace djr
