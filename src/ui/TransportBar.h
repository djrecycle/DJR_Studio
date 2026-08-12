#pragma once

#include "Icons.h"
#include "UiControls.h"

#include "app/SnapSetting.h"
#include "audio/AudioEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Transport strip: play/stop/record/loop cluster, tempo and signature pod,
    position + time readout, pattern/song switch, snap selector and master meter.
*/
class TransportBar final : public juce::Component,
                           private juce::Button::Listener,
                           private juce::Timer
{
public:
    explicit TransportBar(AudioEngine& audioEngine);
    ~TransportBar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    void setSnapChangeCallback(std::function<void(SnapUnit)> callback);
    /** Fired when a signature is picked, before anything is applied, so the
        host can record an undo point and then apply it.
    */
    void setTimeSignatureChangeCallback(std::function<void(int, int)> callback);
    void setUndoCallback(std::function<void()> callback);
    void setRedoCallback(std::function<void()> callback);
    void setMetronomeToggleCallback(std::function<void()> callback);
    /** Lights the metronome button while the click is on. */
    void setMetronomeActive(bool isActive);
    /** Greys the buttons out when there is nothing to undo or redo, and puts
        what they would reverse into their tooltips.
    */
    void setUndoState(bool canUndo, bool canRedo,
                      const juce::String& undoName, const juce::String& redoName);
    void setPatternModeChangeCallback(std::function<void(bool)> callback);
    /** Owner decides what recording means; the bar only asks for the toggle. */
    void setRecordToggleCallback(std::function<void()> callback);
    void setPatternMode(bool shouldUsePatternMode);
    bool isPatternMode() const noexcept;
    SnapUnit getSnapUnit() const noexcept;

private:
    //==============================================================================
    class TransportButton final : public juce::Button
    {
    public:
        TransportButton(const juce::String& buttonName, Icon icon);

        void setIcon(Icon icon);
        void setActiveColour(juce::Colour colour);
        void setActive(bool shouldBeActive);
        void setIconColour(juce::Colour colour);
        void setShowRightDivider(bool shouldShow);
        void setRoundedEdges(bool left, bool right);

        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        Icon iconKind;
        juce::Colour activeColour;
        juce::Colour iconColour;
        bool active = false;
        bool rightDivider = true;
        bool roundLeft = false;
        bool roundRight = false;
    };

    class SegmentButton final : public juce::Button
    {
    public:
        SegmentButton(const juce::String& buttonText, Icon icon);
        void setRoundedEdges(bool left, bool right);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
        int getPreferredWidth() const;

    private:
        Icon iconKind;
        bool roundLeft = false;
        bool roundRight = false;
    };

    //==============================================================================
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;
    void refreshButtons();
    void nudgeTempo(double delta);
    void showSnapMenu();
    void showTimeSignatureMenu();
    juce::String getPositionText() const;
    juce::String getTimeText() const;
    juce::String getMasterDbText() const;

    AudioEngine& engine;
    Transport& transport;

    TransportButton playButton { "Play / pause", Icon::play };
    TransportButton stopButton { "Stop", Icon::stop };
    TransportButton recordButton { "Record", Icon::record };
    TransportButton loopButton { "Loop", Icon::loop };
    TransportButton metronomeButton { "Metronome", Icon::metronome };
    IconChipButton tempoUpButton { "Tempo +1 BPM (shift = +0.1)", Icon::caretUp };
    IconChipButton tempoDownButton { "Tempo -1 BPM (shift = -0.1)", Icon::caretDown };
    SegmentButton patternButton { "Pattern", Icon::grid };
    SegmentButton songButton { "Song", Icon::lines };
    HitAreaButton snapButton { "Snap grid" };
    IconChipButton undoButton { "Undo", Icon::undo };
    IconChipButton redoButton { "Redo", Icon::redo };

    juce::Rectangle<int> transportPod;
    juce::Rectangle<int> tempoPod;
    juce::Rectangle<int> tempoValueArea;
    juce::Rectangle<int> signatureArea;
    juce::Rectangle<int> positionPod;
    juce::Rectangle<int> positionArea;
    juce::Rectangle<int> timeArea;
    juce::Rectangle<int> modePod;
    juce::Rectangle<int> snapPod;
    juce::Rectangle<int> historyPod;
    juce::Rectangle<int> masterPod;
    juce::Rectangle<int> masterMeterArea;

    std::function<void(SnapUnit)> snapChangeCallback;
    std::function<void(int, int)> timeSignatureChangeCallback;
    std::function<void()> undoCallback;
    std::function<void()> redoCallback;
    std::function<void()> metronomeToggleCallback;
    std::function<void(bool)> patternModeChangeCallback;
    std::function<void()> recordToggleCallback;
    SnapUnit snapUnit = SnapUnit::step;
    bool patternMode = true;
    float smoothedMasterLeft = 0.0f;
    float smoothedMasterRight = 0.0f;
    double tempoDragStart = 120.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace djr
