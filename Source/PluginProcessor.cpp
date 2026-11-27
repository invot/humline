#include "PluginProcessor.h"
#include "PluginEditor.h"


namespace dbc
{

namespace
{
    const juce::StringArray kGridNames { "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    const juce::StringArray kLengthNames { "1/32", "1/16", "1/8", "1/4", "1/2" };
    const double kLengthBeats[] { 0.125, 0.25, 0.5, 1.0, 2.0 };

    const juce::StringArray kNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::StringArray scaleNames()
    {
        juce::StringArray a;

        for (int i = 0; i < (int) ScaleType::NumScales; ++i)
            a.add (scaleName ((ScaleType) i));

        return a;
    }

    juce::StringArray chordPresetNames()
    {
        juce::StringArray a;

        for (int i = 0; i < (int) ChordMode::NumModes; ++i)
            a.add (chordModeName ((ChordMode) i));

        return a;
    }

    juce::StringArray chordVoicingNames()
    {
        juce::StringArray a;

        for (int i = 0; i < (int) ChordVoicing::NumVoicings; ++i)
            a.add (chordVoicingName ((ChordVoicing) i));

        return a;
    }

    juce::StringArray bendModeNames() { return { "IntelliBend", "TruBend" }; }

    juce::StringArray chordChannelNames()
    {
        juce::StringArray a { "Same as voice" };

        for (int i = 1; i <= 16; ++i)
            a.add ("Ch " + juce::String (i));

        return a;
    }

    // Parameter id prefixes of the four CC dials, in Settings order.
    const char* const kDialIds[numCcDials] { "ccA", "ccE", "ccO", "ccDyn" };
    const char* const kDialNames[numCcDials] { "Vowel A", "Vowel E", "Vowel O", "Envelope" };

    const char* defaultPadNames[numDrumPads] {
        "Really Low Bassdrum", "Snare", "Clap", "Bassdrum",
        "Closed Hat", "Open Hat", "Low Tom", "High Tom"
    };
}

//==============================================================================
HumlineProcessor::HumlineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          // The attached instrument's audio, so Humline can hear
                          // what its MIDI out is driving (see InstrumentProbe).
                          .withInput  ("Instrument", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    const auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    p.inputGain      = get ("inputGain");
    p.quantise       = get ("quantise");
    p.grid           = get ("grid");
    p.timingStrength = get ("timingStrength");
    p.minLength      = get ("minLength");
    p.swing          = get ("swing");
    p.keyLock        = get ("keyLock");
    p.keyRoot        = get ("keyRoot");
    p.scale          = get ("scale");
    p.pitchSnap      = get ("pitchSnap");
    p.octaveShift    = get ("octaveShift");
    p.chordsOn       = get ("chordsOn");
    p.chordMode      = get ("chordMode");
    p.chordVoicing   = get ("chordVoicing");
    p.sticky         = get ("sticky");
    p.drumsOn        = get ("drumsOn");
    p.drumSens       = get ("drumSens");
    p.voiceChan      = get ("voiceChan");
    p.drumChan       = get ("drumChan");
    p.drumOnDrumChan = get ("drumOnDrumChan");
    p.velocityAmt    = get ("velocityAmt");
    p.sendCC         = get ("sendCC");
    p.ccA            = get ("ccA");
    p.ccE            = get ("ccE");
    p.ccO            = get ("ccO");
    p.ccDyn          = get ("ccDyn");
    p.midiOut        = get ("midiOut");
    p.pitchOn        = get ("pitchOn");
    p.bendOn         = get ("bendOn");
    p.bendRange      = get ("bendRange");
    p.bendMode       = get ("bendMode");
    p.bendChords     = get ("bendChords");
    p.chordRootBass  = get ("chordRootBass");
    p.chordFollow    = get ("chordFollow");
    p.chordOctave    = get ("chordOctave");
    p.chordChan      = get ("chordChan");
    p.passInput      = get ("passInput");

    take.reserve (TakeRecorder::maxNotes);

    for (int d = 0; d < numCcDials; ++d)
    {
        const juce::String id (kDialIds[d]);
        p.ccInMin[d]  = apvts.getRawParameterValue (id + "InMin");
        p.ccInMax[d]  = apvts.getRawParameterValue (id + "InMax");
        p.ccOutMin[d] = apvts.getRawParameterValue (id + "OutMin");
        p.ccOutMax[d] = apvts.getRawParameterValue (id + "OutMax");
    }

    for (auto& o : chordOverrides)
        o.store ((int) ChordQuality::Preset);

    for (int i = 0; i < numDrumPads; ++i)
    {
        padNotes[i].store (Settings().padNote[i]);
        padMutes[i].store (false);
        padVelocity[i].store (true);
        padNames[i] = defaultPadNames[i];
    }

    engine.onTrainingComplete = [this]
    {
        const int pad = engine.trainingPad();

        if (onTrainingFinished != nullptr)
            juce::MessageManager::callAsync ([this, pad]
            {
                if (onTrainingFinished != nullptr)
                    onTrainingFinished (pad);
            });

        ++uiStateVersion;
    };

    startTimerHz (10);   // instrument checks (see timerCallback)
}

juce::AudioProcessorValueTreeState::ParameterLayout HumlineProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

