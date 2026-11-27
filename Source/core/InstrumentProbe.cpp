#include "InstrumentProbe.h"

#include <algorithm>
#include <cmath>

namespace dbc
{

const char* instrumentTargetName (InstrumentTarget t) noexcept
{
    switch (t)
    {
        case InstrumentTarget::Instrument: return "Instrument";
        case InstrumentTarget::DrumKit:    return "Drum kit";
        case InstrumentTarget::None:
        case InstrumentTarget::NumTargets: break;
    }

    return "None";
}

namespace
{
    constexpr double kBaselineStart = 0.02;   // skip the first few ms (block edges)
    constexpr double kBaselineEnd   = 0.12;
    constexpr double kMaxOnset      = 0.25;   // the answer must start within this
    constexpr double kSustainAt     = 0.28;   // sustain is measured 0.28 - 0.36 s in
    constexpr double kSustainEnd    = 0.36;
    constexpr uint8_t kVelocity     = 72;

    float rms (const std::vector<float>& x, int start, int end) noexcept
    {
        start = std::clamp (start, 0, (int) x.size());
        end   = std::clamp (end, start, (int) x.size());

        if (end <= start)
            return 0.0f;

        double sum = 0.0;

        for (int i = start; i < end; ++i)
            sum += (double) x[(size_t) i] * x[(size_t) i];

        return (float) std::sqrt (sum / (double) (end - start));
    }

    float toDb (float g) noexcept { return g > 1.0e-5f ? 20.0f * std::log10 (g) : -100.0f; }

    /** YIN over x[start .. start + window + maxLag). Returns f0 (0 if none) and the
        aperiodicity at the chosen lag. */
    void yin (const std::vector<float>& x, int start, double sr, float& f0, float& aperiodicity)
    {
        f0 = 0.0f;
        aperiodicity = 1.0f;

        const int window = (int) (0.03 * sr);
        const int minLag = std::max (2, (int) (sr / 1200.0));
        const int maxLag = (int) (sr / 50.0);

        if (start < 0 || start + window + maxLag + 1 >= (int) x.size())
            return;

        std::vector<float> d ((size_t) maxLag + 2, 0.0f);
        const float* s = x.data() + start;

        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            double sum = 0.0;

            for (int i = 0; i < window; ++i)
            {
                const double diff = (double) s[i] - (double) s[i + tau];
                sum += diff * diff;
            }

            d[(size_t) tau] = (float) sum;
        }

        // Cumulative mean normalised difference.
        double running = 0.0;
        d[0] = 1.0f;

        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            running += d[(size_t) tau];
            d[(size_t) tau] = running > 0.0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
        }

        int best = -1;

        for (int tau = minLag; tau <= maxLag; ++tau)
        {
            if (d[(size_t) tau] < 0.2f)
            {
                while (tau + 1 <= maxLag && d[(size_t) tau + 1] < d[(size_t) tau])
                    ++tau;

                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            best = minLag;

            for (int tau = minLag; tau <= maxLag; ++tau)
                if (d[(size_t) tau] < d[(size_t) best])
                    best = tau;
        }

        aperiodicity = d[(size_t) best];

        // Parabolic interpolation around the minimum.
        double lag = best;
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double den = a - 2.0 * b + c;

        if (std::abs (den) > 1.0e-9)
            lag += 0.5 * (a - c) / den;

        if (lag > 0.0)
            f0 = (float) (sr / lag);
    }

    float zeroCrossingHz (const std::vector<float>& x, int start, int end, double sr) noexcept
    {
        start = std::clamp (start, 1, (int) x.size());
        end   = std::clamp (end, start, (int) x.size());

        if (end - start < 2)
            return 0.0f;

        int crossings = 0;

        for (int i = start; i < end; ++i)
            if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f))
                ++crossings;

        return (float) (0.5 * crossings * sr / (double) (end - start));
    }
}

//==============================================================================
const InstrumentProbe::Step* InstrumentProbe::steps() noexcept
{
    static const Step s[numProbeNotes] {
        // C4 before C3: if C4's release is still ringing under C3, the two
        // share C3's period, so the pitch read for C3 stays right.
        { 60, 0.12, 0.52, 0.82 },   // C4, held
        { 48, 0.82, 1.22, 1.52 },   // C3, held - an octave down
        { 42, 1.52, 1.62, 1.92 },   // F#2 / GM closed hat, short
    };

    return s;
}

