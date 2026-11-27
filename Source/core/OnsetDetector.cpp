#include "OnsetDetector.h"

namespace dbc
{

void OnsetDetector::prepare (double sampleRate, int hopSize)
{
    sr  = sampleRate > 0.0 ? sampleRate : 48000.0;
    hop = std::max (32, hopSize);

    lowLp.design (120.0, 0.707, sr);
    f1Bp.design (800.0, 1.2, sr);
    f2Bp.design (2000.0, 1.2, sr);
    highHp.design (5000.0, 0.707, sr);

    reset();
}

void OnsetDetector::reset()
{
    lowLp.ic1 = lowLp.ic2 = 0.0;
    f1Bp.ic1 = f1Bp.ic2 = 0.0;
    f2Bp.ic1 = f2Bp.ic2 = 0.0;
    highHp.ic1 = highHp.ic2 = 0.0;

    accLow = accF1 = accF2 = accHigh = 0.0;
    accRms = 0.0;
    accZcr = 0;
    accCount = 0;
    prevSign = 1.0f;

    prevTotal = 0.0;
    prevBand[0] = prevBand[1] = prevBand[2] = prevBand[3] = 0.0;
    noiseFloor = 1.0e-6;
    fluxAvg = 1.0e-6;
    framesSinceOnset = 1000;
    counter = 0;
}

void OnsetDetector::pushSample (float x) noexcept
{
    lowLp.process (x);
    f1Bp.process (x);
    f2Bp.process (x);
    highHp.process (x);

    accLow  += (double) lowLp.lp * lowLp.lp;
    accF1   += (double) f1Bp.bp * f1Bp.bp;
    accF2   += (double) f2Bp.bp * f2Bp.bp;
    accHigh += (double) highHp.hp * highHp.hp;
    accRms  += (double) x * x;

    const float sign = x >= 0.0f ? 1.0f : -1.0f;

    if (sign != prevSign)
    {
        ++accZcr;
        prevSign = sign;
    }

    ++accCount;
}

OnsetDetector::Frame OnsetDetector::analyseFrame (float sensitivity) noexcept
{
    Frame f;

    const int n = std::max (1, accCount);
    const double inv = 1.0 / (double) n;

    f.low  = (float) std::sqrt (accLow * inv);
    f.f1   = (float) std::sqrt (accF1 * inv);
    f.f2   = (float) std::sqrt (accF2 * inv);
    f.high = (float) std::sqrt (accHigh * inv);
    f.rms  = (float) std::sqrt (accRms * inv);
    f.zcr  = (float) accZcr / (float) n;

    accLow = accF1 = accF2 = accHigh = 0.0;
    accRms = 0.0;
    accZcr = 0;
    accCount = 0;

    const double total = (double) f.total();
    const double band[4] { (double) f.low, (double) f.f1, (double) f.f2, (double) f.high };

    double flux = 0.0;

    for (int i = 0; i < 4; ++i)
    {
        flux += std::max (0.0, band[i] - prevBand[i]);
        prevBand[i] = band[i];
    }

    // Slow-tracking floor: drops quickly, rises slowly.
    if (total < noiseFloor)
        noiseFloor += (total - noiseFloor) * 0.05;
    else
        noiseFloor += (total - noiseFloor) * 0.0008;

    noiseFloor = std::max (noiseFloor, 1.0e-8);

    fluxAvg += (flux - fluxAvg) * (flux > fluxAvg ? 0.25 : 0.02);
    fluxAvg = std::max (fluxAvg, 1.0e-8);

    const double sens = std::clamp ((double) sensitivity, 0.0, 1.0);
    const double riseFactor = 3.2 - 2.2 * sens;

    const bool energyHit = total > noiseFloor * riseFactor
                           && total > prevTotal * (1.75 - 0.55 * sens)
                           && total > 2.0e-5;

    const bool fluxHit = flux > fluxAvg * (3.4 - 1.2 * sens)
                         && total > prevTotal * 1.25
                         && total > noiseFloor * 1.6
                         && total > 2.0e-5;

    const int minGap = (int) std::max (2.0, 0.075 * sr / (double) hop);   // 75 ms

    if ((energyHit || fluxHit) && framesSinceOnset > minGap)
    {
        f.onset = true;
        framesSinceOnset = 0;
        noiseFloor = std::max (noiseFloor, total * 0.02);
    }
    else
    {
        ++framesSinceOnset;
    }

    prevTotal = total;

    return f;
}

} // namespace dbc