   #if JUCE_MAJOR_VERSION >= 7
    const auto pid = [] (const char* id) { return juce::ParameterID { id, 1 }; };
   #else
    const auto pid = [] (const char* id) { return juce::String (id); };
   #endif

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("inputGain"), "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    // The attached instrument decides what the voice becomes (notes for an
    // instrument, triggers for a drum kit). Live sends each event the moment it
    // is detected; Quantised snaps to the grid, which needs a constant delay.
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("quantise"), "Quantise", false));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        pid ("grid"), "Timing Snap", kGridNames, 4));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("timingStrength"), "Timing Strength",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        pid ("minLength"), "Minimum Length", kLengthNames, 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("swing"), "Swing", juce::NormalisableRange<float> (0.0f, 50.0f, 1.0f), 0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("keyLock"), "Key Lock", true));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid ("keyRoot"), "Key", kNoteNames, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid ("scale"), "Scale", scaleNames(), 1));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("pitchSnap"), "Pitch Snap", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("octaveShift"), "Octave", -2, 2, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("chordsOn"), "Chords", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        pid ("chordMode"), "Chord Preset", chordPresetNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        pid ("chordVoicing"), "Chord Voicing", chordVoicingNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("sticky"), "Stickiness", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("drumsOn"), "Beatbox", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("drumSens"), "Drum Sensitivity", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f));

    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("voiceChan"), "Voice MIDI Channel", 1, 16, 1));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("drumChan"), "Drum MIDI Channel", 1, 16, 10));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("drumOnDrumChan"), "Drums On Channel 10", true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        pid ("velocityAmt"), "Velocity Amount", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 75.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("sendCC"), "Send CC", true));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("ccA"), "CC Vowel A", 1, 127, 20));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("ccE"), "CC Vowel E", 1, 127, 21));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("ccO"), "CC Vowel O", 1, 127, 22));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("ccDyn"), "CC Dynamics", 1, 127, 11));

    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("midiOut"), "MIDI Out", true));

    // Pitch: on/off (Play page) and pitch bend.
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("pitchOn"), "Pitch", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("bendOn"), "Pitch Bend", false));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("bendRange"), "Pitch Bend Range", 1, 24, 2));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid ("bendMode"), "Pitch Bend Type", bendModeNames(), 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("bendChords"), "Bend Chords", true));

    // Chords page.
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("chordRootBass"), "Root Bassline", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("chordFollow"), "Chords Follow Octave", true));
    layout.add (std::make_unique<juce::AudioParameterInt> (pid ("chordOctave"), "Chord Octave Shift", -2, 2, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (pid ("chordChan"), "Chord MIDI Channel",
                                                               chordChannelNames(), 0));

    // CC dials: the input range that sweeps each dial, and the CC span it covers.
    for (int d = 0; d < numCcDials; ++d)
    {
        const juce::String id (kDialIds[d]), name (kDialNames[d]);

       #if JUCE_MAJOR_VERSION >= 7
        const auto sid = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };
       #else
        const auto sid = [] (const juce::String& s) { return s; };
       #endif

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            sid (id + "InMin"), name + " Input Min", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            sid (id + "InMax"), name + " Input Max", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));
        layout.add (std::make_unique<juce::AudioParameterInt> (sid (id + "OutMin"), name + " CC Min", 0, 127, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (sid (id + "OutMax"), name + " CC Max", 0, 127, 127));
    }

    // Leave the audio on Humline's mixer track intact instead of replacing it,
    // so an instrument routed to the same track can still be heard. Off by
    // default, and ignored by the standalone app (it would feed the speakers
    // back into the mic).
    layout.add (std::make_unique<juce::AudioParameterBool> (pid ("passInput"), "Pass Input", false));

    return layout;
}

//==============================================================================
void HumlineProcessor::previewPad (int pad)
{
    if (pad >= 0 && pad < numDrumPads)
        previewDrumNote (padNotes[pad].load());
}

void HumlineProcessor::previewDrumNote (int note)
{
    pendingPreview.store (juce::jlimit (0, 127, note));
}

//==============================================================================
HumlineProcessor::~HumlineProcessor()
{
    stopTimer();
}

void HumlineProcessor::setTarget (InstrumentTarget t)
{
    probe.cancel();
    target.store (juce::jlimit (0, (int) InstrumentTarget::NumTargets - 1, (int) t));
    ++uiStateVersion;
}

