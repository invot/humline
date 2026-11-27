#pragma once

#include "Types.h"

#include <array>
#include <string>

namespace dbc
{

/** A sung note expands into at most this many voices: four chord tones plus a bass note. */
inline constexpr int maxChordNotes = 5;

/** The chord one sung note maps to. Plain numbers, so building one on the audio
    thread costs nothing but arithmetic. */
struct Chord
{
    int root   = 60;   // chord root, in the octave the singer used
    int degree = 0;    // 0-based scale degree
    int count  = 0;
    std::array<int, maxChordNotes> notes {};
};

inline const char* chordModeName (ChordMode m) noexcept
{
    switch (m)
    {
        case ChordMode::Triads:   return "Triads";
        case ChordMode::Sevenths: return "Sevenths";
        case ChordMode::Sus:      return "Sus";
        case ChordMode::Power:    return "Power";
        case ChordMode::NumModes: break;
    }

    return "Triads";
}

inline const char* chordVoicingName (ChordVoicing v) noexcept
{
    switch (v)
    {
        case ChordVoicing::Wide: return "Wide";
        case ChordVoicing::Open: return "Open";
        case ChordVoicing::Close: break;
        case ChordVoicing::NumVoicings: break;
    }

    return "Close";
}

inline const char* chordQualityName (ChordQuality q) noexcept
{
    switch (q)
    {
        case ChordQuality::Major:           return "Major";
        case ChordQuality::Minor:           return "Minor";
        case ChordQuality::Diminished:      return "Diminished";
        case ChordQuality::Augmented:       return "Augmented";
        case ChordQuality::Sus2:            return "Sus2";
        case ChordQuality::Sus4:            return "Sus4";
        case ChordQuality::Dominant7:       return "Dominant 7";
        case ChordQuality::Major7:          return "Major 7";
        case ChordQuality::Minor7:          return "Minor 7";
        case ChordQuality::HalfDiminished7: return "Half-dim 7";
        case ChordQuality::Power:           return "Power (5)";
        case ChordQuality::Preset:
        case ChordQuality::NumQualities:    break;
    }

    return "From preset";
}

/** Everything on the Chords page that shapes the chord for one sung note. */
struct ChordOptions
{
    ChordMode    mode     = ChordMode::Triads;
    ChordVoicing voicing  = ChordVoicing::Close;
    bool         rootBass = false;   // add the root an octave under the chord
    bool         follow   = true;    // build on the sung octave ...
    int          octave   = 0;       // ... or with the root in C4..B4 shifted by this many octaves
    std::array<ChordQuality, 12> overrides {};   // per scale degree

    static ChordOptions from (const Settings& s) noexcept
    {
        ChordOptions o;
        o.mode     = s.chordMode;
        o.voicing  = s.chordVoicing;
        o.rootBass = s.chordRootBass;
        o.follow   = s.chordFollow;
        o.octave   = s.chordOctave;

        for (size_t i = 0; i < o.overrides.size(); ++i)
            o.overrides[i] = s.chordOverride[i];

        return o;
    }
};

/** Degrees in a scale (7 for the diatonic modes, 5 for a pentatonic). */
int scaleDegreeCount (ScaleType t) noexcept;

/** The chord a (pitch-snapped) note maps to in this key. */
Chord chordForNote (int note, const KeySettings& key, const ChordOptions& options) noexcept;

/** The chord on one scale degree, built from `tonicNote` upwards; used by the Chords page.
    The follow/octave options are ignored: the page shows every chord from the same tonic. */
Chord chordForDegree (int degree, int tonicNote, ScaleType scale, const ChordOptions& options) noexcept;

inline Chord chordForNote (int note, const KeySettings& key, ChordMode mode, ChordVoicing voicing) noexcept
{
    ChordOptions o;
    o.mode = mode;
    o.voicing = voicing;
    return chordForNote (note, key, o);
}

inline Chord chordForDegree (int degree, int tonicNote, ScaleType scale,
                             ChordMode mode, ChordVoicing voicing) noexcept
{
    ChordOptions o;
    o.mode = mode;
    o.voicing = voicing;
    return chordForDegree (degree, tonicNote, scale, o);
}

//==============================================================================
// Labels. These allocate, so they are for the editor only - never called from
// the audio thread.

std::string chordName (const Chord&);
std::string chordRoman (const Chord&);

} // namespace dbc
