#pragma once

#include <juce_core/juce_core.h>

namespace djr
{

/** Flags LV2 plugins whose GUI cannot fully work in this host.

    JUCE's LV2 hosting reads a plugin's host-settable ("patch:writable")
    properties as ordinary parameters, but only for five numeric types -
    Int, Long, Float, Double, Bool. A property typed atom:Path (the LV2 way
    a plugin says "the host may set this to a file path") is silently
    dropped: it never becomes a parameter, so nothing in JUCE's own API
    says the plugin has one at all. A plugin built around that - drumkv1
    loading a sample per drum pad is the case this was written for - loads
    and runs fine; the one control that depends on the missing property
    just does nothing, with no error anywhere to explain why.

    This reads the plugin's own Turtle files to notice that in advance, so
    the browser can say so before the user goes looking for a bug that
    is not in this host's own code. It is a heuristic, not an RDF engine:
    a real one already has to exist for LV2 hosting at all (lilv, vendored
    inside JUCE for exactly this), and linking a second one of our own into
    the same binary risks colliding with JUCE's copy over identical C
    symbol names. This is deliberately simpler and self-contained - good
    enough for how LV2 bundles are conventionally written, but a bundle
    that structures its Turtle unusually can be missed. A miss only means
    a plugin that deserves the warning does not get one, never the other
    way around: nothing here can produce a false positive an author did
    not put in their own file (see declaresUnsupportedPatchParameter()).
*/
namespace Lv2TtlInspector
{
    /** True if `turtleText` declares a patch:writable property whose
        rdfs:range is not one of the five numeric atom types JUCE's LV2
        hosting turns into a parameter (atom:Int, Long, Float, Double,
        Bool) - atom:Path is the practical case, but any other type not on
        that list counts too, matching JUCE's own whitelist rather than
        naming Path specifically.

        Pure text matching over the combined content of a plugin's manifest
        and everything it rdfs:seeAlso's - no file access, so every input
        shape is exercised directly in engine_tests.cpp.
    */
    bool declaresUnsupportedPatchParameter(const juce::String& turtleText);

    /** Walks every LV2 bundle under `searchPaths` once and returns the URI
        of every plugin whose own Turtle files trip
        declaresUnsupportedPatchParameter() - the same search paths
        PluginScanner already walks for the LV2 format, so this finds
        exactly the plugins a scan would otherwise report with no warning
        at all. Uses plain file reads, not lilv, for the reason above.
    */
    juce::StringArray findFlaggedPluginUris(const juce::FileSearchPath& searchPaths);
}

} // namespace djr
