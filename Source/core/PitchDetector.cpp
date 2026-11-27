#include "PitchDetector.h"

namespace dbc
{

void PitchDetector::Biquad::designLowpass (double fc, double sampleRate) noexcept
{
    const double w0    = 2.0 * 3.14159265358979323846 * fc / sampleRate;
    const double cosw0 = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * 0.7071067811865476);
    const double a0    = 1.0 + alpha;

    b0 = (1.0 - cosw0) * 0.5 / a0;
    b1 = (1.0 - cosw0) / a0;
    b2 = b0;
    a1 = (-2.0 * cosw0) / a0;
    a2 = (1.0 - alpha) / a0;
}

void PitchDetector::prepare (double sampleRate, int /*expectedHopSize*/)
{
    sr    = sampleRate > 0.0 ? sampleRate : 48000.0;
    srDec = sr / (double) decimation;

    // Anti-aliasing for the 4x decimation: two cascaded low passes.
    lp1.designLowpass (std::min (4200.0, srDec * 0.42), sr);
    lp2.designLowpass (std::min (4200.0, srDec * 0.42), sr);

    reset();
}

void PitchDetector::reset()
{
    lp1.z1 = lp1.z2 = 0.0;
    lp2.z1 = lp2.z2 = 0.0;
    decCounter = 0;
    ringPos    = 0;
    ringFill   = 0;

    for (auto& v : ring)
        v = 0.0f;
}

void PitchDetector::pushSamples (const float* x, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        const float filtered = lp2.process (lp1.process (x[i]));

        if (++decCounter >= decimation)
        {
            decCounter = 0;
            ring[ringPos] = filtered;
            ringPos = (ringPos + 1) % windowSize;
            ringFill = std::min (windowSize, ringFill + 1);
        }
    }
}

PitchDetector::Frame PitchDetector::analyse() noexcept
{
    Frame f;

    if (ringFill < windowSize / 2)
        return f;

    // Oldest sample first.
    float x[windowSize];

    for (int i = 0; i < windowSize; ++i)
        x[i] = ring[(ringPos + i) % windowSize];

    double mean = 0.0;

    for (int i = 0; i < windowSize; ++i)
        mean += x[i];

    mean /= windowSize;

    double energy = 0.0;

    for (int i = 0; i < windowSize; ++i)
    {
        x[i] = (float) (x[i] - mean);
        energy += (double) x[i] * x[i];
    }

    f.rms = (float) std::sqrt (energy / windowSize);

    if (f.rms < 1.0e-4f)
        return f;

    const int minLag = (int) std::ceil (srDec / 1000.0);   // 1 kHz
    const int maxLag = std::min (windowSize / 2 - 1, (int) std::floor (srDec / 55.0)); // 55 Hz
    const int W      = windowSize - maxLag;

    if (maxLag <= minLag + 2)
        return f;

    static thread_local std::array<double, windowSize / 2 + 2> diff {};
    static thread_local std::array<double, windowSize / 2 + 2> cmnd {};

    double runningSum = 0.0;
    diff[0] = 0.0;
    cmnd[0] = 1.0;

    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double sum = 0.0;

        for (int j = 0; j < W; ++j)
        {
            const double d = (double) x[j] - (double) x[j + tau];
            sum += d * d;
        }

        diff[tau] = sum;
        runningSum += sum;
        cmnd[tau] = runningSum > 0.0 ? (sum * tau / runningSum) : 1.0;
    }

    int tauEstimate = -1;

    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (cmnd[tau] < 0.15)
        {
            while (tau + 1 <= maxLag && cmnd[tau + 1] < cmnd[tau])
                ++tau;

            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 0)
    {
        double best = 1.0e9;
        int    bestTau = -1;

        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (cmnd[tau] < best)
            {
                best    = cmnd[tau];
                bestTau = tau;
            }
        }

        if (bestTau > 0 && best < 0.55)
            tauEstimate = bestTau;
    }

    if (tauEstimate <= 0)
        return f;

    // Parabolic interpolation around the estimate.
    double refined = (double) tauEstimate;

    if (tauEstimate > minLag && tauEstimate < maxLag)
    {
        const double s0 = cmnd[tauEstimate - 1];
        const double s1 = cmnd[tauEstimate];
        const double s2 = cmnd[tauEstimate + 1];
        const double denom = 2.0 * (2.0 * s1 - s0 - s2);

        if (std::abs (denom) > 1.0e-12)
            refined = (double) tauEstimate + (s2 - s0) / denom;
    }

    if (refined <= 0.0)
        return f;

    f.clarity = (float) std::clamp (1.0 - cmnd[tauEstimate], 0.0, 1.0);
    f.hz      = (float) (srDec / refined);

    return f;
}

} // namespace dbc
