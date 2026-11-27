#include "Quantizer.h"

namespace dbc
{

double Quantizer::snapTime (double beat, const Settings& s) noexcept
{
    const double g = gridBeats (s.grid);

    if (g <= 0.0 || s.timingStrength <= 0.0f)
        return beat;

    double snapped = std::round (beat / g) * g;

    if (s.swing > 0.0f)
    {
        // Delay every odd grid line (the "and") by up to half a grid unit.
        const auto index = (long long) std::llround (snapped / g);

        if ((index % 2) != 0)
            snapped += (double) s.swing * g * 0.5;
    }

    const double amount = std::clamp ((double) s.timingStrength, 0.0, 1.0);

    return beat + (snapped - beat) * amount;
}

int Quantizer::nearestScaleTone (int note, const KeySettings& k) noexcept
{
    if (! k.enabled || allowedPitchClasses (k) == allNotesMask)
        return note;

    int best     = note;
    int bestDist = 1000;

    for (int d = -11; d <= 11; ++d)
    {
        const int candidate = note + d;

        if (keyAllows (k, candidate))
        {
            const int dist = std::abs (d);

            if (dist < bestDist)
            {
                bestDist = dist;
                best     = candidate;
            }
        }
    }

    return best;
}

int Quantizer::snapPitch (float midiFloat, const KeySettings& k) noexcept
{
    if (! k.enabled || k.snap <= 0.0001f || allowedPitchClasses (k) == allNotesMask)
        return (int) std::lround (midiFloat) + k.octave * 12;

    // With notes switched off the nearest allowed note can be up to 11
    // semitones away, so search an octave either side.
    const int base = (int) std::floor (midiFloat) - 12;
    int   best = (int) std::lround (midiFloat);
    float bestDist = 1.0e9f;

    for (int d = 0; d <= 25; ++d)
    {
        const int candidate = base + d;

        if (! keyAllows (k, candidate))
            continue;

        const float dist = std::abs (midiFloat - (float) candidate);

        if (dist < bestDist - 1.0e-6f)
        {
            bestDist = dist;
            best     = candidate;
        }
    }

    const float amount  = std::clamp (k.snap, 0.0f, 1.0f);
    const float blended = midiFloat + ((float) best - midiFloat) * amount;

    return (int) std::lround (blended) + k.octave * 12;
}

QuantizedNote Quantizer::quantize (const NoteSegment& in, const Settings& s) noexcept
{
    QuantizedNote q;

    const double start = snapTime (in.startBeats, s);
    double end         = snapTime (in.endBeats, s);

    const double minLen = std::max (0.01, (double) s.minLengthBeats);

    if (end - start < minLen)
    {
        end = start + minLen;
    }
    else if (gridBeats (s.grid) > 0.0)
    {
        // Put the release on the grid as well (it already is when strength is 1),
        // then enforce the minimum length.
        const double g = gridBeats (s.grid);

        if (s.timingStrength >= 0.999f)
        {
            const double len   = std::max (g, std::round ((end - start) / g) * g);
            end = start + std::max (len, minLen);
        }
    }

    q.startBeats = start;
    q.endBeats   = end;
    q.note       = std::clamp (snapPitch (in.pitch, s.keyState()), 0, 127);
    q.velocity   = std::clamp (in.velocity, 0.05f, 1.0f);

    return q;
}

} // namespace dbc
