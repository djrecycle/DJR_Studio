#include "MidiSamplerProcessor.h"

#include "ui/Theme.h"
#include "ui/UiControls.h"

#include <cmath>
#include <limits>

namespace djr
{

namespace
{
    /** The plugin's own editor - a pad list, a way to load a sample into
        whichever pad is selected, and just enough per-pad controls (gain,
        trigger note) to make a kit usable without leaving this window.
    */
    class MidiSamplerGui final : public juce::AudioProcessorEditor,
                                 private juce::Button::Listener,
                                 private juce::Slider::Listener,
                                 private juce::ListBoxModel
    {
    public:
        explicit MidiSamplerGui(MidiSamplerProcessor& processorToUse)
            : juce::AudioProcessorEditor(processorToUse), processor(processorToUse)
        {
            padList.setModel(this);
            padList.setRowHeight(26);
            padList.setColour(juce::ListBox::backgroundColourId, Theme::panelDeep());
            addAndMakeVisible(padList);

            loadButton.addListener(this);
            addAndMakeVisible(loadButton);

            clearButton.addListener(this);
            addAndMakeVisible(clearButton);

            previewButton.addListener(this);
            addAndMakeVisible(previewButton);

            gainSlider.setRange(0.0, 1.5, 0.01);
            gainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            gainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
            gainSlider.addListener(this);
            addAndMakeVisible(gainSlider);

            noteLabel.setJustificationType(juce::Justification::centredLeft);
            noteLabel.setColour(juce::Label::textColourId, Theme::mutedText());
            addAndMakeVisible(noteLabel);

            noteDownButton.setButtonText("-");
            noteDownButton.addListener(this);
            addAndMakeVisible(noteDownButton);

            noteUpButton.setButtonText("+");
            noteUpButton.addListener(this);
            addAndMakeVisible(noteUpButton);

            keyboardModeToggle.setColour(juce::ToggleButton::textColourId, Theme::text());
            keyboardModeToggle.addListener(this);
            addAndMakeVisible(keyboardModeToggle);

            setSize(560, 446);
            refreshSelection();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(Theme::panel());

            auto header = getLocalBounds().removeFromTop(headerHeight).reduced(12, 0);
            g.setColour(Theme::text());
            g.setFont(Theme::display(15.0f));
            g.drawText("DJR MIDI Sampler", header, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            area.removeFromTop(headerHeight);
            area = area.reduced(12);

            auto controls = area.removeFromBottom(122);
            area.removeFromBottom(8);

            padList.setBounds(area);

            auto row = controls.removeFromTop(28);
            loadButton.setBounds(row.removeFromLeft(140));
            row.removeFromLeft(6);
            clearButton.setBounds(row.removeFromLeft(80));
            row.removeFromLeft(6);
            previewButton.setBounds(row.removeFromLeft(90));

            controls.removeFromTop(8);
            auto gainRow = controls.removeFromTop(24);
            gainSlider.setBounds(gainRow);

            controls.removeFromTop(8);
            auto noteRow = controls.removeFromTop(24);
            noteDownButton.setBounds(noteRow.removeFromLeft(24));
            noteRow.removeFromLeft(4);
            noteUpButton.setBounds(noteRow.removeFromLeft(24));
            noteRow.removeFromLeft(8);
            noteLabel.setBounds(noteRow);

            controls.removeFromTop(8);
            keyboardModeToggle.setBounds(controls.removeFromTop(24));
        }

        int getNumRows() override { return processor.numPads; }

        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
        {
            if (! juce::isPositiveAndBelow(row, processor.numPads))
                return;

            const auto& pad = processor.getPad(row);
            juce::Rectangle<int> bounds(0, 0, width, height);

            g.setColour(rowIsSelected ? Theme::panelAlt() : Theme::panelDeep());
            g.fillRect(bounds);

            g.setColour(Theme::mutedText());
            g.setFont(Theme::mono(11.0f));
            g.drawText(juce::MidiMessage::getMidiNoteName(pad.midiNote, true, true, 3),
                       bounds.removeFromLeft(56).reduced(8, 0), juce::Justification::centredLeft, false);

            // A keyboard-mode pad answers every note nothing else claims, not
            // just its own - worth flagging in the list, since it otherwise
            // looks like any other single-note pad.
            const juce::String keyboardTag = pad.keyboardMode ? TRANS(" (keyboard)") : juce::String();

            g.setColour(pad.hasSample() ? Theme::text() : Theme::faintText());
            g.setFont(Theme::ui(12.5f));
            g.drawText((pad.hasSample() ? pad.name : TRANS("(empty)")) + keyboardTag,
                       bounds.reduced(4, 0), juce::Justification::centredLeft, true);
        }

        void listBoxItemClicked(int row, const juce::MouseEvent&) override
        {
            padList.selectRow(row);
            refreshSelection();
        }

        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
        {
            padList.selectRow(row);
            processor.previewPad(row);
        }

        void buttonClicked(juce::Button* button) override
        {
            const auto selected = padList.getSelectedRow();

            if (! juce::isPositiveAndBelow(selected, processor.numPads))
                return;

            if (button == &loadButton)
            {
                chooseFileForPad(selected);
                return;
            }

            if (button == &clearButton)
            {
                processor.clearPad(selected);
                padList.updateContent();
                refreshSelection();
                return;
            }

            if (button == &previewButton)
            {
                processor.previewPad(selected);
                return;
            }

            if (button == &noteDownButton || button == &noteUpButton)
            {
                const auto& pad = processor.getPad(selected);
                const auto delta = button == &noteUpButton ? 1 : -1;
                processor.setPadMidiNote(selected, juce::jlimit(0, 127, pad.midiNote + delta));
                padList.updateContent();
                refreshSelection();
                return;
            }

            if (button == &keyboardModeToggle)
            {
                processor.setPadKeyboardMode(selected, keyboardModeToggle.getToggleState());
                padList.updateContent();
            }
        }

        void sliderValueChanged(juce::Slider* slider) override
        {
            if (slider != &gainSlider)
                return;

            const auto selected = padList.getSelectedRow();

            if (juce::isPositiveAndBelow(selected, processor.numPads))
                processor.setPadGain(selected, static_cast<float>(gainSlider.getValue()));
        }

        void chooseFileForPad(int padIndex)
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                TRANS("Load a sample"),
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

            fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
                [this, padIndex] (const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();

                    if (file == juce::File())
                        return;

                    processor.loadSampleIntoPad(padIndex, file);
                    padList.updateContent();
                    refreshSelection();
                });
        }