void HumlineProcessor::startInstrumentCheck()
{
    if (! probeReady.load() || isCheckingInstrument())
        return;

    const auto s = buildSettings();
    probe.start (s.voiceChannel, s.drumsOnDrumChan ? 10 : s.drumChannel);
    checkStartedMs = juce::Time::getMillisecondCounter();
    autoCheckTicks = 0;
    ++uiStateVersion;
}

void HumlineProcessor::timerCallback()
{
    // A finished check: analyse it here, off the audio thread.
    if (probeReady.load() && probe.hasRecording())
    {
        lastCheck.result = probe.analyse();
        lastCheck.when = juce::Time::getCurrentTime();
        ++lastCheck.count;

        // Checks only run while nothing is attached or when asked for, so the
        // answer (silence included) becomes the target.
        target.store ((int) lastCheck.result.target);

        ++uiStateVersion;
        return;
    }

    // The host stopped calling processBlock mid-check: give up rather than
    // show "Checking..." for ever.
    if (probe.isRunning()
        && juce::Time::getMillisecondCounter() - checkStartedMs > (juce::uint32) (1000.0 * InstrumentProbe::durationSeconds()) + 3000)
    {
        probe.cancel();
        ++uiStateVersion;
    }

    // Automatic checks while the "add a synth" screen is up: every 4 s, only
    // with the transport stopped and nobody singing into the mic.
    if (autoCheck.load() && getTarget() == InstrumentTarget::None && ! isCheckingInstrument())
    {
        const bool quiet = inputLevelDb.load() < -45.0f && engine.getLiveState().activeNote < 0;

        if (++autoCheckTicks >= 40 && quiet && ! transportPlaying.load())
            startInstrumentCheck();
    }
    else
    {
        autoCheckTicks = 30;   // the first automatic check comes 1 s after the screen opens
    }
}

bool HumlineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    // The instrument input is optional: off, mono or stereo.
    if (layouts.inputBuses.size() > 1)
    {
        const auto side = layouts.getChannelSet (true, 1);

        if (! side.isDisabled() && side != juce::AudioChannelSet::mono() && side != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void HumlineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    probeReady.store (false);
    engine.prepare (sampleRate, samplesPerBlock);
    monoBuffer.setSize (1, std::max (32, samplesPerBlock), false, true, false);
    sideBuffer.setSize (1, std::max (32, samplesPerBlock), false, true, false);
    blockEvents.reserve (256);
    probeEvents.reserve (32);
    probe.prepare (sampleRate);
    preparedRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    probeReady.store (true);
    previewSamplesLeft = 0;
    previewNote = -1;
    lastSentTarget.store (-1);
    fallbackPpq = 0.0;
    warnedNoPlayHead = false;
}

void HumlineProcessor::releaseResources()
{
    engine.reset();
}

void HumlineProcessor::mixInputToMono (const juce::AudioBuffer<float>& buffer, int numSamples)
{
    float* mono = monoBuffer.getWritePointer (0);
    juce::FloatVectorOperations::clear (mono, numSamples);

    // Only average the channels that actually carry signal. A USB mic usually
    // delivers on one channel of a stereo pair; averaging it with a silent
    // second channel would throw away 6 dB and push quiet singing under the
    // detection thresholds.
    // Main input only: the second input bus is the attached instrument.
    const int inputs = juce::jmin (buffer.getNumChannels(), getMainBusNumInputChannels());
    int live = 0;

    for (int ch = 0; ch < inputs; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);

        if (buffer.getMagnitude (ch, 0, numSamples) > 1.0e-6f)
        {
            juce::FloatVectorOperations::add (mono, src, numSamples);
            ++live;
        }
    }

    if (live > 1)
        juce::FloatVectorOperations::multiply (mono, 1.0f / (float) live, numSamples);
}

void HumlineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0)
        return;

    if (monoBuffer.getNumSamples() < numSamples)
        monoBuffer.setSize (1, numSamples, false, true, false);

    const auto settings = buildSettings();

    // --- mono sum with input gain -------------------------------------------
    mixInputToMono (buffer, numSamples);
    float* mono = monoBuffer.getWritePointer (0);

    const float gain = juce::Decibels::decibelsToGain (settings.inputGainDb);
    monoBuffer.applyGain (0, 0, numSamples, gain);

    // --- transport ----------------------------------------------------------
    double ppq = 0.0;
    double bpm = 120.0;
    bool havePpq = false;

    if (auto* ph = getPlayHead())
    {
       #if JUCE_MAJOR_VERSION >= 7
        if (const auto pos = ph->getPosition())
        {
            if (const auto pPpq = pos->getPpqPosition())
            {
                ppq = *pPpq;
                havePpq = true;
            }

            if (const auto pBpm = pos->getBpm())
                bpm = *pBpm;
        }
       #else
        juce::AudioPlayHead::CurrentPositionInfo info;

        if (ph->getCurrentPosition (info))
        {
            ppq = info.ppqPosition;
            bpm = info.bpm > 1.0 ? info.bpm : 120.0;
            havePpq = true;
        }
       #endif
    }

    if (! havePpq)
        ppq = fallbackPpq;

    // --- analysis -----------------------------------------------------------
    auto& events = blockEvents;
    engine.process (mono, numSamples, ppq, bpm, settings, events);

    fallbackPpq = ppq + (double) numSamples / std::max (1.0, getSampleRate()) * bpm / 60.0;

    // --- the take -----------------------------------------------------------
    // The history ring only holds 32 notes, so drain it every block. If the
    // editor happens to hold the lock, the notes wait in the ring for the next block.
    {
        const juce::SpinLock::ScopedTryLockType lock (takeLock);

        if (lock.isLocked() && takeRecorder.capture (engine.getLiveState(), take) > 0)
        {
            takeBpm.store (bpm);
            ++takeVersion;
        }
    }

    // --- the attached instrument --------------------------------------------
    // The "Instrument" input carries what the MIDI out is driving. The probe
    // records it (and the main input, as a fallback) while a check runs.
    const float* side = nullptr;

    if (getBusCount (true) > 1)
    {
        auto* bus = getBus (true, 1);

        // (Some callers hand over only the main channels; then there is no instrument input.)
        if (bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0
            && bus->getChannelIndexInProcessBlockBuffer (0) + bus->getNumberOfChannels() <= buffer.getNumChannels())
        {
            const auto sideIn = getBusBuffer (buffer, true, 1);

            if (sideBuffer.getNumSamples() < numSamples)
                sideBuffer.setSize (1, numSamples, false, true, false);

            sideBuffer.clear();

            for (int ch = 0; ch < sideIn.getNumChannels(); ++ch)
                sideBuffer.addFrom (0, 0, sideIn, ch, 0, numSamples, 1.0f / (float) sideIn.getNumChannels());

            side = sideBuffer.getReadPointer (0);
        }
    }

    instrumentInputActive.store (side != nullptr);

    probeEvents.clear();
    probe.process (side, mono, numSamples, probeEvents);
    const bool probing = probe.isRunning() || ! probeEvents.empty();

    // A drum preview: a short note on the drum channel, to the attached kit.
    const int drumChannel = settings.drumsOnDrumChan ? 10 : settings.drumChannel;

    if (previewSamplesLeft > 0)
    {
        previewSamplesLeft -= numSamples;

        if (previewSamplesLeft <= 0)
            probeEvents.push_back (MidiEvent { std::max (0, numSamples + previewSamplesLeft - 1),
                                               (uint8_t) (0x80 | (previewChannel - 1)), (uint8_t) previewNote, 0, true });
    }

    if (const int preview = pendingPreview.exchange (-1); preview >= 0 && ! probing)
    {
        if (previewSamplesLeft > 0)
            probeEvents.push_back (MidiEvent { 0, (uint8_t) (0x80 | (previewChannel - 1)), (uint8_t) previewNote, 0, true });

        previewNote = preview;
        previewChannel = drumChannel;
        previewSamplesLeft = std::max (1, (int) (0.12 * preparedRate));
        probeEvents.push_back (MidiEvent { 0, (uint8_t) (0x90 | (previewChannel - 1)), (uint8_t) preview, 110, true });
    }

    // --- MIDI out -----------------------------------------------------------
    // Nothing attached: no MIDI, apart from the check's test notes. A change of
    // target first silences whatever the old one was playing.
    const auto currentTarget = getTarget();
    const bool midiOn = p.midiOut->load() > 0.5f;

    if (lastSentTarget.exchange ((int) currentTarget) != (int) currentTarget)
    {
        const int channels[] { settings.voiceChannel, drumChannel,
                               settings.chordChannel > 0 ? settings.chordChannel : settings.voiceChannel };

        for (int ch : channels)
            midiMessages.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    }

    if (midiOn && currentTarget != InstrumentTarget::None)
    {
        for (const auto& e : events)
        {
            // While a check runs, the engine's new notes would muddle the answer.
            if (probing && (e.status & 0xF0) == 0x90 && e.data2 > 0)
                continue;

            midiMessages.addEvent (juce::MidiMessage ((int) e.status, (int) e.data1, (int) e.data2),
                                   juce::jlimit (0, numSamples - 1, e.offset));
        }
    }

    for (const auto& e : probeEvents)
        midiMessages.addEvent (juce::MidiMessage ((int) e.status, (int) e.data1, (int) e.data2),
                               juce::jlimit (0, numSamples - 1, e.offset));

    // --- live meters --------------------------------------------------------
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        peak = std::max (peak, std::abs (mono[i]));

    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    inputLevelDb.store (db);

    // --- audio out ----------------------------------------------------------
    // Humline makes no sound of its own: the attached instrument does. By
    // default the output is silent - in the standalone app passing the mic
    // through would feed the speakers straight back into the mic. Pass Input
    // (DAW only) keeps the main input, e.g. an instrument routed onto
    // Humline's mixer track.
    const bool passInput = p.passInput->load() > 0.5f && ! isStandaloneApp();
    const int inputChannels = getMainBusNumInputChannels();

    for (int ch = 0; ch < numChannels; ++ch)
        if (! passInput || ch >= inputChannels)
            buffer.clear (ch, 0, numSamples);
}

