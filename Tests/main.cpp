// Headless tests for the audio -> MIDI core.  No JUCE, no drivers: the engine is
// fed synthetic vocals and beatbox hits and the emitted MIDI is checked.

#include "core/InstrumentProbe.h"
#include "core/KeyDetector.h"
#include "core/Quantizer.h"
#include "core/Take.h"
#include "core/VoiceEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace dbc;

namespace
{

constexpr double sr = 48000.0;
int gFailures = 0;

void check (bool cond, const std::string& what)
{
    if (! cond)
    {
        std::printf ("  FAIL  %s\n", what.c_str());
        ++gFailures;
    }
    else
    {
        std::printf ("  ok    %s\n", what.c_str());
    }
}

void checkNear (double a, double b, double tol, const std::string& what)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "%s (got %.4f, want %.4f +/- %.4f)", what.c_str(), a, b, tol);
    check (std::abs (a - b) <= tol, buf);
}

struct Rng
{
    uint32_t s = 0x12345678u;
    float next() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return (float) ((s >> 9) & 0x7FFFFF) / (float) 0x7FFFFF * 2.0f - 1.0f;
    }
};

void addTone (std::vector<float>& buf, double t0, double dur, double hz, float amp,
              double cents = 0.0, bool vibrato = false)
{
    const int start = (int) (t0 * sr);
    const int len   = (int) (dur * sr);
    const double f  = hz * std::pow (2.0, cents / 1200.0);

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;

        if (idx < 0 || idx >= (int) buf.size())
            continue;

        const double t = (double) i / sr;
        const double env = std::min (1.0, t / 0.012) * std::min (1.0, (dur - t) / 0.02);
        const double vib = vibrato ? std::sin (2.0 * 3.14159265358979 * 5.0 * t) * 0.18 : 0.0;
        const double phase = 2.0 * 3.14159265358979 * f * t + vib;

        // A couple of harmonics so it looks a bit more like a voice.
        double v = std::sin (phase) + 0.25 * std::sin (2.0 * phase) + 0.10 * std::sin (3.0 * phase);
        buf[(size_t) idx] += (float) (v * amp * std::max (0.0, env));
    }
}

void addKick (std::vector<float>& buf, double t0)
{
    const int start = (int) (t0 * sr);
    const int len   = (int) (0.18 * sr);

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;
        if (idx >= (int) buf.size()) break;

        const double t = (double) i / sr;
        const double env = std::exp (-t / 0.055);
        const double f = 55.0 + 90.0 * std::exp (-t / 0.012);
        buf[(size_t) idx] += (float) (0.85 * env * std::sin (2.0 * 3.14159265358979 * f * t));
    }
}

void addSnare (std::vector<float>& buf, double t0)
{
    Rng rng;
    const int start = (int) (t0 * sr);
    const int len   = (int) (0.25 * sr);
    double lp1 = 0.0, lp2 = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;
        if (idx >= (int) buf.size()) break;

        const double t = (double) i / sr;
        const double env = std::exp (-t / 0.09);
        const double n = rng.next();
        lp1 += (n - lp1) * 0.35;      // ~2.6 kHz one pole
        lp2 += (lp1 - lp2) * 0.35;
        buf[(size_t) idx] += (float) (0.55 * env * lp2 * 3.0);
    }
}

void addHat (std::vector<float>& buf, double t0)
{
    Rng rng;
    rng.s = 0x9e3779b9u;
    const int start = (int) (t0 * sr);
    const int len   = (int) (0.09 * sr);
    double prev = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;
        if (idx >= (int) buf.size()) break;

        const double t = (double) i / sr;
        const double env = std::exp (-t / 0.022);
        const double n = rng.next();
        const double hp = n - prev;    // crude but very "hatty"
        prev = n;
        buf[(size_t) idx] += (float) (0.45 * env * hp);
    }
}

struct Event
{
    int64_t abs    = 0;
    int     status = 0;
    int     data1  = 0;
    int     data2  = 0;

    int channel() const noexcept { return (status & 0x0F) + 1; }
    int type() const noexcept { return status & 0xF0; }
};

std::vector<Event> runEngineWith (VoiceEngine& engine, const std::vector<float>& audio, const Settings& s,
                                  double bpm = 120.0, int blockSize = 512)
{
    std::vector<Event> out;
    std::vector<MidiEvent> block;
    const double spb = 60.0 / bpm;

    for (int pos = 0; pos < (int) audio.size(); pos += blockSize)
    {
        const int n = std::min (blockSize, (int) audio.size() - pos);
        const double ppq = (double) pos / sr / spb;

        engine.process (audio.data() + pos, n, ppq, bpm, s, block);

        for (const auto& m : block)
            out.push_back ({ (int64_t) pos + m.offset, m.status, m.data1, m.data2 });
    }

    // Events are sent a constant offset late so quantisation is never truncated;
    // remove it so the test measures musical positions.
    const int64_t latency = engine.getOutputLatencySamples();

    for (auto& e : out)
        e.abs -= latency;

    return out;
}

std::vector<Event> runEngine (const std::vector<float>& audio, const Settings& s, double bpm = 120.0,
                              int blockSize = 512)
{
    VoiceEngine engine;
    engine.prepare (sr, blockSize);
    return runEngineWith (engine, audio, s, bpm, blockSize);
}

std::vector<Event> noteOns (const std::vector<Event>& events, int channel = -1)
{
    std::vector<Event> res;

    for (const auto& e : events)
        if (e.type() == 0x90 && e.data2 > 0 && (channel < 0 || e.channel() == channel))
            res.push_back (e);

    return res;
}

double beatsOf (int64_t abs, double bpm = 120.0)
{
    return (double) abs / sr * bpm / 60.0;
}

//==============================================================================
void testQuantizer()
{
    std::printf ("\n[quantiser]\n");

    Settings s;
    s.grid = SnapGrid::Sixteenth;
    s.timingStrength = 1.0f;

    checkNear (Quantizer::snapTime (0.225, s), 0.25, 1e-9, "1/16 snaps 0.225 -> 0.25");
    checkNear (Quantizer::snapTime (0.30, s), 0.25, 1e-9, "1/16 snaps 0.30 -> 0.25");
    checkNear (Quantizer::snapTime (0.38, s), 0.50, 1e-9, "1/16 snaps 0.38 -> 0.50");
    checkNear (Quantizer::snapTime (7.06, s), 7.0, 1e-9, "1/16 snaps 7.06 -> 7.0");

    s.timingStrength = 0.5f;
    checkNear (Quantizer::snapTime (0.30, s), 0.275, 1e-9, "half strength moves halfway");

    s.timingStrength = 0.0f;
    checkNear (Quantizer::snapTime (0.30, s), 0.30, 1e-9, "zero strength leaves timing alone");

    s.timingStrength = 1.0f;
    s.grid = SnapGrid::Off;
    checkNear (Quantizer::snapTime (0.31, s), 0.31, 1e-9, "grid off leaves timing alone");

    s.grid = SnapGrid::EighthTriplet;
    checkNear (Quantizer::snapTime (0.34, s), 1.0 / 3.0, 1e-9, "1/8T snaps 0.34 -> 0.333");

    s.grid = SnapGrid::Sixteenth;
    s.swing = 0.5f;
    const double swung = Quantizer::snapTime (0.25, s);
    checkNear (swung, 0.3125, 1e-9, "swing pushes the off-beat late");

    KeySettings k;
    k.enabled = true;
    k.root    = 0;
    k.scale   = ScaleType::Major;
    k.snap    = 1.0f;

    check (Quantizer::snapPitch (61.2f, k) == 62, "C# out of key snaps up to D in C major");
    check (Quantizer::snapPitch (60.4f, k) == 60, "C in key stays C");
    check (Quantizer::snapPitch (63.6f, k) == 64, "D# in C major snaps to E");

    k.scale = ScaleType::NaturalMinor;
    k.root  = 9;   // A minor
    check (Quantizer::snapPitch (66.1f, k) == 67, "F# snaps to G in A minor");

    k.snap = 0.0f;
    check (Quantizer::snapPitch (61.2f, k) == 61, "snap amount 0 keeps chromatic pitch");

    k.enabled = false;
    k.snap = 1.0f;
    check (Quantizer::snapPitch (61.2f, k) == 61, "key lock off keeps chromatic pitch");

    Settings qs;
    qs.minLengthBeats = 0.25f;
    qs.grid = SnapGrid::Off;
    NoteSegment seg { 1.0, 1.02, 57.0f, 0.8f };
    auto q = Quantizer::quantize (seg, qs);
    checkNear (q.endBeats - q.startBeats, 0.25, 1e-9, "minimum length is enforced");
    check (q.note == 57, "in-key note passes through");
}

