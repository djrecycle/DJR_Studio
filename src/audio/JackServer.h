#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace djr
{

/** The few things this app asks a JACK server directly, past what JUCE's
    JACK device does on its own: JUCE only ever reports the buffer size, never
    sets it, and it opens on whichever client sorts first - under PipeWire
    that can be a "Dummy" sink with nothing behind it.

    Under PipeWire's JACK layer with `jack.global-buffer-size = true` (what
    Ubuntu Studio's audio configuration writes) setBufferSize() sets PipeWire's
    `clock.force-quantum`, so it changes the period for every application on
    the graph - the same thing Ardour or Carla's buffer-size menu does.
    Callers are expected to restore the size they found when they are done.

    libjack is loaded with dlopen rather than linked, so the app still starts
    on a system with no JACK library at all; everything then reports "no JACK".
*/
namespace JackServer
{
    /** The server's current period in frames, or nothing when no JACK server
        (PipeWire's included) can be reached.
    */
    std::optional<int> getBufferSize();

    /** Asks the server for a new period. True when the server accepted it. */
    bool setBufferSize(int frames);

    /** The client owning the first physical audio port - a sound card or a
        Bluetooth headset rather than a virtual sink - that plays (`playback`)
        or captures (`! playback`) sound. Empty when there is none.
    */
    juce::String findHardwareClient(bool playback);
}

} // namespace djr
