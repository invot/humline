#pragma once

#include "core/InstrumentProbe.h"
#include "core/Take.h"
#include "core/VoiceEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace dbc
{

class HumlineProcessor : public juce::AudioProcessor,
                         private juce::Timer
{
public:
    HumlineProcessor();
    ~HumlineProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Bypassed, Humline still has nothing of its own to say: the mic does not
        become the track's output, and anything it left sounding is released. */
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                { return true; }

    const juce::String getName() const override    { return "Humline"; }
    bool acceptsMidi() const override              { return true; }
    bool producesMidi() const override             { return true; }
    bool isMidiEffect() const override             { return false; }
    double getTailLengthSeconds() const override   { return 0.0; }

    int getNumPrograms() override                  { return 1; }
    int getCurrentProgram() override               { return 0; }
    void setCurrentProgram (int) override          {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    dbc::VoiceEngine engine;

    dbc::Settings buildSettings() const;
    const dbc::LiveState& getLiveState() const noexcept { return engine.getLiveState(); }

    void  setPadNote (int pad, int note);
    int   getPadNote (int pad) const noexcept;
    bool  isPadMuted (int pad) const noexcept;
    void  setPadMuted (int pad, bool muted);
    void  setPadName (int pad, const juce::String& name);
    juce::String getPadName (int pad) const;

    /** Records takes for a pad until its bank of 12 is full or training is stopped.
        Returns false when the bank is already full. */
    bool startTraining (int pad);
    void cancelTraining();

    /** Velocity on: a trigger's velocity follows how loud the hit was. Off: always 127. */
    void setPadVelocity (int pad, bool followsInput);
    bool getPadVelocity (int pad) const noexcept;

    // --- key restriction: notes switched off by hand -------------------------
    /** Bit i set = pitch class i is allowed (on top of the key's scale). */
    int  getNoteMask() const noexcept { return noteMask.load(); }
    void setNoteMask (int mask);
    void toggleNote (int pitchClass);

    // --- chords: a chord picked by hand per scale degree -----------------------
    ChordQuality getChordOverride (int degree) const noexcept;
    void setChordOverride (int degree, ChordQuality q);

    /** Sends a pad's drum note to the attached drum kit, so clicking a pad plays it. */
    void previewPad (int pad);
    /** Sends a drum note (short note-on/off on the drum channel) to the attached kit. */
    void previewDrumNote (int note);

    // --- what the MIDI out is driving ---------------------------------------
    /** Instrument: sung notes and chords. DrumKit: beatbox triggers. None: no MIDI
        out, and the editor asks for a synth. */
    InstrumentTarget getTarget() const noexcept { return (InstrumentTarget) target.load(); }
    /** Sets the target by hand (or from a check). Cancels a check in progress. */
    void setTarget (InstrumentTarget t);

    /** Plays the attached instrument a few test notes and listens for the answer
        on the "Instrument" sidechain input (or the main input). About 2 s. */
    void startInstrumentCheck();
    bool isCheckingInstrument() const noexcept { return probe.isRunning() || probe.hasRecording(); }

    /** While on (the editor's "add a synth" screen is showing and nothing is
        attached), a check runs every few seconds - only while the transport is
        stopped and the input is quiet, so it never lands in a recording. */
    void setAutoCheck (bool shouldCheck) noexcept { autoCheck.store (shouldCheck); }

    struct InstrumentCheck
    {
        InstrumentProbe::Result result;
        juce::Time when;
        int count = 0;            // checks finished so far (0 = none yet)
    };
    /** The last finished check. Message thread only. */
    const InstrumentCheck& getLastInstrumentCheck() const noexcept { return lastCheck; }

    /** False while the host transport is playing, which pauses the automatic checks. */
    bool isTransportPlaying() const noexcept { return transportPlaying.load(); }
    /** True when the host has the "Instrument" sidechain input switched on. */
    bool hasInstrumentInput() const noexcept { return instrumentInputActive.load(); }

    // --- the take: every note finished since the last Clear (Notes page) -------
    /** A copy of the take, in the order the notes finished. */
    std::vector<HistoryNote> getTake() const;
    void clearTake();
    /** Removes the note at `index` of a getTake() copy, if it is still that note. */
    bool removeTakeNote (size_t index, const HistoryNote& expected);
    /** Bumped whenever the take changes, so the editor only redraws when it has to. */
    int getTakeVersion() const noexcept { return takeVersion.load(); }
    /** Host tempo when the take's latest note was captured. */
    double getTakeBpm() const noexcept { return takeBpm.load(); }

    /** "Humline take <bpm> BPM <key>.mid" (no key while Key Lock is off). */
    juce::String takeFileName (TakeFilter filter) const;
    /** Writes the take as a type-1 Standard MIDI File. False when nothing passes the filter. */
    bool writeTakeMidi (juce::OutputStream& out, TakeFilter filter) const;
    /** Writes the take to Humline's temp folder for a drag; an invalid File when empty. */
    juce::File writeTakeToTempFile (TakeFilter filter) const;

    bool isStandaloneApp() const noexcept { return wrapperType == wrapperType_Standalone; }

    /** Bumped whenever pad/template state changes so the editor can refresh. */
    std::atomic<int> uiStateVersion { 0 };

    /** Notified (message thread) when a training pass completes. */
    std::function<void (int pad)> onTrainingFinished;

    std::atomic<float> inputLevelDb { -100.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct ParamRefs
    {
        std::atomic<float>* inputGain = nullptr;
        std::atomic<float>* quantise = nullptr;
        std::atomic<float>* grid = nullptr;
        std::atomic<float>* timingStrength = nullptr;
        std::atomic<float>* minLength = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* keyLock = nullptr;
        std::atomic<float>* keyRoot = nullptr;
        std::atomic<float>* scale = nullptr;
        std::atomic<float>* pitchSnap = nullptr;
        std::atomic<float>* octaveShift = nullptr;
        std::atomic<float>* chordsOn = nullptr;
        std::atomic<float>* chordMode = nullptr;
        std::atomic<float>* chordVoicing = nullptr;
        std::atomic<float>* sticky = nullptr;
        std::atomic<float>* drumsOn = nullptr;
        std::atomic<float>* drumSens = nullptr;
        std::atomic<float>* voiceChan = nullptr;
        std::atomic<float>* drumChan = nullptr;
        std::atomic<float>* drumOnDrumChan = nullptr;
        std::atomic<float>* velocityAmt = nullptr;
        std::atomic<float>* sendCC = nullptr;
        std::atomic<float>* ccA = nullptr;
        std::atomic<float>* ccE = nullptr;
        std::atomic<float>* ccO = nullptr;
        std::atomic<float>* ccDyn = nullptr;
        std::atomic<float>* midiOut = nullptr;
        std::atomic<float>* pitchOn = nullptr;
        std::atomic<float>* bendOn = nullptr;
        std::atomic<float>* bendRange = nullptr;
        std::atomic<float>* bendMode = nullptr;
        std::atomic<float>* bendChords = nullptr;
        std::atomic<float>* chordRootBass = nullptr;
        std::atomic<float>* chordFollow = nullptr;
        std::atomic<float>* chordOctave = nullptr;
        std::atomic<float>* chordChan = nullptr;
        std::atomic<float>* passInput = nullptr;
        std::atomic<float>* ccInMin[numCcDials] {};
        std::atomic<float>* ccInMax[numCcDials] {};
        std::atomic<float>* ccOutMin[numCcDials] {};
        std::atomic<float>* ccOutMax[numCcDials] {};
    };

    void timerCallback() override;
    void mixInputToMono (const juce::AudioBuffer<float>& buffer, int numSamples);

    /** Sends the note-offs the engine still owes, plus all-notes-off on every
        channel Humline can reach, and forgets the engine's MIDI state. */
    void silenceMidiOut (juce::MidiBuffer& midi, const Settings& s, int offset);

    ParamRefs p;

    std::atomic<int>  padNotes[numDrumPads];
    std::atomic<bool> padMutes[numDrumPads];
    std::atomic<bool> padVelocity[numDrumPads];
    juce::String      padNames[numDrumPads];

    std::atomic<int>  noteMask { allNotesMask };
    std::atomic<int>  chordOverrides[12];

    juce::AudioBuffer<float> monoBuffer;
    std::vector<dbc::MidiEvent> blockEvents;
    juce::AudioBuffer<float> sideBuffer;   // the "Instrument" input, mono
    std::vector<dbc::MidiEvent> probeEvents;
    std::vector<dbc::MidiEvent> silenceEvents;    // scratch for silenceMidiOut, reserved up front
    std::atomic<bool> pendingSilence { false };   // the next block has to release the old notes
    bool midiOutWasOn = true;                     // audio thread: an off edge silences
    bool bypassed     = false;                    // audio thread

    std::atomic<int> pendingPreview { -1 };
    int previewNote = -1, previewChannel = 10, previewSamplesLeft = 0;
    double preparedRate = 48000.0;

    // The target and the instrument check.
    std::atomic<int>  target { (int) InstrumentTarget::None };
    std::atomic<int>  lastSentTarget { -1 };    // audio thread: sends all-notes-off on a change
    InstrumentProbe   probe;
    InstrumentCheck   lastCheck;
    std::atomic<bool> autoCheck { false }, transportPlaying { false }, instrumentInputActive { false };
    std::atomic<bool> probeReady { false };     // prepareToPlay has sized the probe
    int autoCheckTicks = 0;
    juce::uint32 checkStartedMs = 0;

    double fallbackPpq = 0.0;
    bool   warnedNoPlayHead = false;

    // The take. The audio thread appends under a try-lock (it never waits and,
    // with the capacity reserved up front, never allocates); the message thread
    // holds the lock only to copy or edit.
    mutable juce::SpinLock takeLock;
    std::vector<HistoryNote> take;
    TakeRecorder takeRecorder;
    std::atomic<int>    takeVersion { 0 };
    std::atomic<double> takeBpm { 120.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumlineProcessor)
};

} // namespace dbc