//==============================================================================
void testMelody()
{
    std::printf ("\n[melody: timing + key correction]\n");

    std::vector<float> audio ((size_t) (3.4 * sr), 0.0f);

    // C3, E3, G3 sung late and slightly sharp.
    addTone (audio, 0.53, 0.40, 130.81, 0.35, 30.0, true);
    addTone (audio, 1.03, 0.40, 164.81, 0.35, 30.0, true);
    addTone (audio, 1.53, 0.40, 196.00, 0.35, 30.0, true);

    Settings s;
    s.grid = SnapGrid::Sixteenth;
    s.timingStrength = 1.0f;
    s.keyLock = true;
    s.keyRoot = 0;
    s.scale = ScaleType::Major;
    s.pitchSnap = 1.0f;
    s.drumsEnabled = true;

    const auto events = runEngine (audio, s);
    const auto notes  = noteOns (events, 1);

    std::printf ("  emitted %d voice note(s)\n", (int) notes.size());

    for (const auto& n : notes)
        std::printf ("    note %3d  at beat %7.3f  vel %d\n", n.data1, beatsOf (n.abs), n.data2);

    check (notes.size() == 3, "three sung notes produce three MIDI notes");

    if (notes.size() == 3)
    {
        check (notes[0].data1 == 48, "first note is C3");
        check (notes[1].data1 == 52, "second note is E3");
        check (notes[2].data1 == 55, "third note is G3");

        checkNear (beatsOf (notes[0].abs), 1.00, 0.02, "late C3 pulled onto beat 1");
        checkNear (beatsOf (notes[1].abs), 2.00, 0.02, "late E3 pulled onto beat 2");
        checkNear (beatsOf (notes[2].abs), 3.00, 0.02, "late G3 pulled onto beat 3");

        for (const auto& n : notes)
        {
            const auto offIt = std::find_if (events.begin(), events.end(), [&] (const Event& e)
            {
                return e.type() == 0x80 && e.data1 == n.data1 && e.abs > n.abs;
            });

            check (offIt != events.end(), "every note gets a note-off");
        }
    }

    // Same performance, snap disabled: the raw timing must survive.
    Settings raw = s;
    raw.grid = SnapGrid::Off;
    raw.timingStrength = 0.0f;
    raw.keyLock = false;

    const auto rawNotes = noteOns (runEngine (audio, raw), 1);
    check (rawNotes.size() == 3, "three notes with snapping off");

    if (rawNotes.size() == 3)
    {
        check (beatsOf (rawNotes[0].abs) > 1.01 && beatsOf (rawNotes[0].abs) < 1.16,
               "unsnapped note keeps its late timing");
        check (std::abs (rawNotes[0].data1 - 48) <= 1, "unsnapped pitch stays near C3");
    }

    // Out-of-key performance gets pulled into the scale.
    std::vector<float> offKey ((size_t) (2.2 * sr), 0.0f);
    addTone (offKey, 0.50, 0.60, 138.59, 0.35);   // C#3
    addTone (offKey, 1.30, 0.60, 155.56, 0.35);   // D#3

    Settings ks = s;
    ks.drumsEnabled = false;

    const auto keyNotes = noteOns (runEngine (offKey, ks), 1);
    check (keyNotes.size() == 2, "two out-of-key notes detected");

    if (keyNotes.size() == 2)
    {
        check (keyNotes[0].data1 == 50, "C#3 pulled to D3 in C major");
        check (keyNotes[1].data1 == 52, "D#3 pulled to E3 in C major");
    }
}

//==============================================================================
void testBeatbox()
{
    std::printf ("\n[beatbox: drums]\n");

    std::vector<float> audio ((size_t) (2.6 * sr), 0.0f);

    addKick  (audio, 0.50);
    addSnare (audio, 1.00);
    addHat   (audio, 1.50);

    Settings s;
    s.grid = SnapGrid::Sixteenth;
    s.drumsEnabled = true;
    s.drumSensitivity = 0.5f;
    s.drumsOnDrumChan = true;
    s.drumChannel = 10;

    const auto events = runEngine (audio, s);
    const auto notes  = noteOns (events, 10);

    std::printf ("  emitted %d drum note(s)\n", (int) notes.size());

    for (const auto& n : notes)
        std::printf ("    note %3d  at beat %7.3f\n", n.data1, beatsOf (n.abs));

    check (notes.size() == 3, "kick, snare and hat produce three drum notes");

    if (notes.size() >= 3)
    {
        check (notes[0].data1 == 35 || notes[0].data1 == 36, "first hit is a bass drum");
        check (notes[1].data1 == 38 || notes[1].data1 == 39, "second hit is a snare/clap");
        check (notes[2].data1 == 42 || notes[2].data1 == 46, "third hit is a hat");

        checkNear (beatsOf (notes[0].abs), 1.00, 0.03, "kick lands on beat 1");
        checkNear (beatsOf (notes[1].abs), 2.00, 0.03, "snare lands on beat 2");
        checkNear (beatsOf (notes[2].abs), 3.00, 0.03, "hat lands on beat 3");
    }

    // Trained trigger templates should take over from the rules.
    {
        VoiceEngine engine;
        engine.prepare (sr, 512);

        std::vector<float> kicks ((size_t) (1.2 * sr), 0.0f);
        addKick (kicks, 0.2);
        addKick (kicks, 0.6);
        addKick (kicks, 1.0);

        engine.startTraining (2, 3);

        std::vector<MidiEvent> block;
        for (int pos = 0; pos < (int) kicks.size(); pos += 512)
        {
            const int n = std::min (512, (int) kicks.size() - pos);
            engine.process (kicks.data() + pos, n, (double) pos / sr * 2.0, 120.0, s, block);
        }

        check (engine.getClassifier().hasTemplate (2), "training a pad stores a template");
        check (! engine.isTraining(), "training stops after the requested takes");
        check (engine.getClassifier().numTrained() == 1, "one trained pad");

        // Reuse the engine: the learned templates must survive a reset.
        engine.reset();
        const auto trained = noteOns (runEngineWith (engine, kicks, s), 10);
        check (! trained.empty(), "trained engine still triggers the kick");
        check (! trained.empty() && trained[0].data1 == 39, "trained kick maps to pad 2's note");
    }
}

