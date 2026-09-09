#include "Lv2UiLibraryPinning.h"

#include <regex>

namespace djr::Lv2UiLibraryPinning
{

namespace
{
    // Held for the life of the process, deliberately never closed - see the
    // header comment for why. pinnedPaths mirrors pinnedLibraries just to
    // make "is this one already pinned" a lookup rather than a linear scan
    // through DynamicLibrary objects, which do not expose the path they
    // were opened with.
    juce::OwnedArray<juce::DynamicLibrary> pinnedLibraries;
    juce::StringArray pinnedPaths;

    /** True if `manifestText` declares `uri` as a subject - either written
        out in full (`<http://.../avldrums#BlackPearl>`, rare in practice)
        or, the near-universal case, through a Turtle `@prefix` shorthand
        (`@prefix avldrums: <http://.../avldrums#> .` earlier in the same
        file, then `avldrums:BlackPearl` as the actual subject). A plain
        substring search on the full URI - this function's first cut - never
        matches a real-world manifest.ttl at all: they are conventionally
        written entirely in prefixed form, AVLdrumkits' own included, so
        that version silently found nothing for every real plugin it was
        ever asked about. Not a real Turtle parser - just enough to expand
        the one shorthand LV2 manifests actually use before comparing.
    */
    bool manifestDeclaresUri(const juce::String& manifestText, const juce::String& uri)
    {
        if (manifestText.contains(uri))
            return true;

        static const std::regex prefixPattern(R"(@prefix\s+(\w+):\s*<([^>]+)>\s*\.)");
        const auto text = manifestText.toStdString();

        for (auto it = std::sregex_iterator(text.begin(), text.end(), prefixPattern);
            it != std::sregex_iterator(); ++it)
        {
            const juce::String base((*it)[2].str());

            if (! uri.startsWith(base))
                continue;

            const juce::String prefix((*it)[1].str());
            const auto shorthand = prefix + ":" + uri.substring(base.length());

            if (manifestText.contains(shorthand))
                return true;
        }

        return false;
    }
}

void pinLibrariesFor(const juce::PluginDescription& description, const juce::FileSearchPath& searchPaths)
{
    if (description.pluginFormatName != "LV2")
        return;

    for (int i = 0; i < searchPaths.getNumPaths(); ++i)
    {
        const auto searchDir = searchPaths[i];

        if (! searchDir.isDirectory())
            continue;

        for (const auto& bundle : searchDir.findChildFiles(juce::File::findDirectories, false, "*.lv2"))
        {
            const auto manifestFile = bundle.getChildFile("manifest.ttl");

            if (! manifestFile.existsAsFile()
                || ! manifestDeclaresUri(manifestFile.loadFileAsString(), description.fileOrIdentifier))
                continue;

            // Every shared library this bundle owns, not just the one this
            // plugin's own UI happens to use - a sibling plugin in the same
            // bundle (AVLdrumkits' several kit presets, all sharing one GUI
            // binary) hits this exact failure just as easily, and pinning a
            // few extra small .so files nobody ends up using costs nothing.
            for (const auto& library : bundle.findChildFiles(juce::File::findFiles, false, "*.so"))
            {
                if (isPinned(library))
                    continue;

                auto handle = std::make_unique<juce::DynamicLibrary>();

                if (handle->open(library.getFullPathName()))
                {
                    pinnedPaths.add(library.getFullPathName());
                    pinnedLibraries.add(handle.release());
                }
            }

            // This bundle declared the plugin; no other bundle should also
            // claim the same URI, so there is nothing further to search for.
            return;
        }
    }
}

bool isPinned(const juce::File& libraryFile)
{
    return pinnedPaths.contains(libraryFile.getFullPathName());
}

int getPinnedCount()
{
    return pinnedLibraries.size();
}

} // namespace djr::Lv2UiLibraryPinning
