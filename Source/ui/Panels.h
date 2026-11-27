#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

#include <array>

namespace dbc::ui
{

/** Pitch classes the key lets through: the scale, minus notes switched off by hand. */
inline std::array<bool, 12> allowedNotes (HumlineProcessor& proc)
{
    std::array<bool, 12> on {};

    if (proc.apvts.getRawParameterValue ("keyLock")->load() <= 0.5f)
        return on;

    dbc::KeySettings k;
    k.root  = juce::jlimit (0, 11, (int) proc.apvts.getRawParameterValue ("keyRoot")->load());
    k.scale = (dbc::ScaleType) juce::jlimit (0, (int) dbc::ScaleType::NumScales - 1,
                                             (int) proc.apvts.getRawParameterValue ("scale")->load());
    k.mask  = (uint16_t) proc.getNoteMask();

    const auto allowed = dbc::allowedPitchClasses (k);

    for (int pc = 0; pc < 12; ++pc)
        on[(size_t) pc] = (allowed & (1u << pc)) != 0;

    return on;
}

inline void setIntParameter (HumlineProcessor& proc, const char* id, int value)
{
    if (auto* param = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter (id)))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
}

/** A combo for a small integer parameter (octave shifts, bend range). Items are
    value - lowest + 1, so it stays in step with the parameter from a timer. */
struct IntCombo
{
    static void fill (juce::ComboBox& box, int lo, int hi, bool signedLabels, const juce::String& suffix = {})
    {
        for (int v = lo; v <= hi; ++v)
            box.addItem ((signedLabels && v > 0 ? "+" : "") + juce::String (v) + suffix, v - lo + 1);
    }

    static void sync (juce::ComboBox& box, HumlineProcessor& proc, const char* id, int lo)
    {
        const int v = (int) proc.apvts.getRawParameterValue (id)->load();

        if (box.getSelectedId() != v - lo + 1)
            box.setSelectedId (v - lo + 1, juce::dontSendNotification);
    }
};

//==============================================================================
class PlayPanel : public juce::Component, private juce::Timer
{
public:
    explicit PlayPanel (HumlineProcessor& p)
        : proc (p),
          gain ("Level", pal::teal, " dB"),
          pitchSnapSlider ("Pitch Snap", pal::pink, " %"),
          stickySlider ("Stickiness", pal::pink, " %"),
          strengthSlider ("Strength", pal::teal, " %"),
          swingSlider ("Swing", pal::teal, " %"),
          gridCombo ("Timing Snap"),
          minLenCombo ("Min Length"),
          octaveCombo ("Octave"),
          pitchOn ("Pitch", pal::pink),
          triggersOn ("Triggers", pal::teal),
          chordsOn ("Chords", pal::purple),
          bendOn ("Pitch Bend", pal::pink),
          modeSwitch ({ "Live", "Quantised" }, pal::teal)
    {
        addAndMakeVisible (pads);
        addAndMakeVisible (dial);
        addAndMakeVisible (keys);
        addAndMakeVisible (modeSwitch);

        modeSwitch.setTooltip ("What Humline sends follows the attached instrument: notes for a synth, "
                               "triggers for a drum kit.\n"
                               "Live: every event is sent the moment it is detected.\n"
                               "Quantised: snapped to the grid (sent a constant delay later).");
        modeSwitch.onChange = [this] (int index)
        {
            if (auto* param = proc.apvts.getParameter ("quantise"))
                param->setValueNotifyingHost (index == 1 ? 1.0f : 0.0f);

            refreshLive();
            repaint();
        };

        juce::Component* controls[] { &gain, &pitchSnapSlider, &stickySlider, &strengthSlider, &swingSlider,
                                      &gridCombo, &minLenCombo, &octaveCombo,
                                      &pitchOn, &triggersOn, &chordsOn, &bendOn };

        for (auto* c : controls)
            addAndMakeVisible (c);

        gain.attach (proc.apvts, "inputGain");
        pitchSnapSlider.attach (proc.apvts, "pitchSnap");
        stickySlider.attach (proc.apvts, "sticky");
        strengthSlider.attach (proc.apvts, "timingStrength");
        swingSlider.attach (proc.apvts, "swing");

        gridCombo.attach (proc.apvts, "grid");
        minLenCombo.attach (proc.apvts, "minLength");

        // What gets recorded / sent: switch pitch or triggers off independently.
        pitchOn.attach (proc.apvts, "pitchOn");
        triggersOn.attach (proc.apvts, "drumsOn");
        chordsOn.attach (proc.apvts, "chordsOn");
        bendOn.attach (proc.apvts, "bendOn");

        IntCombo::fill (octaveCombo.box, -2, 2, true);
        octaveCombo.box.onChange = [this]
        {
            setIntParameter (proc, "octaveShift", octaveCombo.box.getSelectedId() - 3);
        };

        pads.onPadClicked = [this] (int pad)
        {
            selectedPad = pad;
            refreshPadNames();
            proc.previewPad (pad);
        };
        // Clicking an in-key note on the wheel or the keyboard switches it off
        // (and back on): sung notes then snap to the nearest note still on.
        keys.onNoteClicked = [this] (int note) { proc.toggleNote (note); };
        dial.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        dial.onNoteClicked = [this] (int pc) { proc.toggleNote (pc); };

        refreshLive();   // fill the octave box and the wheel now, not on the first tick

        startTimerHz (12);
    }

    ~PlayPanel() override { stopTimer(); }