//==============================================================================
void testSustainAndPolyphonyGuard()
{
    std::printf ("\n[sustained note]\n");

    std::vector<float> audio ((size_t) (2.0 * sr), 0.0f);
    addTone (audio, 0.40, 1.10, 220.0, 0.30, 0.0, true);

    Settings s;
    s.grid = SnapGrid::Off;
    s.keyLock = false;

    const auto notes = noteOns (runEngine (audio, s), 1);

    std::printf ("  emitted %d voice note(s)\n", (int) notes.size());

    check (notes.size() == 1, "a held note produces a single MIDI note");

    if (! notes.empty())
    {
        check (notes[0].data1 == 57, "220 Hz is A3");
        checkNear (beatsOf (notes[0].abs), 0.80, 0.06, "sustained note starts on time");
    }
}

//==============================================================================
void testChords()
{
    std::printf ("\n[chords]\n");

    KeySettings k;
    k.enabled = true;
    k.root    = 0;
    k.scale   = ScaleType::Major;
    k.snap    = 1.0f;

    auto c = chordForNote (48, k, ChordMode::Triads, ChordVoicing::Close);
    check (c.count == 3, "a sung C becomes a three-note chord");
    check (c.notes[0] == 48 && c.notes[1] == 52 && c.notes[2] == 55, "the C triad is C E G");
    check (chordName (c) == "C", "the tonic triad is named C");
    check (chordRoman (c) == "I", "the tonic triad is I");

    auto dm = chordForNote (50, k, ChordMode::Triads, ChordVoicing::Close);
    check (chordName (dm) == "Dm", "D maps to Dm in C major");
    check (dm.notes[0] == 50 && dm.notes[1] == 53 && dm.notes[2] == 57, "Dm is D F A");
    check (chordRoman (dm) == "ii", "the second degree is lower case");

    auto bdim = chordForNote (59, k, ChordMode::Triads, ChordVoicing::Close);
    check (chordName (bdim) == "Bdim", "B maps to Bdim in C major");

    auto g7 = chordForNote (55, k, ChordMode::Sevenths, ChordVoicing::Close);
    check (g7.count == 4 && chordName (g7) == "G7", "the sevenths preset gives G7 on the fifth");
    check (g7.notes[3] == 65, "G7 adds the F above the triad");

    auto sus = chordForNote (48, k, ChordMode::Sus, ChordVoicing::Close);
    check (chordName (sus) == "Csus4" && sus.notes[1] == 53, "the sus preset replaces the third");

    auto power = chordForNote (48, k, ChordMode::Power, ChordVoicing::Close);
    check (chordName (power) == "C5" && power.count == 3, "the power preset is root, fifth, octave");

    auto wide = chordForNote (48, k, ChordMode::Triads, ChordVoicing::Wide);
    check (wide.count == 3 && wide.notes[1] == 64 && wide.notes[2] == 67,
           "the wide voicing lifts the upper voices an octave");
    check (chordName (wide) == "C", "the wide voicing keeps the chord name");

    ChordOptions bassOpts;
    bassOpts.rootBass = true;
    auto bass = chordForNote (48, k, bassOpts);
    check (bass.count == 4 && bass.notes[0] == 36, "root bassline adds the root an octave down");

    auto open = chordForNote (48, k, ChordMode::Triads, ChordVoicing::Open);
    check (open.count == 3 && open.notes[0] == 48 && open.notes[1] == 55 && open.notes[2] == 64,
           "the open voicing spreads C E G as C G E");
    check (chordName (open) == "C", "the open voicing keeps the chord name");

    KeySettings am = k;
    am.root  = 9;
    am.scale = ScaleType::NaturalMinor;

    auto amin = chordForNote (57, am, ChordMode::Triads, ChordVoicing::Close);
    check (chordName (amin) == "Am" && amin.notes[1] == 60 && amin.notes[2] == 64,
           "A minor maps to Am");

    check (scaleDegreeCount (ScaleType::Major) == 7 && scaleDegreeCount (ScaleType::MinorPentatonic) == 5,
           "degree counts follow the scale");

    auto degreeChord = chordForDegree (4, 48, ScaleType::Major, ChordMode::Triads, ChordVoicing::Close);
    check (degreeChord.notes[0] == 55 && chordName (degreeChord) == "G",
           "the fifth degree of C major is G");

    // A scale the chord table does not know is spelled out instead of mislabelled.
    auto exotic = chordForDegree (0, 48, ScaleType::MajorPentatonic, ChordMode::Triads, ChordVoicing::Close);
    check (chordName (exotic) == "C(E A)", "an unusual stack is spelled out note by note");

    // --- the engine actually plays the chord --------------------------------
    std::vector<float> audio ((size_t) (3.4 * sr), 0.0f);
    addTone (audio, 0.53, 0.40, 130.81, 0.35, 30.0, true);
    addTone (audio, 1.03, 0.40, 164.81, 0.35, 30.0, true);
    addTone (audio, 1.53, 0.40, 196.00, 0.35, 30.0, true);

    Settings s;
    s.grid = SnapGrid::Sixteenth;
    s.keyLock = true;
    s.keyRoot = 0;
    s.scale = ScaleType::Major;
    s.chordsEnabled = true;

    const auto events = runEngine (audio, s);
    const auto notes  = noteOns (events, 1);

    std::printf ("  emitted %d voice note(s) with chords on\n", (int) notes.size());

    check (notes.size() == 9, "three sung notes become three chords");

    if (notes.size() == 9)
    {
        check (notes[0].data1 == 48 && notes[1].data1 == 52 && notes[2].data1 == 55, "first chord is C E G");
        checkNear (beatsOf (notes[0].abs), 1.00, 0.02, "the whole chord lands on beat 1");
        check (notes[3].data1 == 52 && notes[4].data1 == 55 && notes[5].data1 == 59, "second chord is E G B");
        check (notes[6].data1 == 55 && notes[7].data1 == 59 && notes[8].data1 == 62, "third chord is G B D");
    }

    int offs = 0;

    for (const auto& e : events)
        if (e.type() == 0x80 && e.channel() == 1)
            ++offs;

    check (offs == 9, "every chord voice is released");

    Settings plain = s;
    plain.chordsEnabled = false;
    check (noteOns (runEngine (audio, plain), 1).size() == 3, "chords off keeps one note per sung note");

    // A single held note keeps its chord on until the singer stops.
    std::vector<float> held ((size_t) (2.0 * sr), 0.0f);
    addTone (held, 0.40, 1.10, 220.0, 0.30, 0.0, true);

    Settings hs;
    hs.grid = SnapGrid::Off;
    hs.keyLock = false;
    hs.chordsEnabled = true;

    const auto heldEvents = runEngine (held, hs);
    const auto heldOns = noteOns (heldEvents, 1);

    std::printf ("  emitted %d voice note(s) for a held note\n", (int) heldOns.size());

    check (heldOns.size() == 3, "a held A expands into the vi chord");

    for (const auto& n : heldOns)
        std::printf ("    held chord note %3d at beat %7.3f\n", n.data1, beatsOf (n.abs));

    if (heldOns.size() == 3)
    {
        check (heldOns[0].data1 == 57 && heldOns[1].data1 == 60 && heldOns[2].data1 == 64,
               "the held chord is Am (A C E)");
    }
}

//==============================================================================
/** A tone that holds f0, glides (log-linearly) to f1, then holds f1. */
void addGlide (std::vector<float>& buf, double t0, double dur, double f0, double f1,
               double glideStart, double glideEnd, float amp)
{
    const int start = (int) (t0 * sr);
    const int len   = (int) (dur * sr);
    double phase = 0.0;

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;

        if (idx < 0 || idx >= (int) buf.size())
            continue;

        const double t = (double) i / sr;
        const double g = std::clamp ((t - glideStart) / (glideEnd - glideStart), 0.0, 1.0);
        const double f = f0 * std::pow (f1 / f0, g);
        phase += 2.0 * 3.14159265358979 * f / sr;

        const double env = std::min (1.0, t / 0.012) * std::min (1.0, (dur - t) / 0.02);
        const double v = std::sin (phase) + 0.25 * std::sin (2.0 * phase) + 0.10 * std::sin (3.0 * phase);
        buf[(size_t) idx] += (float) (v * amp * std::max (0.0, env));
    }
}

