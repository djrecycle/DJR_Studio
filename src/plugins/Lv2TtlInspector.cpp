#include "Lv2TtlInspector.h"

#include <cctype>
#include <map>
#include <regex>

namespace djr
{
namespace Lv2TtlInspector
{

namespace
{
    constexpr const char* patchNamespace = "http://lv2plug.in/ns/ext/patch#";
    constexpr const char* atomNamespace = "http://lv2plug.in/ns/ext/atom#";

    /** The only atom types JUCE's LV2 hosting turns into a parameter -
        mirrors juce_LV2PluginFormat.cpp's own whitelist, so a plugin is
        only flagged for exactly what JUCE actually cannot expose.
    */
    const juce::StringArray supportedAtomTypes { "Int", "Long", "Float", "Double", "Bool" };

    /** Strips '#' comments outside angle brackets and quotes, then
        collapses every run of whitespace (including inside quotes - their
        contents are never inspected below) to a single space. Turtle does
        not care about whitespace between tokens, so this turns the whole
        document into one line without changing what any pattern below
        sees - and lets a `.*?` in a regex stand in for "everything up to"
        without needing DOTALL.
    */
    juce::String normalise(const juce::String& turtleText)
    {
        // juce::String indexes through a UTF-8 buffer, so operator[] walks
        // from the start on every call - fine for one lookup, disastrous
        // for a per-character scan of a real plugin's Turtle file (some run
        // well into five figures of characters). std::string has ordinary
        // O(1) random access, so the whole scan runs on that instead and
        // only comes back to juce::String once, at the end.
        const auto source = turtleText.toStdString();
        std::string noComments;
        noComments.reserve(source.size());
        bool inAngle = false;
        bool inQuote = false;

        for (std::string::size_type i = 0; i < source.size(); ++i)
        {
            const auto c = source[i];

            if (c == '<' && ! inQuote)
                inAngle = true;
            else if (c == '>' && ! inQuote)
                inAngle = false;
            else if (c == '"' && ! inAngle)
                inQuote = ! inQuote;

            if (c == '#' && ! inAngle && ! inQuote)
            {
                while (i < source.size() && source[i] != '\n')
                    ++i;

                noComments += '\n';
                continue;
            }

            noComments += c;
        }

        std::string collapsed;
        collapsed.reserve(noComments.size());
        bool lastWasSpace = false;

        for (auto c : noComments)
        {
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                if (! lastWasSpace)
                    collapsed += ' ';

                lastWasSpace = true;
            }
            else
            {
                collapsed += c;
                lastWasSpace = false;
            }
        }

        return juce::String(collapsed).trim();
    }

    /** alias (without its trailing colon) -> namespace URI, from every
        "@prefix alias: <uri> ." in the document.
    */
    std::map<juce::String, juce::String> parsePrefixes(const juce::String& normalised)
    {
        std::map<juce::String, juce::String> prefixes;
        static const std::regex prefixPattern(R"(@prefix\s+([A-Za-z][\w-]*)\s*:\s*<([^>]*)>\s*\.)");

        const auto text = normalised.toStdString();

        for (auto it = std::sregex_iterator(text.begin(), text.end(), prefixPattern); it != std::sregex_iterator(); ++it)
            prefixes[juce::String((*it)[1].str())] = juce::String((*it)[2].str());

        return prefixes;
    }

    /** Every alias a document gave `ns` - more than one is valid Turtle,
        and each is an equally valid way to write a type from it.
    */
    juce::StringArray aliasesFor(const std::map<juce::String, juce::String>& prefixes, const juce::String& ns)
    {
        juce::StringArray aliases;

        for (const auto& [alias, uri] : prefixes)
            if (uri == ns)
                aliases.add(alias);

        return aliases;
    }

    juce::String escapeForRegex(const juce::String& token)
    {
        static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
        return juce::String(std::regex_replace(token.toStdString(), special, R"(\$&)"));
    }