    void refreshPadNames()
    {
        juce::StringArray names;
        bool mutes[dbc::numDrumPads] {};

        for (int i = 0; i < dbc::numDrumPads; ++i)
        {
            names.add (proc.getPadName (i));
            mutes[i] = proc.isPadMuted (i);
        }

        pads.setNames (names);
        pads.setMutes (mutes);
        pads.setSelected (selectedPad);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();

        auto left = area.removeFromLeft (300).toFloat();
        area.removeFromLeft (16.0f);
        auto right = area.toFloat();

        drawPanel (g, left);
        drawPanel (g, right);

        drawSectionHeader (g, left.toNearestInt().removeFromTop (46).withTrimmedLeft (16), "Triggers", pal::teal);
        const bool drumKit = proc.getTarget() == dbc::InstrumentTarget::DrumKit;
        drawSectionHeader (g, right.toNearestInt().removeFromTop (46).withTrimmedLeft (16),
                           drumKit ? "Drum kit" : "Instrument", pal::pink);

        juce::String hint;
        const bool quantised = currentMode() == dbc::EngineMode::Both;

        if (drumKit)
            hint = quantised ? "Drum kit, quantised: every hit is a drum, pulled onto the grid. "
                               "Snapping needs a short constant delay (shown in the footer)."
                             : "Drum kit: every hit is a drum, sent the moment you make it - no grid, no delay. "
                               "Teach it your sounds on the Train page.";
        else
            hint = quantised ? "Instrument, quantised: notes are pulled onto the grid and into the key. "
                               "Snapping needs a short constant delay (shown in the footer)."
                             : "Instrument: sing - notes are pulled into the key and sent as you sing them, "
                               "no grid, no delay. Click a note on the wheel or keyboard to switch it off.";

        g.setColour (pal::dim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (hint, hintArea, juce::Justification::topLeft, 5);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (300);
        area.removeFromLeft (16);
        auto right = area;

        gain.setBounds (left.getX() + 14, left.getY() + 52, left.getWidth() - 28, 26);
        pads.setBounds (left.getX() + 14, left.getY() + 92, left.getWidth() - 28, left.getHeight() - 106);

        auto inner = right.reduced (16, 14);
        inner.removeFromTop (6);

        // Mode switch on the header row, to the right of the "Mode" title.
        auto modeRow = inner.removeFromTop (34);
        modeSwitch.setBounds (modeRow.withTrimmedLeft (130).removeFromLeft (juce::jmin (260, modeRow.getWidth() - 130)));
        inner.removeFromTop (12);

        auto bottom = inner.removeFromBottom (86);
        keys.setBounds (bottom.withTrimmedLeft (316).reduced (0, 6));

        // Pitch / Triggers / Chords / Pitch Bend switches, under the wheel.
        auto toggles = bottom.withWidth (300).reduced (0, 6);
        auto topRow = toggles.removeFromTop (34);
        toggles.removeFromTop (6);
        auto bottomRow = toggles.removeFromTop (34);
        pitchOn.setBounds (topRow.removeFromLeft (146));
        triggersOn.setBounds (topRow.withTrimmedLeft (8));
        chordsOn.setBounds (bottomRow.removeFromLeft (146));
        bendOn.setBounds (bottomRow.withTrimmedLeft (8));

        auto column = inner.removeFromRight (300);
        dial.setBounds (inner.reduced (6));

        const auto take = [&column] (int height, int gap)
        {
            auto r = column.removeFromTop (height);
            column.removeFromTop (gap);
            return r;
        };

        // Timing first, then the pitch controls. Key, scale and octave live on
        // the Key page only, so this column stays readable.
        gridCombo.setBounds (take (44, 14));
        strengthSlider.setBounds (take (24, 14));
        minLenCombo.setBounds (take (44, 14));
        swingSlider.setBounds (take (24, 14));
        pitchSnapSlider.setBounds (take (24, 14));
        stickySlider.setBounds (take (24, 14));
        octaveCombo.setBounds (take (44, 14).withWidth (120));

        hintArea = column;
    }

    void refreshLive()
    {
        const auto& live = proc.getLiveState();

        dial.setState (live.pitchClass, live.voiced, live.pitchClassEnergy);
        dial.setSelectedNotes (allowedNotes (proc));   // the notes the key lets through
        pads.setFlash (live.padFlash);

        const int activeNote = live.activeNote;
        keys.setActiveNote (activeNote >= 0 ? activeNote : (live.voiced ? (int) std::lround (live.midiFloat) : -1));
        keys.setScale (proc.apvts.getRawParameterValue ("keyLock")->load() > 0.5f,
                       (int) proc.apvts.getRawParameterValue ("keyRoot")->load(),
                       (dbc::ScaleType) (int) proc.apvts.getRawParameterValue ("scale")->load(),
                       proc.getNoteMask());

        IntCombo::sync (octaveCombo.box, proc, "octaveShift", -2);

        // Mode: keep the switch in step with the parameter (host automation,
        // preset load) and grey out what the current mode does not use.
        const auto mode = currentMode();
        const bool liveMode = mode != dbc::EngineMode::Both;
        const auto target = proc.getTarget();

        if (modeSwitch.getSelected() != (liveMode ? 0 : 1) || target != shownTarget)
        {
            modeSwitch.setSelected (liveMode ? 0 : 1);
            shownTarget = target;
            repaint();
        }

        // A drum kit gets triggers, an instrument gets notes.
        const bool drums  = target == dbc::InstrumentTarget::DrumKit;
        const bool melody = ! drums;

        // The look and feel does not grey disabled controls, so fade them too.
        const auto use = [] (juce::Component& c, bool on)
        {
            c.setEnabled (on);
            c.setAlpha (on ? 1.0f : 0.35f);
        };

        use (triggersOn, drums);
        use (pads, drums);
        use (pitchOn, melody);
        use (chordsOn, melody);
        use (bendOn, melody);
        use (pitchSnapSlider, melody);
        use (stickySlider, melody);
        use (octaveCombo, melody);

        // The live modes send events straight away, so the grid does not apply.
        use (gridCombo, ! liveMode);
        use (strengthSlider, ! liveMode);
        use (minLenCombo, ! liveMode);
        use (swingSlider, ! liveMode);
    }

private:
    dbc::EngineMode currentMode() const
    {
        return dbc::engineModeFor (proc.getTarget(), proc.apvts.getRawParameterValue ("quantise")->load() > 0.5f);
    }

    void timerCallback() override { refreshLive(); }

    HumlineProcessor& proc;

    TriggerPads pads;
    PitchDial dial;
    MiniKeyboard keys;

    MicSlider gain, pitchSnapSlider, stickySlider, strengthSlider, swingSlider;
    LabelledCombo gridCombo, minLenCombo, octaveCombo;
    TogglePill pitchOn, triggersOn, chordsOn, bendOn;
    SegmentedChoice modeSwitch;

    juce::Rectangle<int> hintArea;
    int selectedPad = 0;
    dbc::InstrumentTarget shownTarget = dbc::InstrumentTarget::None;
};

//==============================================================================
class TrainPanel : public juce::Component, private juce::Timer
{
public:
    explicit TrainPanel (HumlineProcessor& p)
        : proc (p)
    {
        addAndMakeVisible (pads);
        addAndMakeVisible (nameEditor);
        addAndMakeVisible (learnButton);
        addAndMakeVisible (clearButton);
        addAndMakeVisible (noteCombo);
        addAndMakeVisible (muteButton);
        addAndMakeVisible (velocityButton);

        nameEditor.setMultiLine (false);
        nameEditor.setFont (uiFont (14.0f));
        nameEditor.onTextChange = [this]
        {
            proc.setPadName (pads.getSelected(), nameEditor.getText());
            refreshPadNames();
        };

        learnButton.setButtonText ("Record takes");
        learnButton.setColour (juce::TextButton::buttonOnColourId, pal::teal);
        learnButton.onClick = [this]
        {
            bankFull = false;

            if (proc.engine.isTraining())
                proc.cancelTraining();
            else
                bankFull = ! proc.startTraining (pads.getSelected());

            updateTrainingUi();
        };

        clearButton.setButtonText ("Clear all takes");
        clearButton.onClick = [this]
        {
            if (proc.engine.isTraining())
                proc.cancelTraining();

            proc.engine.getClassifier().clearTemplate (pads.getSelected());
            bankFull = false;
            ++proc.uiStateVersion;
            updateTrainingUi();
            repaint();
        };

        // Velocity on: hits follow how loud they were. Off: always full velocity.
        velocityButton.setButtonText ("Velocity");
        velocityButton.setClickingTogglesState (true);
        velocityButton.setColour (juce::TextButton::buttonOnColourId, pal::teal);
        velocityButton.onClick = [this]
        {
            proc.setPadVelocity (pads.getSelected(), velocityButton.getToggleState());
        };

        muteButton.setButtonText ("Mute");
        muteButton.setClickingTogglesState (true);
        muteButton.setColour (juce::TextButton::buttonOnColourId, pal::pink);
        muteButton.onClick = [this]
        {
            const int pad = pads.getSelected();
            muteButton.setToggleState (! proc.isPadMuted (pad), juce::dontSendNotification);
            proc.setPadMuted (pad, muteButton.getToggleState());
            refreshPadNames();
        };

        noteCombo.onChange = [this]
        {
            const int note = noteCombo.getSelectedId() - 1;

            if (note >= 0)
            {
                proc.setPadNote (pads.getSelected(), note);
                proc.previewDrumNote (note);
                refreshQuickPicks();
            }
        };

        for (int i = 0; i < 128; ++i)
            noteCombo.addItem (drumNoteName (i), i + 1);

        for (size_t i = 0; i < quickPicks.size(); ++i)
        {
            auto& b = quickPicks[i];
            const int note = quickNotes[i];
            b.setButtonText (noteLabel (note) + " / " + juce::String (note) + "   " + gmDrumName (note));
            b.setColour (juce::TextButton::buttonOnColourId, pal::purple.withAlpha (0.75f));
            b.onClick = [this, note]
            {
                proc.setPadNote (pads.getSelected(), note);
                proc.previewDrumNote (note);
                noteCombo.setSelectedId (note + 1, juce::dontSendNotification);
                refreshQuickPicks();
            };
            addAndMakeVisible (b);
        }

        pads.onPadClicked = [this] (int pad)
        {
            refreshPadNames();
            updateFromSelectedPad();
            proc.previewPad (pad);
        };

        proc.onTrainingFinished = [this] (int pad) { juce::ignoreUnused (pad); updateTrainingUi(); repaint(); };

        refreshPadNames();
        updateFromSelectedPad();
        startTimerHz (12);
    }

    ~TrainPanel() override
    {
        stopTimer();

        // The processor outlives the editor: never leave it holding a callback
        // into a deleted panel.
        proc.onTrainingFinished = nullptr;
    }

    /** General MIDI percussion name (notes 35-81), empty elsewhere. */
    static juce::String gmDrumName (int note)
    {
        static const char* const drumNames[] {
            "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap",
            "Electric Snare", "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat",
            "Low Tom", "Open Hi-Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1",
            "High Tom", "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell", "Tambourine",
            "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap", "Ride Cymbal 2",
            "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga",
            "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa",
            "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro",
            "Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica",
            "Mute Triangle", "Open Triangle"
        };

        if (note < 35 || note > 81)
            return {};

        return drumNames[note - 35];
    }

    /** Drum-map note naming, where note 36 is C1 (as in most drum racks). */
    static juce::String noteLabel (int note)
    {
        return juce::String (dbc::pitchClassName (note)) + juce::String (note / 12 - 2);
    }

    static juce::String drumNoteName (int note)
    {
        const auto gm = gmDrumName (note);
        return noteLabel (note) + "  /  " + juce::String (note) + (gm.isNotEmpty() ? "   " + gm : juce::String());
    }

    void refreshQuickPicks()
    {
        const int current = proc.getPadNote (pads.getSelected());

        for (size_t i = 0; i < quickPicks.size(); ++i)
            quickPicks[i].setToggleState (quickNotes[i] == current, juce::dontSendNotification);
    }

    void refreshPadNames()
    {
        juce::StringArray names;
        bool mutes[dbc::numDrumPads] {};

        for (int i = 0; i < dbc::numDrumPads; ++i)
        {
            names.add (proc.getPadName (i));
            mutes[i] = proc.isPadMuted (i);
        }

        pads.setNames (names);
        pads.setMutes (mutes);
        pads.setSelected (pads.getSelected());
    }

    void updateFromSelectedPad()
    {
        const int pad = pads.getSelected();
        nameEditor.setText (proc.getPadName (pad), juce::dontSendNotification);
        noteCombo.setSelectedId (proc.getPadNote (pad) + 1, juce::dontSendNotification);
        muteButton.setToggleState (proc.isPadMuted (pad), juce::dontSendNotification);
        velocityButton.setToggleState (proc.getPadVelocity (pad), juce::dontSendNotification);
        bankFull = false;
        refreshQuickPicks();
        updateTrainingUi();
    }