//==============================================================================
std::vector<HistoryNote> HumlineProcessor::getTake() const
{
    const juce::SpinLock::ScopedLockType lock (takeLock);
    return take;
}

void HumlineProcessor::clearTake()
{
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        take.clear();   // keeps the reserved capacity
    }

    ++takeVersion;
}

bool HumlineProcessor::removeTakeNote (size_t index, const HistoryNote& expected)
{
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);

        // Notes are only ever appended, so an index from an older copy still
        // points at the same note unless the take was cleared or edited since.
        if (index >= take.size())
            return false;

        const auto& n = take[index];

        if (n.startBeats != expected.startBeats || n.lengthBeats != expected.lengthBeats
            || n.note != expected.note || n.drum != expected.drum)
            return false;

        take.erase (take.begin() + (std::ptrdiff_t) index);
    }

    ++takeVersion;
    return true;
}

juce::String HumlineProcessor::takeFileName (TakeFilter filter) const
{
    juce::String name ("Humline take ");
    name << juce::roundToInt (takeBpm.load()) << " BPM";

    if (p.keyLock->load() > 0.5f)
    {
        const auto settings = buildSettings();
        name << " " << kNoteNames[settings.keyRoot] << " " << scaleName (settings.scale);
    }

    if (filter == TakeFilter::Voice)  name << " voice";
    if (filter == TakeFilter::Drums)  name << " drums";

    // FL Studio imports .mid, not .midi.
    return juce::File::createLegalFileName (name + ".mid");
}

bool HumlineProcessor::writeTakeMidi (juce::OutputStream& out, TakeFilter filter) const
{
    constexpr int ticksPerQuarter = 960;
    constexpr juce::uint8 velocity = 100;   // the take carries no velocity

    const auto notes = layoutTakeForExport (getTake(), filter, ticksPerQuarter);

    if (notes.empty())
        return false;

    const double bpm = juce::jlimit (20.0, 999.0, takeBpm.load());
    const auto settings = buildSettings();
    const int voiceChannel = settings.voiceChannel;
    const int drumChannel  = settings.drumsOnDrumChan ? settings.drumChannel : settings.voiceChannel;

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ticksPerQuarter);

    juce::MidiMessageSequence conductor;
    conductor.addEvent (juce::MidiMessage::textMetaEvent (3, "Humline take"), 0.0);
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm)), 0.0);
    conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    file.addTrack (conductor);

    // One track per lane, so a drum take and a melody can be split apart in FL.
    for (const bool drums : { false, true })
    {
        const int channel = drums ? drumChannel : voiceChannel;
        juce::MidiMessageSequence track;
        bool any = false;

        track.addEvent (juce::MidiMessage::textMetaEvent (3, drums ? "Humline drums" : "Humline voice"), 0.0);

        // Note-offs first: addEvent puts a new event after others on the same
        // tick, so a note ending where the next begins is released before it.
        for (const auto& n : notes)
            if (n.drum == drums)
                track.addEvent (juce::MidiMessage::noteOff (channel, n.note),
                                (double) (n.startTick + n.lengthTick));

        for (const auto& n : notes)
        {
            if (n.drum == drums)
            {
                track.addEvent (juce::MidiMessage::noteOn (channel, n.note, velocity), (double) n.startTick);
                any = true;
            }
        }

        if (any)
            file.addTrack (track);
    }

    return file.writeTo (out, 1);
}

juce::File HumlineProcessor::writeTakeToTempFile (TakeFilter filter) const
{
    juce::MemoryOutputStream data;

    if (! writeTakeMidi (data, filter))
        return {};

    // Kept after the drag: the drop target may read the file after the drag
    // has finished. The next drag of the same take overwrites it.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Humline");

    if (! dir.createDirectory())
        return {};

    const auto file = dir.getChildFile (takeFileName (filter));

    if (! file.replaceWithData (data.getData(), data.getDataSize()))
        return {};

    return file;
}

