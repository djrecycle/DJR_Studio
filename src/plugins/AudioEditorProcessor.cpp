#include "AudioEditorProcessor.h"

#include "ui/SampleEditorView.h"

namespace djr
{

namespace
{
    /** The editor's own frame. A plugin GUI is handed to the shell as an
        AudioProcessorEditor, so the sample editor needs one wrapped around it;
        it holds no state of its own beyond the view it fills.
    */
    class AudioEditorGui final : public juce::AudioProcessorEditor,
                              private juce::Timer
    {
    public:
        explicit AudioEditorGui(AudioEditorProcessor& owner)
            : juce::AudioProcessorEditor(owner), processor(owner)
        {
            // A function rather than a pointer, for the same reason the panel
            // uses one: what is being edited is replaced rather than mutated,
            // and a pointer taken once would outlive it.
            view.setClipSource([this] { return processor.getEditedClip(); }, "DJR Audio Editor");
            view.setEmptyMessage(TRANS("Press Record, then play: what passes through this insert lands here."));
            view.setCaptureCallbacks([this] { processor.toggleCapture(); },
                                     [this] { processor.clearCapture(); });

            // A capture has no file behind it, so an export starts wherever the
            // machine keeps music rather than beside a source that is not there.
            view.setExportContext(&processor.getAudioFormats(),
                                  juce::File::getSpecialLocation(juce::File::userMusicDirectory));
            view.setExportCallback([this] (const juce::File& file, const juce::String& error)
            {
                view.setNotice(file != juce::File() ? TRANS("Exported: ") + file.getFileName()
                                                    : error);
            });

            view.setLoadCallback([this] { chooseFileToLoad(); });

            // An edit rewrites the clip; the plugin copies the result back over
            // the audio the clip was built from, so a later recording appends
            // to what is on screen.
            view.setEditCallback([this] (const juce::String&)
            {
                processor.commitClipEdits();
                refreshFromProcessor();
            });

            if (processor.canSendToPlaylist())
                view.setSendCallback([this] { processor.sendToPlaylist(); });

            view.setDragExportCallback([this] { return processor.writeForDrag(); });
            addAndMakeVisible(view);
            setSize(760, 420);

            // Ten a second is enough to read a timer by and slow enough that
            // the redraw is free; the clip behind it is rebuilt on its own,
            // slower schedule.
            startTimerHz(10);
            refreshFromProcessor();
        }

        void resized() override
        {
            view.setBounds(getLocalBounds());
        }

    private:
        void chooseFileToLoad()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                TRANS("Open an audio file"),
                juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

            fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                         | juce::FileBrowserComponent::canSelectFiles,
                [this] (const juce::FileChooser& chooser)
                {
                    const auto file = chooser.getResult();

                    if (file == juce::File())
                        return;

                    const auto error = processor.loadFile(file);
                    view.setNotice(error.isEmpty() ? TRANS("Loaded: ") + file.getFileName() : error);
                    refreshFromProcessor();
                });
        }

        void timerCallback() override
        {
            refreshFromProcessor();
        }

        void refreshFromProcessor()
        {
            view.setCaptureState(processor.isCapturing(),
                                 processor.getCapturedSeconds(),
                                 processor.getDroppedSamples(),
                                 processor.hasReachedCaptureLimit());

            // Only when the clip behind the view has actually been replaced:
            // repainting a waveform that has not changed is work the message
            // thread is doing instead of the capture's own draining.
            view.setTitle(processor.getEditedName());

            if (const auto version = processor.getCaptureVersion(); version != seenVersion)
            {
                seenVersion = version;
                view.refresh();

                // Follows the capture while it grows, and once more on the
                // rebuild that lands after it stops, so the end of the take is
                // on screen. After that the zoom is the reader's.
                if (processor.isCapturing() || wasCapturing || ! sawClip)
                    view.zoomToWholeSample();

                sawClip = processor.getEditedClip() != nullptr;
            }

            wasCapturing = processor.isCapturing();
        }

