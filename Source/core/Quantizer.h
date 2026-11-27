#pragma once

#include "Types.h"

namespace dbc
{

/** A raw, un-quantised note painted by the analysis stage (times in beats/PPQ). */
struct NoteSegment
{
    double startBeats = 0.0;
    double endBeats   = 0.0;
    float  pitch      = 60.0f;   // fractional MIDI note
    float  velocity   = 0.8f;
};

struct QuantizedNote
{
    double startBeats = 0.0;
    double endBeats   = 0.0;
    int    note       = 60;
    float  velocity   = 0.8f;
};

class Quantizer
{
public:
    /** Snap a beat position to the grid, honouring swing and the strength amount. */
    static double snapTime (double beat, const Settings& s) noexcept;

    /** Snap a fractional MIDI pitch to the key, honouring the snap amount. */
    static int snapPitch (float midiFloat, const KeySettings& k) noexcept;

    /** Quantise timing, length and pitch of one segment. */
    static QuantizedNote quantize (const NoteSegment& in, const Settings& s) noexcept;

    /** Nearest scale tone distance helper (used by tests and the editor). */
    static int nearestScaleTone (int note, const KeySettings& k) noexcept;
};

} // namespace dbc