std::vector<int> bendValues (const std::vector<Event>& events, int channel)
{
    std::vector<int> values;

    for (const auto& e : events)
        if (e.type() == 0xE0 && e.channel() == channel)
            values.push_back (e.data1 | (e.data2 << 7));

    return values;
}

/** Features from the Dubler 2 how-to guides: triggers, pitch, control dials,
    key restriction and chords. */
void testDublerFeatures()
{
    std::printf ("\n[dubler guides: key restriction]\n");

    KeySettings k;
    k.root = 0;
    k.scale = ScaleType::Major;
    k.snap = 1.0f;

    KeySettings noE = k;
    noE.mask = (uint16_t) (allNotesMask & ~(1u << 4));
    check (Quantizer::snapPitch (52.0f, noE) == 53, "a switched-off E snaps to the nearest in-key note (F)");
    check (Quantizer::snapPitch (50.2f, noE) == 50, "notes that are still on are unchanged");

    KeySettings custom = k;
    custom.scale = ScaleType::Chromatic;
    custom.mask = (uint16_t) ((1u << 0) | (1u << 4) | (1u << 7));
    check (Quantizer::snapPitch (61.2f, custom) == 60, "a hand-picked C E G set snaps C# to C");
    check (Quantizer::snapPitch (66.0f, custom) == 67, "... and F# to G");

    KeySettings none = k;
    none.mask = 0;
    check (Quantizer::snapPitch (61.4f, none) == 60 || Quantizer::snapPitch (61.4f, none) == 62,
           "switching every note off falls back to the scale");

    {
        std::vector<float> audio ((size_t) (3.4 * sr), 0.0f);
        addTone (audio, 0.53, 0.40, 130.81, 0.35, 30.0, true);
        addTone (audio, 1.03, 0.40, 164.81, 0.35, 30.0, true);
        addTone (audio, 1.53, 0.40, 196.00, 0.35, 30.0, true);

        Settings s;
        s.noteMask = noE.mask;
        const auto notes = noteOns (runEngine (audio, s), 1);
        check (notes.size() == 3 && notes[1].data1 == 53, "sung E comes out as F with E switched off");

        std::printf ("\n[dubler guides: pitch on/off]\n");
        Settings off;
        off.pitchEnabled = false;
        check (noteOns (runEngine (audio, off), 1).empty(), "pitch switched off sends no melody notes");

        std::printf ("\n[dubler guides: chord channel]\n");
        Settings ch;
        ch.chordsEnabled = true;
        ch.chordChannel = 2;
        const auto events = runEngine (audio, ch);
        const auto melody = noteOns (events, 1);
        const auto chords = noteOns (events, 2);
        check (melody.size() == 3, "with a chord channel the melody stays on the voice channel");
        check (chords.size() == 9, "... and the chords go out on their own channel");

        if (melody.size() == 3)
            check (melody[0].data1 == 48 && melody[1].data1 == 52 && melody[2].data1 == 55,
                   "the melody notes are the sung notes");

        int chordOffs = 0;

        for (const auto& e : events)
            if (e.type() == 0x80 && e.channel() == 2)
                ++chordOffs;

        check (chordOffs == 9, "every chord-channel note is released");
    }

    std::printf ("\n[dubler guides: chord options]\n");

    ChordOptions fixed;
    fixed.follow = false;
    auto low = chordForNote (36, k, fixed);
    check (low.count == 3 && low.notes[0] == 60 && low.notes[1] == 64 && low.notes[2] == 67,
           "follow off: a low C still plays C major at C4");

    fixed.octave = -1;
    auto down = chordForNote (72, k, fixed);
    check (down.count == 3 && down.notes[0] == 48, "follow off with a shift of -1 plays at C3");

    ChordOptions custom7;
    custom7.overrides[1] = ChordQuality::Major;   // II instead of ii
    auto dMajor = chordForNote (50, k, custom7);
    check (dMajor.count == 3 && dMajor.notes[1] == 54 && chordName (dMajor) == "D",
           "a custom chord on degree ii plays D major");
    auto cStill = chordForNote (48, k, custom7);
    check (chordName (cStill) == "C", "other degrees keep the preset chord");

    ChordOptions sus2;
    sus2.overrides[0] = ChordQuality::Sus2;
    auto cSus2 = chordForDegree (0, 48, ScaleType::Major, sus2);
    check (cSus2.count == 3 && cSus2.notes[1] == 50 && cSus2.notes[2] == 55, "the Chords page shows the custom chord");

    ChordOptions seventh;
    seventh.overrides[4] = ChordQuality::Dominant7;
    seventh.rootBass = true;
    auto g7 = chordForNote (55, k, seventh);
    check (g7.count == 5 && g7.notes[0] == 43 && chordName (g7) == "G7",
           "a seventh chord plus the root bassline uses all five voices");

    std::printf ("\n[dubler guides: control dials]\n");
    check (ccDialValue (0.5f, 0.0f, 1.0f, 0, 127) == 64, "full range: half way is CC 64");
    check (ccDialValue (0.25f, 0.0f, 0.5f, 0, 127) == 64, "a narrower input range stretches over the dial");
    check (ccDialValue (0.9f, 0.0f, 0.5f, 0, 127) == 127, "readings above the input max clamp to the top");
    check (ccDialValue (0.5f, 0.0f, 1.0f, 20, 40) == 30, "the output range caps the CC span");
    check (ccDialValue (0.0f, 0.0f, 1.0f, 127, 0) == 127, "a reversed output range inverts the dial");

    std::printf ("\n[dubler guides: trigger velocity + takes]\n");
    {
        std::vector<float> audio ((size_t) (2.6 * sr), 0.0f);
        addKick  (audio, 0.50);
        addSnare (audio, 1.00);
        addHat   (audio, 1.50);

        Settings s;
        const auto dynamic = noteOns (runEngine (audio, s), 10);

        Settings flat = s;

        for (auto& v : flat.padVelocity)
            v = false;

        const auto full = noteOns (runEngine (audio, flat), 10);

        bool allFull = ! full.empty(), anyBelow = false;

        for (const auto& n : full)
            allFull = allFull && n.data2 == 127;

        for (const auto& n : dynamic)
            anyBelow = anyBelow || n.data2 < 127;

        check (full.size() == dynamic.size() && allFull, "velocity off: every trigger hit is full velocity");
        check (anyBelow, "velocity on: hits follow how loud they were");
    }

    {
        DrumClassifier c;
        DrumFeatures f;
        f.low = 0.8f;

        int added = 0;

        for (int i = 0; i < 20; ++i)
            added += c.addExample (3, f) ? 1 : 0;

        check (added == DrumClassifier::maxTakes && c.numExamples (3) == 12, "a trigger keeps up to 12 takes");

        DrumFeatures a, b;
        a.low = 0.9f; a.f1 = 0.1f;
        b.high = 0.9f; b.f1 = 0.1f;
        DrumClassifier two;
        two.addExample (5, a);
        two.addExample (5, b);
        check (two.classify (a) == 5 && two.classify (b) == 5,
               "each take is matched on its own, not just their average");
    }

    std::printf ("\n[dubler guides: pitch bend]\n");
    {
        // Hold C3, slide up two semitones to D3, hold.
        std::vector<float> audio ((size_t) (2.6 * sr), 0.0f);
        addGlide (audio, 0.50, 1.60, 130.81, 146.83, 0.50, 0.90, 0.35);

        Settings s;
        s.grid = SnapGrid::Off;
        s.keyLock = false;

        const auto plain = noteOns (runEngine (audio, s), 1);
        check (plain.size() == 2, "without bend, a slide from C to D is two notes");

        Settings b = s;
        b.bendEnabled = true;
        b.bendRange = 12;
        b.bendMode = BendMode::TruBend;

        const auto events = runEngine (audio, b);
        const auto notes  = noteOns (events, 1);
        const auto bends  = bendValues (events, 1);

        int peak = 8192;

        for (int v : bends)
            peak = std::max (peak, v);

        std::printf ("  %d note(s), %d bend message(s), peak %d\n", (int) notes.size(), (int) bends.size(), peak);

        check (notes.size() == 1 && notes[0].data1 == 48, "with bend (range 12), the slide bends one C3");
        checkNear ((double) (peak - 8192) / 8191.0 * 12.0, 2.0, 0.3, "the bend reaches two semitones");
        check (! bends.empty() && bends.back() == 8192, "the bend is re-centred when the note ends");

        Settings narrow = b;
        narrow.bendRange = 1;
        check (noteOns (runEngine (audio, narrow), 1).size() == 2,
               "with a range of 1, going past the range changes note");
    }

    {
        // A steady note 20 cents sharp.
        std::vector<float> audio ((size_t) (2.0 * sr), 0.0f);
        addTone (audio, 0.40, 1.10, 220.0, 0.30, 20.0, false);

        Settings s;
        s.grid = SnapGrid::Off;
        s.keyLock = false;
        s.bendEnabled = true;
        s.bendRange = 2;

        s.bendMode = BendMode::IntelliBend;
        const auto intelli = bendValues (runEngine (audio, s), 1);

        s.bendMode = BendMode::TruBend;
        const auto tru = bendValues (runEngine (audio, s), 1);

        int intelliMax = 0, truMax = 0;

        for (int v : intelli) intelliMax = std::max (intelliMax, std::abs (v - 8192));
        for (int v : tru)     truMax     = std::max (truMax, std::abs (v - 8192));

        std::printf ("  held sharp note: IntelliBend max %d, TruBend max %d\n", intelliMax, truMax);

        check (intelliMax == 0, "IntelliBend keeps a steady note anchored (no bend)");
        checkNear ((double) truMax / 8191.0 * 2.0, 0.20, 0.06, "TruBend follows the 20 cents");
    }
}

