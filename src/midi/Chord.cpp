#include "Chord.h"

namespace djr
{

namespace Chord
{
    const std::vector<int>& intervalsFor(ChordType type)
    {
        static const std::vector<int> majorIntervals { 0, 4, 7 };
        static const std::vector<int> minorIntervals { 0, 3, 7 };
        static const std::vector<int> diminishedIntervals { 0, 3, 6 };
        static const std::vector<int> augmentedIntervals { 0, 4, 8 };
        static const std::vector<int> sus2Intervals { 0, 2, 7 };
        static const std::vector<int> sus4Intervals { 0, 5, 7 };
        static const std::vector<int> major7Intervals { 0, 4, 7, 11 };
        static const std::vector<int> minor7Intervals { 0, 3, 7, 10 };
        static const std::vector<int> dominant7Intervals { 0, 4, 7, 10 };
        static const std::vector<int> diminished7Intervals { 0, 3, 6, 9 };
        static const std::vector<int> halfDiminished7Intervals { 0, 3, 6, 10 };
        static const std::vector<int> minorMajor7Intervals { 0, 3, 7, 11 };

        switch (type)
        {
            case ChordType::minor:           return minorIntervals;
            case ChordType::diminished:      return diminishedIntervals;
            case ChordType::augmented:       return augmentedIntervals;
            case ChordType::sus2:            return sus2Intervals;
            case ChordType::sus4:            return sus4Intervals;
            case ChordType::major7:          return major7Intervals;
            case ChordType::minor7:          return minor7Intervals;
            case ChordType::dominant7:       return dominant7Intervals;
            case ChordType::diminished7:     return diminished7Intervals;
            case ChordType::halfDiminished7: return halfDiminished7Intervals;
            case ChordType::minorMajor7:     return minorMajor7Intervals;
            case ChordType::major:
            case ChordType::count:
            default:                         return majorIntervals;
        }
    }

    juce::String nameFor(ChordType type)
    {
        switch (type)
        {
            case ChordType::major:           return TRANS("Major");
            case ChordType::minor:           return TRANS("Minor");
            case ChordType::diminished:      return TRANS("Diminished");
            case ChordType::augmented:       return TRANS("Augmented");
            case ChordType::sus2:            return TRANS("Sus2");
            case ChordType::sus4:            return TRANS("Sus4");
            case ChordType::major7:          return TRANS("Major 7th");
            case ChordType::minor7:          return TRANS("Minor 7th");
            case ChordType::dominant7:       return TRANS("Dominant 7th");
            case ChordType::diminished7:     return TRANS("Diminished 7th");
            case ChordType::halfDiminished7: return TRANS("Half-diminished 7th");
            case ChordType::minorMajor7:     return TRANS("Minor-major 7th");
            case ChordType::count:
            default:                         return {};
        }
    }

    juce::String shortNameFor(ChordType type)
    {
        switch (type)
        {
            case ChordType::major:           return TRANS("Maj");
            case ChordType::minor:           return TRANS("Min");
            case ChordType::diminished:      return TRANS("Dim");
            case ChordType::augmented:       return TRANS("Aug");
            case ChordType::sus2:            return TRANS("Sus2");
            case ChordType::sus4:            return TRANS("Sus4");
            case ChordType::major7:          return TRANS("Maj7");
            case ChordType::minor7:          return TRANS("Min7");
            case ChordType::dominant7:       return TRANS("Dom7");
            case ChordType::diminished7:     return TRANS("Dim7");
            case ChordType::halfDiminished7: return TRANS("m7b5");
            case ChordType::minorMajor7:     return TRANS("mMaj7");
            case ChordType::count:
            default:                         return {};
        }
    }
}

} // namespace djr
