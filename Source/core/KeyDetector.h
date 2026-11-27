#pragma once

#include "Types.h"

#include <array>
#include <vector>

namespace dbc
{

/** One candidate key for a set of sung (or clicked) pitch classes. */
struct KeySuggestion
{
    int       root  = 0;
    ScaleType scale = ScaleType::Major;
    float     score = 0.0f;   // correlation with the key profile, -1..1
    float     match = 0.0f;   // share of the weight that falls inside the scale, 0..1
};

/** Estimates the key of a melody from a pitch-class histogram.

    Uses the Krumhansl-Kessler major/minor key profiles: the histogram is
    correlated with the profile rotated to every root, which favours keys whose
    tonic and fifth carry the most weight, not just keys that contain the notes. */
class KeyDetector
{
public:
    /** Returns the best keys first. An empty or silent histogram returns nothing. */
    static std::vector<KeySuggestion> suggest (const std::array<float, 12>& histogram, int maxResults = 5);

    /** Number of distinct pitch classes with meaningful weight. */
    static int distinctNotes (const std::array<float, 12>& histogram) noexcept;
};

} // namespace dbc
