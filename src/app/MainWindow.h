#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace djr
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(juce::String name);
    ~MainWindow() override;

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace djr
