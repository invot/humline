#pragma once

#include "Chords.h"
#include "DrumClassifier.h"
#include "OnsetDetector.h"
#include "PitchDetector.h"
#include "Quantizer.h"
#include "Types.h"

#include <atomic>
#include <functional>
#include <vector>

namespace dbc
{

/** Turns a mono vocal signal into quantised MIDI.  No JUCE dependencies, so the
    whole thing can be exercised from a plain console test. */
class VoiceEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** input: mono block (mic, already gain staged).  Times are in quarter notes. */
    void process (const float* input, int numSamples,
                  double blockStartPpq, double bpm,
                  const Settings& settings,
                  std::vector<MidiEvent>& midiOut);

    const LiveState& getLiveState() const noexcept { return live; }
    LiveState& getLiveState() noexcept { return live; }

    /** In Both mode, events are sent this long after their musical position so a
        note that lands just before a grid line can still be scheduled on it. The
        stream is late by exactly this amount, so a recorded clip needs one
        constant nudge. The live modes (Beatbox, Melody) send events as soon as
        they are detected, so this is 0 there. */
    int getOutputLatencySamples() const noexcept { return (int) lastLatencySamples; }

    /** Sends a note-off (at offset 0) for every note the engine still believes is
        sounding, and forgets its MIDI state, queued events included. Used when
        that state is about to go away: MIDI Out switched off, the plugin
        bypassed or unloaded, a project loaded. */
    void silence (std::vector<MidiEvent>& midiOut);

    DrumClassifier& getClassifier() noexcept { return classifier; }
    const DrumClassifier& getClassifier() const noexcept { return classifier; }

    // --- trigger training -----------------------------------------------------
    void startTraining (int pad, int takes);
    void cancelTraining() noexcept;
    bool isTraining() const noexcept { return training; }
    int  trainingPad() const noexcept { return trainPad; }
    int  trainingTakes() const noexcept { return trainTakes; }
    int  trainingProgress() const noexcept { return trainCollected; }

    std::function<void()> onTrainingComplete;

    // --- key detection --------------------------------------------------------
    /** While listening, every voiced frame adds its (unsnapped) pitch class to a
        histogram the Key page turns into key suggestions. */
    void setKeyListening (bool shouldListen) noexcept { keyListening.store (shouldListen); }
    bool isKeyListening() const noexcept { return keyListening.load(); }
    void clearKeyHistogram() noexcept { keyClearRequested.store (true); }
    std::array<float, 12> getKeyHistogram() const noexcept;

private:
    struct PendingOnset
    {
        int64_t abs     = 0;
        float   low     = 0.0f;
        float   f1      = 0.0f;
        float   f2      = 0.0f;
        float   high    = 0.0f;
        float   zcr     = 0.0f;
        float   clarity = 0.0f;
        float   energy  = 0.0f;
        int     sincePeak = 0;     // frames since the loudest frame of the hit
    };

    struct PendingEvent
    {
        int64_t abs    = 0;
        uint8_t status = 0;
        uint8_t d1     = 0;
        uint8_t d2     = 0;
        bool    drum   = false;
    };

    struct Region
    {
        int64_t start = 0;
        int64_t end   = 0;
    };

    void processFrame (const Settings& s);
    void finaliseOnset (const PendingOnset& o, const DrumFeatures& f, const Settings& s, bool liveDecision);
    DrumFeatures liveFeatures (const PendingOnset& o, const OnsetDetector::Frame& now, float clarity) const noexcept;
    void updatePitchTracker (int64_t frameEnd, const PitchDetector::Frame& pf, const Settings& s);
    void updateLive (const OnsetDetector::Frame& of, const PitchDetector::Frame& pf, int64_t frameEnd);