    void updateTrainingUi()
    {
        const bool training = proc.engine.isTraining();
        learnButton.setButtonText (training ? "Recording... (click to stop)" : "Record takes");
        learnButton.setToggleState (training, juce::dontSendNotification);
        clearButton.setButtonText (proc.engine.getClassifier().hasTemplate (pads.getSelected())
                                       ? "Clear all takes" : "No takes yet");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (300).toFloat();
        area.removeFromLeft (16.0f);
        auto right = area.toFloat();

        drawPanel (g, left);
        drawPanel (g, right);

        drawSectionHeader (g, left.toNearestInt().removeFromTop (46).withTrimmedLeft (16), "Triggers", pal::teal);

        auto rr = right.toNearestInt();
        drawSectionHeader (g, rr.removeFromTop (46).withTrimmedLeft (20), "Train", pal::teal);

        g.setColour (pal::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText ("Pick a pad, press Record takes and make that sound - short, sharp and the same each\n"
                          "time. Each take lights a circle (up to 12); stop whenever you like. Velocity off makes\n"
                          "every hit full velocity. Below, choose which drum the pad plays (its MIDI note).",
                          trainHelpArea, juce::Justification::topLeft, 4);

        // take pips: one per stored take
        const int maxTakes = dbc::DrumClassifier::maxTakes;
        const int takes = proc.engine.getClassifier().numExamples (pads.getSelected());

        for (int i = 0; i < maxTakes; ++i)
        {
            auto pip = juce::Rectangle<float> ((float) (pipArea.getX() + i * 32), (float) pipArea.getY() + 6.0f,
                                               26.0f, 26.0f);
            g.setColour (i < takes ? pal::yellow.withAlpha (0.95f) : pal::panelSoft);
            g.fillEllipse (pip);
            g.setColour (i < takes ? juce::Colours::black.withAlpha (0.7f) : pal::dim);
            g.setFont (uiFont (12.0f, true));
            g.drawText (juce::String (i + 1), pip, juce::Justification::centred, false);
        }

        if (bankFull)
        {
            g.setColour (pal::pink);
            g.setFont (uiFont (11.5f));
            g.drawText ("This trigger already has 12 takes - clear them to record new ones.",
                        juce::Rectangle<float> ((float) pipArea.getX() + (float) maxTakes * 32.0f + 8.0f,
                                                (float) pipArea.getY(), 360.0f, (float) pipArea.getHeight()),
                        juce::Justification::centredLeft, true);
        }

        drawCaption (g, noteArea.withHeight (16), "Drum sound / MIDI note output  (click a pad to hear it on your drum kit)");

        if (proc.getTarget() != dbc::InstrumentTarget::DrumKit)
        {
            g.setColour (pal::pink);
            g.setFont (uiFont (11.5f));
            g.drawText ("Triggers play a drum kit - Humline is driving an instrument, so they send nothing.",
                        pickArea.withY (pickArea.getBottom() + 6).withHeight (18).toFloat(),
                        juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (300);
        area.removeFromLeft (16);
        auto right = area.reduced (20, 16);

        pads.setBounds (left.getX() + 14, left.getY() + 52, left.getWidth() - 28, left.getHeight() - 66);

        right.removeFromTop (30);
        trainHelpArea = right.removeFromTop (60);
        right.removeFromTop (10);

        learnArea = right.removeFromTop (40);
        learnButton.setBounds (learnArea.getX(), learnArea.getY(), 210, 34);
        muteButton.setBounds (learnArea.getRight() - 90, learnArea.getY(), 90, 34);
        velocityButton.setBounds (learnArea.getRight() - 200, learnArea.getY(), 100, 34);

        right.removeFromTop (14);
        pipArea = right.removeFromTop (38);
        right.removeFromTop (10);

        auto nameRow = right.removeFromTop (54);
        nameEditor.setBounds (nameRow.getX(), nameRow.getY() + 18, 260, 32);
        clearButton.setBounds (nameRow.getX() + 272, nameRow.getY() + 18, 130, 32);

        right.removeFromTop (16);
        noteArea = right.removeFromTop (54);
        noteCombo.setBounds (noteArea.getX(), noteArea.getY() + 18, juce::jmin (420, right.getWidth()), 32);

        right.removeFromTop (12);
        pickArea = right.removeFromTop (80);

        const int cols = 4;
        const int gap = 8;
        const int w = (pickArea.getWidth() - gap * (cols - 1)) / cols;

        for (size_t i = 0; i < quickPicks.size(); ++i)
        {
            const int col = (int) i % cols;
            const int row = (int) i / cols;
            quickPicks[i].setBounds (pickArea.getX() + col * (w + gap), pickArea.getY() + row * 40, w, 34);
        }
    }

private:
    void timerCallback() override
    {
        if (proc.engine.isTraining() || trainingWasActive)
        {
            trainingWasActive = proc.engine.isTraining();
            updateTrainingUi();
        }

        if (proc.engine.isTraining())
            repaint (pipArea);
    }

    HumlineProcessor& proc;

    TriggerPads pads;
    juce::TextEditor nameEditor;
    juce::TextButton learnButton, clearButton, muteButton, velocityButton;
    juce::ComboBox noteCombo;
    bool bankFull = false;

    static constexpr std::array<int, 8> quickNotes { 36, 38, 42, 39, 46, 45, 50, 49 };
    std::array<juce::TextButton, 8> quickPicks;

    juce::Rectangle<int> learnArea, pipArea, noteArea, pickArea, trainHelpArea;
    bool trainingWasActive = false;
};

//==============================================================================
class KeyPanel : public juce::Component, private juce::Timer
{
public:
    explicit KeyPanel (HumlineProcessor& p)
        : proc (p),
          pitchSnapSlider ("Pitch Snap", pal::pink, " %"),
          stickySlider ("Stickiness", pal::pink, " %"),
          keyCombo ("Key"),
          scaleCombo ("Scale"),
          octaveCombo ("Octave"),
          keyLock ("Key Lock", pal::purple)
    {
        juce::Component* controls[] { &dial, &keys, &pitchSnapSlider, &stickySlider,
                                      &keyCombo, &scaleCombo, &octaveCombo, &keyLock,
                                      &detectButton, &clearButton, &suggestions,
                                      &subsetButton, &allNotesButton };

        for (auto* c : controls)
            addAndMakeVisible (c);

        pitchSnapSlider.attach (proc.apvts, "pitchSnap");
        stickySlider.attach (proc.apvts, "sticky");
        keyCombo.attach (proc.apvts, "keyRoot");
        scaleCombo.attach (proc.apvts, "scale");
        keyLock.attach (proc.apvts, "keyLock");

        octaveCombo.box.addItemList ({ "-2", "-1", "0", "+1", "+2" }, 1);
        octaveCombo.box.onChange = [this]
        {
            if (auto* param = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter ("octaveShift")))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) (octaveCombo.box.getSelectedItemIndex() - 2)));
        };
        octaveCombo.box.setSelectedItemIndex (2, juce::dontSendNotification);

        // Clicking the wheel toggles notes; the suggestions follow.
        dial.setMouseCursor (juce::MouseCursor::PointingHandCursor);
        dial.onNoteClicked = [this] (int pc)
        {
            manual[(size_t) pc] = ! manual[(size_t) pc];
            updateSuggestions();
        };

        detectButton.setClickingTogglesState (false);
        detectButton.setColour (juce::TextButton::buttonOnColourId, pal::pink);
        detectButton.onClick = [this] { setListening (! proc.engine.isKeyListening()); };

        clearButton.setButtonText ("Clear");
        clearButton.onClick = [this]
        {
            manual.fill (false);
            proc.engine.clearKeyHistogram();
            sung.fill (0.0f);
            statusText = {};
            updateSuggestions();
        };

        suggestions.onPicked = [this] (const dbc::KeySuggestion& k)
        {
            applyKey (k);

            // Subset: only the notes chosen on the wheel stay on. Full: the whole key.
            int mask = (int) dbc::allNotesMask;

            if (subsetButton.getToggleState())
            {
                int chosen = 0;

                for (int pc = 0; pc < 12; ++pc)
                    if (lit[(size_t) pc] && dbc::scaleContains (k.scale, k.root, pc))
                        chosen |= 1 << pc;

                if (chosen != 0)
                    mask = chosen;
            }

            proc.setNoteMask (mask);

            statusText = juce::String ("Key set to ") + dbc::pitchClassName (k.root) + " " + dbc::scaleName (k.scale)
                         + (mask != (int) dbc::allNotesMask ? " (selected notes only)" : "");
            repaint();
        };

        subsetButton.setButtonText ("Only the selected notes");
        subsetButton.setClickingTogglesState (true);
        subsetButton.setColour (juce::TextButton::buttonOnColourId, pal::purple);

        allNotesButton.setButtonText ("All notes on");
        allNotesButton.onClick = [this] { proc.setNoteMask ((int) dbc::allNotesMask); repaint(); };

        // On the keyboard, clicking an in-key note switches it off (or back on).
        keys.onNoteClicked = [this] (int note) { proc.toggleNote (note); repaint (notesArea); };

        updateDetectButton();
        startTimerHz (12);
    }

