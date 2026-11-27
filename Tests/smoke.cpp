// Console smoke test: builds the plugin and its editor without a DAW, then
// pushes synthetic audio through processBlock and checks that MIDI comes out.

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::fprintf (stderr, "[1] JUCE initialised\n");

    dbc::HumlineProcessor proc;
    std::fprintf (stderr, "[2] processor constructed (name=%s)\n", proc.getName().toRawUTF8());

    proc.prepareToPlay (48000.0, 512);
    std::fprintf (stderr, "[3] prepared\n");

    // A short sung A3. Nothing is attached yet, so no notes go out.
    const auto countNoteOns = [] (const juce::MidiBuffer& m)
    {
        int n = 0;

        for (const auto meta : m)
            n += meta.getMessage().isNoteOn() ? 1 : 0;

        return n;
    };

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    int events = 0;
    int samplesSeen = 0;
    float outputPeak = 0.0f;

    const auto singA3 = [&] (int blocks)
    {
        int notes = 0;
        samplesSeen = 0;

        for (int block = 0; block < blocks; ++block)
        {
            buffer.clear();

            for (int i = 0; i < 512; ++i)
            {
                const double t = (double) (samplesSeen + i) / 48000.0;
                const double env = (t > 0.1 && t < 0.9) ? std::min (1.0, std::min ((t - 0.1) / 0.01, (0.9 - t) / 0.02)) : 0.0;
                const float s = (float) (0.35 * std::max (0.0, env) * std::sin (2.0 * 3.14159265358979 * 220.0 * t));
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            midi.clear();
            proc.processBlock (buffer, midi);
            notes += countNoteOns (midi);
            samplesSeen += 512;
        }

        return notes;
    };

    const int notesWithNothing = singA3 (120);
    proc.engine.reset();
    std::fprintf (stderr, "[3a] nothing attached: %d note(s) out (want 0), target %s\n",
                  notesWithNothing, dbc::instrumentTargetName (proc.getTarget()));

    if (notesWithNothing != 0 || proc.getTarget() != dbc::InstrumentTarget::None)
        return 3;

    proc.clearTake();
    proc.setTarget (dbc::InstrumentTarget::Instrument);
    samplesSeen = 0;

    for (int block = 0; block < 120; ++block)
    {
        buffer.clear();

        for (int i = 0; i < 512; ++i)
        {
            const double t = (double) (samplesSeen + i) / 48000.0;
            const double env = (t > 0.1 && t < 0.9) ? std::min (1.0, std::min ((t - 0.1) / 0.01, (0.9 - t) / 0.02)) : 0.0;
            const float s = (float) (0.35 * std::max (0.0, env) * std::sin (2.0 * 3.14159265358979 * 220.0 * t));
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }

        midi.clear();
        proc.processBlock (buffer, midi);
        events += countNoteOns (midi);
        samplesSeen += 512;
        outputPeak = std::max (outputPeak, buffer.getMagnitude (0, 0, 512));

        if (block == 0)
            std::fprintf (stderr, "[4] processBlock ran\n");
    }

    std::fprintf (stderr, "[5] instrument attached: %d samples, %d note(s) out, output peak %.3g (want silence)\n",
                  samplesSeen, events, outputPeak);

    if (events == 0 || outputPeak > 1.0e-6f)
        return 4;

    // A pad click sends its drum note to the attached kit (no sound of its own).
    {
        proc.setTarget (dbc::InstrumentTarget::DrumKit);
        proc.previewPad (0);
        int on = -1, off = -1;

        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);

            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();

                if (m.isNoteOn() && m.getChannel() == 10)  on  = m.getNoteNumber();
                if (m.isNoteOff() && m.getChannel() == 10) off = m.getNoteNumber();
            }
        }

        std::fprintf (stderr, "[5a] pad preview: note-on %d, note-off %d on channel 10 (want %d)\n",
                      on, off, proc.getPadNote (0));

        if (on != proc.getPadNote (0) || off != on)
            return 4;
    }

    // The instrument check through the real processor: a fake synth on the
    // "Instrument" input answers the test notes, and the target becomes Instrument.
    {
        dbc::HumlineProcessor checker;
        checker.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> io (4, 512);   // main in 0-1, instrument in 2-3
        juce::MidiBuffer m;
        std::vector<int> held;
        double phase = 0.0;
        int testNotes = 0;

        checker.startInstrumentCheck();

        for (int block = 0; block < 260 && checker.isCheckingInstrument(); ++block)
        {
            io.clear();

            // The synth plays whatever the last block's MIDI left held (a saw per note).
            for (int i = 0; i < 512; ++i)
            {
                float s = 0.0f;

                for (int note : held)
                {
                    const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                    s += (float) (0.3 * (2.0 * std::fmod (phase * f, 1.0) - 1.0));
                }

                phase += 1.0 / 48000.0;
                io.setSample (2, i, s);
                io.setSample (3, i, s);
            }

            m.clear();
            checker.processBlock (io, m);

            for (const auto meta : m)
            {
                const auto msg = meta.getMessage();

                if (msg.getChannel() != 1)
                    continue;

                if (msg.isNoteOn())
                {
                    held.push_back (msg.getNoteNumber());
                    ++testNotes;
                }
                else if (msg.isNoteOff())
                {
                    held.erase (std::remove (held.begin(), held.end(), msg.getNoteNumber()), held.end());
                }
            }

            // The analysis runs on the message thread.
            if (! checker.isCheckingInstrument() || block % 20 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }

        for (int i = 0; i < 20 && checker.getTarget() == dbc::InstrumentTarget::None; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        const auto& last = checker.getLastInstrumentCheck();
        std::fprintf (stderr, "[5b] instrument check: %d test note(s), %d check(s) done, input %s, target %s (%s)\n",
                      testNotes, last.count, checker.hasInstrumentInput() ? "connected" : "missing",
                      dbc::instrumentTargetName (checker.getTarget()), last.result.reason.c_str());

        if (testNotes != 3 || checker.getTarget() != dbc::InstrumentTarget::Instrument || last.result.heardOnMainInput)
            return 4;

        // The target is saved with the project.
        juce::MemoryBlock state;
        checker.getStateInformation (state);
        dbc::HumlineProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        std::fprintf (stderr, "[5c] target after save / load: %s\n", dbc::instrumentTargetName (restored.getTarget()));

        if (restored.getTarget() != dbc::InstrumentTarget::Instrument)
            return 4;
    }

    proc.setTarget (dbc::InstrumentTarget::Instrument);

    // Regression: a combo attached to a choice parameter must contain the choices.
    {
        dbc::ui::LabelledCombo combo ("Scale");
        combo.attach (proc.apvts, "scale");

        std::fprintf (stderr, "[6] scale combo: %d item(s), first = %s\n",
                      combo.box.getNumItems(), combo.box.getItemText (0).toRawUTF8());

        if (combo.box.getNumItems() != (int) dbc::ScaleType::NumScales)
            return 2;
    }

    // Settings that live outside the parameters survive a save / load.
    {
        proc.setNoteMask (0xFFF & ~(1 << 4));                        // E switched off
        proc.setChordOverride (1, dbc::ChordQuality::Major);         // II instead of ii
        proc.setPadVelocity (3, false);

        dbc::DrumFeatures f;
        f.low = 0.7f;

        for (int i = 0; i < 5; ++i)
        {
            f.f1 = 0.01f * (float) i;
            proc.engine.getClassifier().addExample (2, f);
        }

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        dbc::HumlineProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        const bool ok = restored.getNoteMask() == (0xFFF & ~(1 << 4))
                     && restored.getChordOverride (1) == dbc::ChordQuality::Major
                     && restored.getChordOverride (0) == dbc::ChordQuality::Preset
                     && ! restored.getPadVelocity (3) && restored.getPadVelocity (0)
                     && restored.engine.getClassifier().numExamples (2) == 5
                     && std::abs (restored.engine.getClassifier().getExample (2, 4).f1 - 0.04f) < 1.0e-6f;

        std::fprintf (stderr, "[6a] note mask, custom chords, trigger velocity and takes round-trip: %s\n",
                      ok ? "yes" : "NO");

        if (! ok)
            return 5;

        proc.setNoteMask (0xFFF);
        proc.setChordOverride (1, dbc::ChordQuality::Preset);
        proc.setPadVelocity (3, true);
        proc.engine.getClassifier().clearTemplate (2);
    }

    // The take: the sung notes were captured, export as a readable .mid, and
    // survive a save / load; Clear empties it.
    {
        const auto take = proc.getTake();
        const auto expected = dbc::layoutTakeForExport (take, dbc::TakeFilter::All, 960);

        juce::MemoryOutputStream data;
        const bool written = proc.writeTakeMidi (data, dbc::TakeFilter::All);

        juce::MidiFile file;
        juce::MemoryInputStream in (data.getData(), data.getDataSize(), false);
        const bool read = written && file.readFrom (in);

        int ons = 0, offs = 0, tempos = 0;

        for (int t = 0; read && t < file.getNumTracks(); ++t)
        {
            for (const auto* e : *file.getTrack (t))
            {
                ons    += e->message.isNoteOn() ? 1 : 0;
                offs   += e->message.isNoteOff() ? 1 : 0;
                tempos += e->message.isTempoMetaEvent() ? 1 : 0;
            }
        }

        const auto name = proc.takeFileName (dbc::TakeFilter::All);
        const auto dragFile = proc.writeTakeToTempFile (dbc::TakeFilter::All);

        std::fprintf (stderr, "[6b] take: %d note(s); .mid %d bytes, %d track(s), %d on / %d off, %d tempo; file %s\n",
                      (int) take.size(), (int) data.getDataSize(), file.getNumTracks(), ons, offs, tempos,
                      dragFile.getFullPathName().toRawUTF8());

        const bool exportOk = ! take.empty() && read && file.getTimeFormat() == 960
                           && ons == (int) expected.size() && offs == ons && tempos == 1
                           && name.startsWith ("Humline take ") && name.endsWith (".mid")
                           && dragFile.existsAsFile() && dragFile.getSize() == (juce::int64) data.getDataSize();

        juce::MemoryBlock state;
        proc.getStateInformation (state);

        dbc::HumlineProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        const auto back = restored.getTake();

        bool sameTake = back.size() == take.size();

        for (size_t i = 0; sameTake && i < take.size(); ++i)
            sameTake = back[i].startBeats == take[i].startBeats && back[i].lengthBeats == take[i].lengthBeats
                    && back[i].note == take[i].note && back[i].drum == take[i].drum;

        restored.clearTake();
        const bool cleared = restored.getTake().empty()
                          && ! restored.writeTakeMidi (data, dbc::TakeFilter::All)
                          && restored.writeTakeToTempFile (dbc::TakeFilter::All) == juce::File();

        std::fprintf (stderr, "[6c] take exports: %s, round-trips: %s, clears: %s\n",
                      exportOk ? "yes" : "NO", sameTake ? "yes" : "NO", cleared ? "yes" : "NO");

        if (! exportOk || ! sameTake || ! cleared)
            return 6;
    }

    // Pass Input: off replaces the input, on leaves it intact; the standalone app ignores it.
    {
        const auto runQuiet = [] (dbc::HumlineProcessor& p, bool pass) -> float
        {
            if (auto* param = dynamic_cast<juce::AudioParameterBool*> (p.apvts.getParameter ("passInput")))
                *param = pass;

            p.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer m;
            float worst = 0.0f;

            // Too quiet to trigger anything, so the only output is what passes through.
            for (int block = 0; block < 20; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        buf.setSample (ch, i, 0.002f);

                m.clear();
                p.processBlock (buf, m);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        worst = std::max (worst, std::abs (buf.getSample (ch, i) - (pass ? 0.002f : 0.0f)));
            }

            p.releaseResources();
            return worst;
        };

        dbc::HumlineProcessor plugin;
        plugin.setTarget (dbc::InstrumentTarget::Instrument);
        const float offError = runQuiet (plugin, false);
        const float onError  = runQuiet (plugin, true);

        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        dbc::HumlineProcessor app;
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        const float appError = runQuiet (app, false);   // expects silence...
        const float appPass  = [&]
        {
            // ...and still silence with Pass Input on.
            if (auto* param = dynamic_cast<juce::AudioParameterBool*> (app.apvts.getParameter ("passInput")))
                *param = true;

            app.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer m;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, 0.002f);

            app.processBlock (buf, m);
            return buf.getMagnitude (0, 512);
        }();

        const bool passOk = offError < 1.0e-6f && onError < 1.0e-6f && appError < 1.0e-6f && appPass < 1.0e-6f;

        std::fprintf (stderr, "[6d] pass input: off err %.2g, on err %.2g, standalone %.2g / %.2g -> %s\n",
                      offError, onError, appError, appPass, passOk ? "ok" : "WRONG");

        if (! passOk)
            return 7;
    }

    if (auto* editor = proc.createEditor())
    {
        std::fprintf (stderr, "[7] editor constructed (%d x %d)\n", editor->getWidth(), editor->getHeight());
        editor->setSize (1180, 740);

        // Optional: render every page to PNG for a visual check of the layout.
        const auto snapDir = juce::SystemStats::getEnvironmentVariable ("HUMLINE_SNAPSHOT_DIR", {});

        if (snapDir.isNotEmpty())
        {
            for (auto* child : editor->getChildren())
            {
                if (auto* tabBar = dynamic_cast<dbc::ui::TabBar*> (child))
                {
                    const char* pageNames[] = { "play", "train", "key-empty", "chords", "notes", "assign" };

                    for (int page = 0; page < 6; ++page)
                    {
                        tabBar->setSelected (page, true);
                        const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
                        juce::File file (snapDir + "/" + pageNames[page] + ".png");
                        file.deleteFile();
                        juce::FileOutputStream out (file);
                        juce::PNGImageFormat().writeImageToStream (image, out);
                    }

                    // Key page with notes clicked on the wheel: selection + suggestions.
                    tabBar->setSelected (2, true);

                    for (auto* pageComp : editor->getChildren())
                        if (auto* keyPage = dynamic_cast<dbc::ui::KeyPanel*> (pageComp))
                            for (auto* c : keyPage->getChildren())
                                if (auto* dial = dynamic_cast<dbc::ui::PitchDial*> (c))
                                    for (int pc : { 0, 2, 4, 7, 9 })
                                        dial->onNoteClicked (pc);

                    const auto keyImage = editor->createComponentSnapshot (editor->getLocalBounds());
                    juce::File keyFile (snapDir + "/key.png");
                    keyFile.deleteFile();
                    juce::FileOutputStream keyOut (keyFile);
                    juce::PNGImageFormat().writeImageToStream (keyImage, keyOut);

                    // The "add a synth" screen, shown while nothing is attached.
                    proc.setTarget (dbc::InstrumentTarget::None);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

                    const auto noneImage = editor->createComponentSnapshot (editor->getLocalBounds());
                    juce::File noneFile (snapDir + "/no-instrument.png");
                    noneFile.deleteFile();
                    juce::FileOutputStream noneOut (noneFile);
                    juce::PNGImageFormat().writeImageToStream (noneImage, noneOut);

                    std::fprintf (stderr, "[7a] page snapshots written to %s\n", snapDir.toRawUTF8());
                }
            }
        }

        delete editor;
    }

    proc.releaseResources();
    std::fprintf (stderr, "[8] done\n");

    return events > 0 ? 0 : 1;
}