double InstrumentProbe::durationSeconds() noexcept
{
    return steps()[numProbeNotes - 1].windowEnd;
}

void InstrumentProbe::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    totalSamples = (int) std::ceil (durationSeconds() * sr);
    recSide.assign ((size_t) totalSamples, 0.0f);
    recMain.assign ((size_t) totalSamples, 0.0f);
    phase.store (Phase::Idle);
    startRequested.store (false);
    cancelRequested.store (false);
    pos = 0;
    nextStep = 0;
    soundingNote = -1;
}

void InstrumentProbe::start (int voiceChannel, int drumChannel) noexcept
{
    pendingVoiceChan.store (std::clamp (voiceChannel, 1, 16));
    pendingDrumChan.store (std::clamp (drumChannel, 1, 16));
    cancelRequested.store (false);
    startRequested.store (true);
}

void InstrumentProbe::emit (std::vector<MidiEvent>& out, int offset, bool on, int note) const
{
    const auto add = [&] (int channel)
    {
        MidiEvent e;
        e.offset = offset;
        e.status = (uint8_t) ((on ? 0x90 : 0x80) | (channel - 1));
        e.data1  = (uint8_t) note;
        e.data2  = on ? kVelocity : 0;
        e.drum   = channel == chanDrum;
        out.push_back (e);
    };

    add (chanVoice);

    if (chanDrum != chanVoice)
        add (chanDrum);
}

void InstrumentProbe::process (const float* sidechain, const float* mainInput, int numSamples,
                               std::vector<MidiEvent>& out)
{
    if (cancelRequested.exchange (false))
    {
        startRequested.store (false);

        if (phase.load() == Phase::Running)
        {
            if (soundingNote >= 0)
                emit (out, 0, false, soundingNote);

            soundingNote = -1;
            phase.store (Phase::Idle);
        }
    }

    if (phase.load() == Phase::Idle && startRequested.exchange (false) && totalSamples > 0)
    {
        chanVoice = pendingVoiceChan.load();
        chanDrum  = pendingDrumChan.load();
        pos = 0;
        nextStep = 0;
        soundingNote = -1;
        std::fill (recSide.begin(), recSide.end(), 0.0f);
        std::fill (recMain.begin(), recMain.end(), 0.0f);
        phase.store (Phase::Running);
    }

    if (phase.load() != Phase::Running)
        return;

    const int n = std::min (numSamples, totalSamples - pos);

    for (int i = 0; i < n; ++i)
    {
        const float a = sidechain != nullptr ? sidechain[i] : 0.0f;
        const float b = mainInput != nullptr ? mainInput[i] : 0.0f;
        recSide[(size_t) (pos + i)] = a;
        recMain[(size_t) (pos + i)] = b;
    }

    // Note-ons and note-offs that fall inside this block.
    const auto* s = steps();

    for (int k = 0; k < numProbeNotes; ++k)
    {
        const int onAt  = (int) (s[k].onAt * sr);
        const int offAt = (int) (s[k].offAt * sr);

        if (onAt >= pos && onAt < pos + numSamples)
        {
            emit (out, std::min (onAt - pos, numSamples - 1), true, s[k].note);
            soundingNote = s[k].note;
        }

        if (offAt >= pos && offAt < pos + numSamples)
        {
            emit (out, std::min (offAt - pos, numSamples - 1), false, s[k].note);
            soundingNote = -1;
        }
    }

    pos += n;

    if (pos >= totalSamples)
    {
        if (soundingNote >= 0)
            emit (out, std::max (0, n - 1), false, soundingNote);

        soundingNote = -1;
        phase.store (Phase::Recorded);
    }
}

