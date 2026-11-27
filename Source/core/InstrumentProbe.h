#pragma once

#include "Types.h"

#include <atomic>
#include <string>
#include <vector>

namespace dbc
{

//==============================================================================
/** What Humline's MIDI out is driving. It decides the mode: an instrument gets
    sung notes (and chords), a drum kit gets beatbox triggers, and with nothing
    attached Humline sends no MIDI and asks for a synth. */
enum class InstrumentTarget : int
{
    None = 0,
    Instrument,
    DrumKit,
    NumTargets
};

const char* instrumentTargetName (InstrumentTarget t) noexcept;

/** The engine mode a target calls for. Live: a drum kit gets Beatbox and an
    instrument (or nothing yet) gets Melody. Quantised uses Both, with the
    other half switched off (see HumlineProcessor::buildSettings). */
inline EngineMode engineModeFor (InstrumentTarget t, bool quantised) noexcept
{
    if (quantised)
        return EngineMode::Both;

    return t == InstrumentTarget::DrumKit ? EngineMode::Beatbox : EngineMode::Melody;
}

//==============================================================================
/** Finds out what is on the other end of the MIDI out by playing it a few
    short test notes and listening to what comes back.

    A plugin cannot see which instrument receives its MIDI, so Humline listens
    instead. The instrument's audio has to reach Humline: through its
    "Instrument" sidechain input (the normal setup), or, failing that, through
    its main input (the instrument plays into the same mixer insert, or a mic
    hears the speakers).

    The test plays three notes on the voice channel (and the drum channel, if
    it is different):
      - C4, held 0.4 s, then C3, held 0.4 s: an instrument plays both, an
        octave apart, and keeps sounding while the note is held.
      - F#2 (GM closed hat, note 42), short: a drum kit answers with noise, an
        instrument with a low pitched note.
    No answer at all means nothing is attached.

    Threading: start() and cancel() may be called from any thread; process()
    runs on the audio thread; analyse() runs on any other thread once
    hasRecording() is true. The audio thread never writes the recording while
    it is waiting to be analysed. */
class InstrumentProbe
{
public:
    static constexpr int numProbeNotes = 3;

    struct NoteResult
    {
        int   note        = 0;
        bool  heard       = false;
        float peakDb      = -100.0f;
        float onsetMs     = 0.0f;     // from note-on to the answer starting
        float f0          = 0.0f;     // Hz, 0 when unpitched
        float aperiodicity = 1.0f;    // YIN minimum: low = clearly pitched
        float sustain     = 0.0f;     // level 0.3 s in, relative to the peak
        float zcrHz       = 0.0f;     // zero-crossing rate / 2: brightness / noisiness

        bool pitched() const noexcept { return heard && f0 > 0.0f && aperiodicity < 0.4f; }
    };

    struct Result
    {
        InstrumentTarget target = InstrumentTarget::None;
        bool heardOnMainInput   = false;   // the answer came in on the main (mic) input
        NoteResult notes[numProbeNotes] {};
        std::string reason;                // one line, for the UI
    };

    void prepare (double sampleRate);

    /** Asks for a test. It starts at the next process() call (if one is not
        already running). */
    void start (int voiceChannel, int drumChannel) noexcept;

    /** Stops a running test; the next process() call sends its note-offs. */
    void cancel() noexcept
    {
        cancelRequested.store (true);

        startRequested.store (false);   // never asked for, or never started: drop it now

        // A test that has already finished is dropped as well: it was asked for
        // before this cancel, and analysing it later would overrule the target
        // being picked now.
        auto recorded = Phase::Recorded;
        phase.compare_exchange_strong (recorded, Phase::Idle);
    }

    bool isRunning() const noexcept   { return phase.load() == Phase::Running || startRequested.load(); }
    bool hasRecording() const noexcept { return phase.load() == Phase::Recorded; }

    /** Seconds one test takes. */
    static double durationSeconds() noexcept;

    /** Audio thread. Either input may be null. Adds the test notes to `out`. */
    void process (const float* sidechain, const float* mainInput, int numSamples,
                  std::vector<MidiEvent>& out);

    /** Works out what answered. Call once hasRecording() is true; it frees the
        probe for the next test. */
    Result analyse();

    /** The classification on its own, so it can be tested without audio. */
    static InstrumentTarget classify (const NoteResult (&notes)[numProbeNotes], std::string* reason = nullptr);

private:
    enum class Phase : int { Idle, Running, Recorded };

    struct Step
    {
        int    note;
        double onAt, offAt, windowEnd;   // seconds from the start of the test
    };

    static const Step* steps() noexcept;

    void emit (std::vector<MidiEvent>& out, int offset, bool on, int note) const;
    NoteResult analyseNote (const std::vector<float>& rec, int stepIndex, float noiseRms) const;

    double sr = 48000.0;
    std::vector<float> recSide, recMain;
    int    totalSamples = 0;
    int    pos = 0;
    int    nextStep = 0;          // step whose note-on is due next
    int    soundingNote = -1;     // note currently held (for cancel)
    int    chanVoice = 1, chanDrum = 10;

    std::atomic<Phase> phase { Phase::Idle };
    std::atomic<bool>  startRequested { false }, cancelRequested { false };
    std::atomic<int>   pendingVoiceChan { 1 }, pendingDrumChan { 10 };
};

} // namespace dbc
