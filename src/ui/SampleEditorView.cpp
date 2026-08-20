#include "SampleEditorView.h"

#include "Theme.h"

#include <cmath>

namespace djr
{

namespace
{
    constexpr int headerHeight = 34;
    constexpr int rulerHeight = 18;
    constexpr int edgePadding = 8;
    /** Below this many pixels per sample the samples are drawn as a line; above
        it there is room to mark each one, which is what "sample level" means.
    */
    constexpr double pixelsPerSampleForDots = 5.0;
    /** How far in the zoom goes: sixty-four pixels for one sample is past any
        useful detail, and it stops the arithmetic running away.
    */
    constexpr double finestSamplesPerPixel = 1.0 / 64.0;
}

SampleEditorView::SampleEditorView()
{
    normaliseButton.setStyle(PillButton::Style::outline);
    reverseButton.setStyle(PillButton::Style::outline);
    fitButton.setStyle(PillButton::Style::ghost);

    for (auto* button : { &normaliseButton, &reverseButton, &exportButton, &fitButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    for (auto* button : { &zoomInButton, &zoomOutButton })
    {
        button->addListener(this);
        addAndMakeVisible(button);
    }

    zoomOutButton.setIconInset(8.0f);
    setWantsKeyboardFocus(false);
}

SampleEditorView::~SampleEditorView()
{
    for (auto* button : { &normaliseButton, &reverseButton, &exportButton, &fitButton })
        button->removeListener(this);

    for (auto* button : { &zoomInButton, &zoomOutButton })
        button->removeListener(this);
}

void SampleEditorView::setClipSource(std::function<AudioClip*()> source, const juce::String& title)
{
    clipSource = std::move(source);
    clipTitle = title;
    refreshClipPointer();
    zoomToFit();
    repaint();
}

void SampleEditorView::refreshClipPointer()
{
    clip = clipSource ? clipSource() : nullptr;
}

void SampleEditorView::refresh()
{
    refreshClipPointer();
    clampView();
    repaint();
}

void SampleEditorView::setExportContext(juce::AudioFormatManager* formats, juce::File defaultFolder)
{
    exportFormats = formats;
    exportFolder = std::move(defaultFolder);
}

void SampleEditorView::setExportCallback(std::function<void(const juce::File&, const juce::String&)> callback)
{
    exportCallback = std::move(callback);
}

void SampleEditorView::setEditCallback(std::function<void(const juce::String&)> callback)
{
    editCallback = std::move(callback);
}

void SampleEditorView::setBeforeEditCallback(std::function<void(const juce::String&)> callback)
{
    beforeEditCallback = std::move(callback);
}

//==============================================================================
int SampleEditorView::getTotalSamples() const
{
    return clip != nullptr ? clip->getNumSourceSamples() : 0;
}

double SampleEditorView::getFitSamplesPerPixel() const
{
    const auto area = getWaveformArea();
    const auto total = getTotalSamples();

    if (area.getWidth() <= 0 || total <= 0)
        return 1.0;

    return juce::jmax(finestSamplesPerPixel,
                      static_cast<double>(total) / static_cast<double>(area.getWidth()));
}

void SampleEditorView::zoomToFit()
{
    samplesPerPixel = getFitSamplesPerPixel();
    viewStartSample = 0.0;
    clampView();
}

void SampleEditorView::zoomBy(double factor, int anchorX)
{
    const auto area = getWaveformArea();

    if (area.getWidth() <= 0 || getTotalSamples() <= 0)
        return;

    // The sample under the pointer before and after the zoom is the same one,
    // so zooming in on something does not push it off screen.
    const auto anchored = juce::jlimit(area.getX(), area.getRight(), anchorX);
    const auto sampleUnderPointer = sampleAtX(anchored);

    samplesPerPixel = juce::jlimit(finestSamplesPerPixel,
                                   getFitSamplesPerPixel(),
                                   samplesPerPixel * factor);

    viewStartSample = sampleUnderPointer
                    - static_cast<double>(anchored - area.getX()) * samplesPerPixel;

    clampView();
    repaint();
}

void SampleEditorView::scrollBy(int pixels)
{
    viewStartSample += static_cast<double>(pixels) * samplesPerPixel;
    clampView();
    repaint();
}

void SampleEditorView::clampView()
{
    const auto area = getWaveformArea();
    const auto total = getTotalSamples();

    if (total <= 0 || area.getWidth() <= 0)
    {
        viewStartSample = 0.0;
        return;
    }

    samplesPerPixel = juce::jlimit(finestSamplesPerPixel, getFitSamplesPerPixel(), samplesPerPixel);

    const auto visible = static_cast<double>(area.getWidth()) * samplesPerPixel;
    viewStartSample = juce::jlimit(0.0, juce::jmax(0.0, total - visible), viewStartSample);
}

double SampleEditorView::sampleAtX(int x) const
{
    const auto area = getWaveformArea();
    return viewStartSample + static_cast<double>(x - area.getX()) * samplesPerPixel;
}

double SampleEditorView::xAtSample(double sample) const
{
    const auto area = getWaveformArea();
    return area.getX() + (sample - viewStartSample) / samplesPerPixel;
}

//==============================================================================
juce::Rectangle<int> SampleEditorView::getWaveformArea() const
{
    return getLocalBounds()
        .withTrimmedTop(headerHeight + rulerHeight)
        .reduced(edgePadding, edgePadding);
}

juce::Rectangle<int> SampleEditorView::getChannelLane(int channel, int numChannels) const
{
    const auto area = getWaveformArea();

    if (numChannels <= 1)
        return area;

    // Stereo is drawn as two lanes rather than one merged envelope: at sample
    // level a merged picture would show two channels' points on one line and
    // no way to tell which is which.
    const auto laneHeight = area.getHeight() / numChannels;
    return area.withHeight(laneHeight).translated(0, channel * laneHeight).reduced(0, 2);
}

void SampleEditorView::resized()
{
    refreshClipPointer();

    auto header = getLocalBounds().withHeight(headerHeight).reduced(edgePadding, 6);

    normaliseButton.setBounds(header.removeFromLeft(normaliseButton.getPreferredWidth()));
    header.removeFromLeft(6);
    reverseButton.setBounds(header.removeFromLeft(reverseButton.getPreferredWidth()));
    header.removeFromLeft(6);
    exportButton.setBounds(header.removeFromLeft(exportButton.getPreferredWidth()));

    fitButton.setBounds(header.removeFromRight(fitButton.getPreferredWidth()));
    header.removeFromRight(6);
    zoomInButton.setBounds(header.removeFromRight(22).withSizeKeepingCentre(20, 20));
    header.removeFromRight(2);
    zoomOutButton.setBounds(header.removeFromRight(22).withSizeKeepingCentre(20, 20));

    clampView();
}

//==============================================================================
void SampleEditorView::paint(juce::Graphics& g)
{
    refreshClipPointer();

    g.fillAll(Theme::panel());

    auto header = getLocalBounds().withHeight(headerHeight);
    g.setColour(Theme::panelHeader());
    g.fillRect(header);
    g.setColour(Theme::divider());
    g.fillRect(header.withHeight(1).withY(header.getBottom() - 1));

    if (clip == nullptr)
    {
        drawEmptyState(g);
        return;
    }

    // What is showing, in the units the zoom is actually in. A reader who wants
    // to know whether they are at sample level should not have to guess.
    const auto zoomText = samplesPerPixel >= 1.0
        ? juce::String(juce::roundToInt(samplesPerPixel)) + TRANS(" smp / px")
        : juce::String(juce::roundToInt(1.0 / samplesPerPixel)) + TRANS(" px / smp");

    const auto rate = clip->getClipSampleRate();
    const auto lengthText = juce::String(clip->getSourceLengthSeconds(), 2) + TRANS(" s")
                          + "  -  " + juce::String(getTotalSamples()) + TRANS(" smp");

    g.setColour(Theme::text());
    g.setFont(Theme::ui(11.5f, true));
    const auto titleArea = header.reduced(edgePadding, 0)
                                 .withTrimmedLeft(exportButton.getRight() + 12 - edgePadding)
                                 .withTrimmedRight(140);
    g.drawText(clipTitle.isNotEmpty() ? clipTitle : clip->getName(),
               titleArea.withHeight(14).translated(0, 4), juce::Justification::centredLeft, true);

    g.setColour(Theme::faintText());
    g.setFont(Theme::ui(9.5f));
    g.drawText(lengthText + "  -  " + zoomText,
               titleArea.withHeight(12).translated(0, 18), juce::Justification::centredLeft, true);

    drawRuler(g, getLocalBounds().withTrimmedTop(headerHeight).withHeight(rulerHeight));

    const auto area = getWaveformArea();
    g.setColour(Theme::panelDeep());
    g.fillRoundedRectangle(area.toFloat(), 3.0f);

    // The part the clip plays, which is the part an edit will touch. Drawn
    // before the waveform so the audio itself stays the brightest thing here.
    const auto first = clip->getSourceOffsetSeconds() * rate;
    const auto last = first + clip->getPlayLengthSeconds() * rate;
    const auto regionLeft = juce::jmax(static_cast<double>(area.getX()), xAtSample(first));
    const auto regionRight = juce::jmin(static_cast<double>(area.getRight()), xAtSample(last));

    if (regionRight > regionLeft)
    {
        g.setColour(Theme::accent().withAlpha(0.10f));
        g.fillRect(juce::Rectangle<float>(static_cast<float>(regionLeft), static_cast<float>(area.getY()),
                                          static_cast<float>(regionRight - regionLeft),
                                          static_cast<float>(area.getHeight())));

        g.setColour(Theme::accent().withAlpha(0.45f));

        for (const auto edge : { regionLeft, regionRight })
            g.fillRect(juce::Rectangle<float>(static_cast<float>(edge), static_cast<float>(area.getY()),
                                              1.0f, static_cast<float>(area.getHeight())));
    }

    const auto numChannels = juce::jmax(1, clip->getNumSourceChannels());

    for (int channel = 0; channel < numChannels; ++channel)
        drawChannel(g, channel, getChannelLane(channel, numChannels));

    g.setColour(Theme::outline());
    g.drawRoundedRectangle(area.toFloat(), 3.0f, 1.0f);
}

void SampleEditorView::drawEmptyState(juce::Graphics& g) const
{
    g.setColour(Theme::mutedText());
    g.setFont(Theme::ui(11.0f));
    g.drawText(TRANS("Double click an audio clip in the playlist to edit its samples."),
               getLocalBounds().withTrimmedTop(headerHeight),
               juce::Justification::centred, true);
}

void SampleEditorView::drawRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto waveform = getWaveformArea();
    const auto rate = clip != nullptr ? clip->getClipSampleRate() : 0.0;

    if (rate <= 0.0 || waveform.getWidth() <= 0)
        return;

    // Ticks are chosen in whichever unit the zoom has reached: seconds while
    // the whole file is in view, and samples once one is more than a pixel wide.
    const auto secondsPerPixel = samplesPerPixel / rate;
    const auto niceSteps = { 0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 };
    auto step = 60.0;

    for (const auto candidate : niceSteps)
    {
        if (candidate / secondsPerPixel >= 70.0)
        {
            step = candidate;
            break;
        }
    }

    const auto firstSecond = std::floor((viewStartSample / rate) / step) * step;

    g.setFont(Theme::ui(9.0f));

    for (auto seconds = firstSecond; ; seconds += step)
    {
        const auto x = xAtSample(seconds * rate);

        if (x > waveform.getRight())
            break;

        if (x < waveform.getX())
            continue;

        g.setColour(Theme::divider());
        g.fillRect(juce::Rectangle<float>(static_cast<float>(x),
                                          static_cast<float>(area.getBottom() - 5), 1.0f, 5.0f));

        g.setColour(Theme::faintText());
        g.drawText(step < 1.0 ? juce::String(seconds, 3) : juce::String(seconds, 2),
                   juce::Rectangle<int>(static_cast<int>(x) + 3, area.getY(), 60, area.getHeight()),
                   juce::Justification::centredLeft, false);
    }
}

void SampleEditorView::drawChannel(juce::Graphics& g, int channel, juce::Rectangle<int> lane) const
{
    if (clip == nullptr || lane.getHeight() <= 2)
        return;

    const auto centre = static_cast<float>(lane.getCentreY());
    const auto halfHeight = static_cast<float>(lane.getHeight()) * 0.5f - 1.0f;

    g.setColour(Theme::divider());
    g.fillRect(juce::Rectangle<float>(static_cast<float>(lane.getX()), centre,
                                      static_cast<float>(lane.getWidth()), 1.0f));

    const auto amplitudeToY = [centre, halfHeight] (float value)
    {
        return centre - juce::jlimit(-1.0f, 1.0f, value) * halfHeight;
    };

    g.setColour(Theme::accent());

    if (samplesPerPixel >= 1.0)
    {
        // Zoomed out: one vertical line per pixel between the loudest and the
        // quietest sample it covers, which is the only honest way to draw more
        // samples than there are pixels.
        for (int x = lane.getX(); x < lane.getRight(); ++x)
        {
            const auto firstSample = static_cast<int>(std::floor(sampleAtX(x)));
            const auto count = juce::jmax(1, static_cast<int>(std::ceil(samplesPerPixel)));

            float lowest = 0.0f;
            float highest = 0.0f;

            if (! clip->getSampleRange(channel, firstSample, count, lowest, highest))
                continue;

            g.fillRect(juce::Rectangle<float>(static_cast<float>(x), amplitudeToY(highest),
                                              1.0f,
                                              juce::jmax(1.0f, amplitudeToY(lowest) - amplitudeToY(highest))));
        }

        return;
    }

    // Zoomed in past one sample per pixel: the samples are joined by a line,
    // and once they are far enough apart each one is marked. This is the level
    // the timeline never reaches, and the reason this window exists.
    const auto pixelsPerSample = 1.0 / samplesPerPixel;
    const auto firstSample = juce::jmax(0, static_cast<int>(std::floor(sampleAtX(lane.getX()))) - 1);
    const auto lastSample = juce::jmin(getTotalSamples() - 1,
                                       static_cast<int>(std::ceil(sampleAtX(lane.getRight()))) + 1);

    juce::Path line;
    auto started = false;

    for (int sample = firstSample; sample <= lastSample; ++sample)
    {
        float value = 0.0f;
        float unused = 0.0f;

        if (! clip->getSampleRange(channel, sample, 1, value, unused))
            continue;

        const auto x = static_cast<float>(xAtSample(sample));
        const auto y = amplitudeToY(value);

        if (! started)
        {
            line.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            line.lineTo(x, y);
        }

        if (pixelsPerSample >= pixelsPerSampleForDots)
        {
            g.fillEllipse(x - 2.0f, y - 2.0f, 4.0f, 4.0f);

            // A stem down to the centre line, so a run of samples reads as
            // values rather than as a curve that happens to wobble.
            g.setColour(Theme::accent().withAlpha(0.35f));
            g.fillRect(juce::Rectangle<float>(x - 0.5f, juce::jmin(y, centre), 1.0f, std::abs(y - centre)));
            g.setColour(Theme::accent());
        }
    }

    if (started)
        g.strokePath(line, juce::PathStrokeType(1.2f));
}

//==============================================================================
void SampleEditorView::mouseWheelMove(const juce::MouseEvent& event,
                                      const juce::MouseWheelDetails& wheel)
{
    refreshClipPointer();

    if (clip == nullptr)
        return;

    // Wheel scrolls, Ctrl-wheel zooms - the same pair the playlist uses, so the
    // habit carries over.
    if (event.mods.isCommandDown())
    {
        zoomBy(wheel.deltaY > 0 ? 0.8 : 1.25, event.x);
        return;
    }

    scrollBy(juce::roundToInt(-wheel.deltaY * 160.0f));
}

void SampleEditorView::mouseDown(const juce::MouseEvent& event)
{
    refreshClipPointer();

    dragStartX = event.x;
    dragStartSample = viewStartSample;
}

void SampleEditorView::mouseDrag(const juce::MouseEvent& event)
{
    refreshClipPointer();

    if (clip == nullptr)
        return;

    // Dragging moves the paper under the view, which is what a picture bigger
    // than its window wants.
    viewStartSample = dragStartSample - static_cast<double>(event.x - dragStartX) * samplesPerPixel;
    clampView();
    repaint();
}

void SampleEditorView::buttonClicked(juce::Button* button)
{
    refreshClipPointer();

    if (button == &normaliseButton)
        applyEdit(AudioClip::SampleEdit::normalise);
    else if (button == &reverseButton)
        applyEdit(AudioClip::SampleEdit::reverse);
    else if (button == &zoomInButton)
        zoomBy(0.5, getWaveformArea().getCentreX());
    else if (button == &zoomOutButton)
        zoomBy(2.0, getWaveformArea().getCentreX());
    else if (button == &exportButton)
        exportSample();
    else if (button == &fitButton)
        zoomToFit(), repaint();
}

void SampleEditorView::applyEdit(AudioClip::SampleEdit edit)
{
    if (clip == nullptr)
        return;

    const auto name = edit == AudioClip::SampleEdit::normalise ? TRANS("Normalize sample")
                                                               : TRANS("Reverse sample");

    // Asked before the snapshot: normalising audio that already reaches full
    // scale changes nothing, and an undo step that undoes nothing is worse than
    // no step at all.
    if (! clip->canApplySampleEdit(edit))
    {
        if (editCallback)
            editCallback({});

        return;
    }

    // Snapshot first: the edit replaces the clip's audio, and undo restores the
    // clip that still points at the old buffer.
    if (beforeEditCallback)
        beforeEditCallback(name);

    if (! clip->applySampleEdit(edit))
        return;

    repaint();

    if (editCallback)
        editCallback(name);
}

void SampleEditorView::exportSample()
{
    if (clip == nullptr)
        return;

    const auto folder = exportFolder != juce::File()
        ? exportFolder
        : juce::File::getSpecialLocation(juce::File::userMusicDirectory);

    // Named after the clip, with a suffix, because the point of exporting an
    // edited sample is that it is not the file it came from.
    const auto suggested = folder.getChildFile(clip->getName() + "-edit.wav");

    exportChooser = std::make_unique<juce::FileChooser>(TRANS("Export sample"),
                                                        suggested,
                                                        "*.wav;*.aiff;*.aif;*.flac;*.ogg");

    exportChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
                               [this] (const juce::FileChooser& chooser)
                               {
                                   const auto file = chooser.getResult();

                                   if (file == juce::File())
                                       return;

                                   const auto error = writeSampleTo(file);

                                   // The written file, not the chosen one: an
                                   // extension nothing can write became a wav.
                                   if (exportCallback)
                                       exportCallback(error.isEmpty() ? exportedFile : juce::File(), error);
                               });
}

juce::String SampleEditorView::writeSampleTo(const juce::File& file)
{
    refreshClipPointer();

    if (clip == nullptr)
        return TRANS("There is no sample to export.");

    if (exportFormats == nullptr)
        return TRANS("No audio formats are available to write with.");

    // The clip writes itself: the samples are its own, and the same call is
    // what the engine tests exercise without a window in the way.
    juce::String error;
    const auto written = clip->exportPlayedRegion(file, *exportFormats, error);

    if (written == juce::File())
        return error.isNotEmpty() ? error : TRANS("Writing the sample failed.");

    exportedFile = written;
    return {};
}

} // namespace djr
