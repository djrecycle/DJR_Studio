#pragma once

#include "Project.h"

namespace djr
{

class Mixer;

/** Rebuilds a mixer's track list so it holds exactly the tracks a project
    describes: the same number of them, each of the kind the file asks for.

    Without this, opening a project only ever wrote over the tracks that
    happened to be there already - a seventh track was dropped on the floor and
    a leftover sixth one kept whatever the last song had put in it.

    A slot whose kind already matches is left in place, so its plugins and clips
    survive for the per-track restore that follows; only a slot that has to
    change kind is thrown away and built again.

    Returns true when the list actually changed, which is the caller's cue that
    every view holding a track pointer has to re-read it.
*/
bool applyProjectTrackLayout(Mixer& mixer, const juce::Array<ProjectTrackState>& states);

} // namespace djr