        AudioEditorProcessor& processor;
        SampleEditorView view;
        /** Kept alive while it is on screen; the chooser runs asynchronously. */
        std::unique_ptr<juce::FileChooser> fileChooser;
        int seenVersion = -1;
        bool sawClip = false;
        bool wasCapturing = false;
    };
}

const char* const AudioEditorProcessor::identifier = "djr:builtin:audio-editor";

juce::PluginDescription AudioEditorProcessor::getDescription()
{
    juce::PluginDescription description;
    description.name = "DJR Audio Editor";
    description.descriptiveName = "Sample editor and recorder";
    description.pluginFormatName = "DJR";
    description.category = "Built-in";
    description.manufacturerName = "DJR Studio";
    description.version = "1.0";
    description.fileOrIdentifier = identifier;
    description.isInstrument = false;
    description.numInputChannels = 2;
    description.numOutputChannels = 2;
    // Fixed, because it is written into every project that loads the plugin.
    description.uniqueId = 0x444A5245; // 'DJRE'
    description.deprecatedUid = description.uniqueId;
    return description;
}

bool AudioEditorProcessor::matches(const juce::PluginDescription& description)
{
    return description.fileOrIdentifier == identifier;
}

AudioEditorProcessor::AudioEditorProcessor()
    : juce::AudioPluginInstance(BusesProperties()
                                    .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                    .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    audioFormats.registerBasicFormats();
}

juce::AudioFormatManager& AudioEditorProcessor::getAudioFormats() noexcept
{
    return audioFormats;
}

AudioClip* AudioEditorProcessor::getEditedClip() const noexcept
{
    return capturedClip.get();
}

void AudioEditorProcessor::toggleCapture()
{
    if (capture.isCapturing())
    {
        capture.stop();
        // Drained and rebuilt straight away rather than at the next tick: the
        // last blocks are still in the ring, and a stop that visibly loses the
        // end of the take is the one thing a recorder may not do.
        capture.drain();
        rebuildClip();
        stopTimer();
        return;
    }

    capture.start();
    editedName = TRANS("Capture");
    // A new take is a new file: overwriting the one the last save wrote would
    // rewrite audio a clip on the timeline may already be playing.
    savedAudioFileName = {};
    startTimer(50);
}

void AudioEditorProcessor::clearCapture()
{
    capture.clear();
    capturedClip.reset();
    editedName = {};
    savedAudioFileName = {};
    ++captureVersion;
    stopTimer();
}

bool AudioEditorProcessor::isCapturing() const noexcept
{
    return capture.isCapturing();
}

double AudioEditorProcessor::getCapturedSeconds() const noexcept
{
    return capture.getCapturedSeconds();
}

int AudioEditorProcessor::getDroppedSamples() const noexcept
{
    return capture.getDroppedSamples();
}

bool AudioEditorProcessor::hasReachedCaptureLimit() const noexcept
{
    return capture.hasReachedLimit();
}

int AudioEditorProcessor::getCaptureVersion() const noexcept
{
    return captureVersion;
}

int AudioEditorProcessor::getRebuildIntervalMs() const noexcept
{
    return capture.getCapturedSeconds() < 10.0 ? 250 : 1000;
}

void AudioEditorProcessor::rebuildClip()
{
    capturedClip = AudioClip::createFromBuffer(editedName.isNotEmpty() ? editedName : TRANS("Capture"),
                                               capture.getAudio(),
                                               capture.getNumCapturedSamples(),
                                               capture.getSampleRate());
    lastRebuildMs = juce::Time::getMillisecondCounter();
    ++captureVersion;
}

void AudioEditorProcessor::timerCallback()
{
    const auto arrived = capture.drain();

    // The capture can stop itself at the limit; the timer has to stop with it,
    // or it keeps draining a ring nothing is filling.
    if (! capture.isCapturing())
    {
        if (arrived > 0 || capturedClip == nullptr)
            rebuildClip();

        stopTimer();
        return;
    }

    if (arrived <= 0)
        return;

    if (juce::Time::getMillisecondCounter() - lastRebuildMs >= static_cast<juce::uint32>(getRebuildIntervalMs()))
        rebuildClip();
}

