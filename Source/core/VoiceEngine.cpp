#include "VoiceEngine.h"

namespace dbc
{

void VoiceEngine::prepare (double sr, int /*maxBlockSize*/)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    hop = (int) std::max (64.0, sampleRate * 0.0053);
    hopCountdown = hop;

    pitch.prepare (sampleRate, hop);
    onset.prepare (sampleRate, hop);

    reset();
}

void VoiceEngine::reset()
{
    pitch.reset();
    onset.reset();

    absPos = 0;
    blockStartAbs = 0;
    blockStartPpq = 0.0;
    hopCountdown = hop;

    pendingOnsets.clear();
    pendingEvents.clear();
    drumRegions.clear();

    runActive = false;
    runFrames = 0;
    runCount  = 0;
    runOff    = 0;
    runSum    = 0.0f;
    lastVoiced = 0;
    runPeakLevel = 0.0f;
    runQuietFrames = 0;
    liveReleaseAbs = -1;
    lastOnsetAbs = -1000000;
    lastDrumAbs = -1000000;
    lastDrumEnergy = 0.0f;
    ccCounter = 0;

    for (auto& v : lastCc)
        v = -1000;

    activeOutCount = 0;
    noteOnSent = false;
    bendChans[0] = bendChans[1] = -1;
    lastBend = 8192;
    bendDev = 0.0f;
    live = LiveState();
}

void VoiceEngine::process (const float* input, int numSamples,
                           double startPpq, double tempo,
                           const Settings& s,
                           std::vector<MidiEvent>& midiOut)
{
    midiOut.clear();

    if (numSamples <= 0 || input == nullptr)
        return;

    blockStartAbs = absPos;
    blockStartPpq = startPpq;
    bpm = tempo > 1.0 ? tempo : 120.0;

    latencyFor (s);   // keeps the reported output delay current for this mode

    int i = 0;

    while (i < numSamples)
    {
        const int n = std::min (numSamples - i, hopCountdown);

        for (int k = 0; k < n; ++k)
            onset.pushSample (input[i + k]);

        pitch.pushSamples (input + i, n);

        i += n;
        absPos += n;
        hopCountdown -= n;

        if (hopCountdown <= 0)
        {
            hopCountdown = hop;
            processFrame (s);
        }
    }

    flush (midiOut, numSamples);
}

