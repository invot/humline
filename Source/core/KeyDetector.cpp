#include "KeyDetector.h"

namespace dbc
{

namespace
{
    // Krumhansl & Kessler (1982) probe-tone ratings, tonic first.
    constexpr std::array<double, 12> kMajorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                                     2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    constexpr std::array<double, 12> kMinorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                                     2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double correlate (const std::array<float, 12>& h, const std::array<double, 12>& profile, int root)
    {
        double meanH = 0.0, meanP = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            meanH += h[(size_t) i];
            meanP += profile[(size_t) i];
        }

        meanH /= 12.0;
        meanP /= 12.0;

        double num = 0.0, dh = 0.0, dp = 0.0;

        for (int pc = 0; pc < 12; ++pc)
        {
            const double x = (double) h[(size_t) pc] - meanH;
            const double y = profile[(size_t) (((pc - root) % 12 + 12) % 12)] - meanP;
            num += x * y;
            dh  += x * x;
            dp  += y * y;
        }

        const double den = std::sqrt (dh * dp);
        return den > 1.0e-12 ? num / den : 0.0;
    }
}

int KeyDetector::distinctNotes (const std::array<float, 12>& h) noexcept
{
    float peak = 0.0f;

    for (auto v : h)
        peak = std::max (peak, v);

    if (peak <= 0.0f)
        return 0;

    int n = 0;

    for (auto v : h)
        if (v > peak * 0.08f)
            ++n;

    return n;
}

std::vector<KeySuggestion> KeyDetector::suggest (const std::array<float, 12>& histogram, int maxResults)
{
    std::vector<KeySuggestion> out;

    float total = 0.0f;

    for (auto v : histogram)
        total += std::max (0.0f, v);

    if (total <= 1.0e-9f)
        return out;

    std::array<float, 12> h {};

    for (int i = 0; i < 12; ++i)
        h[(size_t) i] = std::max (0.0f, histogram[(size_t) i]) / total;

    const ScaleType scales[] { ScaleType::Major, ScaleType::NaturalMinor };

    for (int root = 0; root < 12; ++root)
    {
        for (auto scale : scales)
        {
            KeySuggestion k;
            k.root  = root;
            k.scale = scale;
            k.score = (float) correlate (h, scale == ScaleType::Major ? kMajorProfile : kMinorProfile, root);

            float inside = 0.0f;

            for (int pc = 0; pc < 12; ++pc)
                if (scaleContains (scale, root, pc))
                    inside += h[(size_t) pc];

            k.match = inside;
            out.push_back (k);
        }
    }

    // A key that leaves sung notes outside the scale is a worse answer than one
    // that holds them all, whatever the profile says, so coverage weighs in too.
    std::sort (out.begin(), out.end(), [] (const KeySuggestion& a, const KeySuggestion& b)
    {
        const float sa = a.score + 0.6f * a.match;
        const float sb = b.score + 0.6f * b.match;
        return sa > sb;
    });

    if ((int) out.size() > maxResults)
        out.resize ((size_t) std::max (0, maxResults));

    return out;
}

} // namespace dbc