//==============================================================================
// A short pitched "boop" that rings on a while: the rules call it a sung note,
// but a user may well want to train it as a trigger.
void addBoop (std::vector<float>& buf, double t0)
{
    const int start = (int) (t0 * sr);
    const int len   = (int) (0.30 * sr);

    for (int i = 0; i < len; ++i)
    {
        const int idx = start + i;
        if (idx >= (int) buf.size()) break;

        const double t = (double) i / sr;
        const double env = std::min (1.0, t / 0.003) * std::exp (-t / 0.20);
        buf[(size_t) idx] += (float) (0.5 * env * std::sin (2.0 * 3.14159265358979 * 330.0 * t));
    }
}

void testTrainingUnrecognisedSound()
{
    std::printf ("\n[training a sound the rules reject]\n");

    VoiceEngine engine;
    engine.prepare (sr, 512);

    Settings s;
    s.grid = SnapGrid::Sixteenth;

    std::vector<float> boops ((size_t) (2.6 * sr), 0.0f);
    addBoop (boops, 0.2);
    addBoop (boops, 0.8);
    addBoop (boops, 1.4);
    addBoop (boops, 2.0);

    engine.startTraining (6, 4);
    const auto events = runEngineWith (engine, boops, s);

    check (engine.getClassifier().hasTemplate (6), "every take counts while training, even an unrecognised sound");
    check (! engine.isTraining(), "training finishes after four takes");

    int padHits = 0;

    for (const auto& e : noteOns (events, 10))
        if (e.data1 == s.padNote[6])
            ++padHits;

    check (padHits >= 3, "takes are heard on the pad being trained");
}

//==============================================================================
void testKeyDetector()
{
    std::printf ("\n[key detection]\n");

    // A melody in C major, tonic and fifth leaning.
    std::array<float, 12> cMajor {};
    const int cNotes[] { 0, 2, 4, 5, 7, 9, 11 };
    const float cWeights[] { 6, 2, 4, 2, 5, 2, 1 };

    for (int i = 0; i < 7; ++i)
        cMajor[(size_t) cNotes[i]] = cWeights[i];

    auto k = KeyDetector::suggest (cMajor);
    check (! k.empty() && k[0].root == 0 && k[0].scale == ScaleType::Major, "a C-major melody suggests C major");
    check (! k.empty() && k[0].match > 0.99f, "and every note is inside it");

    // A minor: A C E heavy.
    std::array<float, 12> aMinor {};
    aMinor[9] = 6; aMinor[0] = 4; aMinor[4] = 5; aMinor[2] = 2; aMinor[11] = 1.5f; aMinor[5] = 1.5f; aMinor[7] = 2;
    k = KeyDetector::suggest (aMinor);
    check (! k.empty() && k[0].root == 9 && k[0].scale == ScaleType::NaturalMinor, "an A-minor melody suggests A minor");

    // Clicked notes (all equal weight) from E major.
    std::array<float, 12> eMajor {};
    for (int pc : { 4, 6, 8, 9, 11, 1, 3 })
        eMajor[(size_t) pc] = 1.0f;
    k = KeyDetector::suggest (eMajor, 5);
    bool found = false;
    for (const auto& sug : k)
        found = found || (sug.root == 4 && sug.scale == ScaleType::Major)
                      || (sug.root == 1 && sug.scale == ScaleType::NaturalMinor);
    check (found, "the seven notes of E major suggest E major or its relative C# minor");
    check ((int) k.size() == 5, "five suggestions are returned");

    check (KeyDetector::suggest ({}).empty(), "silence suggests nothing");

    // --- the engine feeds it from the microphone -----------------------------
    VoiceEngine engine;
    engine.prepare (sr, 512);
    engine.setKeyListening (true);

    std::vector<float> audio ((size_t) (3.2 * sr), 0.0f);
    addTone (audio, 0.20, 0.55, 196.00, 0.3);   // G3
    addTone (audio, 0.85, 0.40, 246.94, 0.3);   // B3
    addTone (audio, 1.35, 0.40, 293.66, 0.3);   // D4
    addTone (audio, 1.85, 0.40, 261.63, 0.3);   // C4
    addTone (audio, 2.35, 0.70, 196.00, 0.3);   // G3

    Settings s;
    s.keyRoot = 0;               // the current key is C major ...
    s.scale = ScaleType::Major;
    runEngineWith (engine, audio, s);

    const auto hist = engine.getKeyHistogram();
    check (hist[7] > hist[11] && hist[7] > hist[2], "the most-sung note (G) has the most weight");
    check (hist[1] + hist[3] + hist[6] + hist[8] + hist[10] < 0.05f * (hist[7] + hist[11] + hist[2]),
           "notes that were not sung stay near zero");

    k = KeyDetector::suggest (hist);
    check (! k.empty() && k[0].root == 7 && k[0].scale == ScaleType::Major,
           "... but singing G B D C G suggests G major");

    engine.clearKeyHistogram();
    engine.setKeyListening (false);
    runEngineWith (engine, audio, s);
    float total = 0.0f;
    for (auto v : engine.getKeyHistogram()) total += v;
    check (total == 0.0f, "clearing works and nothing is collected while not listening");
}