//==============================================================================
void VoiceEngine::processFrame (const Settings& s)
{
    const int64_t frameEnd = absPos;

    auto of = onset.analyseFrame (s.drumSensitivity);
    auto pf = pitch.analyse();

    updateLive (of, pf, frameEnd);

    const int64_t refractory = (int64_t) (0.10 * sampleRate);

    // Melody mode does not listen for drums at all (so nothing ever waits on a
    // drum decision); training a trigger still works in every mode.
    const bool wantDrums = s.listensForDrums() || training;

    if (of.onset && wantDrums && ! startsInRegion (frameEnd) && frameEnd - lastOnsetAbs > refractory)
        pendingOnsets.push_back ({ frameEnd, of.low, of.f1, of.f2, of.high, of.zcr, pf.clarity, of.total(), 0 });

    if (of.onset && frameEnd - lastOnsetAbs > refractory)
        lastOnsetAbs = frameEnd;   // also anchors sung notes in Melody mode

    // 160 ms is long enough for a real drum to die away but short enough that a
    // held vocal still reads as sustained.
    const int64_t need = (int64_t) (0.16 * sampleRate);

    // Beatbox mode: every hit is a drum, so there is no drum-or-note question to
    // wait 160 ms for. The hit is classified just after its peak instead, and
    // training keeps using the slower, more accurate measurement.
    const bool liveDrums = s.mode == EngineMode::Beatbox && ! training;

    for (size_t idx = 0; idx < pendingOnsets.size();)
    {
        auto& o = pendingOnsets[idx];

        // Track the true peak of the hit: the first frame is only part of the attack,
        // and the peak frame is the most representative one for classification.
        if (of.total() > o.energy)
        {
            o.energy  = of.total();
            o.low     = of.low;
            o.f1      = of.f1;
            o.f2      = of.f2;
            o.high    = of.high;
            o.zcr     = of.zcr;
            o.clarity = std::max (o.clarity, pf.clarity);
            o.sincePeak = 0;
        }
        else
        {
            ++o.sincePeak;
        }

        if (liveDrums && o.sincePeak >= liveDecayFrames)
        {
            finaliseOnset (o, liveFeatures (o, of, std::max (o.clarity, pf.clarity)), s, true);
            pendingOnsets.erase (pendingOnsets.begin() + (long) idx);
        }
        else if (frameEnd - o.abs >= need)
        {
            const float ratio   = of.total() / std::max (o.energy, 1.0e-9f);
            const float sustain = std::clamp ((ratio - 0.08f) / 0.60f, 0.0f, 1.0f);

            // Clarity at the attack frame is often 0 (the pitch window is still
            // empty); by the time we decide, a held note is clearly periodic.
            const float clarity = std::max (o.clarity, pf.clarity);

            const auto f = DrumFeatures::fromRaw (o.low, o.f1, o.f2, o.high, o.zcr, sustain, clarity);
            finaliseOnset (o, f, s, false);

            pendingOnsets.erase (pendingOnsets.begin() + (long) idx);
        }
        else
        {
            ++idx;
        }
    }

    // Melody mode releases a note as soon as the voice level drops away. The
    // pitch window still holds ~85 ms of the note after the singer stops, so
    // waiting for the pitch to vanish would hold every note that much too long.
    if (s.isLive() && runActive)
    {
        runPeakLevel = std::max (runPeakLevel, of.rms);

        if (noteOnSent && of.rms < 0.12f * runPeakLevel)
        {
            if (++runQuietFrames >= 2)
            {
                closeRun (frameEnd, s);
                liveReleaseAbs = frameEnd;
            }
        }
        else
        {
            runQuietFrames = 0;
        }
    }

    updatePitchTracker (frameEnd, pf, s);

    // Vowel / dynamics CC stream (used by the Assign page).
    if (s.sendCC && ++ccCounter >= 8)
    {
        ccCounter = 0;

        const int ch = std::clamp (s.voiceChannel, 1, 16) - 1;
        const float readings[numCcDials] { live.vowelA, live.vowelE, live.vowelO, live.dynamics };
        const int controllers[numCcDials] { s.ccVowelA, s.ccVowelE, s.ccVowelO, s.ccDynamics };
        int values[numCcDials] {};

        // Each dial's input range is stretched over the whole dial, then the
        // dial is mapped onto its CC output span (Assign page).
        for (int i = 0; i < numCcDials; ++i)
            values[i] = ccDialValue (readings[i], s.ccInMin[i], s.ccInMax[i], s.ccOutMin[i], s.ccOutMax[i]);

        for (int i = 0; i < numCcDials; ++i)
        {
            if (std::abs (values[i] - lastCc[i]) > 1)
            {
                lastCc[i] = values[i];
                schedule (absPos, (uint8_t) (0xB0 | ch),
                          (uint8_t) std::clamp (controllers[i], 1, 127),
                          (uint8_t) std::clamp (values[i], 0, 127));
            }
        }
    }
}

DrumFeatures VoiceEngine::liveFeatures (const PendingOnset& o, const OnsetDetector::Frame& now,
                                        float clarity) const noexcept
{
    // The slow path measures how much of the hit is left 160 ms in. Here only a
    // couple of frames have passed since the peak, so the decay so far is
    // extrapolated (as an exponential) to 160 ms: a click has almost gone, a
    // held sound has barely moved.
    const double dt = std::max (1, o.sincePeak) * (double) hop / sampleRate;
    const double ratio = std::clamp ((double) now.total() / std::max ((double) o.energy, 1.0e-9), 1.0e-6, 1.0);
    const double at160 = std::pow (ratio, 0.16 / dt);
    const float sustain = (float) std::clamp ((at160 - 0.08) / 0.60, 0.0, 1.0);

    return DrumFeatures::fromRaw (o.low, o.f1, o.f2, o.high, o.zcr, sustain, clarity);
}

