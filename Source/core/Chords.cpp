#include "Chords.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <initializer_list>

namespace dbc
{
namespace
{

int bitsSet (uint16_t mask) noexcept
{
    int count = 0;

    for (int semitone = 0; semitone < 12; ++semitone)
        if ((mask & (1u << semitone)) != 0)
            ++count;

    return count;
}

/** Semitones above the tonic of a scale degree, wrapping into higher octaves.
    The scale mask is the single source of truth for the notes in a scale. */
int degreeSemitone (ScaleType scale, int index) noexcept
{
    const uint16_t mask = scaleMask (scale);
    const int count = bitsSet (mask);          // every scale has notes
    const int octave = index / count;
    int degree = index - octave * count;

    for (int semitone = 0; semitone < 12; ++semitone)
        if ((mask & (1u << semitone)) != 0 && degree-- == 0)
            return semitone + 12 * octave;

    return 0;   // not reached: the scale has enough degrees
}

/** Index of the scale degree whose pitch class is `rel`, or -1. */
int degreeForPitchClass (ScaleType scale, int rel) noexcept
{
    const uint16_t mask = scaleMask (scale);
    int degree = 0;

    for (int semitone = 0; semitone < 12; ++semitone)
    {
        if ((mask & (1u << semitone)) == 0)
            continue;

        if (semitone == rel)
            return degree;

        ++degree;
    }

    return -1;
}

/** Nearest degree to a note that is not in the scale, measured around the octave. */
int nearestDegree (ScaleType scale, int rel) noexcept
{
    const uint16_t mask = scaleMask (scale);
    int bestDegree = 0, bestDistance = 13, degree = 0;

    for (int semitone = 0; semitone < 12; ++semitone)
    {
        if ((mask & (1u << semitone)) == 0)
            continue;

        const int raw  = std::abs (semitone - rel);
        const int distance = std::min (raw, 12 - raw);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestDegree   = degree;
        }

        ++degree;
    }

    return bestDegree;
}

/** Chord tones above the root, in semitones, before any voicing. */
int chordIntervals (int degree, ScaleType scale, const ChordOptions& o, int* out) noexcept
{
    const auto quality = (degree >= 0 && degree < (int) o.overrides.size())
                             ? o.overrides[(size_t) degree] : ChordQuality::Preset;

    const auto fixed = [out] (std::initializer_list<int> intervals)
    {
        int n = 0;

        for (int i : intervals)
            out[n++] = i;

        return n;
    };

    switch (quality)
    {
        case ChordQuality::Major:           return fixed ({ 0, 4, 7 });
        case ChordQuality::Minor:           return fixed ({ 0, 3, 7 });
        case ChordQuality::Diminished:      return fixed ({ 0, 3, 6 });
        case ChordQuality::Augmented:       return fixed ({ 0, 4, 8 });
        case ChordQuality::Sus2:            return fixed ({ 0, 2, 7 });
        case ChordQuality::Sus4:            return fixed ({ 0, 5, 7 });
        case ChordQuality::Dominant7:       return fixed ({ 0, 4, 7, 10 });
        case ChordQuality::Major7:          return fixed ({ 0, 4, 7, 11 });
        case ChordQuality::Minor7:          return fixed ({ 0, 3, 7, 10 });
        case ChordQuality::HalfDiminished7: return fixed ({ 0, 3, 6, 10 });
        case ChordQuality::Power:           return fixed ({ 0, 7, 12 });
        case ChordQuality::Preset:
        case ChordQuality::NumQualities:    break;
    }

    if (o.mode == ChordMode::Sus)
        return fixed ({ 0, 5, 7 });

    if (o.mode == ChordMode::Power)
        return fixed ({ 0, 7, 12 });

    // Stack thirds from the scale itself, so the chord is diatonic.
    const int base   = degreeSemitone (scale, degree);
    const int voices = o.mode == ChordMode::Sevenths ? 4 : 3;

    for (int k = 0; k < voices; ++k)
        out[k] = degreeSemitone (scale, degree + 2 * k) - base;

    return voices;
}

Chord build (int rootAbs, int degree, ScaleType scale, const ChordOptions& o) noexcept
{
    Chord chord;
    chord.root   = rootAbs;
    chord.degree = degree;

    int intervals[4] {};
    const int n = chordIntervals (degree, scale, o, intervals);

    std::array<int, 4> tones {};

    for (int k = 0; k < n; ++k)
    {
        int lift = 0;

        if (o.voicing == ChordVoicing::Wide && k > 0)
            lift = 12;
        else if (o.voicing == ChordVoicing::Open && (k == 1 || k == 3))
            lift = 12;   // root, fifth, then the third (and seventh) on top

        tones[(size_t) k] = rootAbs + intervals[k] + lift;
    }

    // Insertion sort: at most four notes.
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && tones[(size_t) j - 1] > tones[(size_t) j]; --j)
            std::swap (tones[(size_t) j - 1], tones[(size_t) j]);