//==============================================================================
dbc::Settings HumlineProcessor::buildSettings() const
{
    dbc::Settings s;

    s.inputGainDb      = p.inputGain->load();
    s.mode             = engineModeFor (getTarget(), p.quantise->load() > 0.5f);
    s.grid             = (SnapGrid) juce::jlimit (0, (int) SnapGrid::NumGrids - 1, (int) p.grid->load());
    s.timingStrength   = p.timingStrength->load() * 0.01f;
    s.minLengthBeats   = (float) kLengthBeats[juce::jlimit (0, 4, (int) p.minLength->load())];
    s.swing            = p.swing->load() * 0.01f;
    s.keyLock          = p.keyLock->load() > 0.5f;
    s.keyRoot          = juce::jlimit (0, 11, (int) p.keyRoot->load());
    s.scale            = (ScaleType) juce::jlimit (0, (int) ScaleType::NumScales - 1, (int) p.scale->load());
    s.pitchSnap        = p.pitchSnap->load() * 0.01f;
    s.octaveShift      = juce::jlimit (-2, 2, (int) p.octaveShift->load());
    s.chordsEnabled    = p.chordsOn->load() > 0.5f;
    s.chordMode        = (ChordMode) juce::jlimit (0, (int) ChordMode::NumModes - 1, (int) p.chordMode->load());
    s.chordVoicing     = (ChordVoicing) juce::jlimit (0, (int) ChordVoicing::NumVoicings - 1,
                                                      (int) p.chordVoicing->load());
    s.stickyness       = p.sticky->load() * 0.01f;
    s.drumsEnabled     = p.drumsOn->load() > 0.5f;
    s.drumSensitivity  = p.drumSens->load() * 0.01f;
    s.voiceChannel     = juce::jlimit (1, 16, (int) p.voiceChan->load());
    s.drumChannel      = juce::jlimit (1, 16, (int) p.drumChan->load());
    s.drumsOnDrumChan  = p.drumOnDrumChan->load() > 0.5f;
    s.velocityAmount   = p.velocityAmt->load() * 0.01f;
    s.sendCC           = p.sendCC->load() > 0.5f;
    s.ccVowelA         = juce::jlimit (1, 127, (int) p.ccA->load());
    s.ccVowelE         = juce::jlimit (1, 127, (int) p.ccE->load());
    s.ccVowelO         = juce::jlimit (1, 127, (int) p.ccO->load());
    s.ccDynamics       = juce::jlimit (1, 127, (int) p.ccDyn->load());

    s.pitchEnabled     = p.pitchOn->load() > 0.5f;

    // The target picks what the voice becomes: an instrument gets no drums, a
    // drum kit gets no notes (this matters in Quantised, which hears both).
    if (getTarget() == InstrumentTarget::Instrument)
        s.drumsEnabled = false;
    else if (getTarget() == InstrumentTarget::DrumKit)
        s.pitchEnabled = false;
    s.bendEnabled      = p.bendOn->load() > 0.5f;
    s.bendRange        = juce::jlimit (1, 24, (int) p.bendRange->load());
    s.bendMode         = (BendMode) juce::jlimit (0, (int) BendMode::NumModes - 1, (int) p.bendMode->load());
    s.bendChords       = p.bendChords->load() > 0.5f;
    s.chordRootBass    = p.chordRootBass->load() > 0.5f;
    s.chordFollow      = p.chordFollow->load() > 0.5f;
    s.chordOctave      = juce::jlimit (-2, 2, (int) p.chordOctave->load());
    s.chordChannel     = juce::jlimit (0, 16, (int) p.chordChan->load());
    s.noteMask         = (uint16_t) (noteMask.load() & allNotesMask);

    for (int d = 0; d < 12; ++d)
        s.chordOverride[d] = (ChordQuality) juce::jlimit (0, (int) ChordQuality::NumQualities - 1,
                                                          chordOverrides[d].load());

    for (int d = 0; d < numCcDials; ++d)
    {
        s.ccInMin[d]  = p.ccInMin[d]->load() * 0.01f;
        s.ccInMax[d]  = p.ccInMax[d]->load() * 0.01f;
        s.ccOutMin[d] = juce::jlimit (0, 127, (int) p.ccOutMin[d]->load());
        s.ccOutMax[d] = juce::jlimit (0, 127, (int) p.ccOutMax[d]->load());
    }

    for (int i = 0; i < numDrumPads; ++i)
    {
        s.padNote[i]     = juce::jlimit (0, 127, padNotes[i].load());
        s.padMuted[i]    = padMutes[i].load();
        s.padVelocity[i] = padVelocity[i].load();
    }

    return s;
}

//==============================================================================
void HumlineProcessor::setPadNote (int pad, int note)
{
    if (pad >= 0 && pad < numDrumPads)
    {
        padNotes[pad].store (juce::jlimit (0, 127, note));
        ++uiStateVersion;
    }
}

int HumlineProcessor::getPadNote (int pad) const noexcept
{
    return (pad >= 0 && pad < numDrumPads) ? padNotes[pad].load() : 36;
}

bool HumlineProcessor::isPadMuted (int pad) const noexcept
{
    return (pad >= 0 && pad < numDrumPads) && padMutes[pad].load();
}