void VoiceEngine::finaliseOnset (const PendingOnset& o, const DrumFeatures& f, const Settings& s, bool liveDecision)
{
    if (startsInRegion (o.abs))
        return;

    // A decaying tail of the previous hit is not a new hit.
    if (lastDrumAbs > 0
        && o.abs - lastDrumAbs < (int64_t) (0.30 * sampleRate)
        && o.energy < 0.35f * lastDrumEnergy)
        return;

    int pad = -1;

    if (training)
    {
        // While learning a trigger every percussive hit is a take for that pad,
        // whether or not the rules would have called it a drum: the whole point
        // of training is to teach the plugin sounds it does not recognise yet.
        if (f.sustain > 0.85f)
            return;

        pad = trainPad;

        // Each take is stored as its own example and counts straight away, so
        // stopping part-way keeps the takes recorded so far.
        classifier.addExample (trainPad, f);
        ++trainCollected;

        if (trainCollected >= trainTakes
            || classifier.numExamples (trainPad) >= DrumClassifier::maxTakes)
        {
            training = false;
            trainCollected = 0;

            if (onTrainingComplete)
                onTrainingComplete();
        }
    }
    else
    {
        // Only percussive material becomes a drum: anything that rings on is a note.
        // The live estimate of the decay is rougher, so it gets more room.
        if (f.sustain > (liveDecision ? 0.75f : 0.55f))
            return;

        pad = classifier.classify (f);

        if (pad < 0)
            pad = DrumClassifier::heuristic (f);

        // In Beatbox mode nothing is a sung note, so a hit the rules would have
        // left to the pitch tracker still becomes the closest basic drum.
        if (pad < 0 && s.mode == EngineMode::Beatbox)
            pad = (f.low >= std::max (f.f2, f.high)) ? 3 : (f.high > f.f2 ? 4 : 1);

        if (pad < 0 || pad >= numDrumPads)
            return;   // that was a sung note: the pitch tracker handles it
    }

    lastDrumAbs = o.abs;
    lastDrumEnergy = o.energy;

    if (s.drumsEnabled && ! s.padMuted[pad])
        emitDrumNote (pad, o.abs, o.energy, s, liveDecision);

    addRegion (o.abs - (int64_t) (0.05 * sampleRate),
               o.abs + (int64_t) (0.09 * sampleRate));
}

