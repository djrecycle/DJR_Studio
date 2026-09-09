#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace djr::Lv2UiLibraryPinning
{

/** Keeps every shared library in `description`'s own LV2 bundle open for
    the rest of this process, if it is not already - a no-op for anything
    that is not an LV2 plugin, or whose bundle cannot be found under
    `searchPaths`.

    JUCE's own LV2 UI hosting opens a fresh handle to a plugin's UI library
    each time an editor is created, and closes it again when that editor is
    destroyed - fine for a UI library with no global state, but GTK-based
    ones (AVLdrumkits' avldrumsUI_gl.so among them) register their widget
    types with GObject's global type system on load, and GObject has no way
    to un-register a type. Closing such a library and later re-opening it -
    the same plugin removed and reopened, or a sibling in the same bundle
    that shares the binary (AVLdrumkits' several kit presets all do) - runs
    that registration a second time against types that are still there from
    the first, which GObject rejects outright ("cannot register existing
    type"): logged as a flood of GLib-CRITICAL warnings, and DJR_Studio
    stops responding shortly after, in the field.

    dlopen/dlclose are reference-counted at the OS level: as long as some
    handle to a library stays open, the library itself stays mapped and its
    registered types stay valid, no matter how many times something else's
    own handle to the same path opens and closes. Pinning one handle per
    library here - held for the life of the process, deliberately never
    closed - is what keeps JUCE's own open/close cycle from ever actually
    unmapping it, without needing to change anything about how JUCE itself
    manages that library's lifetime.
*/
void pinLibrariesFor(const juce::PluginDescription& description,
                     const juce::FileSearchPath& searchPaths);

/** True if `libraryFile` was already pinned by an earlier call to
    pinLibrariesFor() - exposed for tests; nothing production code should
    need to ask this itself.
*/
bool isPinned(const juce::File& libraryFile);

/** How many distinct libraries are currently pinned - exposed for tests. */
int getPinnedCount();

} // namespace djr::Lv2UiLibraryPinning
