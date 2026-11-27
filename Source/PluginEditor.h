#pragma once

#include "PluginProcessor.h"
#include "ui/Panels.h"
#include "ui/InstrumentScreen.h"

namespace dbc
{

class HumlineEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit HumlineEditor (HumlineProcessor&);
    ~HumlineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showTargetMenu();
    void refreshTarget();

    HumlineProcessor& proc;

    ui::HumlineLNF lnf;
    ui::TabBar tabs { juce::StringArray { "Play", "Train", "Key", "Chords", "Notes", "Assign" } };

    ui::PlayPanel   play;
    ui::TrainPanel  train;
    ui::KeyPanel    key;
    ui::ChordsPanel chords;
    ui::NotesPanel  notes;
    ui::AssignPanel assign;
    ui::MonitorStrip monitor;

    /** Covers the pages while nothing is attached to the MIDI out. */
    ui::InstrumentScreen instrumentScreen;

    /** Header chip: what the MIDI out is driving. Click for check / pick by hand. */
    juce::TextButton targetButton;

    /** Standalone only: opens the audio device / microphone chooser. Hidden in a DAW,
        where the host owns input routing. */
    juce::TextButton audioButton { "Audio / MIDI settings" };

    juce::Rectangle<int> headerArea, footerArea, contentArea;
    int lastUiVersion = -1;
    int housekeepingTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumlineEditor)
};

} // namespace dbc