//==============================================================================
void VoiceEngine::updatePitchTracker (int64_t frameEnd, const PitchDetector::Frame& pf, const Settings& s)
{
    // Pitch switched off on the Play page: finish the note that is sounding
    // and stay quiet. Triggers keep working.
    if (! s.pitchEnabled || ! s.listensForPitch())
    {
        if (runActive)
            closeRun (std::max (lastVoiced, runStart), s);

        return;
    }

    const bool voiced = pf.clarity > 0.60f && pf.rms > 1.0e-4f && pf.hz > 30.0f;

    if (voiced)
    {
        const float midi = 69.0f + 12.0f * std::log2 (pf.hz / 440.0f);
        const int64_t prevVoiced = lastVoiced;
        lastVoiced = frameEnd;

        // After a live release the pitch window still holds the old note for a
        // moment: do not start it again from that tail. A new attack, or the
        // window having moved past the release, ends the hold.
        const int64_t tail = (int64_t) PitchDetector::windowSize * PitchDetector::decimation;
        const bool inReleaseTail = s.isLive() && liveReleaseAbs > 0
                                   && frameEnd - liveReleaseAbs < tail && lastOnsetAbs <= liveReleaseAbs;

        if (! runActive && inReleaseTail)
        {
            // stay quiet
        }
        else if (! runActive)
        {
            // Anchor the note to the detected attack when there was one, so the
            // pitch window's own delay does not smear the timing.
            const int64_t anchorWindow = (int64_t) (0.12 * sampleRate);
            const int64_t anchor = (lastOnsetAbs > 0 && frameEnd - lastOnsetAbs <= anchorWindow)
                                       ? lastOnsetAbs : frameEnd;
            startRun (midi, anchor);
        }
        else
        {
            // Stickiness widens the band that still counts as the same note, so a
            // higher setting holds the note through slides and vibrato.
            float threshold = 0.80f + 0.90f * std::clamp (s.stickyness, 0.0f, 1.0f);

            // With pitch bend on, the voice bends the sounding note and only
            // moves to a new note once it has gone further than the bend range.
            const bool bending = s.bendEnabled && noteOnSent;

            if (bending)
                threshold = std::max (threshold, (float) std::clamp (s.bendRange, 1, 24) - 0.5f);

            if (std::abs (midi - runNote) > threshold)
            {
                if (++runOff >= 2)
                {
                    closeRun (prevVoiced, s);
                    startRun (midi, frameEnd);
                }
            }
            else
            {
                runOff  = 0;
                ++runFrames;
                runVelocity = std::max (runVelocity, std::min (1.0f, pf.rms * 6.0f));

                // While bending, the note the bend is measured from stays put;
                // otherwise a slow slide would drag it along and never change note.
                if (! bending)
                {
                    runSum += midi;
                    ++runCount;
                    runNote = runSum / (float) runCount;
                }

                if (! noteOnSent && runFrames >= (s.isLive() ? liveStableFramesFor (s) : stableFramesFor (s)))
                    confirmRun (frameEnd, s);
                else if (bending && std::min (1.0f, pf.rms * 6.0f) > 0.5f * runVelocity)
                    updateBend (frameEnd, midi, s);   // not while the note fades: the pitch is unreliable there
            }
        }
    }
    else if (runActive && (frameEnd - lastVoiced) > (int64_t) (0.05 * sampleRate))
    {
        closeRun (lastVoiced, s);
    }
}

void VoiceEngine::startRun (float midi, int64_t at)
{
    runActive   = true;
    runStart    = at;
    runSum      = midi;
    runCount    = 1;
    runNote     = midi;
    runFrames   = 1;
    runOff      = 0;
    runVelocity = 0.0f;
    noteOnSent  = false;
    runPeakLevel = 0.0f;
    runQuietFrames = 0;
}

int VoiceEngine::stableFramesFor (const Settings& s) noexcept
{
    // ... and makes a new note wait longer before it is confirmed, which is the
    // other half of what the Stickiness control means.
    return 2 + (int) std::lround (std::clamp (s.stickyness, 0.0f, 1.0f) * 4.0f);
}

int VoiceEngine::liveStableFramesFor (const Settings& s) noexcept
{
    // Melody mode: about 11-21 ms of steady pitch, so the note sounds while it
    // is still being sung but a passing slide does not trigger one.
    return 2 + (int) std::lround (std::clamp (s.stickyness, 0.0f, 1.0f) * 2.0f);
}