    ~KeyPanel() override
    {
        stopTimer();
        proc.engine.setKeyListening (false);
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (430).toFloat();
        area.removeFromLeft (16.0f);
        auto right = area.toFloat();

        drawPanel (g, left);
        drawPanel (g, right);

        drawSectionHeader (g, left.toNearestInt().removeFromTop (46).withTrimmedLeft (16), "Select Notes", pal::purple);
        drawSectionHeader (g, right.toNearestInt().removeFromTop (46).withTrimmedLeft (20), "Key Restriction", pal::purple);

        g.setColour (pal::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText ("Click notes on the wheel, or press Key Detect and sing a few bars of your melody.",
                          selectHelpArea, juce::Justification::topLeft, 2);

        if (statusText.isNotEmpty())
        {
            g.setColour (proc.engine.isKeyListening() ? pal::pink : pal::purple);
            g.setFont (uiFont (12.0f, true));
            g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 2);
        }

        drawCaption (g, suggestCaption, "Suggested keys  (click one to apply it)");

        const auto scale = (dbc::ScaleType) (int) proc.apvts.getRawParameterValue ("scale")->load();
        const int root = (int) proc.apvts.getRawParameterValue ("keyRoot")->load();
        const int mask = proc.getNoteMask();

        juce::String notes, off;

        for (int i = 0; i < 12; ++i)
        {
            if (! dbc::scaleContains (scale, root, i))
                continue;

            ((mask & (1 << i)) != 0 ? notes : off) << dbc::pitchClassName (i) << "  ";
        }

        g.setColour (pal::purple.withAlpha (0.9f));
        g.setFont (uiFont (13.0f, true));
        g.drawText ("In key:   " + notes + (off.isNotEmpty() ? "     off:  " + off : juce::String()),
                    notesArea.toFloat().withTrimmedRight (120.0f), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (430);
        area.removeFromLeft (16);
        auto right = area.reduced (22, 16);

        // --- left: the note wheel and Key Detect -----------------------------
        auto leftInner = left.reduced (18, 14);
        leftInner.removeFromTop (34);
        selectHelpArea = leftInner.removeFromTop (34);

        auto bottom = leftInner.removeFromBottom (36);
        detectButton.setBounds (bottom.removeFromLeft (220));
        bottom.removeFromLeft (10);
        clearButton.setBounds (bottom.removeFromLeft (90));

        leftInner.removeFromBottom (4);
        statusArea = leftInner.removeFromBottom (34);
        dial.setBounds (leftInner.reduced (6));

        // --- right: key, suggestions, snapping -------------------------------
        auto header = right.removeFromTop (30);
        keyLock.setBounds (header.removeFromRight (110).withTrimmedBottom (2));

        auto row = right.removeFromTop (44);
        keyCombo.setBounds (row.getX(), row.getY(), 110, row.getHeight());
        scaleCombo.setBounds (row.getX() + 120, row.getY(), 190, row.getHeight());
        octaveCombo.setBounds (row.getX() + 320, row.getY(), 90, row.getHeight());

        right.removeFromTop (10);
        auto captionRow = right.removeFromTop (26);
        subsetButton.setBounds (captionRow.removeFromRight (190));
        suggestCaption = captionRow.withTrimmedTop (10);
        right.removeFromTop (4);
        suggestions.setBounds (right.removeFromTop (170));
        right.removeFromTop (10);

        auto sliders = right.removeFromTop (24);
        const int half = sliders.getWidth() / 2;
        pitchSnapSlider.setBounds (sliders.removeFromLeft (half).withTrimmedRight (12));
        stickySlider.setBounds (sliders.withTrimmedLeft (12));

        notesArea = right.removeFromBottom (26);
        allNotesButton.setBounds (notesArea.withTrimmedLeft (notesArea.getWidth() - 110));
        right.removeFromTop (8);
        keys.setBounds (right.reduced (0, 4));
    }

    void refreshLive()
    {
        const auto& live = proc.getLiveState();

        dial.setState (live.pitchClass, live.voiced, live.pitchClassEnergy);
        keys.setActiveNote (live.voiced ? (int) std::lround (live.midiFloat) : -1);
        keys.setScale (proc.apvts.getRawParameterValue ("keyLock")->load() > 0.5f,
                       (int) proc.apvts.getRawParameterValue ("keyRoot")->load(),
                       (dbc::ScaleType) (int) proc.apvts.getRawParameterValue ("scale")->load(),
                       proc.getNoteMask());

        const int oct = juce::jlimit (0, 4, (int) proc.apvts.getRawParameterValue ("octaveShift")->load() + 2);

        if (octaveCombo.box.getSelectedItemIndex() != oct)
            octaveCombo.box.setSelectedItemIndex (oct, juce::dontSendNotification);
    }

private:
    static constexpr double maxListenSeconds = 30.0;

    void timerCallback() override
    {
        refreshLive();

        if (proc.engine.isKeyListening())
        {
            sung = proc.engine.getKeyHistogram();

            const double elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001 - listenStarted;
            const int notesHeard = dbc::KeyDetector::distinctNotes (sung);

            statusText = "Listening... " + juce::String (notesHeard) + " note" + (notesHeard == 1 ? "" : "s")
                         + " heard. Click Stop when you are done.";

            if (elapsed > maxListenSeconds)
                setListening (false);
            else
                repaint (statusArea);
        }

        updateSuggestions();
        repaint (notesArea);
    }

    void setListening (bool shouldListen)
    {
        if (shouldListen)
        {
            proc.engine.clearKeyHistogram();
            sung.fill (0.0f);
            listenStarted = juce::Time::getMillisecondCounterHiRes() * 0.001;
            proc.engine.setKeyListening (true);
            statusText = "Listening... sing your melody.";
        }
        else
        {
            proc.engine.setKeyListening (false);
            sung = proc.engine.getKeyHistogram();
            updateSuggestions();

            // Auto key detection: the best match is applied straight away; the
            // other suggestions stay one click away.
            if (dbc::KeyDetector::distinctNotes (sung) >= 3 && ! current.empty())
            {
                applyKey (current.front());
                statusText = juce::String ("Detected ") + dbc::pitchClassName (current.front().root) + " "
                             + dbc::scaleName (current.front().scale) + " - applied.";
            }
            else
            {
                statusText = dbc::KeyDetector::distinctNotes (sung) == 0
                               ? "No notes heard - check the input level on the Play page."
                               : "Not enough notes to be sure - sing a bit more of the melody.";
            }
        }

        updateDetectButton();
        repaint();
    }

    void updateDetectButton()
    {
        const bool listening = proc.engine.isKeyListening();
        detectButton.setButtonText (listening ? "Stop listening" : "Key Detect (sing)");
        detectButton.setToggleState (listening, juce::dontSendNotification);
    }

    void updateSuggestions()
    {
        // Sung weight and clicked notes are combined: each counts as one share.
        std::array<float, 12> combined {};
        float sungTotal = 0.0f;
        int   manualCount = 0;

        for (int i = 0; i < 12; ++i)
        {
            sungTotal += sung[(size_t) i];
            manualCount += manual[(size_t) i] ? 1 : 0;
        }

        float peak = 0.0f;

        for (auto v : sung)
            peak = std::max (peak, v);

        lit = {};

        for (int i = 0; i < 12; ++i)
        {
            if (sungTotal > 0.0f)
                combined[(size_t) i] += sung[(size_t) i] / sungTotal;

            if (manualCount > 0 && manual[(size_t) i])
                combined[(size_t) i] += 1.0f / (float) manualCount;

            lit[(size_t) i] = manual[(size_t) i] || (peak > 0.0f && sung[(size_t) i] > peak * 0.08f);
        }

        dial.setSelectedNotes (lit);

        current = dbc::KeyDetector::suggest (combined, 5);

        const int root = (int) proc.apvts.getRawParameterValue ("keyRoot")->load();
        const auto scale = (dbc::ScaleType) (int) proc.apvts.getRawParameterValue ("scale")->load();

        suggestions.setSuggestions (current, root, scale);
    }

    void applyKey (const dbc::KeySuggestion& k)
    {
        if (auto* r = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("keyRoot")))
            *r = k.root;

        if (auto* s = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter ("scale")))
            *s = (int) k.scale;

        if (auto* lock = dynamic_cast<juce::AudioParameterBool*> (proc.apvts.getParameter ("keyLock")))
            *lock = true;

        updateSuggestions();
        repaint();
    }

    HumlineProcessor& proc;

    PitchDial dial;
    MiniKeyboard keys;
    MicSlider pitchSnapSlider, stickySlider;
    LabelledCombo keyCombo, scaleCombo, octaveCombo;
    TogglePill keyLock;
    juce::TextButton detectButton, clearButton, subsetButton, allNotesButton;
    KeySuggestionList suggestions;

    std::array<bool, 12>  lit {};      // notes chosen on the wheel (clicked or sung)
    std::array<bool, 12>  manual {};
    std::array<float, 12> sung {};
    std::vector<dbc::KeySuggestion> current;
    double listenStarted = 0.0;
    juce::String statusText;

    juce::Rectangle<int> selectHelpArea, statusArea, suggestCaption, notesArea;
};