    void startRun (float midi, int64_t at);
    void closeRun (int64_t endAbs, const Settings& s);
    void confirmRun (int64_t frameEnd, const Settings& s);
    void updateBend (int64_t frameEnd, float sungMidi, const Settings& s);
    void sendBend (int64_t abs, int value14);
    void dropBendsAfter (int64_t abs);
    static int stableFramesFor (const Settings& s) noexcept;
    static int liveStableFramesFor (const Settings& s) noexcept;
    void emitDrumNote (int pad, int64_t onsetAbs, float energy, const Settings& s, bool liveDecision);

    /** Live modes decide this many frames after a hit's peak (~11 ms at 48 kHz). */
    static constexpr int liveDecayFrames = 2;

    void addRegion (int64_t start, int64_t end);
    bool startsInRegion (int64_t abs) const;

    double  ppqFromAbs (int64_t abs) const noexcept;
    int64_t absFromPpq (double ppq) const noexcept;

    void schedule (int64_t abs, uint8_t status, uint8_t d1, uint8_t d2, bool drum = false);
    void flush (std::vector<MidiEvent>& out, int numSamples);
    void pushHistory (double startBeats, double endBeats, int note, bool drum);

    int64_t latencyFor (const Settings& s) noexcept;

    /** The host moved the playhead (a loop, a locate): everything the old
        position held or queued is released, and the take keeps what was sung. */
    void releaseTimeline (const Settings& s);

    //==========================================================================
    double sampleRate = 48000.0;
    int    hop        = 256;
    int    hopCountdown = 256;

    int64_t absPos        = 0;
    int64_t blockStartAbs = 0;
    double  blockStartPpq = 0.0;
    double  bpm           = 120.0;

    // The previous block's musical position, so a host loop or locate can be
    // told apart from ordinary playback.
    bool    haveLastBlock  = false;
    double  lastBlockPpq   = 0.0;
    double  lastBlockBeats = 0.0;

    PitchDetector pitch;
    OnsetDetector onset;

    int64_t lastLatencySamples = 8736;
    int64_t lastOnsetAbs = -1000000;
    int64_t lastDrumAbs = -1000000;
    float   lastDrumEnergy = 0.0f;
    int     ccCounter = 0;
    int     lastCc[4] { -1000, -1000, -1000, -1000 };

    std::vector<PendingOnset> pendingOnsets;
    std::vector<PendingEvent> pendingEvents;
    std::vector<Region>       drumRegions;

    // pitch run tracking
    bool    runActive   = false;
    int64_t runStart    = 0;
    float   runSum      = 0.0f;
    int     runCount    = 0;
    float   runNote     = 60.0f;
    float   runVelocity = 0.0f;
    int     runFrames   = 0;
    int     runOff      = 0;
    int64_t lastVoiced  = 0;
    float   runPeakLevel   = 0.0f;    // live release: loudest frame of the note
    int     runQuietFrames = 0;
    int64_t liveReleaseAbs = -1;

    // A sung note can sound as the melody plus a chord on its own channel.
    static constexpr int maxOutNotes = maxChordNotes + 1;

    bool    noteOnSent      = false;
    int     activeOutNotes[maxOutNotes] {};                // every voice that is on
    int     activeOutChans[maxOutNotes] {};                // ... and its channel (0-based)
    int     activeOutCount  = 0;
    int     activeMelody    = 60;                          // the snapped sung note
    int64_t activeOnAbs     = 0;
    double  activeStartBeats = 0.0;

    // pitch bend: channels that bend with the voice, and the last value sent
    int     bendChans[2]    { -1, -1 };
    int     lastBend        = 8192;
    float   bendDev         = 0.0f;   // smoothed deviation from the note, semitones
    int64_t lastBendAbs     = 0;

    // training
    bool         training       = false;
    int          trainPad       = 0;
    int          trainTakes     = 8;
    int          trainCollected = 0;

    // key detection
    std::atomic<bool>  keyListening { false };
    std::atomic<bool>  keyClearRequested { false };
    std::atomic<float> keyHist[12] {};

    DrumClassifier classifier;
    LiveState live;
};

} // namespace dbc