void VoiceEngine::confirmRun (int64_t frameEnd, const Settings& s)
{
    if (! runActive || noteOnSent)
        return;

    const bool liveNote = s.isLive();

    // Wait until every attack has been resolved as drum or note, otherwise a
    // beatboxed kick can leak out as a very low sung note first. Melody mode
    // does not listen for drums, so it has nothing to wait for.
    if (! liveNote && ! pendingOnsets.empty())
        return;

    const int64_t minSamples = (int64_t) ((liveNote ? 0.025 : 0.090) * sampleRate);

    if (frameEnd - runStart < minSamples)
        return;

    if (startsInRegion (runStart))
        return;

    const float pitch = runCount > 0 ? runSum / (float) runCount : runNote;
    const float vel   = std::max (0.35f, runVelocity);

    NoteSegment seg;
    seg.startBeats = ppqFromAbs (liveNote ? frameEnd : runStart);
    seg.endBeats   = seg.startBeats + std::max (0.05, (double) s.minLengthBeats);
    seg.pitch      = pitch;
    seg.velocity   = std::clamp (vel * s.velocityAmount + 0.8f * (1.0f - s.velocityAmount), 0.05f, 1.0f);

    // Live: the pitch is still pulled into the key, but the note starts now
    // instead of waiting for a grid line.
    Settings noGrid;

    if (liveNote)
    {
        noGrid = s;
        noGrid.grid = SnapGrid::Off;
    }

    const auto q = Quantizer::quantize (seg, liveNote ? noGrid : s);

    activeStartBeats = q.startBeats;
    activeOnAbs      = liveNote ? frameEnd : absFromPpq (q.startBeats) + latencyFor (s);
    activeMelody     = q.note;

    const int voiceCh = std::clamp (s.voiceChannel, 1, 16) - 1;

    activeOutCount = 0;

    const auto addOut = [this] (int note, int channel)
    {
        if (activeOutCount < maxOutNotes && note >= 0 && note <= 127)
        {
            activeOutNotes[activeOutCount] = note;
            activeOutChans[activeOutCount] = channel;
            ++activeOutCount;
        }
    };

    // --- chords ------------------------------------------------------------
    // A single sung note becomes the chord mapped to it in the key; every voice
    // starts on the same sample so the MIDI out reads as a chord track. With a
    // chord channel of its own, the melody stays on the voice channel and the
    // chord goes out alongside it.
    bool chordOwnChannel = false;
    int  chordCh = voiceCh;

    if (s.chordsEnabled)
    {
        if (s.chordChannel >= 1 && s.chordChannel <= 16 && s.chordChannel - 1 != voiceCh)
        {
            chordOwnChannel = true;
            chordCh = s.chordChannel - 1;
            addOut (q.note, voiceCh);
        }

        const auto chord = chordForNote (q.note, s.keyState(), ChordOptions::from (s));

        for (int i = 0; i < chord.count; ++i)
            addOut (chord.notes[(size_t) i], chordCh);
    }

    if (activeOutCount == 0)
        addOut (q.note, voiceCh);

    // --- pitch bend channels -----------------------------------------------
    bendChans[0] = bendChans[1] = -1;
    bendDev = 0.0f;

    if (s.bendEnabled)
    {
        if (! s.chordsEnabled)
        {
            bendChans[0] = voiceCh;
        }
        else if (chordOwnChannel)
        {
            bendChans[0] = voiceCh;

            if (s.bendChords)
                bendChans[1] = chordCh;
        }
        else if (s.bendChords)
        {
            bendChans[0] = voiceCh;   // chord and melody share the channel
        }
    }

    const auto velocity = (uint8_t) std::clamp ((int) (q.velocity * 127.0f), 1, 127);

    for (int i = 0; i < activeOutCount; ++i)
        schedule (activeOnAbs, (uint8_t) (0x90 | activeOutChans[i]),
                  (uint8_t) activeOutNotes[i], velocity);

    noteOnSent = true;
    live.activeNote = q.note;
}