//==============================================================================
class ChordsPanel : public juce::Component, private juce::Timer
{
public:
    explicit ChordsPanel (HumlineProcessor& p)
        : proc (p),
          presetCombo ("Chord Preset"),
          voicingCombo ("Voicing"),
          octaveCombo ("Fixed octave"),
          channelCombo ("Chord MIDI channel"),
          chordsOn ("Expand to chords", pal::purple),
          rootBass ("Root bassline", pal::purple),
          follow ("Follow sung octave", pal::purple),
          bendChords ("Bend chords", pal::pink)
    {
        juce::Component* controls[] { &chordsOn, &presetCombo, &voicingCombo, &rootBass, &bendChords,
                                      &follow, &octaveCombo, &channelCombo };

        for (auto* c : controls)
            addAndMakeVisible (c);

        chordsOn.attach (proc.apvts, "chordsOn");
        presetCombo.attach (proc.apvts, "chordMode");
        voicingCombo.attach (proc.apvts, "chordVoicing");
        rootBass.attach (proc.apvts, "chordRootBass");
        follow.attach (proc.apvts, "chordFollow");
        bendChords.attach (proc.apvts, "bendChords");
        channelCombo.attach (proc.apvts, "chordChan");

        IntCombo::fill (octaveCombo.box, -2, 2, true);
        octaveCombo.box.onChange = [this]
        {
            setIntParameter (proc, "chordOctave", octaveCombo.box.getSelectedId() - 3);
        };

        // One drop-down per chord tile: pick a different chord for that note.
        for (size_t i = 0; i < overrideBoxes.size(); ++i)
        {
            auto& box = overrideBoxes[i];

            for (int q = 0; q < (int) dbc::ChordQuality::NumQualities; ++q)
                box.addItem (dbc::chordQualityName ((dbc::ChordQuality) q), q + 1);

            box.onChange = [this, i]
            {
                proc.setChordOverride ((int) i, (dbc::ChordQuality) (overrideBoxes[i].getSelectedId() - 1));
                rebuild();
            };

            addChildComponent (box);
        }

        rebuild();
        IntCombo::sync (octaveCombo.box, proc, "chordOctave", -2);
        startTimerHz (10);
    }

    ~ChordsPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (430).toFloat();
        area.removeFromLeft (16.0f);
        auto right = area.toFloat();

        drawPanel (g, left);
        drawPanel (g, right);

        drawSectionHeader (g, left.toNearestInt().removeFromTop (46).withTrimmedLeft (16),
                           "Chord Preset", pal::purple);

        const int root   = juce::jlimit (0, 11, (int) proc.apvts.getRawParameterValue ("keyRoot")->load());
        const auto scale = (dbc::ScaleType) juce::jlimit (0, (int) dbc::ScaleType::NumScales - 1,
                                                          (int) proc.apvts.getRawParameterValue ("scale")->load());

        drawSectionHeader (g, right.toNearestInt().removeFromTop (46).withTrimmedLeft (20),
                           juce::String ("Chords in ") + dbc::pitchClassName (root) + " " + dbc::scaleName (scale),
                           pal::purple);

        g.setColour (pal::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText ("Every note you sing plays the chord mapped to it in the key. Pick a different chord for "
                          "any note with its drop-down. Root bassline adds the root underneath; with Follow off the "
                          "chords stay at one fixed octave. Give chords their own MIDI channel to keep the melody "
                          "separate.",
                          helpArea.toNearestInt(), juce::Justification::topLeft, 6);

        drawTiles (g);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (430);
        area.removeFromLeft (16);
        auto right = area.reduced (20, 16);

        auto li = left.reduced (20, 16);
        li.removeFromTop (30);
        chordsOn.setBounds (li.removeFromTop (34).withWidth (200));
        li.removeFromTop (16);

        const int half = (li.getWidth() - 12) / 2;

        auto row = li.removeFromTop (46);
        presetCombo.setBounds (row.removeFromLeft (half));
        voicingCombo.setBounds (row.withTrimmedLeft (12));
        li.removeFromTop (16);

        row = li.removeFromTop (32);
        rootBass.setBounds (row.removeFromLeft (half));
        bendChords.setBounds (row.withTrimmedLeft (12));
        li.removeFromTop (14);

        row = li.removeFromTop (46);
        follow.setBounds (row.removeFromLeft (half).withTrimmedTop (14));
        octaveCombo.setBounds (row.withTrimmedLeft (12).withWidth (120));
        li.removeFromTop (14);

        channelCombo.setBounds (li.removeFromTop (46).withWidth (half));
        li.removeFromTop (18);
        helpArea = li;

        right.removeFromTop (34);
        tileArea = right;
        layoutOverrideBoxes();
    }

private:
    juce::String signature() const
    {
        juce::String sig;

        for (auto* id : { "keyRoot", "scale", "chordMode", "chordVoicing", "chordRootBass" })
            sig << (int) proc.apvts.getRawParameterValue (id)->load() << ",";

        for (int d = 0; d < 12; ++d)
            sig << (int) proc.getChordOverride (d) << ",";

        return sig;
    }

    void rebuild()
    {
        const int root   = juce::jlimit (0, 11, (int) proc.apvts.getRawParameterValue ("keyRoot")->load());
        const auto scale = (dbc::ScaleType) juce::jlimit (0, (int) dbc::ScaleType::NumScales - 1,
                                                          (int) proc.apvts.getRawParameterValue ("scale")->load());

        // The tiles show exactly what the engine will play (follow/octave aside).
        const auto options = dbc::ChordOptions::from (proc.buildSettings());

        tileCount = juce::jlimit (2, 7, dbc::scaleDegreeCount (scale));
        lastSignature = signature();

        const int tonic = 48 + root;

        for (int i = 0; i < tileCount; ++i)
        {
            auto& tile = tiles[(size_t) i];
            tile.chord = dbc::chordForDegree (i, tonic, scale, options);
            tile.name  = dbc::chordName (tile.chord);
            tile.roman = dbc::chordRoman (tile.chord);
        }

        for (int i = 0; i < (int) overrideBoxes.size(); ++i)
        {
            auto& box = overrideBoxes[(size_t) i];
            box.setVisible (i < tileCount);
            box.setSelectedId ((int) proc.getChordOverride (i) + 1, juce::dontSendNotification);
        }

        layoutOverrideBoxes();
        repaint();
    }

    juce::Rectangle<float> tileBounds (int i) const
    {
        auto area = tileArea.toFloat();
        const int cols = 2;
        const int rows = (tileCount + cols - 1) / cols;
        const float gap = 10.0f;
        const float w = (area.getWidth() - gap * (float) (cols - 1)) / (float) cols;
        const float h = (area.getHeight() - gap * (float) (rows - 1)) / (float) juce::jmax (1, rows);

        return { area.getX() + (float) (i % cols) * (w + gap), area.getY() + (float) (i / cols) * (h + gap), w, h };
    }

    void layoutOverrideBoxes()
    {
        for (int i = 0; i < tileCount && i < (int) overrideBoxes.size(); ++i)
        {
            const auto r = tileBounds (i).toNearestInt();
            overrideBoxes[(size_t) i].setBounds (r.getRight() - 146, r.getCentreY() - 13, 132, 26);
        }
    }

    void drawTiles (juce::Graphics& g)
    {
        if (tileCount <= 0 || tileArea.getWidth() < 40 || tileArea.getHeight() < 40)
            return;

        const int activeNote = proc.getLiveState().activeNote;
        const int activePc = activeNote >= 0 ? ((activeNote % 12) + 12) % 12 : -1;

        for (int i = 0; i < tileCount; ++i)
        {
            auto r = tileBounds (i);

            const auto& tile = tiles[(size_t) i];
            const auto& c = tile.chord;
            const bool lit = activePc >= 0 && (((c.root % 12) + 12) % 12) == activePc;

            g.setColour (lit ? pal::purple.withAlpha (0.22f) : pal::panelSoft);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (lit ? pal::purple : pal::stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, lit ? 1.6f : 1.0f);

            auto inner = r.reduced (14.0f, 10.0f).withTrimmedRight (140.0f);

            if (! tile.roman.empty())
            {
                g.setColour (pal::purple);
                g.setFont (uiFont (11.0f, true));
                g.drawText (juce::String::fromUTF8 (tile.roman.c_str()), inner.removeFromTop (14.0f),
                            juce::Justification::topLeft, false);
            }

            g.setColour (pal::text);
            g.setFont (uiFont (18.0f, true));
            g.drawText (tile.name.c_str(), inner.removeFromTop (24.0f), juce::Justification::topLeft, false);

            juce::String notes;

            for (int k = 0; k < c.count; ++k)
                notes << (k > 0 ? " " : "") << dbc::pitchClassName (c.notes[(size_t) k]);

            g.setColour (pal::dim);
            g.setFont (uiFont (12.0f));
            g.drawText (notes, inner.removeFromTop (18.0f), juce::Justification::topLeft, false);
        }
    }

    void timerCallback() override
    {
        if (signature() != lastSignature)
            rebuild();

        IntCombo::sync (octaveCombo.box, proc, "chordOctave", -2);
        octaveCombo.setEnabled (proc.apvts.getRawParameterValue ("chordFollow")->load() <= 0.5f);

        const int activeNote = proc.getLiveState().activeNote;
        const int pc = activeNote >= 0 ? ((activeNote % 12) + 12) % 12 : -1;

        if (pc != lastActivePc)
        {
            lastActivePc = pc;
            repaint (tileArea);
        }
    }

    HumlineProcessor& proc;

    LabelledCombo presetCombo, voicingCombo, octaveCombo, channelCombo;
    TogglePill chordsOn, rootBass, follow, bendChords;
    std::array<juce::ComboBox, 7> overrideBoxes;

    struct Tile
    {
        dbc::Chord chord;
        std::string name, roman;
    };

    std::array<Tile, 7> tiles {};
    int tileCount = 7;
    juce::String lastSignature;
    int lastActivePc = -2;

    juce::Rectangle<int> helpArea, tileArea;
};