void AudioEditorProcessor::fillInPluginDescription(juce::PluginDescription& description) const
{
    description = getDescription();
}

const juce::String AudioEditorProcessor::getName() const
{
    return "DJR Audio Editor";
}

void AudioEditorProcessor::prepareToPlay(double sampleRate, int)
{
    // Sizing the ring allocates, which is why it happens here rather than in
    // processBlock. Nothing else is touched: this runs on the host's audio
    // side, and the captured audio and the clip built from it belong to the
    // message thread alone.
    capture.prepareRing(sampleRate, 2);
}

void AudioEditorProcessor::releaseResources()
{
    capture.stop();
}

void AudioEditorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Straight through. An insert that is only watching has no business
    // changing what it watches, and the channels past the ones we declared are
    // the host's, not ours - so nothing is cleared either.
    capture.captureBlock(buffer);
}

double AudioEditorProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

bool AudioEditorProcessor::acceptsMidi() const
{
    return false;
}

bool AudioEditorProcessor::producesMidi() const
{
    return false;
}

juce::AudioProcessorEditor* AudioEditorProcessor::createEditor()
{
    return new AudioEditorGui(*this);
}

bool AudioEditorProcessor::hasEditor() const
{
    return true;
}

int AudioEditorProcessor::getNumPrograms()
{
    return 1;
}

int AudioEditorProcessor::getCurrentProgram()
{
    return 0;
}

void AudioEditorProcessor::setCurrentProgram(int)
{
}

const juce::String AudioEditorProcessor::getProgramName(int)
{
    return {};
}

void AudioEditorProcessor::changeProgramName(int, const juce::String&)
{
}

juce::String AudioEditorProcessor::getEditedName() const
{
    return editedName;
}

void AudioEditorProcessor::setWorkingFolder(const juce::File& folder)
{
    workingFolder = folder;
}

juce::File AudioEditorProcessor::getWorkingFolder() const
{
    // The project's Samples folder when the host has said where that is. A
    // plugin with no host to ask still has to put a file somewhere, and the
    // music folder is the one place every machine has.
    if (workingFolder != juce::File() && workingFolder.createDirectory())
        return workingFolder;

    return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
}

juce::String AudioEditorProcessor::loadFile(const juce::File& file)
{
    if (! file.existsAsFile())
        return TRANS("File not found: ") + file.getFileName();

    std::unique_ptr<juce::AudioFormatReader> reader(audioFormats.createReaderFor(file));

    if (reader == nullptr)
        return TRANS("Unsupported format: ") + file.getFileName();

    if (reader->lengthInSamples <= 0)
        return TRANS("Empty file: ") + file.getFileName();

    // Read at the file's own rate and length: what is loaded is the file, and
    // resampling it here would edit something the file never contained.
    const auto numChannels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    const auto numSamples = static_cast<int>(juce::jmin(reader->lengthInSamples,
                                                        static_cast<juce::int64>(std::numeric_limits<int>::max())));

    juce::AudioBuffer<float> loaded(numChannels, numSamples);

    if (! reader->read(&loaded, 0, numSamples, 0, true, true))
        return TRANS("Could not read ") + file.getFileName();

    adoptAudio(loaded, numSamples, reader->sampleRate, file.getFileNameWithoutExtension());
    return {};
}

void AudioEditorProcessor::adoptAudio(const juce::AudioBuffer<float>& source,
                                      int numSamples,
                                      double sourceSampleRate,
                                      const juce::String& name)
{
    stopTimer();
    capture.adopt(source, numSamples, sourceSampleRate);
    editedName = name;
    // The file's own name goes with it: a save writes a new file rather than
    // overwriting the capture a previous edit left behind.
    savedAudioFileName = {};
    rebuildClip();
}

void AudioEditorProcessor::commitClipEdits()
{
    // Normalise and reverse rewrite the clip, not the buffer the clip was built
    // from. Copying the result back keeps one answer to "what is being edited",
    // so a capture started afterwards appends to the audio on screen rather
    // than to the version before the edit.
    auto* clip = capturedClip.get();

    if (clip == nullptr)
        return;

    int first = 0;
    int length = 0;
    clip->getPlayedRegion(first, length);

    juce::AudioBuffer<float> edited;

    if (length <= 0 || ! clip->copySamples(first, length, edited))
        return;

    capture.adopt(edited, length, clip->getClipSampleRate());
}