//==============================================================================
// The instrument check: Humline plays test notes into a fake instrument and
// listens to what comes back, to tell a synth from a drum kit from nothing.
struct FakeInstrument
{
    enum Kind { Silent, Saw, SlowPad, Pluck, GmKit, KickSampler };

    explicit FakeInstrument (Kind k) : kind (k) {}

    void midi (const MidiEvent& e)
    {
        if ((e.status & 0x0F) != 0)   // answers channel 1 only, like a synth on the voice channel
            return;

        if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
            voices.push_back ({ e.data1, 0.0, true, 0.0 });
        else
            for (auto& v : voices)
                if (v.note == e.data1 && v.held)
                    v.held = false;
    }

    float next()
    {
        constexpr double twoPi = 6.283185307179586;
        float out = 0.0f;

        for (auto& v : voices)
        {
            const double f = 440.0 * std::pow (2.0, (v.note - 69) / 12.0);
            const double t = v.t;
            const double saw = 2.0 * std::fmod (t * f, 1.0) - 1.0;
            const float release = v.held ? 1.0f : (float) std::exp (-v.rel / (kind == SlowPad ? 0.3 : 0.03));
            float s = 0.0f;

            switch (kind)
            {
                case Saw:     s = (float) saw * 0.3f * release; break;
                case SlowPad: s = (float) saw * 0.2f * (float) std::min (1.0, t / 0.3) * release; break;
                case Pluck:   s = (float) saw * 0.3f * (float) std::exp (-t / 0.25); break;
                case KickSampler:
                    s = (float) std::sin (twoPi * 55.0 * std::pow (2.0, (v.note - 60) / 12.0) * t)
                        * 0.6f * (float) std::exp (-t / 0.07);
                    break;
                case GmKit:
                    if (v.note == 48)        // tom
                        s = (float) std::sin (twoPi * (150.0 + 80.0 * std::exp (-t / 0.02)) * t) * 0.5f
                            * (float) std::exp (-t / 0.15);
                    else if (v.note == 42)   // closed hat: bright noise
                    {
                        const float n = rng.next();
                        s = (n - lastNoise) * 0.3f * (float) std::exp (-t / 0.04);
                        lastNoise = n;
                    }
                    break;                   // nothing on 60
                case Silent: break;
            }

            out += s;
            v.t += 1.0 / sr;

            if (! v.held)
                v.rel += 1.0 / sr;
        }

        return out;
    }

    struct Voice { int note; double t; bool held; double rel; };

    Kind kind;
    std::vector<Voice> voices;
    Rng rng;
    float lastNoise = 0.0f;
};

InstrumentProbe::Result runProbe (FakeInstrument::Kind kind, bool onMainInput, float micNoise)
{
    InstrumentProbe probe;
    probe.prepare (sr);
    probe.start (1, 10);

    FakeInstrument inst (kind);
    std::vector<MidiEvent> events;
    std::vector<float> side (512), mainIn (512), delay (300, 0.0f);
    size_t d = 0;
    Rng noise;

    for (int guard = 0; ! probe.hasRecording() && guard < 1000; ++guard)
    {
        // The host plays last block's MIDI into the instrument, which answers a
        // block (plus a little plugin delay) later.
        for (int i = 0; i < 512; ++i)
        {
            for (const auto& e : events)
                if (e.offset == i)
                    inst.midi (e);

            delay[d] = inst.next();
            d = (d + 1) % delay.size();
            const float back = delay[d];

            side[(size_t) i]   = onMainInput ? 0.0f : back;
            mainIn[(size_t) i] = (onMainInput ? back : 0.0f) + noise.next() * micNoise;
        }

        events.clear();
        probe.process (side.data(), mainIn.data(), 512, events);
    }

    return probe.analyse();
}

void testInstrumentProbe()
{
    std::printf ("\n[instrument check: synth, drum kit or nothing]\n");

    // The test notes themselves.
    {
        InstrumentProbe probe;
        probe.prepare (sr);
        probe.start (1, 10);

        std::vector<MidiEvent> all, block;
        std::vector<float> silence (512, 0.0f);

        for (int guard = 0; ! probe.hasRecording() && guard < 1000; ++guard)
        {
            block.clear();
            probe.process (silence.data(), silence.data(), 512, block);
            all.insert (all.end(), block.begin(), block.end());
        }

        int ons = 0, offs = 0;
        bool bothChannels = false;

        for (const auto& e : all)
        {
            if ((e.status & 0xF0) == 0x90) ++ons;
            if ((e.status & 0xF0) == 0x80) ++offs;
            if (e.status == 0x99) bothChannels = true;
        }

        check (ons == 6 && offs == 6, "a check plays 3 notes on the voice and drum channels, and lets them all go");
        check (bothChannels, "the drum channel (10) gets the test notes too");
        check (probe.analyse().target == InstrumentTarget::None, "silence means nothing is attached");
        check (! probe.isRunning() && ! probe.hasRecording(), "the probe is free again after analysing");
    }

    struct Case { FakeInstrument::Kind kind; InstrumentTarget want; const char* name; };
    const Case cases[] {
        { FakeInstrument::Silent,      InstrumentTarget::None,       "no instrument" },
        { FakeInstrument::Saw,         InstrumentTarget::Instrument, "a saw synth" },
        { FakeInstrument::SlowPad,     InstrumentTarget::Instrument, "a slow pad with a long release" },
        { FakeInstrument::Pluck,       InstrumentTarget::Instrument, "a pluck" },
        { FakeInstrument::GmKit,       InstrumentTarget::DrumKit,    "a GM drum kit" },
        { FakeInstrument::KickSampler, InstrumentTarget::DrumKit,    "a kick sample played chromatically" },
    };

    for (const auto& c : cases)
    {
        const auto side = runProbe (c.kind, false, 0.0f);
        check (side.target == c.want && ! side.heardOnMainInput,
               std::string (c.name) + " on the Instrument input -> " + instrumentTargetName (side.target)
                   + " (" + side.reason + ")");

        const auto main = runProbe (c.kind, true, 0.003f);
        check (main.target == c.want && main.heardOnMainInput == (c.want != InstrumentTarget::None),
               std::string (c.name) + " on the main input, over mic noise -> " + instrumentTargetName (main.target));
    }

    // Cancelling mid-way lets the sounding note go.
    {
        InstrumentProbe probe;
        probe.prepare (sr);
        probe.start (1, 1);

        std::vector<MidiEvent> block;
        std::vector<float> silence (512, 0.0f);
        int held = 0;

        for (int i = 0; i < 20; ++i)   // ~0.2 s: the first note is down
        {
            block.clear();
            probe.process (silence.data(), silence.data(), 512, block);
            for (const auto& e : block) held += (e.status & 0xF0) == 0x90 ? 1 : -1;
        }

        probe.cancel();
        block.clear();
        probe.process (silence.data(), silence.data(), 512, block);
        for (const auto& e : block) held += (e.status & 0xF0) == 0x90 ? 1 : -1;

        check (held == 0 && ! probe.isRunning(), "cancelling a check releases its note");
    }

    // The target picks the engine mode.
    check (engineModeFor (InstrumentTarget::DrumKit, false) == EngineMode::Beatbox, "a drum kit plays live triggers");
    check (engineModeFor (InstrumentTarget::Instrument, false) == EngineMode::Melody, "an instrument plays live notes");
    check (engineModeFor (InstrumentTarget::Instrument, true) == EngineMode::Both, "Quantised snaps to the grid");
}