void HumlineProcessor::setPadMuted (int pad, bool muted)
{
    if (pad >= 0 && pad < numDrumPads)
    {
        padMutes[pad].store (muted);
        ++uiStateVersion;
    }
}

void HumlineProcessor::setPadName (int pad, const juce::String& name)
{
    if (pad >= 0 && pad < numDrumPads)
    {
        padNames[pad] = name.upToFirstOccurrenceOf ("\n", false, false).trim();
        ++uiStateVersion;
    }
}

juce::String HumlineProcessor::getPadName (int pad) const
{
    return (pad >= 0 && pad < numDrumPads) ? padNames[pad] : juce::String();
}

bool HumlineProcessor::startTraining (int pad)
{
    const int room = DrumClassifier::maxTakes - engine.getClassifier().numExamples (pad);

    if (room <= 0)
        return false;

    engine.startTraining (pad, room);
    return true;
}

void HumlineProcessor::setPadVelocity (int pad, bool followsInput)
{
    if (pad >= 0 && pad < numDrumPads)
    {
        padVelocity[pad].store (followsInput);
        ++uiStateVersion;
    }
}

bool HumlineProcessor::getPadVelocity (int pad) const noexcept
{
    return pad < 0 || pad >= numDrumPads || padVelocity[pad].load();
}

void HumlineProcessor::setNoteMask (int mask)
{
    noteMask.store (mask & (int) allNotesMask);
    ++uiStateVersion;
}

void HumlineProcessor::toggleNote (int pitchClass)
{
    const int pc = ((pitchClass % 12) + 12) % 12;
    setNoteMask (noteMask.load() ^ (1 << pc));
}

ChordQuality HumlineProcessor::getChordOverride (int degree) const noexcept
{
    if (degree < 0 || degree >= 12)
        return ChordQuality::Preset;

    return (ChordQuality) juce::jlimit (0, (int) ChordQuality::NumQualities - 1, chordOverrides[degree].load());
}

void HumlineProcessor::setChordOverride (int degree, ChordQuality q)
{
    if (degree >= 0 && degree < 12)
    {
        chordOverrides[degree].store ((int) q);
        ++uiStateVersion;
    }
}

void HumlineProcessor::cancelTraining()
{
    engine.cancelTraining();
}

