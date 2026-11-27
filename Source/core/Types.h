#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dbc
{

//==============================================================================
enum class SnapGrid : int
{
    Off = 0,
    Quarter,
    Eighth,
    EighthTriplet,
    Sixteenth,
    SixteenthTriplet,
    ThirtySecond,
    NumGrids
};

inline double gridBeats (SnapGrid g) noexcept
{
    switch (g)
    {
        case SnapGrid::Off:               return 0.0;
        case SnapGrid::Quarter:           return 1.0;
        case SnapGrid::Eighth:            return 0.5;
        case SnapGrid::EighthTriplet:     return 1.0 / 3.0;
        case SnapGrid::Sixteenth:         return 0.25;
        case SnapGrid::SixteenthTriplet:  return 1.0 / 6.0;
        case SnapGrid::ThirtySecond:      return 0.125;
        case SnapGrid::NumGrids:          break;
    }

    return 0.25;
}

inline const char* gridName (SnapGrid g) noexcept
{
    switch (g)
    {
        case SnapGrid::Off:               return "Off";
        case SnapGrid::Quarter:           return "1/4";
        case SnapGrid::Eighth:            return "1/8";
        case SnapGrid::EighthTriplet:     return "1/8T";
        case SnapGrid::Sixteenth:         return "1/16";
        case SnapGrid::SixteenthTriplet:  return "1/16T";
        case SnapGrid::ThirtySecond:      return "1/32";
        case SnapGrid::NumGrids:          break;
    }

    return "1/16";
}

//==============================================================================
enum class ScaleType : int
{
    Chromatic = 0,
    Major,
    NaturalMinor,
    HarmonicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    MinorPentatonic,
    MajorPentatonic,
    Blues,
    NumScales
};

/** Bit i set means the scale contains that semitone above the root. */
inline uint16_t scaleMask (ScaleType t) noexcept
{
    switch (t)
    {
        case ScaleType::Chromatic:      return 0xFFFu;
        case ScaleType::Major:          return 0xAB5u; // 0 2 4 5 7 9 11
        case ScaleType::NaturalMinor:   return 0x5ADu; // 0 2 3 5 7 8 10
        case ScaleType::HarmonicMinor:  return 0x9ADu; // 0 2 3 5 7 8 11
        case ScaleType::Dorian:         return 0x6ADu; // 0 2 3 5 7 9 10
        case ScaleType::Phrygian:       return 0x5ABu; // 0 1 3 5 7 8 10
        case ScaleType::Lydian:         return 0xAD5u; // 0 2 4 6 7 9 11
        case ScaleType::Mixolydian:     return 0x6B5u; // 0 2 4 5 7 9 10
        case ScaleType::MinorPentatonic:return 0x4A9u; // 0 3 5 7 10
        case ScaleType::MajorPentatonic:return 0x295u; // 0 2 4 7 9
        case ScaleType::Blues:          return 0x4E9u; // 0 3 5 6 7 10
        case ScaleType::NumScales:      break;
    }

    return 0xFFFu;
}

inline const char* scaleName (ScaleType t) noexcept
{
    switch (t)
    {
        case ScaleType::Chromatic:       return "Chromatic";
        case ScaleType::Major:           return "Major";
        case ScaleType::NaturalMinor:    return "Minor";
        case ScaleType::HarmonicMinor:   return "Harmonic Minor";
        case ScaleType::Dorian:          return "Dorian";
        case ScaleType::Phrygian:        return "Phrygian";
        case ScaleType::Lydian:          return "Lydian";
        case ScaleType::Mixolydian:      return "Mixolydian";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::Blues:           return "Blues";
        case ScaleType::NumScales:       break;
    }

    return "Chromatics";
}

inline const char* pitchClassName (int pc) noexcept
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

inline bool scaleContains (ScaleType t, int root, int note) noexcept
{
    const int pc = (((note - root) % 12) + 12) % 12;
    return (scaleMask (t) & (1u << pc)) != 0;
}

//==============================================================================
/** Which chord a sung note is expanded into on the Chords page. */
enum class ChordMode : int
{
    Triads = 0,
    Sevenths,
    Sus,
    Power,
    NumModes
};

enum class ChordVoicing : int
{
    Close = 0,
    Wide,     // every voice above the root lifted an octave
    Open,     // root, fifth, then third (and seventh) an octave up
    NumVoicings
};

/** A chord picked by hand for one scale degree on the Chords page, in place of
    what the preset would have built there. */
enum class ChordQuality : int
{
    Preset = 0,
    Major,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,
    Dominant7,
    Major7,
    Minor7,
    HalfDiminished7,
    Power,
    NumQualities
};

/** IntelliBend anchors the bend while you hold a note steady; TruBend follows
    every movement of the voice. */
enum class BendMode : int
{
    IntelliBend = 0,
    TruBend,
    NumModes
};

//==============================================================================
/** Bit i set means pitch class i (C = 0) is allowed. */
constexpr uint16_t allNotesMask = 0xFFFu;

struct KeySettings
{
    bool  enabled   = true;
    int   root      = 0;
    ScaleType scale = ScaleType::Major;
    float snap      = 1.0f;   // 0 = leave pitch alone, 1 = hard snap to scale
    int   octave    = 0;
    uint16_t mask   = allNotesMask;   // notes switched off by hand are cleared here
};

/** The scale rotated onto its root, as absolute pitch classes. */
inline uint16_t scalePitchClasses (ScaleType t, int root) noexcept
{
    const uint32_t m = scaleMask (t);
    const int r = ((root % 12) + 12) % 12;
    return (uint16_t) (((m << r) | (m >> (12 - r))) & allNotesMask);
}

/** Pitch classes a note may be snapped to: the scale minus any notes switched
    off. Switching every note off would leave nothing to sing, so that falls
    back to the whole scale. */
inline uint16_t allowedPitchClasses (const KeySettings& k) noexcept
{
    const uint16_t scale = scalePitchClasses (k.scale, k.root);
    const uint16_t both  = (uint16_t) (scale & k.mask);
    return both != 0 ? both : scale;
}

inline bool keyAllows (const KeySettings& k, int note) noexcept
{
    return (allowedPitchClasses (k) & (1u << (((note % 12) + 12) % 12))) != 0;
}

constexpr int numDrumPads = 8;

//==============================================================================
/** What the voice is turned into.

    Beatbox and Melody are live modes: each listens for one thing only, so it
    never has to wait to tell a drum hit from a sung note, and events go out
    the moment they are detected (no grid snapping, no output delay).
    Both listens for drums and notes at once and snaps them to the grid, which
    needs a constant output delay (see VoiceEngine::getOutputLatencySamples). */
enum class EngineMode : int
{
    Beatbox = 0,
    Melody,
    Both,
    NumModes
};

inline const char* engineModeName (EngineMode m) noexcept
{
    switch (m)
    {
        case EngineMode::Beatbox:  return "Beatbox";
        case EngineMode::Melody:   return "Melody";
        case EngineMode::Both:     return "Both (quantised)";
        case EngineMode::NumModes: break;
    }

    return "Both (quantised)";
}
constexpr int numCcDials  = 4;   // aaa, eee, ooo, envelope

struct Settings
{
    float inputGainDb = 0.0f;

    EngineMode mode = EngineMode::Both;

    bool isLive() const noexcept { return mode == EngineMode::Beatbox || mode == EngineMode::Melody; }
    bool listensForDrums() const noexcept { return mode != EngineMode::Melody; }
    bool listensForPitch() const noexcept { return mode != EngineMode::Beatbox; }

    // --- timing -----------------------------------------------------------
    SnapGrid grid           = SnapGrid::Sixteenth;
    float    timingStrength = 1.0f;
    float    minLengthBeats = 0.125f;
    float    swing          = 0.0f;   // 0 .. 0.5

    // --- key --------------------------------------------------------------
    bool     keyLock    = true;
    int      keyRoot    = 0;
    ScaleType scale     = ScaleType::Major;
    float    pitchSnap  = 1.0f;
    int      octaveShift = 0;

    uint16_t noteMask   = allNotesMask;   // notes switched off on the wheel / keyboard

    // --- chords -----------------------------------------------------------
    bool         chordsEnabled = false;
    ChordMode    chordMode     = ChordMode::Triads;
    ChordVoicing chordVoicing  = ChordVoicing::Close;
    bool         chordRootBass = false;   // also play the sung root under the chord
    bool         chordFollow   = true;    // chord follows the sung octave ...
    int          chordOctave   = 0;       // ... or sits at C4 + this many octaves
    int          chordChannel  = 0;       // 0 = replace the melody on the voice channel
    ChordQuality chordOverride[12] {};    // per scale degree; Preset = use the preset

    // --- tracking ---------------------------------------------------------
    bool  pitchEnabled = true;  // sung notes become MIDI notes at all
    float stickyness = 0.5f;   // how reluctant the tracker is to change note 0..1
    float gate       = 1.0f;   // note-length multiplier 0.2..2

    // --- pitch bend -------------------------------------------------------
    bool     bendEnabled = false;
    int      bendRange   = 2;                     // semitones; must match the synth
    BendMode bendMode    = BendMode::IntelliBend;
    bool     bendChords  = true;                  // bend the chord channel too

    // --- drums ------------------------------------------------------------
    bool  drumsEnabled    = true;
    float drumSensitivity = 0.5f;

    // --- output -----------------------------------------------------------
    int   voiceChannel    = 1;
    int   drumChannel     = 10;
    bool  drumsOnDrumChan = true;
    float velocityAmount  = 0.75f;

    int  padNote[numDrumPads]   = { 35, 38, 39, 36, 42, 46, 45, 50 };
    bool padMuted[numDrumPads]  = { false, false, false, false, false, false, false, false };
    bool padVelocity[numDrumPads] = { true, true, true, true, true, true, true, true };  // off = always 127

    bool  sendCC       = true;
    int   ccVowelA     = 20;
    int   ccVowelE     = 21;
    int   ccVowelO     = 22;
    int   ccDynamics   = 11;

    // Per dial (aaa, eee, ooo, env): the part of the detected range that sweeps
    // the whole dial, and the CC span the dial is mapped onto.
    float ccInMin[numCcDials]  = { 0.0f, 0.0f, 0.0f, 0.0f };
    float ccInMax[numCcDials]  = { 1.0f, 1.0f, 1.0f, 1.0f };
    int   ccOutMin[numCcDials] = { 0, 0, 0, 0 };
    int   ccOutMax[numCcDials] = { 127, 127, 127, 127 };

    KeySettings keyState() const noexcept
    {
        KeySettings k;
        k.enabled = keyLock;
        k.root    = keyRoot;
        k.scale   = scale;
        k.snap    = pitchSnap;
        k.octave  = octaveShift;
        k.mask    = noteMask;
        return k;
    }
};

/** The MIDI channel (1-16) that beatbox triggers go out on: channel 10 while
    "Drums on channel 10" is on, otherwise the drum channel from the Assign page.
    One helper, so the engine, the pad preview, the instrument check, the
    all-notes-off and the take export can never disagree about where a drum goes. */
inline int drumOutChannel (const Settings& s) noexcept
{
    return std::clamp (s.drumsOnDrumChan ? 10 : s.drumChannel, 1, 16);
}

/** A dial's 0..1 reading mapped to its CC value: the input range is stretched to
    fill the dial, then the dial is scaled into the output span. Out-of-range
    readings clamp, and a reversed output span inverts the dial. */
inline int ccDialValue (float value, float inMin, float inMax, int outMin, int outMax) noexcept
{
    const float span = inMax - inMin;
    const float norm = span > 1.0e-4f ? std::clamp ((value - inMin) / span, 0.0f, 1.0f)
                                      : (value >= inMin ? 1.0f : 0.0f);
    const float cc = (float) outMin + norm * (float) (outMax - outMin);
    return std::clamp ((int) std::lround (cc), 0, 127);
}

//==============================================================================
struct MidiEvent
{
    int     offset = 0;
    uint8_t status = 0;
    uint8_t data1  = 0;
    uint8_t data2  = 0;
    bool    drum   = false;   // a trigger (drum) event rather than a sung note / chord
};

//==============================================================================
struct HistoryNote
{
    float startBeats  = 0.0f;
    float lengthBeats = 0.25f;
    int   note        = 60;
    bool  drum        = false;
};

/** Everything the editor needs to draw, updated from the audio thread. */
struct LiveState
{
    float inputLevel = 0.0f;
    float inputPeak  = 0.0f;

    float hz        = 0.0f;
    float clarity   = 0.0f;
    float midiFloat = 60.0f;
    float cents     = 0.0f;
    int   pitchClass = -1;
    int   activeNote = -1;
    bool  voiced     = false;

    float pitchClassEnergy[12] {};

    float padFlash[numDrumPads] {};
    int   lastPad = -1;

    float vowelA = 0.0f, vowelE = 0.0f, vowelO = 0.0f;
    float dynamics = 0.0f;

    float barPhase  = 0.0f;   // 0..1 position inside one beat, for the clock pips
    float beatPhase = 0.0f;

    static constexpr int historySize = 32;
    HistoryNote history[historySize] {};
    int      historyCount = 0;
    int      historyWrite = 0;
    uint32_t historySeq   = 0;
};

} // namespace dbc
