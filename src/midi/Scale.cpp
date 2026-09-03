#include "Scale.h"

namespace djr
{

const std::vector<int>& Scale::intervalsFor(ScaleType scaleType)
{
    static const std::vector<int> chromaticIntervals { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    static const std::vector<int> majorIntervals { 0, 2, 4, 5, 7, 9, 11 };
    static const std::vector<int> naturalMinorIntervals { 0, 2, 3, 5, 7, 8, 10 };
    static const std::vector<int> harmonicMinorIntervals { 0, 2, 3, 5, 7, 8, 11 };
    static const std::vector<int> melodicMinorIntervals { 0, 2, 3, 5, 7, 9, 11 };
    static const std::vector<int> dorianIntervals { 0, 2, 3, 5, 7, 9, 10 };
    static const std::vector<int> phrygianIntervals { 0, 1, 3, 5, 7, 8, 10 };
    static const std::vector<int> lydianIntervals { 0, 2, 4, 6, 7, 9, 11 };
    static const std::vector<int> mixolydianIntervals { 0, 2, 4, 5, 7, 9, 10 };
    static const std::vector<int> locrianIntervals { 0, 1, 3, 5, 6, 8, 10 };
    static const std::vector<int> majorPentatonicIntervals { 0, 2, 4, 7, 9 };
    static const std::vector<int> minorPentatonicIntervals { 0, 3, 5, 7, 10 };
    static const std::vector<int> bluesIntervals { 0, 3, 5, 6, 7, 10 };

    switch (scaleType)
    {
        case ScaleType::major:           return majorIntervals;
        case ScaleType::naturalMinor:    return naturalMinorIntervals;
        case ScaleType::harmonicMinor:   return harmonicMinorIntervals;
        case ScaleType::melodicMinor:    return melodicMinorIntervals;
        case ScaleType::dorian:          return dorianIntervals;
        case ScaleType::phrygian:        return phrygianIntervals;
        case ScaleType::lydian:          return lydianIntervals;
        case ScaleType::mixolydian:      return mixolydianIntervals;
        case ScaleType::locrian:         return locrianIntervals;
        case ScaleType::majorPentatonic: return majorPentatonicIntervals;
        case ScaleType::minorPentatonic: return minorPentatonicIntervals;
        case ScaleType::blues:           return bluesIntervals;
        case ScaleType::chromatic:
        case ScaleType::count:
        default:                         return chromaticIntervals;
    }
}

juce::String Scale::nameFor(ScaleType scaleType)
{
    switch (scaleType)
    {
        case ScaleType::chromatic:       return TRANS("Off (chromatic)");
        case ScaleType::major:           return TRANS("Major");
        case ScaleType::naturalMinor:    return TRANS("Natural minor");
        case ScaleType::harmonicMinor:   return TRANS("Harmonic minor");
        case ScaleType::melodicMinor:    return TRANS("Melodic minor");
        case ScaleType::dorian:          return TRANS("Dorian");
        case ScaleType::phrygian:        return TRANS("Phrygian");
        case ScaleType::lydian:          return TRANS("Lydian");
        case ScaleType::mixolydian:      return TRANS("Mixolydian");
        case ScaleType::locrian:         return TRANS("Locrian");
        case ScaleType::majorPentatonic: return TRANS("Major pentatonic");
        case ScaleType::minorPentatonic: return TRANS("Minor pentatonic");
        case ScaleType::blues:           return TRANS("Blues");
        case ScaleType::count:
        default:                         return {};
    }
}

juce::String Scale::shortNameFor(ScaleType scaleType)
{
    switch (scaleType)
    {
        case ScaleType::chromatic:       return TRANS("Off");
        case ScaleType::major:           return TRANS("Maj");
        case ScaleType::naturalMinor:    return TRANS("Min");
        case ScaleType::harmonicMinor:   return TRANS("HMin");
        case ScaleType::melodicMinor:    return TRANS("MMin");
        case ScaleType::dorian:          return TRANS("Dor");
        case ScaleType::phrygian:        return TRANS("Phr");
        case ScaleType::lydian:          return TRANS("Lyd");
        case ScaleType::mixolydian:      return TRANS("Mix");
        case ScaleType::locrian:         return TRANS("Loc");
        case ScaleType::majorPentatonic: return TRANS("MajP");
        case ScaleType::minorPentatonic: return TRANS("MinP");
        case ScaleType::blues:           return TRANS("Blu");
        case ScaleType::count:
        default:                         return {};
    }
}

juce::String Scale::pitchClassName(int pitchClass)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[static_cast<size_t>(wrap(pitchClass))];
}

} // namespace djr