        void refreshSelection()
        {
            const auto selected = padList.getSelectedRow();
            const auto hasSelection = juce::isPositiveAndBelow(selected, processor.numPads);

            clearButton.setEnabled(hasSelection);
            previewButton.setEnabled(hasSelection);
            gainSlider.setEnabled(hasSelection);
            noteDownButton.setEnabled(hasSelection);
            noteUpButton.setEnabled(hasSelection);
            keyboardModeToggle.setEnabled(hasSelection);

            if (! hasSelection)
            {
                noteLabel.setText({}, juce::dontSendNotification);
                return;
            }

            const auto& pad = processor.getPad(selected);

            gainSlider.setValue(pad.gain, juce::dontSendNotification);
            noteLabel.setText(juce::MidiMessage::getMidiNoteName(pad.midiNote, true, true, 3),
                              juce::dontSendNotification);
            keyboardModeToggle.setToggleState(pad.keyboardMode, juce::dontSendNotification);
        }

        static constexpr int headerHeight = 34;

        MidiSamplerProcessor& processor;
        juce::ListBox padList { "Pads" };
        PillButton loadButton { "Load Sample...", Icon::folder };
        PillButton clearButton { "Clear", Icon::close };
        PillButton previewButton { "Preview", Icon::play };
        juce::Slider gainSlider;
        juce::Label noteLabel;
        juce::TextButton noteDownButton;
        juce::TextButton noteUpButton;
        juce::ToggleButton keyboardModeToggle { "Track pitch to key" };
        std::unique_ptr<juce::FileChooser> fileChooser;
    };

    /** Writes `buffer` out as a 16-bit WAV. Used to give a pad filled by
        loadGeneratedSampleIntoPad() a real file to persist through, the same
        way a loaded-from-disk pad already has one.
    */
    bool writeBufferToWavFile(const juce::AudioBuffer<float>& buffer, double sampleRate, const juce::File& file)
    {
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(stream.get(), sampleRate,
                                      static_cast<unsigned int>(buffer.getNumChannels()), 16, {}, 0));

        if (writer == nullptr)
            return false;

        // The writer owns the stream from here on.
        stream.release();
        return writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
}

const char* const MidiSamplerProcessor::identifier = "djr:builtin:midi-sampler";

