#pragma once

#include <juce_core/juce_core.h>

namespace dbc::standalone
{

// The standalone app owns its audio device; a plugin's input is chosen by the
// host instead. These are no-ops (and inputStatus() is empty) inside a DAW.

bool isRunning();
void showAudioSettings();

/** Device name while an input is open, "NO MIC" when none is, empty in a DAW. */
juce::String inputStatus();

/** JUCE's standalone wrapper mutes the audio input by default to avoid a
    feedback loop. Humline never passes the mic to the output, so there is no
    loop: switch the mute off, otherwise the plugin only ever hears silence. */
void unmuteInput();

/** The standalone player only picks up the MIDI output device when the audio
    device starts; call this periodically so choosing a MIDI output in the
    settings dialog takes effect straight away. */
void syncMidiOutput();

/** Name of the MIDI output device in use, empty if none (or in a DAW). */
juce::String midiOutputName();

} // namespace dbc::standalone
