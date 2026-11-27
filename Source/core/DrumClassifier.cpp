#include "DrumClassifier.h"

namespace dbc
{

DrumFeatures DrumFeatures::fromRaw (float low, float f1, float f2, float high,
                                    float zcr, float sustain, float clarity) noexcept
{
    DrumFeatures out;
    const float total = low + f1 + f2 + high + 1.0e-9f;

    out.low  = low / total;
    out.f1   = f1 / total;
    out.f2   = f2 / total;
    out.high = high / total;
    out.zcr  = std::clamp (zcr, 0.0f, 1.0f);
    out.sustain = std::clamp (sustain, 0.0f, 1.0f);
    out.pitched = std::clamp (clarity, 0.0f, 1.0f);

    return out;
}

const char* DrumClassifier::padName (int pad) noexcept
{
    static const char* names[numDrumPads] {
        "Really Low Bassdrum",
        "Snare",
        "Clap",
        "Bassdrum",
        "Closed Hat",
        "Open Hat",
        "Low Tom",
        "High Tom"
    };

    if (pad < 0 || pad >= numDrumPads)
        return "";

    return names[pad];
}

void DrumClassifier::clearAll()
{
    for (auto& b : banks)
        b = Bank();
}

void DrumClassifier::clearTemplate (int pad)
{
    if (pad >= 0 && pad < numDrumPads)
        banks[pad] = Bank();
}

void DrumClassifier::setTemplate (int pad, const DrumFeatures& f)
{
    clearTemplate (pad);
    addExample (pad, f);
}

bool DrumClassifier::addExample (int pad, const DrumFeatures& f)
{
    if (pad < 0 || pad >= numDrumPads || banks[pad].count >= maxTakes)
        return false;

    banks[pad].takes[banks[pad].count++] = f;
    return true;
}

bool DrumClassifier::hasTemplate (int pad) const noexcept
{
    return numExamples (pad) > 0;
}

int DrumClassifier::numExamples (int pad) const noexcept
{
    return (pad >= 0 && pad < numDrumPads) ? banks[pad].count : 0;
}

DrumFeatures DrumClassifier::getExample (int pad, int index) const noexcept
{
    if (index < 0 || index >= numExamples (pad))
        return {};

    return banks[pad].takes[index];
}

DrumFeatures DrumClassifier::getTemplate (int pad) const noexcept
{
    const int n = numExamples (pad);
    DrumFeatures avg;

    if (n == 0)
        return avg;

    for (int i = 0; i < n; ++i)
    {
        const auto& t = banks[pad].takes[i];
        avg.low += t.low;   avg.f1 += t.f1;   avg.f2 += t.f2;   avg.high += t.high;
        avg.zcr += t.zcr;   avg.sustain += t.sustain;           avg.pitched += t.pitched;
    }

    const float k = 1.0f / (float) n;
    avg.low *= k;  avg.f1 *= k;  avg.f2 *= k;  avg.high *= k;
    avg.zcr *= k;  avg.sustain *= k;  avg.pitched *= k;
    return avg;
}

int DrumClassifier::numTrained() const noexcept
{
    int n = 0;

    for (auto& b : banks)
        if (b.count > 0)
            ++n;

    return n;
}

float DrumClassifier::distance (const DrumFeatures& f, const DrumFeatures& t) noexcept
{
    const float dl = f.low - t.low;
    const float d1 = f.f1 - t.f1;
    const float d2 = f.f2 - t.f2;
    const float dh = f.high - t.high;
    const float dz = f.zcr - t.zcr;
    const float ds = f.sustain - t.sustain;
    const float dp = f.pitched - t.pitched;

    return 1.35f * dl * dl + 1.0f * d1 * d1 + 1.15f * d2 * d2 + 1.6f * dh * dh
         + 0.8f * dz * dz + 0.9f * ds * ds + 0.5f * dp * dp;
}

int DrumClassifier::classify (const DrumFeatures& f) const noexcept
{
    int   bestPad  = -1;
    float bestDist = 1.0e9f;

    for (int pad = 0; pad < numDrumPads; ++pad)
    {
        for (int i = 0; i < banks[pad].count; ++i)
        {
            const float dist = distance (f, banks[pad].takes[i]);

            if (dist < bestDist)
            {
                bestDist = dist;
                bestPad  = pad;
            }
        }
    }

    // Reject the match when it is far away so the rules can take over.
    return bestDist <= 0.055f ? bestPad : -1;
}

int DrumClassifier::heuristic (const DrumFeatures& f) noexcept
{
    // A held, periodic sound is a sung note rather than a drum.
    if (f.pitched > 0.55f && f.sustain > 0.30f)
        return -1;

    if (f.high > 0.55f)
        return f.sustain < 0.35f ? 4 : 5;                  // closed / open hat

    if (f.low > 0.55f)
        return f.low > 0.74f ? 0 : 3;                      // really low / bassdrum

    if (f.low > 0.32f && f.pitched > 0.5f)
        return f.low > 0.45f ? 6 : 7;                      // low / high tom

    if (f.f2 > 0.35f || f.zcr > 0.06f)
        return f.sustain < 0.30f ? 2 : 1;                  // clap / snare

    if (f.pitched > 0.55f)
        return -1;

    return 1;
}

} // namespace dbc
