#pragma once

#include "Types.h"

namespace dbc
{

/** YIN pitch detector running on a 4x decimated signal (monophonic voice/bass). */
class PitchDetector
{
public:
    struct Frame
    {
        float hz      = 0.0f;
        float clarity = 0.0f;   // 1 = perfectly periodic
        float rms     = 0.0f;
    };

    void prepare (double sampleRate, int expectedHopSize);
    void reset();

    void pushSamples (const float* x, int numSamples) noexcept;

    /** Analyses the most recent window. Cheap enough to call once per hop. */
    Frame analyse() noexcept;

    static constexpr int windowSize = 1024;
    static constexpr int decimation = 4;

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void designLowpass (double fc, double sr) noexcept;
        float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    double sr = 48000.0;
    double srDec = 12000.0;

    Biquad lp1, lp2;
    int decCounter = 0;

    float ring[windowSize] {};
    int   ringPos = 0;
    int   ringFill = 0;
};

} // namespace dbc