void VoiceEngine::closeRun (int64_t endAbs, const Settings& s)
{
    if (! runActive)
        return;

    if (noteOnSent && s.isLive())
    {
        // Live: the note stops when the singing stops (with a few ms minimum so
        // a blip still sounds), not on the grid.
        const int64_t offAbs = std::max (endAbs, activeOnAbs + (int64_t) (0.03 * sampleRate));
        const double endBeats = ppqFromAbs (offAbs);

        for (int i = 0; i < activeOutCount; ++i)
        {
            schedule (offAbs, (uint8_t) (0x80 | activeOutChans[i]), (uint8_t) activeOutNotes[i], 64);
            pushHistory (activeStartBeats, endBeats - activeStartBeats, activeOutNotes[i], false);
        }

        if (bendChans[0] >= 0)
        {
            dropBendsAfter (offAbs);
            sendBend (offAbs, 8192);
        }

        bendChans[0] = bendChans[1] = -1;
        activeOutCount = 0;
        noteOnSent = false;
    }
    else if (noteOnSent)
    {
        const double minLen = std::max (0.01, (double) s.minLengthBeats);
        const double g      = gridBeats (s.grid);

        double endBeats = Quantizer::snapTime (ppqFromAbs (endAbs), s);

        if (g > 0.0 && s.timingStrength >= 0.999f)
        {
            const double len = std::max (g, std::round ((endBeats - activeStartBeats) / g) * g);
            endBeats = activeStartBeats + len;
        }

        if (endBeats - activeStartBeats < minLen)
            endBeats = activeStartBeats + minLen;

        const int64_t lat = latencyFor (s);
        int64_t offAbs = absFromPpq (endBeats) + lat;
        const int64_t minOff = activeOnAbs + (int64_t) std::max (16.0, 0.05 * sampleRate);

        if (offAbs < minOff)
            offAbs = minOff;

        for (int i = 0; i < activeOutCount; ++i)
        {
            schedule (offAbs, (uint8_t) (0x80 | activeOutChans[i]),
                      (uint8_t) activeOutNotes[i], 64);
            pushHistory (activeStartBeats, endBeats - activeStartBeats, activeOutNotes[i], false);
        }

        // Re-centre the bend with the note-off, and drop any bend that was
        // queued for after it, so the next note starts in tune.
        if (bendChans[0] >= 0)
        {
            dropBendsAfter (offAbs);
            sendBend (offAbs, 8192);
        }

        bendChans[0] = bendChans[1] = -1;
        activeOutCount = 0;
        noteOnSent = false;
    }

    runActive = false;
    runFrames = 0;
    runCount  = 0;
    runOff    = 0;
    runSum    = 0.0f;
}

//==============================================================================
void VoiceEngine::updateBend (int64_t frameEnd, float sungMidi, const Settings& s)
{
    if (! s.bendEnabled || ! noteOnSent || bendChans[0] < 0)
        return;

    const float range = (float) std::clamp (s.bendRange, 1, 24);

    // How far the voice is from the note that is sounding.
    float dev = sungMidi + 12.0f * (float) s.octaveShift - (float) activeMelody;
    bendDev = 0.5f * bendDev + 0.5f * dev;
    dev = bendDev;

    if (s.bendMode == BendMode::IntelliBend)
    {
        // Close to the note the bend is switched off, so a held note stays
        // anchored in tune; Stickiness sets how wide that anchor is. Past it,
        // the bend fades in over half a semitone so there is no jump.
        const float anchor = 0.25f + 0.75f * std::clamp (s.stickyness, 0.0f, 1.0f);
        dev *= std::clamp ((std::abs (dev) - anchor) / 0.5f, 0.0f, 1.0f);
    }

    dev = std::clamp (dev, -range, range);

    const int value = std::clamp (8192 + (int) std::lround (dev / range * 8191.0f), 0, 16383);

    // About 3 cents of change before another message goes out.
    const int step = std::max (8, (int) (8191.0f * 0.03f / range));

    if (std::abs (value - lastBend) < step)
        return;

    sendBend (std::max (frameEnd + latencyFor (s), activeOnAbs), value);
}

void VoiceEngine::sendBend (int64_t abs, int value14)
{
    value14 = std::clamp (value14, 0, 16383);

    for (int ch : bendChans)
        if (ch >= 0)
            schedule (abs, (uint8_t) (0xE0 | ch), (uint8_t) (value14 & 0x7F), (uint8_t) (value14 >> 7));

    lastBend = value14;
}

void VoiceEngine::dropBendsAfter (int64_t abs)
{
    pendingEvents.erase (std::remove_if (pendingEvents.begin(), pendingEvents.end(),
                                         [abs] (const PendingEvent& e)
                                         {
                                             return (e.status & 0xF0) == 0xE0 && e.abs > abs;
                                         }),
                         pendingEvents.end());
}