//==============================================================================
class AssignPanel : public juce::Component, private juce::Timer
{
public:
    explicit AssignPanel (HumlineProcessor& p)
        : proc (p),
          voiceChan ("Voice Channel"),
          drumChan ("Drum Channel"),
          velocitySlider ("Velocity", pal::teal, " %"),
          drumsOnChan ("Drums on channel 10", pal::purple),
          sendCC ("Send CC", pal::yellow),
          dialA ("aaa", pal::pink),
          dialE ("eee", pal::pink),
          dialO ("ooo", pal::pink),
          dialEnv ("env", pal::yellow),
          ccA ("CC"), ccE ("CC"), ccO ("CC"), ccEnv ("CC"),
          midiOut ("MIDI Out", pal::yellow),
          bendType ("Pitch bend type"),
          bendRange ("Bend range"),
          inA ("Input", pal::pink), inE ("Input", pal::pink), inO ("Input", pal::pink), inEnv ("Input", pal::yellow),
          outA ("CC out", pal::pink), outE ("CC out", pal::pink), outO ("CC out", pal::pink), outEnv ("CC out", pal::yellow)
    {
        juce::Component* controls[] { &voiceChan, &drumChan, &velocitySlider, &drumsOnChan, &sendCC,
                                      &dialA, &dialE, &dialO, &dialEnv, &ccA, &ccE, &ccO, &ccEnv,
                                      &midiOut,
                                      &bendType, &bendRange,
                                      &inA, &inE, &inO, &inEnv, &outA, &outE, &outO, &outEnv };

        for (auto* c : controls)
            addAndMakeVisible (c);

        bendType.attach (proc.apvts, "bendMode");
        IntCombo::fill (bendRange.box, 1, 24, false, " st");
        bendRange.box.onChange = [this] { setIntParam ("bendRange", bendRange.box.getSelectedId()); };

        // Each dial: the part of the vowel/envelope range that sweeps it, and the CC span it covers.
        RangeRow* ins[4]  { &inA, &inE, &inO, &inEnv };
        RangeRow* outs[4] { &outA, &outE, &outO, &outEnv };
        const char* ids[4] { "ccA", "ccE", "ccO", "ccDyn" };

        for (int d = 0; d < 4; ++d)
        {
            const juce::String id (ids[d]);
            ins[d]->attach (proc.apvts, id + "InMin", id + "InMax");
            outs[d]->attach (proc.apvts, id + "OutMin", id + "OutMax");
        }

        midiOut.attach (proc.apvts, "midiOut");

        voiceChan.box.addItemList (channelNames(), 1);
        drumChan.box.addItemList (channelNames(), 1);
        voiceChan.box.onChange = [this] { setIntParam ("voiceChan", voiceChan.box.getSelectedItemIndex() + 1); };
        drumChan.box.onChange = [this] { setIntParam ("drumChan", drumChan.box.getSelectedItemIndex() + 1); };

        velocitySlider.attach (proc.apvts, "velocityAmt");
        drumsOnChan.attach (proc.apvts, "drumOnDrumChan");
        sendCC.attach (proc.apvts, "sendCC");

        setupCcBox (ccA, "ccA", 20);
        setupCcBox (ccE, "ccE", 21);
        setupCcBox (ccO, "ccO", 22);
        setupCcBox (ccEnv, "ccDyn", 11);

        refreshLive();   // fill the channel boxes now, not on the first timer tick
        startTimerHz (30);
    }

    ~AssignPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (420).toFloat();
        area.removeFromLeft (16.0f);
        auto right = area.toFloat();

        drawPanel (g, left);
        drawPanel (g, right);

        drawSectionHeader (g, left.toNearestInt().removeFromTop (46).withTrimmedLeft (16), "MIDI Output", pal::teal);
        drawSectionHeader (g, right.toNearestInt().removeFromTop (46).withTrimmedLeft (20), "CC Dials", pal::yellow);

        drawCaption (g, chanCaption, "Midi output channels");
        drawCaption (g, velCaption, "Velocity");
        drawCaption (g, ccCaption, "Vowel + dynamics to CC");
        drawCaption (g, audioCaption, "Output  -  your synth or drum kit makes the sound");
        drawCaption (g, bendCaption, "Pitch bend  -  set your synth's bend range to match");

        g.setColour (pal::dim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText ("Drums default to channel 10 so a drum rack picks them up.",
                          hintArea.toNearestInt(), juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto left = area.removeFromLeft (420);
        area.removeFromLeft (16);
        auto right = area.reduced (22, 18);

        auto leftInner = left.reduced (18, 16);
        leftInner.removeFromTop (30);

        chanCaption = leftInner.removeFromTop (16);
        auto row = leftInner.removeFromTop (44);
        voiceChan.setBounds (row.getX(), row.getY(), 170, row.getHeight());
        drumChan.setBounds (row.getX() + 182, row.getY(), 170, row.getHeight());

        leftInner.removeFromTop (12);
        drumsOnChan.setBounds (leftInner.getX(), leftInner.getY(), 220, 30);
        sendCC.setBounds (leftInner.getX() + 230, leftInner.getY(), 130, 30);
        leftInner.removeFromTop (40);

        velCaption = leftInner.removeFromTop (16);
        velocitySlider.setBounds (leftInner.getX(), leftInner.getY(), juce::jmin (320, leftInner.getWidth()), 24);
        leftInner.removeFromTop (34);

        bendCaption = leftInner.removeFromTop (16);
        leftInner.removeFromTop (2);
        auto bendRow = leftInner.removeFromTop (44);
        bendType.setBounds (bendRow.removeFromLeft (180));
        bendRange.setBounds (bendRow.withTrimmedLeft (12).withWidth (110));
        leftInner.removeFromTop (12);

        audioCaption = leftInner.removeFromTop (16);
        leftInner.removeFromTop (4);
        auto toggles = leftInner.removeFromTop (32);
        midiOut.setBounds (toggles.removeFromLeft (120));
        leftInner.removeFromTop (12);

        hintArea = leftInner;

        right.removeFromTop (26);
        ccCaption = right.removeFromTop (16);
        right.removeFromTop (6);

        auto grid = right;
        const int cw = grid.getWidth() / 2;
        const int ch = grid.getHeight() / 2;

        juce::Rectangle<int> cells[4];
        cells[0] = { grid.getX(), grid.getY(), cw, ch };
        cells[1] = { grid.getX() + cw, grid.getY(), cw, ch };
        cells[2] = { grid.getX(), grid.getY() + ch, cw, ch };
        cells[3] = { grid.getX() + cw, grid.getY() + ch, cw, ch };

        RingDial* dials[4] { &dialA, &dialE, &dialO, &dialEnv };
        LabelledCombo* boxes[4] { &ccA, &ccE, &ccO, &ccEnv };
        RangeRow* ins[4]  { &inA, &inE, &inO, &inEnv };
        RangeRow* outs[4] { &outA, &outE, &outO, &outEnv };

        for (int i = 0; i < 4; ++i)
        {
            auto cell = cells[i].reduced (8);
            auto controls = cell.removeFromBottom (92);
            dials[i]->setBounds (cell.reduced (4));

            boxes[i]->setBounds (controls.removeFromTop (34).withSizeKeepingCentre (110, 34));
            controls.removeFromTop (6);
            ins[i]->setBounds (controls.removeFromTop (22));
            controls.removeFromTop (6);
            outs[i]->setBounds (controls.removeFromTop (22));
        }
    }

    void refreshLive()
    {
        const auto& live = proc.getLiveState();

        dialA.setValue (live.vowelA);
        dialE.setValue (live.vowelE);
        dialO.setValue (live.vowelO);
        dialEnv.setValue (live.dynamics);

        const int vc = juce::jlimit (0, 15, (int) proc.apvts.getRawParameterValue ("voiceChan")->load() - 1);
        const int dc = juce::jlimit (0, 15, (int) proc.apvts.getRawParameterValue ("drumChan")->load() - 1);

        if (voiceChan.box.getSelectedItemIndex() != vc)
            voiceChan.box.setSelectedItemIndex (vc, juce::dontSendNotification);

        if (drumChan.box.getSelectedItemIndex() != dc)
            drumChan.box.setSelectedItemIndex (dc, juce::dontSendNotification);

        IntCombo::sync (bendRange.box, proc, "bendRange", 1);
    }

private:
    void timerCallback() override { refreshLive(); }

    void setIntParam (const char* id, int value)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterInt*> (proc.apvts.getParameter (id)))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) value));
    }

    void setupCcBox (LabelledCombo& combo, const juce::String& paramId, int defaultCc)
    {
        juce::StringArray items;

        for (int i = 1; i <= 127; ++i)
            items.add ("CC " + juce::String (i));

        combo.box.addItemList (items, 1);
        combo.box.setSelectedItemIndex (defaultCc - 1, juce::dontSendNotification);
        combo.box.onChange = [this, &combo, paramId]
        {
            setIntParam (paramId.toRawUTF8(), combo.box.getSelectedItemIndex() + 1);
        };

        const int current = juce::jlimit (0, 126, (int) proc.apvts.getRawParameterValue (paramId)->load() - 1);
        combo.box.setSelectedItemIndex (current, juce::dontSendNotification);
    }

    static juce::StringArray channelNames()
    {
        juce::StringArray a;

        for (int i = 1; i <= 16; ++i)
            a.add ("Ch " + juce::String (i));

        return a;
    }

    HumlineProcessor& proc;

    LabelledCombo voiceChan, drumChan;
    MicSlider velocitySlider;
    TogglePill drumsOnChan, sendCC;
    RingDial dialA, dialE, dialO, dialEnv;
    LabelledCombo ccA, ccE, ccO, ccEnv;
    TogglePill midiOut;
    LabelledCombo bendType, bendRange;
    RangeRow inA, inE, inO, inEnv, outA, outE, outO, outEnv;

    juce::Rectangle<int> chanCaption, velCaption, ccCaption, audioCaption, bendCaption, hintArea;
};