juce::PluginDescription MidiSamplerProcessor::getDescription()
{
    juce::PluginDescription description;
    description.name = "DJR MIDI Sampler";
    description.descriptiveName = "Multi-pad one-shot sampler";
    description.pluginFormatName = "DJR";
    description.category = "Built-in";
    description.manufacturerName = "DJR Studio";
    description.version = "1.0";
    description.fileOrIdentifier = identifier;
    description.isInstrument = true;
    description.numInputChannels = 0;
    description.numOutputChannels = 2;
    // Fixed, because it is written into every project that loads the plugin.
    description.uniqueId = 0x444A5253; // 'DJRS'
    description.deprecatedUid = description.uniqueId;
    return description;
}

bool MidiSamplerProcessor::matches(const juce::PluginDescription& description)
{
    return description.fileOrIdentifier == identifier;
}

MidiSamplerProcessor::MidiSamplerProcessor()
    : juce::AudioPluginInstance(BusesProperties()
                                    .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    audioFormats.registerBasicFormats();

    for (int i = 0; i < numPads; ++i)
    {
        pads[static_cast<size_t>(i)].midiNote = firstPadNote + i;
        pads[static_cast<size_t>(i)].name = "Pad " + juce::String(i + 1);
    }

    for (auto& pending : pendingPreviews)
        pending.store(false, std::memory_order_relaxed);
}

const MidiSamplerProcessor::Pad& MidiSamplerProcessor::getPad(int index) const noexcept
{
    static const Pad empty;
    return juce::isPositiveAndBelow(index, numPads) ? pads[static_cast<size_t>(index)] : empty;
}

juce::String MidiSamplerProcessor::loadSampleIntoPad(int padIndex, const juce::File& file)
{
    if (! juce::isPositiveAndBelow(padIndex, numPads))
        return TRANS("No such pad.");

    if (! file.existsAsFile())
        return TRANS("File not found: ") + file.getFileName();

    std::unique_ptr<juce::AudioFormatReader> reader(audioFormats.createReaderFor(file));

    if (reader == nullptr)
        return TRANS("Unsupported format: ") + file.getFileName();

    if (reader->lengthInSamples <= 0)
        return TRANS("Empty file: ") + file.getFileName();

    // Read at the file's own rate and channel count: what is loaded is the
    // file, at whatever rate it actually holds - processBlock resamples per
    // voice against the host's rate, so nothing here has to match it.
    const auto numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const auto numSamples = static_cast<int>(juce::jmin(reader->lengthInSamples,
                                                        static_cast<juce::int64>(std::numeric_limits<int>::max())));

    juce::AudioBuffer<float> loaded(numChannels, numSamples);

    if (! reader->read(&loaded, 0, numSamples, 0, true, true))
        return TRANS("Could not read ") + file.getFileName();

    auto& pad = pads[static_cast<size_t>(padIndex)];
    pad.sample = std::move(loaded);
    pad.sampleRate = reader->sampleRate;
    pad.sourceFile = file;
    pad.name = file.getFileNameWithoutExtension();
    return {};
}

void MidiSamplerProcessor::loadGeneratedSampleIntoPad(int padIndex, juce::AudioBuffer<float> audio,
                                                       double sourceSampleRate, const juce::String& name)
{
    if (! juce::isPositiveAndBelow(padIndex, numPads))
        return;

    auto& pad = pads[static_cast<size_t>(padIndex)];
    pad.sample = std::move(audio);
    pad.sampleRate = sourceSampleRate > 0.0 ? sourceSampleRate : 44100.0;
    pad.sourceFile = juce::File();
    pad.name = name;
}

void MidiSamplerProcessor::clearPad(int padIndex) noexcept
{
    if (! juce::isPositiveAndBelow(padIndex, numPads))
        return;

    auto& pad = pads[static_cast<size_t>(padIndex)];
    pad.sample.setSize(0, 0);
    pad.sourceFile = juce::File();
    pad.name = "Empty";
}

void MidiSamplerProcessor::setPadGain(int padIndex, float gain) noexcept
{
    if (juce::isPositiveAndBelow(padIndex, numPads))
        pads[static_cast<size_t>(padIndex)].gain = juce::jlimit(0.0f, 4.0f, gain);
}

void MidiSamplerProcessor::setPadKeyboardMode(int padIndex, bool enabled) noexcept
{
    if (juce::isPositiveAndBelow(padIndex, numPads))
        pads[static_cast<size_t>(padIndex)].keyboardMode = enabled;
}