void AudioEditorProcessor::setSendToPlaylistCallback(std::function<void(const juce::File&, const juce::String&)> callback)
{
    sendToPlaylistCallback = std::move(callback);
}

bool AudioEditorProcessor::canSendToPlaylist() const noexcept
{
    return sendToPlaylistCallback != nullptr;
}

juce::String AudioEditorProcessor::writeEditedAudioTo(const juce::File& file)
{
    // Written from the clip rather than from the capture buffer: the clip is
    // what the edits changed and what the window is showing, and a file that
    // does not match the picture it came from is one nobody can reason about.
    auto* clip = capturedClip.get();

    if (clip == nullptr)
        return TRANS("There is nothing to write yet.");

    juce::String error;
    const auto written = clip->exportPlayedRegion(file, audioFormats, error);

    if (written == juce::File())
        return error.isNotEmpty() ? error : TRANS("Writing the audio failed.");

    return {};
}

void AudioEditorProcessor::sendToPlaylist()
{
    if (sendToPlaylistCallback == nullptr)
        return;

    if (capturedClip == nullptr)
    {
        sendToPlaylistCallback({}, TRANS("There is nothing to send yet."));
        return;
    }

    const auto base = editedName.isNotEmpty() ? juce::File::createLegalFileName(editedName)
                                              : juce::String("capture");

    // A new file each time rather than one that is overwritten: a clip already
    // on the timeline plays from its file, and rewriting that under it would
    // change a take the user did not touch.
    const auto file = getWorkingFolder().getChildFile(base + ".wav").getNonexistentSibling();
    const auto error = writeEditedAudioTo(file);

    sendToPlaylistCallback(error.isEmpty() ? file : juce::File(), error);
}

juce::File AudioEditorProcessor::writeForDrag()
{
    if (capturedClip == nullptr)
        return {};

    const auto base = editedName.isNotEmpty() ? juce::File::createLegalFileName(editedName)
                                              : juce::String("capture");
    const auto file = getWorkingFolder().getChildFile(base + ".wav").getNonexistentSibling();

    return writeEditedAudioTo(file).isEmpty() ? file : juce::File();
}

void AudioEditorProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::XmlElement state("DJRAudioEditor");

    // The audio itself goes beside the project rather than into it: two
    // minutes of stereo is 42 MB, and base64 inside a project file would make
    // reopening it slow in a way nobody could explain from the outside.
    if (capturedClip != nullptr)
    {
        if (savedAudioFileName.isEmpty())
            savedAudioFileName = "editor-" + juce::Uuid().toDashedString().substring(0, 8) + ".wav";

        const auto file = getWorkingFolder().getChildFile(savedAudioFileName);

        if (writeEditedAudioTo(file).isEmpty())
        {
            state.setAttribute("audioFile", savedAudioFileName);
            state.setAttribute("audioPath", file.getFullPathName());
            state.setAttribute("name", editedName);
        }
    }

    copyXmlToBinary(state, destination);
}

void AudioEditorProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto state = getXmlFromBinary(data, sizeInBytes);

    if (state == nullptr || ! state->hasTagName("DJRAudioEditor"))
        return;

    const auto name = state->getStringAttribute("audioFile");

    if (name.isEmpty())
        return;

    // The folder first, the recorded path second: a project moved to another
    // machine keeps its Samples folder and loses its absolute paths.
    auto file = getWorkingFolder().getChildFile(name);

    if (! file.existsAsFile())
        file = juce::File(state->getStringAttribute("audioPath"));

    if (! file.existsAsFile())
        return;

    if (loadFile(file).isEmpty())
    {
        savedAudioFileName = name;
        editedName = state->getStringAttribute("name").isNotEmpty()
                         ? state->getStringAttribute("name")
                         : file.getFileNameWithoutExtension();
    }
}

} // namespace djr
