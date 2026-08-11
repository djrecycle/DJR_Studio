#pragma once

#include "audio/Mixer.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace djr
{

/** Insert chain of the currently selected track, including the drop slot that
    loads the plugin highlighted in the plugin library.
*/
class InsertChainPanel final : public juce::Component
{
public:
    explicit InsertChainPanel(Mixer& mixer);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    void setSelectedTrack(int trackIndex);
    int getSelectedTrack() const noexcept;
    void setLoadSelectedPluginCallback(std::function<void(int)> callback);
    void setOpenEditorCallback(std::function<void(int)> callback);
    /** Opens a specific insert (index >= 0) or the instrument (index < 0). */
    void setOpenSlotCallback(std::function<void(int, int)> callback);
    void refresh();

private:
    juce::Rectangle<int> getInstrumentRowBounds() const;
    juce::Rectangle<int> getRowBounds(int index) const;
    int getNumRows() const;

    Mixer& mixer;
    std::function<void(int)> loadSelectedPluginCallback;
    std::function<void(int)> openEditorCallback;
    std::function<void(int, int)> openSlotCallback;
    int selectedTrack = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InsertChainPanel)
};

} // namespace djr