//==============================================================================
void VoiceEngine::emitDrumNote (int pad, int64_t onsetAbs, float energy, const Settings& s, bool liveDecision)
{
    const double beat = ppqFromAbs (onsetAbs);

    // Live: the hit goes out now, where it was detected. Quantised: it goes on
    // the grid, a constant delay later.
    const double snapped = (liveDecision || s.isLive()) ? beat : Quantizer::snapTime (beat, s);
    const bool now = liveDecision || s.isLive();

    const int64_t onAbs = now ? absPos : absFromPpq (snapped);

    double lenBeats = now ? 0.125 : std::max (gridBeats (s.grid), 0.125);
    const int64_t offAbs = onAbs + (int64_t) std::llround (lenBeats * 60.0 / bpm * sampleRate);

    const int channel = s.drumsOnDrumChan ? s.drumChannel : s.voiceChannel;
    const int ch = std::clamp (channel, 1, 16) - 1;

    // Velocity off for this trigger: every hit is full velocity.
    const float dyn = std::clamp (energy * 12.0f, 0.0f, 1.0f);
    const float vel = s.padVelocity[pad]
                          ? std::clamp (dyn * s.velocityAmount + 0.85f * (1.0f - s.velocityAmount), 0.1f, 1.0f)
                          : 1.0f;

    const int note = std::clamp (s.padNote[pad], 0, 127);

    const int64_t lat = latencyFor (s);

    schedule (onAbs + lat,  (uint8_t) (0x90 | ch), (uint8_t) note,
              (uint8_t) std::clamp ((int) (vel * 127.0f), 1, 127), true);
    schedule (offAbs + lat, (uint8_t) (0x80 | ch), (uint8_t) note, 64, true);

    pushHistory (snapped, lenBeats, note, true);

    live.padFlash[pad] = 1.0f;
    live.lastPad = pad;
}

//==============================================================================
void VoiceEngine::addRegion (int64_t start, int64_t end)
{
    drumRegions.push_back ({ start, end });

    if (drumRegions.size() > 12)
        drumRegions.erase (drumRegions.begin());
}

bool VoiceEngine::startsInRegion (int64_t abs) const
{
    for (const auto& r : drumRegions)
        if (abs >= r.start && abs <= r.end)
            return true;

    return false;
}

//==============================================================================
int64_t VoiceEngine::latencyFor (const Settings& s) noexcept
{
    if (s.isLive())
    {
        lastLatencySamples = 0;
        return 0;
    }

    const double halfGrid = gridBeats (s.grid) * 60.0 / bpm * 0.5;
    const double seconds  = std::clamp (0.18 + halfGrid, 0.22, 0.50);

    lastLatencySamples = (int64_t) std::llround (seconds * sampleRate);
    return lastLatencySamples;
}

double VoiceEngine::ppqFromAbs (int64_t abs) const noexcept
{
    return blockStartPpq + (double) (abs - blockStartAbs) * bpm / 60.0 / sampleRate;
}

int64_t VoiceEngine::absFromPpq (double ppq) const noexcept
{
    return blockStartAbs + (int64_t) std::llround ((ppq - blockStartPpq) * 60.0 / bpm * sampleRate);
}

void VoiceEngine::schedule (int64_t abs, uint8_t status, uint8_t d1, uint8_t d2, bool drum)
{
    PendingEvent e { abs, status, d1, d2, drum };

    auto it = std::upper_bound (pendingEvents.begin(), pendingEvents.end(), e,
                                [] (const PendingEvent& a, const PendingEvent& b)
                                {
                                    return a.abs < b.abs;
                                });

    pendingEvents.insert (it, e);
}

void VoiceEngine::flush (std::vector<MidiEvent>& out, int numSamples)
{
    const int64_t blockEnd = blockStartAbs + numSamples;
    size_t count = 0;

    while (count < pendingEvents.size() && pendingEvents[count].abs < blockEnd)
    {
        const auto& e = pendingEvents[count];

        const int64_t rel = e.abs - blockStartAbs;
        const int offset = (int) std::clamp<int64_t> (rel, 0, (int64_t) numSamples - 1);

        out.push_back ({ offset, e.status, e.d1, e.d2, e.drum });
        ++count;
    }

    if (count > 0)
        pendingEvents.erase (pendingEvents.begin(), pendingEvents.begin() + (long) count);
}