//==============================================================================
/** The take as a small piano roll: melodic notes by pitch on top, one lane per
    drum sound underneath. Dragging it starts a .mid file drag; right-clicking a
    note removes it. */
class TakeGrid : public juce::Component
{
public:
    TakeGrid() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    /** Called when a drag out of the grid starts. */
    std::function<void()> onDragOut;
    /** Called with the take index of a right-clicked note. */
    std::function<void (size_t)> onDeleteNote;

    void setTake (std::vector<HistoryNote> newNotes, TakeFilter newFilter)
    {
        notes = std::move (newNotes);
        filter = newFilter;
        hovered = -1;
        layoutNotes();
        repaint();
    }

    const std::vector<HistoryNote>& getNotes() const noexcept { return notes; }

    void resized() override { layoutNotes(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setColour (pal::panelSoft.withAlpha (0.55f));
        g.fillRoundedRectangle (b, 10.0f);

        if (placed.empty())
        {
            g.setColour (pal::dim);
            g.setFont (uiFont (13.0f));
            g.drawText (notes.empty() ? "Sing or beatbox - every finished note lands here."
                                      : "Nothing in this take matches the filter.",
                        b, juce::Justification::centred, false);
            return;
        }

        // Rows: piano keys (black keys shaded, every C labelled) and drum lanes.
        g.setFont (uiFont (10.0f, true));

        for (int note = lowNote; note <= highNote && voiceArea.getHeight() > 0.0f; ++note)
        {
            const auto row = voiceRow (note);
            const int pc = note % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;

            if (black)
            {
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.fillRect (row.withX (rollArea.getX()).withWidth (rollArea.getWidth()));
            }

            if (pc == 0 && row.getHeight() >= 1.0f)
            {
                // FL Studio's naming: middle C (60) is C5.
                g.setColour (pal::dim);
                g.drawText (juce::MidiMessage::getMidiNoteName (note, true, true, 5),
                            juce::Rectangle<float> (b.getX() + 6.0f, row.getCentreY() - 7.0f, gutter - 8.0f, 14.0f),
                            juce::Justification::centredLeft, false);
                g.setColour (pal::stroke.withAlpha (0.6f));
                g.drawHorizontalLine ((int) row.getBottom(), rollArea.getX(), rollArea.getRight());
            }
        }

        for (size_t lane = 0; lane < drumLanes.size(); ++lane)
        {
            const auto row = drumRow ((int) lane);

            if ((lane % 2) == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.14f));
                g.fillRect (row.withX (rollArea.getX()).withWidth (rollArea.getWidth()));
            }

            if (row.getHeight() >= 9.0f)
            {
                g.setColour (pal::dim);
                g.drawFittedText (drumName (drumLanes[lane]),
                                  juce::Rectangle<float> (b.getX() + 6.0f, row.getY(), gutter - 8.0f, row.getHeight())
                                      .toNearestInt(),
                                  juce::Justification::centredLeft, 1, 0.8f);
            }
        }

        if (! drumLanes.empty() && voiceArea.getHeight() > 0.0f)
        {
            g.setColour (pal::stroke);
            g.drawHorizontalLine ((int) drumArea.getY(), b.getX() + 4.0f, b.getRight() - 4.0f);
        }

        // Beat and bar lines, bar numbers along the top.
        for (int beat = 0; beat <= (int) totalBeats; ++beat)
        {
            const float x = xForBeats ((double) beat);
            const bool bar = (beat % 4) == 0;

            g.setColour (pal::stroke.withAlpha (bar ? 0.9f : 0.35f));
            g.drawVerticalLine ((int) x, rollArea.getY(), rollArea.getBottom());

            if (bar && beat < (int) totalBeats)
            {
                g.setColour (pal::dim.withAlpha (0.8f));
                g.drawText (juce::String (beat / 4 + 1),
                            juce::Rectangle<float> (x + 3.0f, b.getY() + 2.0f, 30.0f, 14.0f),
                            juce::Justification::centredLeft, false);
            }
        }

        // Notes.
        for (size_t i = 0; i < placed.size(); ++i)
        {
            const auto& pn = placed[i];
            const bool hot = (int) i == hovered;
            const auto colour = notes[pn.index].drum ? pal::teal : pal::pink;

            g.setColour (hot ? colour.brighter (0.35f) : colour.withAlpha (0.9f));
            g.fillRoundedRectangle (pn.rect, 2.0f);

            if (hot)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (pn.rect.expanded (0.5f), 2.0f, 1.0f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { setHovered (noteAt (e.position)); }
    void mouseExit (const juce::MouseEvent&) override  { setHovered (-1); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;

        if (e.mods.isPopupMenu())
        {
            const int hit = noteAt (e.position);

            if (hit >= 0 && onDeleteNote)
                onDeleteNote (placed[(size_t) hit].index);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.mods.isPopupMenu() || placed.empty() || e.getDistanceFromDragStart() < 6)
            return;

        dragStarted = true;

        if (onDragOut)
            onDragOut();
    }

private:
    struct PlacedNote
    {
        size_t index;
        juce::Rectangle<float> rect;
    };

    static juce::String drumName (int note)
    {
        switch (note)
        {
            case 35: case 36:                     return "Kick";
            case 37:                              return "Stick";
            case 38: case 40:                     return "Snare";
            case 39:                              return "Clap";
            case 42: case 44:                     return "Hat";
            case 46:                              return "Open hat";
            case 41: case 43: case 45:            return "Low tom";
            case 47: case 48: case 50:            return "High tom";
            case 49: case 52: case 55: case 57:   return "Crash";
            case 51: case 53: case 59:            return "Ride";
            default:                              return "Drum " + juce::String (note);
        }
    }

    float xForBeats (double beats) const noexcept
    {
        return rollArea.getX() + (float) (beats / totalBeats) * rollArea.getWidth();
    }

    juce::Rectangle<float> voiceRow (int note) const noexcept
    {
        const float h = voiceArea.getHeight() / (float) (highNote - lowNote + 1);
        return { voiceArea.getX(), voiceArea.getBottom() - (float) (note - lowNote + 1) * h, voiceArea.getWidth(), h };
    }

    juce::Rectangle<float> drumRow (int lane) const noexcept
    {
        const float h = drumArea.getHeight() / (float) juce::jmax<size_t> (1, drumLanes.size());
        return { drumArea.getX(), drumArea.getY() + (float) lane * h, drumArea.getWidth(), h };
    }

    void layoutNotes()
    {
        placed.clear();
        drumLanes.clear();

        auto b = getLocalBounds().toFloat().reduced (4.0f);
        b.removeFromTop (14.0f);   // bar numbers
        rollArea = b.withTrimmedLeft (gutter);

        bool anyVoice = false;
        int lo = 127, hi = 0;
        double end = 0.0;
        const double origin = takeOriginBeats (notes, filter);

        for (const auto& n : notes)
        {
            if (! takeFilterAccepts (filter, n))
                continue;

            end = juce::jmax (end, (double) n.startBeats + (double) n.lengthBeats - origin);

            if (n.drum)
            {
                if (std::find (drumLanes.begin(), drumLanes.end(), n.note) == drumLanes.end())
                    drumLanes.push_back (n.note);
            }
            else
            {
                anyVoice = true;
                lo = juce::jmin (lo, n.note);
                hi = juce::jmax (hi, n.note);
            }
        }

        // Highest drum note on top, like a drum rack.
        std::sort (drumLanes.begin(), drumLanes.end(), std::greater<int>());

        // Whole bars, at least four.
        totalBeats = juce::jmax (16.0, std::ceil (end / 4.0 - 1.0e-6) * 4.0);

        if (anyVoice)
        {
            // At least two octaves, centred on what was sung.
            lo -= 2;
            hi += 2;

            while (hi - lo + 1 < 24)
            {
                if (lo > 0)   --lo;
                if (hi < 127) ++hi;
            }

            lowNote  = juce::jlimit (0, 127, lo);
            highNote = juce::jlimit (lowNote, 127, hi);
        }

        const float drumH = drumLanes.empty() ? 0.0f
                          : anyVoice ? juce::jmin ((float) drumLanes.size() * 18.0f, rollArea.getHeight() * 0.4f)
                                     : rollArea.getHeight();

        auto split = rollArea;
        drumArea  = split.removeFromBottom (drumH).withX (b.getX()).withWidth (b.getWidth());
        voiceArea = anyVoice ? split.withX (b.getX()).withWidth (b.getWidth()) : juce::Rectangle<float>();

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];

            if (! takeFilterAccepts (filter, n))
                continue;

            const float x = xForBeats ((double) n.startBeats - origin);
            const float w = juce::jmax (3.0f, xForBeats ((double) n.startBeats - origin + (double) n.lengthBeats) - x);
            juce::Rectangle<float> row;

            if (n.drum)
            {
                const auto lane = std::find (drumLanes.begin(), drumLanes.end(), n.note) - drumLanes.begin();
                row = drumRow ((int) lane);
            }
            else
            {
                row = voiceRow (n.note);
            }

            const float h = juce::jlimit (2.0f, 12.0f, row.getHeight() - 2.0f);
            placed.push_back ({ i, { x, row.getCentreY() - h * 0.5f, w - 1.0f, h } });
        }
    }