void MidiSamplerProcessor::setPadMidiNote(int padIndex, int midiNote) noexcept
{
    if (! juce::isPositiveAndBelow(padIndex, numPads) || ! juce::isPositiveAndBelow(midiNote, 128))
        return;

    auto& pad = pads[static_cast<size_t>(padIndex)];
    const auto previousNote = pad.midiNote;

    // Two pads sharing a note means only one of them could ever trigger -
    // whichever was already sitting on the destination note takes the note
    // this pad is leaving, so the swap is lossless rather than a silent steal.
    if (const auto occupant = findPadForNote(midiNote); occupant >= 0 && occupant != padIndex)
        pads[static_cast<size_t>(occupant)].midiNote = previousNote;

    pad.midiNote = midiNote;
}

int MidiSamplerProcessor::findPadForNote(int midiNote) const noexcept
{
    for (int i = 0; i < numPads; ++i)
        if (pads[static_cast<size_t>(i)].midiNote == midiNote)
            return i;

    return -1;
}

int MidiSamplerProcessor::findKeyboardModePad() const noexcept
{
    for (int i = 0; i < numPads; ++i)
    {
        const auto& pad = pads[static_cast<size_t>(i)];

        if (pad.keyboardMode && pad.hasSample())
            return i;
    }

    return -1;
}

void MidiSamplerProcessor::previewPad(int padIndex) noexcept
{
    if (juce::isPositiveAndBelow(padIndex, numPads))
        pendingPreviews[static_cast<size_t>(padIndex)].store(true, std::memory_order_release);
}

void MidiSamplerProcessor::triggerPad(int padIndex, int playedNote) noexcept
{
    if (! juce::isPositiveAndBelow(padIndex, numPads))
        return;

    auto& pad = pads[static_cast<size_t>(padIndex)];

    // Triggering an empty pad would still spend a voice rendering silence -
    // free that slot for a pad that actually has something to play.
    if (! pad.hasSample())
        return;

    // A kit pad always plays at its own recorded pitch, whatever note asked
    // for it (previewPad() passes the pad's own note, so this is 1.0 there
    // too). A keyboard-mode pad re-pitches by how far the played note sits
    // from its own note, treated as the sample's root.
    const auto pitchRatio = pad.keyboardMode
                                 ? std::pow(2.0f, static_cast<float>(playedNote - pad.midiNote) / 12.0f)
                                 : 1.0f;

    for (auto& voice : voices)
    {
        if (! voice.active)
        {
            voice.padIndex = padIndex;
            voice.readPosition = 0.0;
            voice.active = true;
            voice.pitchRatio = pitchRatio;
            return;
        }
    }

    // Every voice busy: steal round-robin rather than the same one each
    // time, so a fast run of hits does not keep cutting off the same pad.
    auto& stolen = voices[static_cast<size_t>(nextVoiceToSteal)];
    stolen.padIndex = padIndex;
    stolen.readPosition = 0.0;
    stolen.active = true;
    stolen.pitchRatio = pitchRatio;
    nextVoiceToSteal = (nextVoiceToSteal + 1) % maxVoices;
}

void MidiSamplerProcessor::setWorkingFolder(const juce::File& folder)
{
    workingFolder = folder;
}