//==============================================================================
// How long after the sound starts does its MIDI go out? Measured on the raw
// output stream (the live modes report no output latency, so nothing is
// subtracted there), with a hop-sized block like a low-latency audio driver.
double msAfter (const Event& e, double startSeconds)
{
    return ((double) e.abs / sr - startSeconds) * 1000.0;
}

void testLiveModes()
{
    std::printf ("\n[live modes: beatbox and melody without the delay]\n");

    // --- Beatbox --------------------------------------------------------------
    {
        std::vector<float> audio ((size_t) (2.4 * sr), 0.0f);
        addKick  (audio, 0.50);
        addSnare (audio, 1.00);
        addHat   (audio, 1.50);

        Settings s;
        s.mode = EngineMode::Beatbox;

        VoiceEngine engine;
        engine.prepare (sr, 128);
        const auto events = runEngineWith (engine, audio, s, 120.0, 128);
        const auto drums  = noteOns (events, 10);

        check (engine.getOutputLatencySamples() == 0, "Beatbox mode has no output delay");
        check (drums.size() == 3, "kick, snare and hat give three drum notes");

        // Singing in Beatbox mode never produces melody notes (a hummed attack
        // may still count as a hit, as a beatboxed bass hum would).
        std::vector<float> hum ((size_t) (1.4 * sr), 0.0f);
        addTone (hum, 0.30, 0.80, 220.0, 0.3);
        VoiceEngine humEngine;
        humEngine.prepare (sr, 128);
        check (noteOns (runEngineWith (humEngine, hum, s, 120.0, 128), 1).empty(),
               "a sung note makes no melody note in Beatbox mode");

        const double starts[] { 0.50, 1.00, 1.50 };

        if (drums.size() == 3)
        {
            for (int i = 0; i < 3; ++i)
            {
                const double ms = msAfter (drums[(size_t) i], starts[i]);
                std::printf ("    drum %d (note %d) out %.1f ms after the hit\n", i, drums[(size_t) i].data1, ms);
                check (ms >= 0.0 && ms < 35.0, "drum hit " + std::to_string (i + 1) + " goes out within 35 ms");
            }

            check (drums[0].data1 == 35 || drums[0].data1 == 36, "live: the kick is a bass drum");
            check (drums[1].data1 == 38 || drums[1].data1 == 39, "live: the snare is a snare/clap");
            check (drums[2].data1 == 42 || drums[2].data1 == 46, "live: the hat is a hat");
        }

        // Same audio in quantised mode, for comparison.
        Settings both;
        both.mode = EngineMode::Both;
        VoiceEngine slow;
        slow.prepare (sr, 128);
        runEngineWith (slow, audio, both, 120.0, 128);
        std::printf ("    (Both mode delays everything by %.0f ms)\n",
                     1000.0 * slow.getOutputLatencySamples() / sr);
        check (slow.getOutputLatencySamples() > (int) (0.2 * sr), "Both mode still quantises with a delay");
    }

    // --- Melody ---------------------------------------------------------------
    {
        std::vector<float> audio ((size_t) (2.6 * sr), 0.0f);
        addTone (audio, 0.50, 0.35, 130.81, 0.35, 30.0, true);   // C3, a bit sharp
        addTone (audio, 1.00, 0.35, 164.81, 0.35);               // E3
        addTone (audio, 1.50, 0.35, 196.00, 0.35);               // G3
        addKick (audio, 2.10);                                   // ignored in Melody mode

        Settings s;
        s.mode = EngineMode::Melody;
        s.keyLock = true;
        s.keyRoot = 0;
        s.scale = ScaleType::Major;

        VoiceEngine engine;
        engine.prepare (sr, 128);
        const auto allEvents = runEngineWith (engine, audio, s, 120.0, 128);
        const auto drums  = noteOns (allEvents, 10);

        // The sung part only: in Melody mode the pitched kick at 2.1 s is just
        // another (very low) note, which is what this mode is meant to do.
        std::vector<Event> events;
        for (const auto& e : allEvents)
            if (e.abs < (int64_t) (2.05 * sr))
                events.push_back (e);

        const auto notes = noteOns (events, 1);

        check (engine.getOutputLatencySamples() == 0, "Melody mode has no output delay");
        check (notes.size() == 3, "three sung notes give three MIDI notes");
        check (drums.empty(), "a beatboxed kick makes no drum note in Melody mode");

        const double starts[] { 0.50, 1.00, 1.50 };
        const int want[] { 48, 52, 55 };

        if (notes.size() == 3)
        {
            for (int i = 0; i < 3; ++i)
            {
                const double ms = msAfter (notes[(size_t) i], starts[i]);
                std::printf ("    note %d (%d) out %.1f ms after it was sung\n", i, notes[(size_t) i].data1, ms);
                check (ms >= 0.0 && ms < 60.0, "sung note " + std::to_string (i + 1) + " goes out within 60 ms");
                check (notes[(size_t) i].data1 == want[i], "and is snapped into the key");
            }
        }

        // Each note stops close to when the singing stops, not on the grid.
        int offs = 0;
        bool offsPrompt = true;

        for (const auto& e : events)
        {
            if (e.type() == 0x80 && e.channel() == 1)
            {
                const double endSec = starts[std::min (offs, 2)] + 0.35;
                const double ms = msAfter (e, endSec);
                offsPrompt = offsPrompt && ms > -40.0 && ms < 120.0;
                ++offs;
            }
        }

        check (offs == 3, "every live note is released");
        check (offsPrompt, "and released near the end of the sung note");
    }
}

//==============================================================================
// Mirrors VoiceEngine::pushHistory, for feeding the recorder by hand.
void pushNote (LiveState& live, float start, float len, int note, bool drum)
{
    live.history[live.historyWrite] = { start, len, note, drum };
    live.historyWrite = (live.historyWrite + 1) % LiveState::historySize;
    live.historyCount = std::min (live.historyCount + 1, LiveState::historySize);
    ++live.historySeq;
}

void testTimelineJump()
{
    std::printf ("\n[timeline jump]\n");

    Settings s;
    s.grid = SnapGrid::Sixteenth;
    s.timingStrength = 1.0f;
    s.keyLock = true;
    s.keyRoot = 0;
    s.scale = ScaleType::Major;
    s.pitchSnap = 1.0f;
    s.drumsEnabled = false;

    std::vector<float> audio ((size_t) (2.4 * sr), 0.0f);
    addTone (audio, 0.02, 0.98, 220.00, 0.35);   // A3, held across the jump at 0.85 s
    addTone (audio, 1.20, 0.70, 261.63, 0.35);   // C4, sung after it

    const int jumpPos = ((int) (0.85 * sr) / 512) * 512;

    VoiceEngine engine;
    engine.prepare (sr, 512);

    std::vector<MidiEvent> block;
    std::vector<Event> events;

    const auto run = [&] (int from, int to, double ppqAtFrom)
    {
        for (int pos = from; pos < to; pos += 512)
        {
            const int n = std::min (512, (int) audio.size() - pos);
            const double ppq = ppqAtFrom + (double) (pos - from) / sr * 2.0;

            engine.process (audio.data() + pos, n, ppq, 120.0, s, block);

            for (const auto& m : block)
                events.push_back ({ (int64_t) pos + m.offset, m.status, m.data1, m.data2 });
        }
    };

    run (0, jumpPos, 0.0);

    check (! noteOns (events, 1).empty(), "a note is sounding before the host jumps");

    // The host loops: the playhead goes back to bar 5 while that note is held.
    const double loopPpq = 16.0;
    const size_t firstAfterJump = events.size();
    run (jumpPos, (int) audio.size(), loopPpq);

    int offsAtJump = 0, allNotesOff = 0;

    for (size_t i = firstAfterJump; i < events.size() && events[i].abs < jumpPos + 512; ++i)
    {
        const auto& e = events[i];

        if (e.type() == 0x80 && e.channel() == 1)                   ++offsAtJump;
        if (e.type() == 0xB0 && e.data1 == 123 && e.channel() == 1) ++allNotesOff;
    }

    check (offsAtJump > 0, "the note held across the jump is released there");
    check (allNotesOff > 0, "the jump sends an all-notes-off on the voice channel");

    // Nothing is placed back on the old timeline, and the engine keeps working on
    // the new one.
    const int64_t latency = engine.getOutputLatencySamples();
    const auto newBeats = [&] (int64_t abs)
    {
        return loopPpq + (double) (abs - latency - jumpPos) / sr * 2.0;
    };

    bool staleNote = false, laterNote = false;

    for (size_t i = firstAfterJump; i < events.size(); ++i)
    {
        const auto& e = events[i];

        if (e.type() != 0x90 || e.data2 == 0 || e.channel() != 1)
            continue;

        const double b = newBeats (e.abs);
        staleNote = staleNote || b < loopPpq - 0.01;
        laterNote = laterNote || (b > loopPpq + 0.2 && b < loopPpq + 2.0);
    }

    check (! staleNote, "no note is placed back on the old timeline");
    check (laterNote, "a note sung after the jump lands on the new timeline");

    // Nothing is left hanging across the jump.
    bool allReleased = true;

    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];

        if (e.type() != 0x90 || e.data2 == 0)
            continue;

        const bool released = std::any_of (events.begin() + (long) i, events.end(),
                                           [&] (const Event& o)
                                           {
                                               return o.type() == 0x80 && o.data1 == e.data1
                                                   && o.channel() == e.channel() && o.abs >= e.abs;
                                           });

        allReleased = allReleased && released;
    }

    check (allReleased, "every note that went out is released, the jump included");
}