//==============================================================================
void HumlineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::ValueTree pads ("PADS");

    for (int i = 0; i < numDrumPads; ++i)
    {
        juce::ValueTree pad ("PAD");
        pad.setProperty ("index", i, nullptr);
        pad.setProperty ("note", padNotes[i].load(), nullptr);
        pad.setProperty ("muted", padMutes[i].load(), nullptr);
        pad.setProperty ("velocity", padVelocity[i].load(), nullptr);
        pad.setProperty ("name", padNames[i], nullptr);
        pads.appendChild (pad, nullptr);
    }

    juce::ValueTree temps ("TEMPLATES");

    // One T node per take; a pad can have up to 12.
    for (int i = 0; i < numDrumPads; ++i)
    {
        for (int example = 0; example < engine.getClassifier().numExamples (i); ++example)
        {
            const auto f = engine.getClassifier().getExample (i, example);
            juce::ValueTree t ("T");
            t.setProperty ("index", i, nullptr);
            t.setProperty ("low", f.low, nullptr);
            t.setProperty ("f1", f.f1, nullptr);
            t.setProperty ("f2", f.f2, nullptr);
            t.setProperty ("high", f.high, nullptr);
            t.setProperty ("zcr", f.zcr, nullptr);
            t.setProperty ("sustain", f.sustain, nullptr);
            t.setProperty ("pitched", f.pitched, nullptr);
            temps.appendChild (t, nullptr);
        }
    }

    juce::ValueTree extra ("HUMLINE");
    extra.setProperty ("noteMask", noteMask.load(), nullptr);

    juce::StringArray overrides;

    for (auto& o : chordOverrides)
        overrides.add (juce::String (o.load()));

    extra.setProperty ("chordOverrides", overrides.joinIntoString (","), nullptr);
    extra.setProperty ("target", target.load(), nullptr);

    // The take, packed as 13 bytes a note (start, length, note, drum) in base64:
    // a long take as one XML node per note would bloat the project file.
    juce::ValueTree takeNode ("TAKE");
    {
        const auto notes = getTake();
        juce::MemoryOutputStream packed;

        for (const auto& n : notes)
        {
            packed.writeFloat (n.startBeats);
            packed.writeFloat (n.lengthBeats);
            packed.writeInt (n.note);
            packed.writeByte (n.drum ? 1 : 0);
        }

        takeNode.setProperty ("count", (int) notes.size(), nullptr);
        takeNode.setProperty ("bpm", takeBpm.load(), nullptr);
        takeNode.setProperty ("notes", packed.getMemoryBlock().toBase64Encoding(), nullptr);
    }

    state.appendChild (pads, nullptr);
    state.appendChild (temps, nullptr);
    state.appendChild (extra, nullptr);
    state.appendChild (takeNode, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HumlineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    apvts.replaceState (tree);

    // Sessions from before the target had a three-way "mode" parameter; its
    // "Both (quantised)" setting carries over as Quantise.
    if (! tree.getChildWithProperty ("id", "quantise").isValid())
    {
        const auto oldMode = tree.getChildWithProperty ("id", "mode");

        if (oldMode.isValid() && (int) (float) oldMode.getProperty ("value", 0.0f) == (int) EngineMode::Both)
            if (auto* q = apvts.getParameter ("quantise"))
                q->setValueNotifyingHost (1.0f);
    }

    engine.getClassifier().clearAll();

    // Older sessions saved no target: nothing attached until a check finds one.
    probe.cancel();
    target.store ((int) InstrumentTarget::None);

    // Older sessions have no HUMLINE node: start from every note on, no custom chords.
    noteMask.store ((int) allNotesMask);

    for (auto& o : chordOverrides)
        o.store ((int) ChordQuality::Preset);

    std::vector<HistoryNote> restoredTake;

    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);

        if (child.hasType ("PADS"))
        {
            for (int j = 0; j < child.getNumChildren(); ++j)
            {
                const auto pad = child.getChild (j);
                const int index = (int) pad.getProperty ("index", j);

                if (index < 0 || index >= numDrumPads)
                    continue;

                padNotes[index].store ((int) pad.getProperty ("note", 36));
                padMutes[index].store ((bool) pad.getProperty ("muted", false));
                padVelocity[index].store ((bool) pad.getProperty ("velocity", true));

                const juce::String n = pad.getProperty ("name", "").toString();

                if (n.isNotEmpty())
                    padNames[index] = n;
            }
        }
        else if (child.hasType ("TEMPLATES"))
        {
            for (int j = 0; j < child.getNumChildren(); ++j)
            {
                const auto t = child.getChild (j);
                const int index = (int) t.getProperty ("index", -1);

                if (index < 0 || index >= numDrumPads)
                    continue;

                DrumFeatures f;
                f.low     = (float) t.getProperty ("low", 0.0);
                f.f1      = (float) t.getProperty ("f1", 0.0);
                f.f2      = (float) t.getProperty ("f2", 0.0);
                f.high    = (float) t.getProperty ("high", 0.0);
                f.zcr     = (float) t.getProperty ("zcr", 0.0);
                f.sustain = (float) t.getProperty ("sustain", 0.0);
                f.pitched = (float) t.getProperty ("pitched", 0.0);

                engine.getClassifier().addExample (index, f);
            }
        }
        else if (child.hasType ("HUMLINE"))
        {
            noteMask.store ((int) child.getProperty ("noteMask", (int) allNotesMask) & (int) allNotesMask);

            auto overrides = juce::StringArray::fromTokens (child.getProperty ("chordOverrides", "").toString(),
                                                            ",", {});

            for (int d = 0; d < 12; ++d)
                chordOverrides[d].store (d < overrides.size() ? overrides[d].getIntValue() : 0);

            target.store (juce::jlimit (0, (int) InstrumentTarget::NumTargets - 1,
                                        (int) child.getProperty ("target", (int) InstrumentTarget::None)));
        }
        else if (child.hasType ("TAKE"))
        {
            juce::MemoryBlock packed;
            packed.fromBase64Encoding (child.getProperty ("notes", "").toString());

            juce::MemoryInputStream in (packed, false);
            const int count = (int) child.getProperty ("count", 0);
            constexpr int bytesPerNote = 13;

            for (int j = 0; j < count && restoredTake.size() < TakeRecorder::maxNotes
                            && in.getNumBytesRemaining() >= bytesPerNote; ++j)
            {
                HistoryNote n;
                n.startBeats  = in.readFloat();
                n.lengthBeats = in.readFloat();
                n.note        = juce::jlimit (0, 127, in.readInt());
                n.drum        = in.readByte() != 0;
                restoredTake.push_back (n);
            }

            takeBpm.store (juce::jlimit (20.0, 999.0, (double) child.getProperty ("bpm", 120.0)));
        }
    }

    // Sessions saved before the Notes page have no TAKE node: an empty take.
    {
        const juce::SpinLock::ScopedLockType lock (takeLock);
        take.assign (restoredTake.begin(), restoredTake.end());   // within the reserved capacity
    }

    ++takeVersion;
    ++uiStateVersion;
}

//==============================================================================
juce::AudioProcessorEditor* HumlineProcessor::createEditor()
{
    return new HumlineEditor (*this);
}

} // namespace dbc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dbc::HumlineProcessor();
}