void VoiceEngine::pushHistory (double startBeats, double endBeats, int note, bool drum)
{
    auto& h = live.history[live.historyWrite];
    h.startBeats  = (float) startBeats;
    h.lengthBeats = (float) std::max (0.05, endBeats - startBeats);
    h.note        = note;
    h.drum        = drum;

    live.historyWrite = (live.historyWrite + 1) % LiveState::historySize;
    live.historyCount = std::min (live.historyCount + 1, LiveState::historySize);
    ++live.historySeq;
}

//==============================================================================
void VoiceEngine::updateLive (const OnsetDetector::Frame& of, const PitchDetector::Frame& pf, int64_t frameEnd)
{
    if (keyClearRequested.exchange (false))
        for (auto& v : keyHist)
            v.store (0.0f, std::memory_order_relaxed);

    live.inputLevel = of.rms;
    live.inputPeak  = std::max (live.inputPeak * 0.9995f, of.rms);
    live.hz         = pf.hz;
    live.clarity    = pf.clarity;

    for (int i = 0; i < 12; ++i)
        live.pitchClassEnergy[i] *= 0.90f;

    const bool voiced = pf.clarity > 0.60f && pf.hz > 30.0f;

    if (voiced)
    {
        const float midi = 69.0f + 12.0f * std::log2 (pf.hz / 440.0f);
        live.midiFloat = midi;
        live.cents     = (midi - std::round (midi)) * 100.0f;

        int pc = (int) std::lround (midi) % 12;

        if (pc < 0)
            pc += 12;

        live.pitchClass = pc;
        live.pitchClassEnergy[pc] = std::min (1.0f, live.pitchClassEnergy[pc] + pf.clarity);

        // Key detection uses the raw pitch, before any snapping to the current
        // key, otherwise the detector would only ever confirm the key it is in.
        // Frames inside a beatboxed hit are left out, and a note only counts once
        // it is close to a semitone (slides between notes add little).
        if (keyListening.load (std::memory_order_relaxed) && ! startsInRegion (frameEnd)
            && pf.rms > 2.0e-3f && std::abs (live.cents) < 35.0f)
        {
            const float w = pf.clarity * std::min (1.0f, pf.rms * 20.0f);
            keyHist[pc].store (keyHist[pc].load (std::memory_order_relaxed) + w, std::memory_order_relaxed);
        }
    }

    live.voiced = voiced;

    for (int i = 0; i < numDrumPads; ++i)
        live.padFlash[i] *= 0.90f;

    const float total = of.total() + 1.0e-9f;
    const float a = of.f1 / total;
    const float e = of.f2 / total;
    const float o = of.low / total;
    const float sum = a + e + o + 1.0e-6f;

    live.vowelA = 0.75f * live.vowelA + 0.25f * (a / sum);
    live.vowelE = 0.75f * live.vowelE + 0.25f * (e / sum);
    live.vowelO = 0.75f * live.vowelO + 0.25f * (o / sum);
    live.dynamics = 0.8f * live.dynamics + 0.2f * std::min (1.0f, of.rms * 5.0f);

    const double ppq = ppqFromAbs (frameEnd);
    live.beatPhase = (float) (ppq - std::floor (ppq));
    live.barPhase  = (float) (ppq * 0.25 - std::floor (ppq * 0.25));
}

std::array<float, 12> VoiceEngine::getKeyHistogram() const noexcept
{
    std::array<float, 12> h {};

    for (int i = 0; i < 12; ++i)
        h[(size_t) i] = keyHist[i].load (std::memory_order_relaxed);

    return h;
}

//==============================================================================
void VoiceEngine::startTraining (int pad, int takes)
{
    training = true;
    trainPad = std::clamp (pad, 0, numDrumPads - 1);
    trainTakes = std::max (1, takes);
    trainCollected = 0;
}

void VoiceEngine::cancelTraining() noexcept
{
    training = false;
    trainCollected = 0;
}

} // namespace dbc
