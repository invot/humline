#include "PluginEditor.h"
#include "StandaloneBridge.h"

namespace dbc
{

HumlineEditor::HumlineEditor (HumlineProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      play (p),
      train (p),
      key (p),
      chords (p),
      notes (p),
      assign (p),
      instrumentScreen (p)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (tabs);
    addAndMakeVisible (play);
    addAndMakeVisible (train);
    addAndMakeVisible (key);
    addAndMakeVisible (chords);
    addAndMakeVisible (notes);
    addAndMakeVisible (assign);
    addAndMakeVisible (monitor);
    addChildComponent (instrumentScreen);   // above the pages
    addChildComponent (audioButton);
    addAndMakeVisible (targetButton);

    targetButton.setColour (juce::TextButton::buttonColourId, ui::pal::panelHi);
    targetButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    targetButton.onClick = [this] { showTargetMenu(); };

    audioButton.setColour (juce::TextButton::buttonColourId, ui::pal::panelHi);
    audioButton.setTooltip ("Choose the microphone, the audio output and the MIDI output device");
    audioButton.onClick = [] { standalone::showAudioSettings(); };
    audioButton.setVisible (standalone::isRunning());

    train.setVisible (false);
    key.setVisible (false);
    chords.setVisible (false);
    notes.setVisible (false);
    assign.setVisible (false);

    monitor.setInterceptsMouseClicks (false, false);

    tabs.onTabChanged = [this] (int index)
    {
        play.setVisible (index == 0);
        train.setVisible (index == 1);
        key.setVisible (index == 2);
        chords.setVisible (index == 3);
        notes.setVisible (index == 4);
        assign.setVisible (index == 5);
    };

    setResizable (true, true);
    setResizeLimits (960, 620, 1600, 1000);
    setSize (1180, 740);

    play.refreshPadNames();

    standalone::unmuteInput();
    standalone::syncMidiOutput();

    refreshTarget();
    startTimerHz (30);
}

HumlineEditor::~HumlineEditor()
{
    proc.setAutoCheck (false);
    stopTimer();
    setLookAndFeel (nullptr);
}

void HumlineEditor::paint (juce::Graphics& g)
{
    using namespace ui;

    g.fillAll (pal::bg);

    // ---- header -----------------------------------------------------------
    auto h = headerArea.toFloat();

    g.setColour (pal::header);
    g.fillRoundedRectangle (h, 14.0f);

    // Logo: an H with one line running through it - the hum line doubling as
    // the letter's crossbar.
    const float logoSize = 26.0f;
    auto logo = juce::Rectangle<float> (h.getX() + 16.0f, h.getCentreY() - logoSize * 0.5f, logoSize, logoSize);

    const float stemTop    = logo.getY() + 5.0f;
    const float stemBottom = logo.getBottom() - 5.0f;
    const float stemLeft   = logo.getX() + 8.0f;
    const float stemRight  = logo.getRight() - 8.0f;

    juce::Path letterH;
    letterH.startNewSubPath (stemLeft, stemTop);
    letterH.lineTo (stemLeft, stemBottom);
    letterH.startNewSubPath (stemRight, stemTop);
    letterH.lineTo (stemRight, stemBottom);

    g.setColour (pal::text);
    g.strokePath (letterH, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    // The line runs past both stems, so the H reads as crossed by it.
    juce::Path humLine;
    humLine.startNewSubPath (logo.getX() + 0.5f, logo.getCentreY());
    humLine.lineTo (logo.getRight() - 0.5f, logo.getCentreY());

    g.setColour (pal::teal);
    g.strokePath (humLine, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

    g.setColour (pal::text);
    g.setFont (uiFont (18.0f, true));
    g.drawText ("humline", h.withTrimmedLeft (54.0f).withWidth (220.0f),
                juce::Justification::centredLeft, false);

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));
    g.drawText ("vocal  >  midi", h.withTrimmedLeft (142.0f).withWidth (160.0f),
                juce::Justification::centredLeft, false);

    // input meter
    auto meterArea = juce::Rectangle<float> (h.getRight() - 300.0f, h.getCentreY() - 4.0f, 150.0f, 8.0f);

    // In the standalone app the caption names the device, so a missing microphone
    // is visible instead of just being silent.
    const auto status = standalone::inputStatus();
    const juce::String inputCaption = status.isNotEmpty() ? status : juce::String ("INPUT");

    g.setColour (pal::dim);
    g.setFont (uiFont (10.0f, true));
    g.drawFittedText (inputCaption, meterArea.translated (0.0f, -16.0f).withHeight (12.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    g.setColour (pal::panelSoft);
    g.fillRoundedRectangle (meterArea, 4.0f);

    const float db = juce::jlimit (-60.0f, 0.0f, proc.inputLevelDb.load());
    const float norm = juce::jmap (db, -60.0f, 0.0f, 0.0f, 1.0f);

    if (norm > 0.01f)
    {
        g.setColour (db > -3.0f ? pal::pink : pal::teal);
        g.fillRoundedRectangle (meterArea.withWidth (juce::jmax (4.0f, meterArea.getWidth() * norm)), 4.0f);
    }

    // MIDI out indicator
    auto midiDot = juce::Rectangle<float> (h.getRight() - 120.0f, h.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (pal::purple);
    g.fillEllipse (midiDot);
    g.setColour (pal::text.withAlpha (0.8f));
    g.setFont (uiFont (11.0f, true));
    const auto midiDevice = standalone::midiOutputName();
    g.drawText (midiDevice.isNotEmpty() ? midiDevice.toUpperCase() : juce::String ("MIDI OUT"), midiDot.translated (16.0f, -5.0f).withWidth (90.0f).withHeight (20.0f),
                juce::Justification::centredLeft, false);

    // ---- footer -----------------------------------------------------------
    auto f = footerArea.toFloat();
    g.setColour (pal::header);
    g.fillRoundedRectangle (f, 12.0f);

    g.setColour (pal::dim);
    g.setFont (uiFont (11.0f));

    const int offsetMs = juce::roundToInt (1000.0 * proc.engine.getOutputLatencySamples()
                                               / juce::jmax (1.0, proc.getSampleRate()));

    const auto mode = proc.buildSettings().mode;
    const auto target = proc.getTarget();
    const auto& check = proc.getLastInstrumentCheck();
    juce::String footerText;

    if (target == InstrumentTarget::None)
        footerText = "No instrument attached - Humline sends no MIDI until a synth or drum kit answers.";
    else if (mode == EngineMode::Both)
        footerText = juce::String (target == InstrumentTarget::DrumKit ? "Drum kit" : "Instrument")
                   + ", quantised - snapped to the grid.    MIDI offset: " + juce::String (offsetMs) + " ms";
    else
        footerText = target == InstrumentTarget::DrumKit
                       ? juce::String ("Drum kit - every hit becomes a drum trigger, live (no delay).")
                       : juce::String ("Instrument - your singing becomes notes, live (no delay).");

    // The instrument answered on the main input: it will be heard as the voice.
    if (target != InstrumentTarget::None && check.count > 0 && check.result.heardOnMainInput
        && check.result.target == target && ! proc.isStandaloneApp())
    {
        g.setColour (pal::pink);
        footerText = "The instrument plays into Humline's own input, so Humline may hear it as your voice. "
                     "Send it to the \"Instrument\" input instead.";
    }

    g.drawText (footerText,
                f.withTrimmedLeft (16.0f).withWidth (f.getWidth() - 460.0f),
                juce::Justification::centredLeft, true);
}

void HumlineEditor::resized()
{
    auto b = getLocalBounds();

    headerArea = b.removeFromTop (56).reduced (14, 8);
    b.removeFromTop (6);
    tabs.setBounds (b.removeFromTop (48).reduced (14, 0));
    b.removeFromTop (10);
    footerArea = b.removeFromBottom (48).reduced (14, 4);
    b.removeFromTop (2);

    contentArea = b.reduced (14, 0);

    play.setBounds (contentArea);
    train.setBounds (contentArea);
    key.setBounds (contentArea);
    chords.setBounds (contentArea);
    notes.setBounds (contentArea);
    assign.setBounds (contentArea);

    audioButton.setBounds (headerArea.getRight() - 470, headerArea.getCentreY() - 13, 150, 26);
    targetButton.setBounds (headerArea.getRight() - (audioButton.isVisible() ? 640 : 470),
                            headerArea.getCentreY() - 13, 160, 26);
    instrumentScreen.setBounds (contentArea);

    auto f = footerArea;
    monitor.setBounds (f.removeFromRight (400).reduced (0, 6));
}

void HumlineEditor::refreshTarget()
{
    const auto target = proc.getTarget();
    const bool checking = proc.isCheckingInstrument();

    const juce::String text = checking ? juce::String ("Checking...")
                            : target == InstrumentTarget::Instrument ? juce::String ("Instrument: synth")
                            : target == InstrumentTarget::DrumKit    ? juce::String ("Instrument: drum kit")
                            : juce::String ("No instrument");

    if (targetButton.getButtonText() != text)
    {
        targetButton.setButtonText (text);
        targetButton.setColour (juce::TextButton::buttonColourId,
                                target == InstrumentTarget::None ? ui::pal::pink.withAlpha (0.55f) : ui::pal::panelHi);
        repaint (footerArea);
    }

    const auto& check = proc.getLastInstrumentCheck();
    targetButton.setTooltip (check.count > 0 ? "Last check: " + juce::String (check.result.reason)
                                             : juce::String ("What Humline's MIDI out is driving. Click to check again "
                                                             "or pick it by hand."));

    const bool showScreen = target == InstrumentTarget::None;

    if (instrumentScreen.isVisible() != showScreen)
    {
        instrumentScreen.setVisible (showScreen);
        repaint (footerArea);
    }

    proc.setAutoCheck (showScreen && isShowing());
}

void HumlineEditor::showTargetMenu()
{
    const auto target = proc.getTarget();
    const bool playing = proc.isTransportPlaying();

    juce::PopupMenu menu;
    menu.addSectionHeader ("What is Humline playing?");
    menu.addItem (1, playing ? "Check again (paused while the transport plays)"
                             : "Check again (plays 3 test notes)",
                  ! proc.isCheckingInstrument() && ! playing);
    menu.addSeparator();
    menu.addItem (2, "A synth / instrument", true, target == InstrumentTarget::Instrument);
    menu.addItem (3, "A drum kit", true, target == InstrumentTarget::DrumKit);
    menu.addItem (4, "Nothing (show the setup screen)", true, target == InstrumentTarget::None);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&targetButton),
                        [safe = juce::Component::SafePointer<HumlineEditor> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            auto& p = safe->proc;

                            switch (result)
                            {
                                case 1: p.startInstrumentCheck(); break;
                                case 2: p.setTarget (InstrumentTarget::Instrument); break;
                                case 3: p.setTarget (InstrumentTarget::DrumKit); break;
                                case 4: p.setTarget (InstrumentTarget::None); break;
                                default: break;
                            }

                            safe->refreshTarget();
                        });
}

void HumlineEditor::timerCallback()
{
    monitor.setState (proc.getLiveState());
    refreshTarget();

    const int version = proc.uiStateVersion.load();

    if (version != lastUiVersion)
    {
        lastUiVersion = version;
        play.refreshPadNames();
        train.refreshPadNames();
    }

    // Standalone housekeeping, about once a second: keep the mic unmuted and
    // follow the MIDI output chosen in the settings dialog.
    if (++housekeepingTicks >= 30)
    {
        housekeepingTicks = 0;
        standalone::unmuteInput();
        standalone::syncMidiOutput();
    }

    repaint (headerArea);
}

} // namespace dbc