void testTake()
{
    std::printf ("\n[take: capture + export layout]\n");

    // Capture from the history ring.
    {
        LiveState live;
        TakeRecorder rec;
        std::vector<HistoryNote> take;

        check (rec.capture (live, take) == 0 && take.empty(), "nothing captured from an empty ring");

        pushNote (live, 1.0f, 0.5f, 60, false);
        pushNote (live, 2.0f, 0.5f, 62, false);
        check (rec.capture (live, take) == 2 && take.size() == 2, "two new notes are captured");
        check (take.size() == 2 && take[0].note == 60 && take[1].note == 62, "in the order they finished");
        check (rec.capture (live, take) == 0 && take.size() == 2, "a second call adds nothing new");

        // Past the end of the ring: still every note, in order.
        for (int i = 0; i < 40; ++i)
        {
            pushNote (live, 3.0f + (float) i, 0.25f, 36 + (i % 3), true);

            if (i % 7 == 6)
                rec.capture (live, take);
        }

        rec.capture (live, take);
        bool ordered = take.size() == 42;

        for (size_t i = 2; ordered && i < take.size(); ++i)
            ordered = take[i].drum && std::abs (take[i].startBeats - (3.0f + (float) (i - 2))) < 1.0e-6f;

        check (ordered, "40 more notes across ring wrap-around arrive complete and in order");

        // More than a ring's worth between two calls: only the 32 still held come back.
        std::vector<HistoryNote> late;
        TakeRecorder lateRec;
        LiveState busy;

        for (int i = 0; i < 50; ++i)
            pushNote (busy, (float) i, 0.25f, 60, false);

        check (lateRec.capture (busy, late) == LiveState::historySize && late.front().startBeats == 18.0f,
               "an overflowed ring yields the 32 newest notes");

        // The engine was reset (counter back to zero): the new notes are still picked up.
        live = LiveState();
        pushNote (live, 100.0f, 1.0f, 64, false);
        const auto before = take.size();
        rec.capture (live, take);
        check (take.size() == before + 1 && take.back().note == 64, "notes after an engine reset are captured");
    }

    // Export layout.
    {
        std::vector<HistoryNote> take {
            { 9.5f, 0.5f, 60, false },     // bar 3, beat 2.5 (origin should be beat 8)
            { 8.0f, 1.0f, 36, true },
            { 10.0f, 2.0f, 62, false },
            { 11.0f, 1.0f, 62, false },    // overlaps the previous 62: that one gets cut
            { 8.0f, 0.25f, 36, true },     // duplicate start of the kick: the longer one stays
        };

        const auto all = layoutTakeForExport (take, TakeFilter::All, 960);

        checkNear (takeOriginBeats (take, TakeFilter::All), 8.0, 1.0e-9, "export starts at the bar of the first note");
        check (all.size() == 4, "duplicate kick is dropped");

        if (all.size() == 4)
        {
            check (all[0].drum && all[0].startTick == 0 && all[0].lengthTick == 960, "kick at tick 0, longer copy kept");
            check (! all[1].drum && all[1].startTick == 1440 && all[1].note == 60, "C at 1.5 beats into the bar");
            check (all[2].startTick == 1920 && all[2].lengthTick == 960, "overlapping repeat of D is shortened");
            check (all[3].startTick == 2880 && all[3].lengthTick == 960, "and the second D keeps its length");
        }

        const auto voice = layoutTakeForExport (take, TakeFilter::Voice, 960);
        const auto drums = layoutTakeForExport (take, TakeFilter::Drums, 960);

        check (voice.size() == 3 && voice.front().startTick == 1440, "voice filter keeps the bar of the take");
        check (drums.size() == 1 && drums.front().drum, "drum filter keeps only the drums");
        check (layoutTakeForExport ({}, TakeFilter::All, 960).empty(), "an empty take exports nothing");
    }

    // A real engine run: every note sent out is in the take.
    {
        std::vector<float> audio ((size_t) (3.4 * sr), 0.0f);
        addTone (audio, 0.53, 0.40, 130.81, 0.35, 30.0, true);
        addTone (audio, 1.03, 0.40, 164.81, 0.35, 30.0, true);
        addTone (audio, 1.53, 0.40, 196.00, 0.35, 30.0, true);

        Settings s;
        s.grid = SnapGrid::Sixteenth;
        s.timingStrength = 1.0f;
        s.keyLock = true;
        s.keyRoot = 0;
        s.scale = ScaleType::Major;
        s.pitchSnap = 1.0f;

        VoiceEngine engine;
        engine.prepare (sr, 512);

        TakeRecorder rec;
        std::vector<HistoryNote> take;
        std::vector<MidiEvent> block;
        int ons = 0;

        for (int pos = 0; pos < (int) audio.size(); pos += 512)
        {
            const int n = std::min (512, (int) audio.size() - pos);
            engine.process (audio.data() + pos, n, (double) pos / sr * 2.0, 120.0, s, block);

            for (const auto& m : block)
                if ((m.status & 0xF0) == 0x90 && m.data2 > 0)
                    ++ons;

            rec.capture (engine.getLiveState(), take);
        }

        std::printf ("  engine sent %d note-on(s), take holds %d note(s)\n", ons, (int) take.size());
        check (ons > 0 && (int) take.size() == ons, "the take holds every note the engine played");
    }
}

} // namespace

int main()
{
    std::printf ("Humline - core engine tests\n");

    testQuantizer();
    testMelody();
    testBeatbox();
    testSustainAndPolyphonyGuard();
    testChords();
    testTrainingUnrecognisedSound();
    testKeyDetector();
    testInstrumentProbe();
    testDublerFeatures();
    testLiveModes();
    testTimelineJump();
    testTake();

    std::printf ("\n%s (%d failure(s))\n", gFailures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", gFailures);
    return gFailures == 0 ? 0 : 1;
}
