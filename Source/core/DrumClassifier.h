#pragma once

#include "Types.h"

namespace dbc
{

struct DrumFeatures
{
    float low    = 0.0f;   // normalised band shares, 0..1
    float f1     = 0.0f;
    float f2     = 0.0f;
    float high   = 0.0f;
    float zcr    = 0.0f;
    float sustain = 0.0f;  // 0 = click, 1 = long
    float pitched = 0.0f;  // 0..1 periodicity at the attack

    static DrumFeatures fromRaw (float low, float f1, float f2, float high,
                                 float zcr, float sustain, float clarity) noexcept;
};

/** Nearest-neighbour classifier over the takes recorded for each trigger, with
    a rule-based fallback so it works before anything has been trained.

    Every take is kept as its own example (up to maxTakes per trigger) rather
    than being averaged away, so a trigger you perform a few slightly different
    ways still matches each of them. */
class DrumClassifier
{
public:
    static constexpr int maxTakes = 12;

    void clearAll();
    void clearTemplate (int pad);

    /** Replaces every take of a pad with this one. */
    void setTemplate (int pad, const DrumFeatures& f);

    /** Adds one take; returns false when the pad's bank is already full. */
    bool addExample (int pad, const DrumFeatures& f);

    bool hasTemplate (int pad) const noexcept;
    int  numExamples (int pad) const noexcept;
    DrumFeatures getExample (int pad, int index) const noexcept;

    /** Mean of a pad's takes. */
    DrumFeatures getTemplate (int pad) const noexcept;
    int  numTrained() const noexcept;

    /** Returns a pad index, or -1 when nothing is close enough / not a drum. */
    int classify (const DrumFeatures& f) const noexcept;

    /** Rule-based guess. Returns a pad index or -1 for "that's a voice". */
    static int heuristic (const DrumFeatures& f) noexcept;

    static const char* padName (int pad) noexcept;

private:
    static float distance (const DrumFeatures& a, const DrumFeatures& b) noexcept;

    struct Bank
    {
        int count = 0;
        DrumFeatures takes[maxTakes];
    };

    Bank banks[numDrumPads];
};

} // namespace dbc