    /** Splits already-normalised text on a "." that is a statement
        terminator - one followed by whitespace or end of string - rather
        than a "." that is just part of a token, such as the dots in a
        domain name inside a bare <...> URI.
    */
    juce::StringArray splitStatements(const juce::String& normalised)
    {
        static const std::regex terminator(R"(\.(?=\s|$))");
        const auto text = normalised.toStdString();

        juce::StringArray statements;
        std::sregex_token_iterator it(text.begin(), text.end(), terminator, -1);

        for (const auto end = std::sregex_token_iterator(); it != end; ++it)
        {
            const juce::String statement = juce::String(it->str()).trim();

            if (statement.isNotEmpty())
                statements.add(statement);
        }

        return statements;
    }
}

bool declaresUnsupportedPatchParameter(const juce::String& turtleText)
{
    const auto normalised = normalise(turtleText);
    const auto prefixes = parsePrefixes(normalised);
    const auto patchAliases = aliasesFor(prefixes, patchNamespace);

    if (patchAliases.isEmpty())
        return false;

    const auto atomAliases = aliasesFor(prefixes, atomNamespace);

    // Every way this document could spell a supported range: each atom
    // alias it declared, paired with each of the five safe type names,
    // plus the full <...#Type> form nobody in practice uses but Turtle
    // allows just as validly.
    juce::StringArray supportedRangeTokens;

    for (const auto& alias : atomAliases)
        for (const auto& type : supportedAtomTypes)
            supportedRangeTokens.add(alias + ":" + type);

    for (const auto& type : supportedAtomTypes)
        supportedRangeTokens.add("<" + juce::String(atomNamespace) + type + ">");

    const auto text = normalised.toStdString();

    for (const auto& patchAlias : patchAliases)
    {
        const std::regex writablePattern(patchAlias.toStdString() + R"(:writable\s+([^.]+?)\s*[.;])");

        for (auto it = std::sregex_iterator(text.begin(), text.end(), writablePattern); it != std::sregex_iterator(); ++it)
        {
            const juce::String list((*it)[1].str());

            for (auto token : juce::StringArray::fromTokens(list, ",", ""))
            {
                token = token.trim();

                if (token.isEmpty())
                    continue;

                // The first "<token> a lv2:Parameter ... rdfs:range X" found
                // after the token's own name is taken as its declaration -
                // conventional LV2 files declare a parameter's range right
                // where they declare it as one, never somewhere unrelated.
                // A range is either a bracketed <...> URI - which can contain
                // its own literal dots, a domain name being the obvious case -
                // or a bare CURIE, which cannot. Trying the bracketed form
                // first is what keeps a stray "." inside a URI from being
                // mistaken for the one ending this statement.
                const std::regex rangePattern(escapeForRegex(token).toStdString()
                                              + R"(\s+a\s+\S*Parameter\S*\s*;.*?rdfs:range\s+(<[^>]*>|[^\s.;]+))");
                std::smatch match;

                if (! std::regex_search(text, match, rangePattern))
                    continue; // No declaration found for it - not enough to accuse it of anything.

                const juce::String range(match[1].str());

                if (! supportedRangeTokens.contains(range))
                    return true;
            }
        }
    }

    return false;
}

juce::StringArray findFlaggedPluginUris(const juce::FileSearchPath& searchPaths)
{
    juce::StringArray flagged;

    for (int i = 0; i < searchPaths.getNumPaths(); ++i)
    {
        const auto searchDir = searchPaths[i];

        if (! searchDir.isDirectory())
            continue;

        for (const auto& bundle : searchDir.findChildFiles(juce::File::findDirectories, false, "*.lv2"))
        {
            const auto manifestFile = bundle.getChildFile("manifest.ttl");

            if (! manifestFile.existsAsFile())
                continue;

            const auto manifestText = manifestFile.loadFileAsString();

            // manifest.ttl conventionally holds just each plugin's URI, its
            // type, and a rdfs:seeAlso to the file(s) with the real
            // content - ports, patch:writable, parameter ranges. A bundle
            // that puts everything inline in manifest.ttl instead is still
            // covered: combined starts as manifestText regardless.
            for (const auto& statement : splitStatements(normalise(manifestText)))
            {
                if (! statement.startsWith("<"))
                    continue;

                static const std::regex subjectPattern(R"(^<([^>]+)>\s+(.*)$)");
                std::smatch subjectMatch;
                const auto statementStd = statement.toStdString();

                if (! std::regex_search(statementStd, subjectMatch, subjectPattern))
                    continue;

                const juce::String pluginUri(subjectMatch[1].str());
                const juce::String predicates(subjectMatch[2].str());

                if (! predicates.contains("lv2:Plugin"))
                    continue;

                juce::String combined = manifestText;

                static const std::regex seeAlsoPattern(R"(rdfs:seeAlso\s+([^;]+))");
                std::smatch seeAlsoMatch;
                const auto predicatesStd = predicates.toStdString();

                if (std::regex_search(predicatesStd, seeAlsoMatch, seeAlsoPattern))
                {
                    const juce::String list(seeAlsoMatch[1].str());

                    for (auto ref : juce::StringArray::fromTokens(list, ",", ""))
                    {
                        ref = ref.trim().trimCharactersAtStart("<").trimCharactersAtEnd(">");

                        if (ref.isEmpty())
                            continue;

                        const auto extra = bundle.getChildFile(ref);

                        if (extra.existsAsFile())
                            combined << "\n" << extra.loadFileAsString();
                    }
                }

                if (declaresUnsupportedPatchParameter(combined))
                    flagged.addIfNotAlreadyThere(pluginUri);
            }
        }
    }

    return flagged;
}

} // namespace Lv2TtlInspector
} // namespace djr
