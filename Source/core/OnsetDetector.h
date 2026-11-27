#pragma once

#include "Types.h"

namespace dbc
{

/** Streaming band-split envelope follower with adaptive onset detection. */
class OnsetDetector
{
public:
    struct Frame
    {
        float rms   = 0.0f;
        float low   = 0.0f;   // < 120 Hz        (kick)
        float f1    = 0.0f;   // ~800 Hz         (vowel formant 1)
        float f2    = 0.0f;   // ~2000 Hz        (vowel formant 2 / snare body)
        float high  = 0.0f;   // > 5 kHz         (hats, sibilance)
        float zcr   = 0.0f;
        bool  onset = false;

        float total() const noexcept { return low + f1 + f2 + high; }
    };

    void prepare (double sampleRate, int hopSize);
    void reset();

    void pushSample (float x) noexcept;

    Frame analyseFrame (float sensitivity) noexcept;

private:
    struct Svf
    {
        double g = 0.0, k = 1.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double ic1 = 0.0, ic2 = 0.0;
        float  lp = 0.0f, bp = 0.0f, hp = 0.0f;

        void design (double fc, double q, double sr) noexcept
        {
            g  = std::tan (3.14159265358979323846 * fc / sr);
            k  = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void process (float in) noexcept
        {
            const double v3 = (double) in - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            bp = (float) v1;
            lp = (float) v2;
            hp = (float) ((double) in - k * v1 - v2);
        }
    };

    double sr = 48000.0;
    int    hop = 256;
    int    counter = 0;

    Svf lowLp, f1Bp, f2Bp, highHp;

    double accLow = 0.0, accF1 = 0.0, accF2 = 0.0, accHigh = 0.0;
    double accRms = 0.0;
    int    accZcr = 0;
    float  prevSign = 1.0f;
    int    accCount = 0;

    double prevTotal = 0.0;
    double prevBand[4] {};
    double noiseFloor = 1.0e-6;
    double fluxAvg    = 1.0e-6;
    int64_t framesSinceOnset = 1000;
};

} // namespace dbc