    int noteAt (juce::Point<float> p) const
    {
        // Last drawn wins, matching what is on top.
        for (size_t i = placed.size(); i-- > 0;)
            if (placed[i].rect.expanded (1.0f, 2.0f).contains (p))
                return (int) i;

        return -1;
    }

    void setHovered (int index)
    {
        if (index != hovered)
        {
            hovered = index;
            repaint();
        }
    }

    static constexpr float gutter = 58.0f;

    std::vector<HistoryNote> notes;
    std::vector<PlacedNote> placed;
    std::vector<int> drumLanes;
    TakeFilter filter = TakeFilter::All;

    juce::Rectangle<float> rollArea, voiceArea, drumArea;
    double totalBeats = 16.0;
    int lowNote = 48, highNote = 72;
    int hovered = -1;
    bool dragStarted = false;
};

//==============================================================================
/** A tile that starts the same .mid drag as the grid. */
class DragTile : public juce::Component
{
public:
    DragTile() { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    std::function<void()> onDragOut;

    void setFileName (const juce::String& name)
    {
        if (name != fileName)
        {
            fileName = name;
            repaint();
        }
    }

    void setActive (bool shouldBeActive)
    {
        if (shouldBeActive != active)
        {
            active = shouldBeActive;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (active ? pal::purple.withAlpha (0.16f) : pal::panelSoft);
        g.fillRoundedRectangle (b, 10.0f);

        const float dashes[] { 5.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (b, 10.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (active ? pal::purple : pal::stroke);
        g.fillPath (dashed);

        auto text = b.reduced (14.0f, 8.0f);
        g.setColour (active ? pal::text : pal::dim);
        g.setFont (uiFont (13.5f, true));
        g.drawText (active ? "Drag into FL Studio's piano roll" : "No notes to drag yet",
                    text.removeFromTop (text.getHeight() * 0.5f), juce::Justification::bottomLeft, true);

        g.setColour (pal::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (fileName, text, juce::Justification::topLeft, true);
    }

    void mouseDown (const juce::MouseEvent&) override { dragStarted = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! active || dragStarted || e.getDistanceFromDragStart() < 6)
            return;

        dragStarted = true;

        if (onDragOut)
            onDragOut();
    }

private:
    juce::String fileName;
    bool active = false;
    bool dragStarted = false;
};

//==============================================================================
/** Notes page: the take, exported to the DAW as a .mid by drag or Save, and the
    Pass Input switch for sharing a mixer track with an instrument. */
class NotesPanel : public juce::Component, private juce::Timer
{
public:
    explicit NotesPanel (HumlineProcessor& p)
        : proc (p),
          filterChoice ({ "All", "Voice", "Drums" }, pal::purple),
          passInput ("Pass input", pal::teal)
    {
        addAndMakeVisible (grid);
        addAndMakeVisible (filterChoice);
        addAndMakeVisible (dragTile);
        addAndMakeVisible (saveButton);
        addAndMakeVisible (clearButton);
        addAndMakeVisible (passInput);

        grid.onDragOut = [this] { dragTakeOut(); };
        dragTile.onDragOut = [this] { dragTakeOut(); };

        grid.onDeleteNote = [this] (size_t index)
        {
            const auto& shown = grid.getNotes();

            if (index < shown.size())
                proc.removeTakeNote (index, shown[index]);

            refresh (true);
        };

        filterChoice.setTooltip ("Which notes go into the .mid: everything, only the sung notes, or only the drums");
        filterChoice.onChange = [this] (int) { refresh (true); };

        saveButton.setTooltip ("Save the take as a .mid file");
        saveButton.onClick = [this] { saveTake(); };

        clearButton.setTooltip ("Empty the take and start a new one");
        clearButton.onClick = [this]
        {
            proc.clearTake();
            refresh (true);
        };

        passInput.attach (proc.apvts, "passInput");
        passInput.button.setTooltip ("Keep the audio already on this mixer track and add Humline's sound to it");

        if (proc.isStandaloneApp())
            passInput.setEnabled (false);

        refresh (true);
        startTimerHz (15);
    }

    ~NotesPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, leftPanel.toFloat());
        drawPanel (g, rightPanel.toFloat());

        drawSectionHeader (g, leftPanel.withHeight (46).withTrimmedLeft (16), "Take", pal::pink);
        drawSectionHeader (g, exportHeader, "Export", pal::purple);
        drawSectionHeader (g, routingHeader, "Routing", pal::teal);

        g.setColour (pal::dim);
        g.setFont (uiFont (11.5f));
        g.drawText (stats, leftPanel.withHeight (46).withTrimmedRight (18).toFloat(),
                    juce::Justification::centredRight, false);

        drawCaption (g, filterCaption, "Notes to export");

        g.setFont (uiFont (11.0f));
        g.setColour (pal::dim);
        g.drawFittedText ("Drop it on the piano roll (or the Channel Rack) and FL imports the notes. "
                          "Right-click a note to remove it.",
                          exportHint, juce::Justification::topLeft, 4);

        g.drawFittedText (proc.isStandaloneApp()
                              ? juce::String ("Not in the standalone app: the mic would feed back through the speakers.")
                              : juce::String ("Keeps the audio already on this mixer track, so an instrument "
                                              "routed here stays audible while Humline plays it."),
                          routingHint, juce::Justification::topLeft, 4);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        rightPanel = area.removeFromRight (310);
        area.removeFromRight (16);
        leftPanel = area;

        grid.setBounds (leftPanel.reduced (16).withTrimmedTop (34));

        auto r = rightPanel.reduced (18, 0);
        exportHeader = r.removeFromTop (46);
        r.removeFromTop (2);

        filterCaption = r.removeFromTop (16);
        filterChoice.setBounds (r.removeFromTop (30));
        r.removeFromTop (14);

        dragTile.setBounds (r.removeFromTop (64));
        r.removeFromTop (12);

        auto buttons = r.removeFromTop (30);
        saveButton.setBounds (buttons.removeFromLeft ((buttons.getWidth() - 10) / 2));
        buttons.removeFromLeft (10);
        clearButton.setBounds (buttons);
        r.removeFromTop (10);

        exportHint = r.removeFromTop (48);
        r.removeFromTop (8);

        routingHeader = r.removeFromTop (46);
        passInput.setBounds (r.removeFromTop (30).withWidth (140));
        r.removeFromTop (8);
        routingHint = r.removeFromTop (48);
    }

private:
    void timerCallback() override { refresh (false); }

    TakeFilter currentFilter() const noexcept { return (TakeFilter) filterChoice.getSelected(); }

    void refresh (bool force)
    {
        const int version = proc.getTakeVersion();
        const auto filter = currentFilter();

        if (force || version != lastVersion)
        {
            lastVersion = version;
            grid.setTake (proc.getTake(), filter);

            int shown = 0;
            double first = 0.0, last = 0.0;

            for (const auto& n : grid.getNotes())
            {
                if (! takeFilterAccepts (filter, n))
                    continue;

                first = shown == 0 ? (double) n.startBeats : juce::jmin (first, (double) n.startBeats);
                last  = juce::jmax (last, (double) n.startBeats + (double) n.lengthBeats);
                ++shown;
            }

            const int bars = shown == 0 ? 0
                           : juce::jmax (1, (int) std::ceil ((last - takeOriginBeats (grid.getNotes(), filter)) / 4.0 - 1.0e-6));

            stats = shown == 0 ? juce::String ("empty")
                               : juce::String (shown) + (shown == 1 ? " note" : " notes") + "  |  "
                                     + juce::String (bars) + (bars == 1 ? " bar" : " bars") + "  |  "
                                     + juce::String (juce::roundToInt (proc.getTakeBpm())) + " BPM";

            dragTile.setActive (shown > 0);
            saveButton.setEnabled (shown > 0);
            clearButton.setEnabled (! grid.getNotes().empty());
            repaint (leftPanel.withHeight (46));
        }

        // The name follows the key, which can change at any time.
        dragTile.setFileName (proc.takeFileName (filter));
    }

    void dragTakeOut()
    {
        const auto file = proc.writeTakeToTempFile (currentFilter());

        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void saveTake()
    {
        const auto filter = currentFilter();

        if (! lastSaveDir.isDirectory())
            lastSaveDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

        chooser = std::make_unique<juce::FileChooser> ("Save the take as a MIDI file",
                                                       lastSaveDir.getChildFile (proc.takeFileName (filter)),
                                                       "*.mid");

        const int chooserFlags = juce::FileBrowserComponent::saveMode
                               | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting;

        chooser->launchAsync (chooserFlags, [this, filter] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File())
                return;

            file = file.withFileExtension (".mid");
            lastSaveDir = file.getParentDirectory();

            juce::MemoryOutputStream data;

            if (! proc.writeTakeMidi (data, filter) || ! file.replaceWithData (data.getData(), data.getDataSize()))
                juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon, "Humline",
                                                        "Could not save " + file.getFullPathName());
        });
    }

    HumlineProcessor& proc;

    TakeGrid grid;
    SegmentedChoice filterChoice;
    DragTile dragTile;
    juce::TextButton saveButton { "Save .mid" }, clearButton { "Clear" };
    TogglePill passInput;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastSaveDir;

    juce::String stats;
    int lastVersion = -1;

    juce::Rectangle<int> leftPanel, rightPanel, exportHeader, routingHeader, filterCaption, exportHint, routingHint;
};

} // namespace dbc::ui