juce::File MidiSamplerProcessor::getWorkingFolder() const
{
    if (workingFolder != juce::File() && workingFolder.createDirectory())
        return workingFolder;

    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

void MidiSamplerProcessor::fillInPluginDescription(juce::PluginDescription& description) const
{
    description = getDescription();
}

const juce::String MidiSamplerProcessor::getName() const
{
    return "DJR MIDI Sampler";
}

void MidiSamplerProcessor::prepareToPlay(double sampleRate, int)
{
    engineSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

void MidiSamplerProcessor::releaseResources()
{
    for (auto& voice : voices)
        voice.active = false;
}

void MidiSamplerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        const auto note = message.getNoteNumber();

        // An exact-note pad wins the note it owns, but only when it actually
        // has something to play - every pad defaults to its own note whether
        // loaded or not, so an untouched empty pad sitting on this note must
        // not block a keyboard-mode pad elsewhere from catching it.
        const auto exactPad = findPadForNote(note);

        if (exactPad >= 0 && pads[static_cast<size_t>(exactPad)].hasSample())
            triggerPad(exactPad, note);
        else if (const auto keyboardPad = findKeyboardModePad(); keyboardPad >= 0)
            triggerPad(keyboardPad, note);
    }

    // The UI's own preview button is not on the audio thread - picked up
    // here, once, rather than reaching into voices from outside it. Always
    // sounds at the pad's own note, whatever mode it is in - an audition is
    // "what does this pad sound like", not a played note.
    for (int i = 0; i < numPads; ++i)
        if (pendingPreviews[static_cast<size_t>(i)].exchange(false, std::memory_order_acquire))
            triggerPad(i, pads[static_cast<size_t>(i)].midiNote);

    const auto numSamples = buffer.getNumSamples();
    const auto numOutChannels = buffer.getNumChannels();

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        auto& pad = pads[static_cast<size_t>(voice.padIndex)];
        const auto sampleChannels = pad.sample.getNumChannels();
        const auto sampleLength = pad.sample.getNumSamples();
        const auto ratio = (pad.sampleRate / engineSampleRate) * static_cast<double>(voice.pitchRatio);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto pos = voice.readPosition;
            const auto index0 = static_cast<int>(pos);

            if (index0 >= sampleLength - 1)
            {
                voice.active = false;
                break;
            }

            const auto frac = static_cast<float>(pos - index0);

            for (int channel = 0; channel < numOutChannels; ++channel)
            {
                const auto sourceChannel = juce::jmin(channel, sampleChannels - 1);
                const auto* samples = pad.sample.getReadPointer(sourceChannel);
                const auto value = samples[index0] + frac * (samples[index0 + 1] - samples[index0]);
                buffer.addSample(channel, i, value * pad.gain);
            }

            voice.readPosition += ratio;
        }
    }
}

double MidiSamplerProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

bool MidiSamplerProcessor::acceptsMidi() const
{
    return true;
}

bool MidiSamplerProcessor::producesMidi() const
{
    return false;
}

juce::AudioProcessorEditor* MidiSamplerProcessor::createEditor()
{
    return new MidiSamplerGui(*this);
}

bool MidiSamplerProcessor::hasEditor() const
{
    return true;
}

int MidiSamplerProcessor::getNumPrograms()
{
    return 1;
}

int MidiSamplerProcessor::getCurrentProgram()
{
    return 0;
}

void MidiSamplerProcessor::setCurrentProgram(int)
{
}

const juce::String MidiSamplerProcessor::getProgramName(int)
{
    return {};
}

void MidiSamplerProcessor::changeProgramName(int, const juce::String&)
{
}

void MidiSamplerProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::XmlElement state("DJRMidiSampler");

    for (int i = 0; i < numPads; ++i)
    {
        auto& pad = pads[static_cast<size_t>(i)];

        // A pad filled by loadGeneratedSampleIntoPad() has audio but no file
        // behind it - write one now, once, so the starter sound this track
        // was given survives a reload instead of coming back empty. From
        // here on this pad is written and restored exactly like one the user
        // loaded from disk themselves.
        if (pad.sourceFile == juce::File() && pad.hasSample())
        {
            const auto file = getWorkingFolder().getChildFile(
                "midi-sampler-pad-" + juce::Uuid().toDashedString().substring(0, 8) + ".wav");

            if (writeBufferToWavFile(pad.sample, pad.sampleRate, file))
                pad.sourceFile = file;
        }

        auto* padXml = state.createNewChildElement("Pad");
        padXml->setAttribute("index", i);
        padXml->setAttribute("midiNote", pad.midiNote);
        padXml->setAttribute("gain", static_cast<double>(pad.gain));
        padXml->setAttribute("keyboardMode", pad.keyboardMode);

        if (pad.sourceFile != juce::File())
            padXml->setAttribute("file", pad.sourceFile.getFullPathName());
    }

    copyXmlToBinary(state, destination);
}

void MidiSamplerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = getXmlFromBinary(data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName("DJRMidiSampler"))
        return;

    for (auto* padXml : state->getChildIterator())
    {
        if (padXml == nullptr || ! padXml->hasTagName("Pad"))
            continue;

        const auto index = padXml->getIntAttribute("index", -1);

        if (! juce::isPositiveAndBelow(index, numPads))
            continue;

        auto& pad = pads[static_cast<size_t>(index)];
        pad.midiNote = padXml->getIntAttribute("midiNote", pad.midiNote);
        pad.gain = static_cast<float>(padXml->getDoubleAttribute("gain", 1.0));
        pad.keyboardMode = padXml->getBoolAttribute("keyboardMode", pad.keyboardMode);

        const auto filePath = padXml->getStringAttribute("file");

        if (filePath.isNotEmpty())
            loadSampleIntoPad(index, juce::File(filePath));
    }
}

} // namespace djr
