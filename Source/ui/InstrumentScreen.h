#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace dbc::ui
{

//==============================================================================
/** Shown over the pages while Humline's MIDI out is not driving anything: asks
    for a synth (or a drum kit), explains how to hook one up, and keeps checking
    for one in the background. */
class InstrumentScreen : public juce::Component, private juce::Timer
{
public:
    explicit InstrumentScreen (HumlineProcessor& p)
        : proc (p)
    {
        for (auto* b : { &checkButton, &synthButton, &kitButton })
        {
            addAndMakeVisible (b);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        checkButton.setColour (juce::TextButton::buttonColourId, pal::teal.withAlpha (0.85f));
        checkButton.setTooltip ("Plays your instrument three short test notes and listens for the answer. "
                                "Paused while the transport plays, so no test notes land in a recording.");
        checkButton.onClick = [this] { proc.startInstrumentCheck(); refresh(); };

        synthButton.setColour (juce::TextButton::buttonColourId, pal::panelHi);
        synthButton.setTooltip ("Skip the check: Humline sends sung notes and chords");
        synthButton.onClick = [this] { proc.setTarget (InstrumentTarget::Instrument); };

        kitButton.setColour (juce::TextButton::buttonColourId, pal::panelHi);
        kitButton.setTooltip ("Skip the check: Humline sends beatbox triggers");
        kitButton.onClick = [this] { proc.setTarget (InstrumentTarget::DrumKit); };

        startTimerHz (4);
    }

    ~InstrumentScreen() override { stopTimer(); }

    void visibilityChanged() override { refresh(); }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        drawPanel (g, area);

        auto inner = getLocalBounds().reduced (40, 30);

        // A keyboard-ish glyph: three keys, the middle one lit (dropped when the
        // window is too short for it).
        auto glyph = inner.removeFromTop (glyphHeight()).withSizeKeepingCentre (96, 56).toFloat();
        for (int i = 0; glyphHeight() > 0 && i < 3; ++i)
        {
            auto key = glyph.withWidth (28.0f).withX (glyph.getX() + (float) i * 34.0f);
            g.setColour (i == 1 ? pal::teal : pal::panelSoft);
            g.fillRoundedRectangle (key, 6.0f);
        }

        inner.removeFromTop (14);
        g.setColour (pal::text);
        g.setFont (uiFont (26.0f, true));
        g.drawText ("Add a synth to play with", inner.removeFromTop (34), juce::Justification::centred, false);

        inner.removeFromTop (6);
        g.setColour (pal::dim);
        g.setFont (uiFont (13.5f));
        g.drawFittedText ("Humline turns your voice into MIDI, but it makes no sound of its own. "
                          "Give it an instrument: a synth for singing, or a drum kit for beatboxing. "
                          "Humline listens for it and switches mode to suit.",
                          inner.removeFromTop (44), juce::Justification::centred, 3);

        inner.removeFromTop (14);

        // How to hook it up.
        auto steps = inner.removeFromTop (150).withSizeKeepingCentre (juce::jmin (640, inner.getWidth()), 150);
        g.setColour (pal::panelSoft);
        g.fillRoundedRectangle (steps.toFloat(), 12.0f);

        auto s = steps.reduced (20, 14);
        g.setColour (pal::text);
        g.setFont (uiFont (12.5f, true));
        g.drawText ("How to connect it", s.removeFromTop (20), juce::Justification::centredLeft, false);
        s.removeFromTop (4);

        const char* lines[] {
            "1.  Add a synth (or a drum kit) to your project.",
            "2.  Send Humline's MIDI out to it. In FL Studio: give Humline a MIDI output port in its",
            "     wrapper settings, and set the instrument's MIDI input port to the same number.",
            "3.  So Humline can hear it, sidechain the instrument's mixer track into Humline's track",
            "     and connect it to Humline's \"Instrument\" input.",
        };

        g.setFont (uiFont (12.5f));
        g.setColour (pal::dim);

        for (auto* line : lines)
            g.drawText (line, s.removeFromTop (20), juce::Justification::centredLeft, false);

        // Status of the background check.
        g.setColour (statusColour);
        g.setFont (uiFont (12.5f, true));
        g.drawFittedText (status, statusArea, juce::Justification::centred, 2);
    }

    void resized() override
    {
        auto inner = getLocalBounds().reduced (40, 30);
        inner.removeFromTop (glyphHeight() + 14 + 34 + 6 + 44 + 14 + 150 + 18);

        statusArea = inner.removeFromTop (36);
        inner.removeFromTop (10);

        auto row = inner.removeFromTop (36).withSizeKeepingCentre (500, 36);
        checkButton.setBounds (row.removeFromLeft (160));
        row.removeFromLeft (12);
        synthButton.setBounds (row.removeFromLeft (158));
        row.removeFromLeft (12);
        kitButton.setBounds (row.removeFromLeft (158));
    }

private:
    int glyphHeight() const noexcept { return getHeight() >= 560 ? 70 : 0; }

    void timerCallback() override { refresh(); }

    void refresh()
    {
        juce::String text;
        auto colour = pal::dim;

        const auto& last = proc.getLastInstrumentCheck();

        if (proc.isCheckingInstrument())
        {
            text = "Listening for an instrument...";
            colour = pal::teal;
        }
        else if (proc.isTransportPlaying())
        {
            text = "Checks are paused while the transport plays, so no test notes land in a recording.";
        }
        else if (last.count > 0)
        {
            const int secs = juce::jmax (0, (int) (juce::Time::getCurrentTime() - last.when).inSeconds());
            text = "Nothing answered the last check (" + juce::String (secs) + " s ago). "
                   "Humline checks again every few seconds.";
            colour = pal::pink;

            if (! proc.hasInstrumentInput() && ! proc.isStandaloneApp())
                text << "\nHumline's \"Instrument\" input is not connected, so it can only hear its main input.";
        }
        else
        {
            text = "Humline checks for an instrument every few seconds.";
        }

        checkButton.setEnabled (! proc.isCheckingInstrument() && ! proc.isTransportPlaying());

        if (text != status || colour != statusColour)
        {
            status = text;
            statusColour = colour;
            repaint (statusArea);
        }
    }

    HumlineProcessor& proc;

    juce::TextButton checkButton { "Check now" };
    juce::TextButton synthButton { "It's a synth" };
    juce::TextButton kitButton   { "It's a drum kit" };

    juce::String status;
    juce::Colour statusColour = pal::dim;
    juce::Rectangle<int> statusArea;
};

} // namespace dbc::ui