    // Root bassline: the root an octave under the chord, so it sits below
    // every voicing.
    if (o.rootBass && rootAbs - 12 >= 0)
        chord.notes[(size_t) chord.count++] = rootAbs - 12;

    for (int k = 0; k < n; ++k)
    {
        const int note = tones[(size_t) k];

        if (note < 0 || note > 127 || chord.count >= maxChordNotes)
            continue;

        if (chord.count > 0 && chord.notes[(size_t) (chord.count - 1)] == note)
            continue;

        chord.notes[(size_t) chord.count++] = note;
    }

    return chord;
}

//==============================================================================
struct Quality
{
    int count;
    int intervals[4];
    const char* suffix;
};

/** Every chord the presets can produce. Anything else is spelled out by name. */
const Quality kQualities[]
{
    { 3, { 0, 4, 7 },       "" },
    { 3, { 0, 3, 7 },       "m" },
    { 3, { 0, 3, 6 },       "dim" },
    { 3, { 0, 4, 8 },       "aug" },
    { 3, { 0, 5, 7 },       "sus4" },
    { 2, { 0, 7 },          "5" },
    { 4, { 0, 4, 7, 10 },   "7" },
    { 4, { 0, 4, 7, 11 },   "maj7" },
    { 4, { 0, 3, 7, 10 },   "m7" },
    { 4, { 0, 3, 6, 10 },   "m7b5" },
    { 4, { 0, 3, 6, 9 },    "dim7" },
    { 4, { 0, 3, 7, 11 },   "mMaj7" },
    { 4, { 0, 4, 8, 11 },   "augmaj7" },
};

/** Pitch classes of the chord above its root, unique and ascending. */
int intervalsOf (const Chord& chord, int* out) noexcept
{
    int n = 0;

    for (int i = 0; i < chord.count; ++i)
    {
        const int pc = (((chord.notes[(size_t) i] - chord.root) % 12) + 12) % 12;

        bool duplicate = false;

        for (int j = 0; j < n; ++j)
            duplicate = duplicate || out[j] == pc;

        if (! duplicate && n < maxChordNotes)
            out[n++] = pc;
    }

    // Spread voicings put the tones out of order (C G E); names need them sorted.
    std::sort (out, out + n);
    return n;
}

} // namespace

//==============================================================================
int scaleDegreeCount (ScaleType t) noexcept
{
    return bitsSet (scaleMask (t));
}

Chord chordForNote (int note, const KeySettings& key, const ChordOptions& options) noexcept
{
    const int rel = (((note - key.root) % 12) + 12) % 12;

    int degree = degreeForPitchClass (key.scale, rel);

    if (degree < 0)
        degree = nearestDegree (key.scale, rel);

    // Follow: the chord stays in the octave the singer used. Otherwise it sits
    // at a fixed octave (root between C4 and B4, shifted), whatever octave the
    // melody is in.
    const int pc   = ((note % 12) + 12) % 12;
    const int root = options.follow ? note
                                    : std::clamp (60 + 12 * options.octave + pc, 12, 115);

    return build (root, degree, key.scale, options);
}

Chord chordForDegree (int degree, int tonicNote, ScaleType scale, const ChordOptions& options) noexcept
{
    degree = std::max (0, degree);

    return build (tonicNote + degreeSemitone (scale, degree), degree, scale, options);
}

//==============================================================================
std::string chordName (const Chord& chord)
{
    int intervals[maxChordNotes] {};
    const int n = intervalsOf (chord, intervals);
    const std::string root = pitchClassName (chord.root);

    if (n < 2)
        return root;

    for (const auto& quality : kQualities)
    {
        if (quality.count != n)
            continue;

        bool match = true;

        for (int i = 0; i < n; ++i)
            match = match && quality.intervals[i] == intervals[i];

        if (match)
            return root + quality.suffix;
    }

    // Quartal stacks from the pentatonic scales, for example: "C(D G)".
    std::string extra;

    for (int i = 1; i < n; ++i)
    {
        if (! extra.empty())
            extra += ' ';

        extra += pitchClassName (chord.root + intervals[i]);
    }

    return root + "(" + extra + ")";
}

std::string chordRoman (const Chord& chord)
{
    static const char* const numerals[] { "I", "II", "III", "IV", "V", "VI",
                                          "VII", "VIII", "IX", "X", "XI", "XII" };

    if (chord.degree < 0 || chord.degree >= 12)
        return {};

    int intervals[maxChordNotes] {};
    const int n = intervalsOf (chord, intervals);

    bool minorThird = false, flatFifth = false, sharpFifth = false;

    for (int i = 0; i < n; ++i)
    {
        minorThird = minorThird || intervals[i] == 3;
        flatFifth  = flatFifth  || intervals[i] == 6;
        sharpFifth = sharpFifth || intervals[i] == 8;
    }

    std::string roman = numerals[chord.degree];

    if (minorThird)
        for (auto& ch : roman)
            ch = (char) std::tolower ((unsigned char) ch);

    if (flatFifth)
        roman += "\xC2\xB0";          // degree sign
    else if (sharpFifth)
        roman += "+";

    return roman;
}

} // namespace dbc