//==============================================================================
InstrumentProbe::NoteResult InstrumentProbe::analyseNote (const std::vector<float>& rec, int k, float noiseRms) const
{
    const auto& step = steps()[k];

    NoteResult r;
    r.note = step.note;

    const int hop   = std::max (16, (int) (sr * 0.005));
    const int frame = hop * 2;
    const int onAt  = (int) (step.onAt * sr);
    const int last  = std::min ((int) (step.windowEnd * sr), (int) rec.size());
    const int maxOnset = onAt + (int) (kMaxOnset * sr);

    // An answer has to stand 12 dB clear of what was there before the test,
    // above -66 dBFS whatever the noise floor, and 6 dB above whatever was
    // still ringing from the previous test note.
    const float before = rms (rec, onAt - (int) (0.03 * sr), onAt);
    const float threshold = std::max ({ noiseRms * 4.0f, 5.0e-4f, before * 2.0f });

    int onset = -1;

    for (int i = onAt; i + frame <= last && i <= maxOnset; i += hop)
    {
        if (rms (rec, i, i + frame) > threshold)
        {
            onset = i;
            break;
        }
    }

    if (onset < 0)
        return r;

    r.heard   = true;
    r.onsetMs = (float) (1000.0 * (onset - onAt) / sr);

    float peak = 0.0f;
    const int peakEnd = std::min (last, onset + (int) (0.15 * sr));

    for (int i = onset; i + frame <= peakEnd; i += hop)
        peak = std::max (peak, rms (rec, i, i + frame));

    r.peakDb = toDb (peak);

    const bool held = step.offAt - step.onAt > kSustainEnd;

    if (held && peak > 0.0f)
        r.sustain = rms (rec, onset + (int) (kSustainAt * sr), onset + (int) (kSustainEnd * sr)) / peak;

    // Pitch a little after the attack, once the transient has passed.
    yin (rec, onset + (int) (0.03 * sr), sr, r.f0, r.aperiodicity);
    r.zcrHz = zeroCrossingHz (rec, onset, onset + (int) (0.05 * sr), sr);

    return r;
}

InstrumentTarget InstrumentProbe::classify (const NoteResult (&n)[numProbeNotes], std::string* reason)
{
    const auto say = [reason] (const char* text) { if (reason != nullptr) *reason = text; };

    const int heard = (int) n[0].heard + (int) n[1].heard + (int) n[2].heard;

    if (heard == 0)
    {
        say ("Nothing answered the test notes.");
        return InstrumentTarget::None;
    }

    // An instrument plays C4 and C3 an octave apart.
    bool tracksOctave = false;

    if (n[0].pitched() && n[1].pitched())
    {
        const double octaves = std::log2 ((double) n[0].f0 / (double) n[1].f0);
        tracksOctave = std::abs (octaves - 1.0) < 70.0 / 1200.0;
    }

    // ... and keeps sounding while the note is held. A one-shot drum sample
    // played chromatically is gone (24 dB down) 0.3 s in on both notes.
    const bool percussive = n[0].sustain < 0.063f && n[1].sustain < 0.063f;

    // A drum kit answers note 42 with a closed hat: noise, not a pitch.
    const bool hatLike = n[2].heard && ! n[2].pitched() && n[2].zcrHz > 2500.0f;

    if (tracksOctave && ! percussive && ! hatLike)
    {
        say ("It plays the test notes as pitches, an octave apart.");
        return InstrumentTarget::Instrument;
    }

    if (hatLike)
        say ("Note 42 came back as a hi-hat.");
    else if (heard < numProbeNotes)
        say ("Only some of the test notes made a sound, like a drum kit.");
    else if (! tracksOctave)
        say ("The test notes came back as different sounds, not as pitches.");
    else
        say ("The test notes came back as short one-shot hits.");

    return InstrumentTarget::DrumKit;
}

InstrumentProbe::Result InstrumentProbe::analyse()
{
    Result result;

    if (phase.load() != Phase::Recorded)
        return result;

    const int b0 = (int) (kBaselineStart * sr), b1 = (int) (kBaselineEnd * sr);

    const auto run = [&] (const std::vector<float>& rec, NoteResult (&out)[numProbeNotes])
    {
        const float noise = rms (rec, b0, b1);
        int heard = 0;

        for (int k = 0; k < numProbeNotes; ++k)
        {
            out[k] = analyseNote (rec, k, noise);
            heard += out[k].heard ? 1 : 0;
        }

        return heard;
    };

    // The sidechain is where the instrument should be; the main input is the
    // fallback (instrument on the same insert, or a mic hearing the speakers).
    if (run (recSide, result.notes) == 0)
    {
        NoteResult mainNotes[numProbeNotes];

        if (run (recMain, mainNotes) > 0)
        {
            std::copy (std::begin (mainNotes), std::end (mainNotes), std::begin (result.notes));
            result.heardOnMainInput = true;
        }
    }

    result.target = classify (result.notes, &result.reason);
    phase.store (Phase::Idle);
    return result;
}

} // namespace dbc
