#pragma once

#include "Theme.h"
#include "../core/KeyDetector.h"
#include "../core/Types.h"

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

namespace dbc::ui
{

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
/** Caption on the left, thin slider in the middle, live value on the right. */
class MicSlider : public juce::Component
{
public:
    MicSlider (juce::String caption, juce::Colour accent, juce::String suffix = {})
        : cap (std::move (caption)), suffixText (std::move (suffix))
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::thumbColourId, accent);
        slider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.65f));
        slider.setColour (juce::Slider::backgroundColourId, pal::stroke);
        addAndMakeVisible (slider);
    }

    void attach (APVTS& state, const juce::String& id, bool suffixFromValue = true)
    {
        attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
        showSuffix = suffixFromValue;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        g.setColour (pal::dim);
        g.setFont (uiFont (11.5f));
        g.drawText (cap, r.removeFromLeft (82).toFloat(), juce::Justification::centredLeft, false);

        g.setFont (uiFont (11.5f, true));
        g.setColour (pal::text.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (slider.getValue())) + (showSuffix ? suffixText : juce::String()),
                    r.removeFromRight (44).toFloat(), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (82);
        r.removeFromRight (44);
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::String cap, suffixText;
    bool showSuffix = true;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

//==============================================================================
class LabelledCombo : public juce::Component
{
public:
    explicit LabelledCombo (juce::String caption)
        : cap (std::move (caption))
    {
        addAndMakeVisible (box);
    }

    void attach (APVTS& state, const juce::String& id)
    {
        // JUCE 6's ComboBoxAttachment does not fill the box: it maps item indices
        // onto the parameter range, so the items have to exist first.
        if (auto* parameter = state.getParameter (id))
        {
            const auto items = parameter->getAllValueStrings();

            if (! items.isEmpty())
            {
                box.clear (juce::dontSendNotification);
                box.addItemList (items, 1);
            }
        }

        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, id, box);
    }

    void paint (juce::Graphics& g) override
    {
        drawCaption (g, getLocalBounds().removeFromTop (14), cap);
    }

    void resized() override
    {
        box.setBounds (getLocalBounds().withTrimmedTop (16));
    }

    juce::ComboBox box;

private:
    juce::String cap;
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

//==============================================================================
class TogglePill : public juce::Component
{
public:
    TogglePill (const juce::String& text, juce::Colour accent)
    {
        button.setButtonText (text);
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, accent);
        addAndMakeVisible (button);
    }

    void attach (APVTS& state, const juce::String& id)
    {
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, id, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::TextButton button;

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

//==============================================================================
class TabBar : public juce::Component
{
public:
    explicit TabBar (juce::StringArray tabs)
        : labels (std::move (tabs))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getSelected() const noexcept { return selected; }

    void setSelected (int index, bool notify)
    {
        index = juce::jlimit (0, labels.size() - 1, index);

        if (index == selected)
            return;

        selected = index;
        repaint();

        if (notify && onTabChanged)
            onTabChanged (selected);
    }

    std::function<void (int)> onTabChanged;

    void paint (juce::Graphics& g) override
    {
        auto full = getLocalBounds().toFloat();

        g.setColour (pal::panel);
        g.fillRoundedRectangle (full, 12.0f);

        const float w = full.getWidth() / (float) labels.size();

        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = full.withWidth (w).withX (full.getX() + (float) i * w).reduced (3.0f, 4.0f);

            if (i == selected)
            {
                g.setColour (pal::panelHi);
                g.fillRoundedRectangle (r, 9.0f);
                g.setColour (juce::Colours::white);
            }
            else
            {
                g.setColour (pal::dim);
            }

            g.setFont (uiFont (15.0f, i == selected));
            g.drawText (labels[i], r, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float w = (float) getWidth() / (float) labels.size();
        const int index = juce::jlimit (0, labels.size() - 1, (int) ((float) e.x / w));
        setSelected (index, true);
    }

private:
    juce::StringArray labels;
    int selected = 0;
};

//==============================================================================
class TriggerPads : public juce::Component
{
public:
    TriggerPads()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setNames (const juce::StringArray& n) { names = n; repaint(); }

    void setMutes (const bool* m)
    {
        for (int i = 0; i < dbc::numDrumPads; ++i)
            muted[i] = m[i];

        repaint();
    }

    void setSelected (int index) { selected = index; repaint(); }
    int  getSelected() const noexcept { return selected; }

    void setFlash (const float* f)
    {
        bool changed = false;

        for (int i = 0; i < dbc::numDrumPads; ++i)
        {
            if (std::abs (flash[i] - f[i]) > 0.01f)
            {
                flash[i] = f[i];
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    std::function<void (int)> onPadClicked;

    static juce::Rectangle<float> padBounds (juce::Rectangle<float> area, int index)
    {
        const float gap = 10.0f;
        const float w = (area.getWidth() - gap) * 0.5f;
        const float h = (area.getHeight() - gap * 3.0f) / 4.0f;
        const int col = index % 2;
        const int row = index / 2;

        return { area.getX() + (float) col * (w + gap),
                 area.getY() + (float) row * (h + gap), w, h };
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();

        for (int i = 0; i < dbc::numDrumPads; ++i)
        {
            auto r = padBounds (area, i);
            const bool sel = (i == selected);
            const float f = flash[i];

            juce::Colour base = sel ? pal::panelHi.brighter (0.25f) : pal::panelSoft;

            if (f > 0.01f)
                base = base.interpolatedWith (pal::orange, juce::jlimit (0.0f, 0.8f, f));

            g.setColour (base);
            g.fillRoundedRectangle (r, 10.0f);

            if (sel)
            {
                g.setColour (pal::gold);
                g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.6f);
            }

            g.setColour (muted[i] ? pal::dim.withAlpha (0.5f) : pal::orange.withAlpha (sel ? 1.0f : 0.85f));
            g.setFont (uiFont (13.5f));
            g.drawFittedText (names[i].isEmpty() ? "Pad " + juce::String (i + 1) : names[i],
                              r.reduced (12.0f, 8.0f).toNearestInt(),
                              juce::Justification::topLeft, 3);

            if (muted[i])
            {
                g.setColour (pal::dim.withAlpha (0.6f));
                g.setFont (uiFont (12.0f, true));
                g.drawText ("muted", r.reduced (12.0f, 8.0f), juce::Justification::bottomRight, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto area = getLocalBounds().toFloat();

        for (int i = 0; i < dbc::numDrumPads; ++i)
        {
            if (padBounds (area, i).contains (e.position))
            {
                selected = i;
                repaint();

                if (onPadClicked)
                    onPadClicked (i);

                return;
            }
        }
    }

private:
    juce::StringArray names;
    bool  muted[dbc::numDrumPads] {};
    float flash[dbc::numDrumPads] {};
    int   selected = 0;
};

//==============================================================================
class PitchDial : public juce::Component
{
public:
    PitchDial() { setInterceptsMouseClicks (true, true); }

    std::function<void (int)> onNoteClicked;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! onNoteClicked)
            return;

        auto b = getLocalBounds().toFloat();
        const auto c = b.getCentre();
        const float dx = e.position.x - c.x;
        const float dy = e.position.y - c.y;

        if (std::sqrt (dx * dx + dy * dy) < 16.0f)
            return;

        float angle = std::atan2 (dy, dx) + juce::MathConstants<float>::halfPi;

        while (angle < 0.0f)
            angle += juce::MathConstants<float>::twoPi;

        const int index = (int) (angle / (juce::MathConstants<float>::twoPi / 12.0f)) % 12;
        onNoteClicked (index);
    }

    void setState (int pitchClass, bool voiced, const float* energy)
    {
        bool changed = (pitchClass != activePc) || (voiced != wasVoiced);

        activePc = pitchClass;
        wasVoiced = voiced;

        for (int i = 0; i < 12; ++i)
        {
            if (std::abs (energy[i] - energies[i]) > 0.02f)
            {
                energies[i] = energy[i];
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    /** Notes chosen on the Key page (clicked, or heard during Key Detect). */
    void setSelectedNotes (const std::array<bool, 12>& sel)
    {
        if (sel != selectedNotes)
        {
            selectedNotes = sel;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        const auto centre = b.getCentre();
        const float R = size * 0.5f - 4.0f;
        const float segW = juce::MathConstants<float>::twoPi / 12.0f;

        g.setColour (pal::panelSoft.withAlpha (0.55f));
        g.fillEllipse (centre.x - R * 0.62f, centre.y - R * 0.62f, R * 1.24f, R * 1.24f);
        g.setColour (pal::stroke.withAlpha (0.7f));
        g.drawEllipse (centre.x - R * 0.62f, centre.y - R * 0.62f, R * 1.24f, R * 1.24f, 1.0f);

        for (int i = 0; i < 12; ++i)
        {
            const float a0 = -juce::MathConstants<float>::halfPi + (float) i * segW + segW * 0.10f;
            const float a1 = a0 + segW * 0.80f;

            // addPieSegment measures angles clockwise from 12 o'clock, while a0/a1
            // (and the label placement below) are measured from 3 o'clock.
            juce::Path p;
            p.addPieSegment (juce::Rectangle<float> (centre.x - R, centre.y - R, R * 2.0f, R * 2.0f),
                              a0 + juce::MathConstants<float>::halfPi,
                              a1 + juce::MathConstants<float>::halfPi, 0.66f);

            juce::Colour c = pal::panelSoft;

            if (i == activePc)
                c = pal::coral;
            else if (selectedNotes[(size_t) i])
                c = pal::gold.withAlpha (0.85f);
            else if (energies[i] > 0.05f)
                c = pal::coral.withAlpha (juce::jlimit (0.10f, 0.55f, energies[i] * 0.6f));
            else
                c = pal::panelSoft.brighter (0.08f);

            g.setColour (c);
            g.fillPath (p);

            const float am = (a0 + a1) * 0.5f;
            const float lx = centre.x + std::cos (am) * R * 0.83f;
            const float ly = centre.y + std::sin (am) * R * 0.83f;

            g.setColour ((i == activePc || selectedNotes[(size_t) i]) ? juce::Colours::black.withAlpha (0.85f)
                                                                      : pal::text.withAlpha (0.8f));
            g.setFont (uiFont (13.0f, true));
            g.drawText (dbc::pitchClassName (i),
                        juce::Rectangle<float> (lx - 26.0f, ly - 9.0f, 52.0f, 18.0f),
                        juce::Justification::centred, false);
        }

        if (wasVoiced && activePc >= 0)
        {
            const float am = -juce::MathConstants<float>::halfPi + ((float) activePc + 0.5f) * segW;
            juce::Path needle;
            needle.startNewSubPath (centre.x + std::cos (am) * R * 0.20f, centre.y + std::sin (am) * R * 0.20f);
            needle.lineTo (centre.x + std::cos (am) * R * 0.94f, centre.y + std::sin (am) * R * 0.94f);
            g.setColour (pal::coral.withAlpha (0.85f));
            g.strokePath (needle, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        g.setColour (pal::stroke.withAlpha (0.9f));
        g.drawEllipse (centre.x - R * 0.30f, centre.y - R * 0.30f, R * 0.60f, R * 0.60f, 1.0f);
    }

private:
    int   activePc = -1;
    bool  wasVoiced = false;
    float energies[12] {};
    std::array<bool, 12> selectedNotes {};
};

//==============================================================================
/** A row of mutually exclusive segments (e.g. Beatbox | Melody | Both). */
class SegmentedChoice : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    SegmentedChoice (juce::StringArray labelsToUse, juce::Colour accentColour)
        : labels (std::move (labelsToUse)), accent (accentColour)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onChange;

    int  getSelected() const noexcept { return selected; }

    void setSelected (int index)
    {
        index = juce::jlimit (0, labels.size() - 1, index);

        if (index != selected)
        {
            selected = index;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setColour (pal::panelSoft);
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);

        const float w = b.getWidth() / (float) labels.size();

        for (int i = 0; i < labels.size(); ++i)
        {
            auto r = juce::Rectangle<float> (b.getX() + (float) i * w, b.getY(), w, b.getHeight()).reduced (3.0f);

            if (i == selected)
            {
                g.setColour (accent);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (juce::Colours::black.withAlpha (0.85f));
            }
            else
            {
                g.setColour (pal::text.withAlpha (0.75f));
            }

            g.setFont (uiFont (13.0f, i == selected));
            g.drawText (labels[i], r, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = juce::jlimit (0, labels.size() - 1,
                                        (int) (e.position.x / ((float) getWidth() / (float) labels.size())));

        if (index != selected)
        {
            setSelected (index);

            if (onChange)
                onChange (index);
        }
    }

private:
    juce::StringArray labels;
    juce::Colour accent;
    int selected = 0;
};

//==============================================================================
/** Ranked key suggestions; clicking a row applies that key. */
class KeySuggestionList : public juce::Component
{
public:
    KeySuggestionList() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setSuggestions (std::vector<dbc::KeySuggestion> s, int currentRoot, dbc::ScaleType currentScale)
    {
        const bool same = s.size() == items.size() && currentRoot == root && currentScale == scale
                          && std::equal (s.begin(), s.end(), items.begin(),
                                         [] (const dbc::KeySuggestion& a, const dbc::KeySuggestion& b)
                                         {
                                             return a.root == b.root && a.scale == b.scale
                                                    && std::abs (a.score - b.score) < 0.01f;
                                         });

        if (same)
            return;

        items = std::move (s);
        root = currentRoot;
        scale = currentScale;
        repaint();
    }

    void setEmptyText (const juce::String& t) { if (t != emptyText) { emptyText = t; repaint(); } }

    std::function<void (const dbc::KeySuggestion&)> onPicked;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        if (items.empty())
        {
            g.setColour (pal::panelSoft.withAlpha (0.6f));
            g.fillRoundedRectangle (b, 10.0f);
            g.setColour (pal::dim);
            g.setFont (uiFont (12.0f));
            g.drawFittedText (emptyText, b.reduced (20.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        const float rowH = rowHeight();

        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& k = items[i];
            auto r = juce::Rectangle<float> (b.getX(), b.getY() + (float) i * rowH, b.getWidth(), rowH - 4.0f);
            const bool current = k.root == root && k.scale == scale;
            const bool hover = (int) i == hoverRow;

            g.setColour (current ? pal::gold.withAlpha (0.22f) : (hover ? pal::panelHi : pal::panelSoft));
            g.fillRoundedRectangle (r, 8.0f);

            if (current)
            {
                g.setColour (pal::gold);
                g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.4f);
            }

            auto inner = r.reduced (14.0f, 0.0f);

            g.setColour (pal::text);
            g.setFont (uiFont (14.0f, true));
            g.drawText (dbc::pitchClassName (k.root), inner.removeFromLeft (40.0f), juce::Justification::centredLeft, false);

            g.setFont (uiFont (14.0f));
            g.drawText (dbc::scaleName (k.scale), inner.removeFromLeft (110.0f), juce::Justification::centredLeft, false);

            auto right = inner.removeFromRight (90.0f);
            g.setColour (current ? pal::gold : pal::dim);
            g.setFont (uiFont (11.5f, true));
            g.drawText (current ? "current key" : (hover ? "apply" : juce::String (juce::roundToInt (k.match * 100.0f)) + "% in key"),
                        right, juce::Justification::centredRight, false);

            // Match dots: how well the notes fit this key's profile.
            const int filled = juce::jlimit (0, 8, juce::roundToInt ((k.score + 0.05f) * 9.0f));
            const float dot = 6.0f;

            for (int d = 0; d < 8; ++d)
            {
                g.setColour (d < filled ? pal::coral : pal::stroke);
                g.fillEllipse (inner.getX() + (float) d * (dot + 4.0f), inner.getCentreY() - dot * 0.5f, dot, dot);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.position.y);

        if (r != hoverRow)
        {
            hoverRow = r;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverRow = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.position.y);

        if (r >= 0 && onPicked)
            onPicked (items[(size_t) r]);
    }

private:
    float rowHeight() const noexcept { return juce::jmin (38.0f, (float) getHeight() / 5.0f); }

    int rowAt (float y) const noexcept
    {
        if (items.empty())
            return -1;

        const int r = (int) (y / rowHeight());
        return (r >= 0 && r < (int) items.size()) ? r : -1;
    }

    std::vector<dbc::KeySuggestion> items;
    int root = 0;
    dbc::ScaleType scale = dbc::ScaleType::Major;
    int hoverRow = -1;
    juce::String emptyText { "Select notes on the wheel, or press Key Detect and sing, to get key suggestions." };
};

//==============================================================================
class MiniKeyboard : public juce::Component
{
public:
    MiniKeyboard() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void setRange (int lo, int hi) { low = lo; high = hi; repaint(); }

    void setScale (bool on, int root, dbc::ScaleType sc, int noteMask = dbc::allNotesMask)
    {
        const int r = ((root % 12) + 12) % 12;

        if (on == keyOn && r == keyRoot && sc == scale && noteMask == mask)
            return;

        keyOn = on;
        keyRoot = r;
        scale = sc;
        mask = noteMask & (int) dbc::allNotesMask;
        repaint();
    }

    /** In the scale and not switched off by hand. */
    bool isAllowed (int n) const noexcept
    {
        return dbc::scaleContains (scale, keyRoot, n) && (mask & (1 << (((n % 12) + 12) % 12))) != 0;
    }

    /** In the scale but switched off by hand (shown crossed out). */
    bool isSwitchedOff (int n) const noexcept
    {
        return keyOn && dbc::scaleContains (scale, keyRoot, n) && ! isAllowed (n);
    }

    void setActiveNote (int n)
    {
        if (n != active)
        {
            active = n;
            repaint();
        }
    }

    std::function<void (int)> onNoteClicked;

    static bool isBlackNote (int n)
    {
        const int pc = ((n % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        int whites = 0;

        for (int n = low; n <= high; ++n)
            if (! isBlackNote (n))
                ++whites;

        if (whites <= 0)
            return;

        const float w = b.getWidth() / (float) whites;
        const float h = b.getHeight();

        int index = 0;

        for (int n = low; n <= high; ++n)
        {
            if (isBlackNote (n))
                continue;

            auto r = juce::Rectangle<float> (b.getX() + (float) index * w, b.getY(), w, h);
            juce::Colour c (0xffd3d3d8);

            if (keyOn && isAllowed (n))
                c = c.interpolatedWith (pal::gold, 0.55f);

            if (keyOn && isAllowed (n) && (((n % 12) + 12) % 12) == keyRoot)
                c = c.interpolatedWith (pal::gold, 0.85f);

            if (n == active)
                c = pal::coral;

            g.setColour (c);
            g.fillRect (r.reduced (0.6f, 0.0f));

            if (isSwitchedOff (n))
            {
                g.setColour (pal::gold.withAlpha (0.7f));
                g.drawLine (r.getX() + w * 0.25f, r.getBottom() - 22.0f, r.getRight() - w * 0.25f, r.getBottom() - 30.0f, 1.5f);
            }

            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawLine (r.getX(), r.getY(), r.getX(), r.getBottom(), 1.0f);

            if (n % 12 == 0)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.setFont (uiFont (9.0f, true));
                g.drawText ("C" + juce::String (n / 12 - 1),
                            r.removeFromBottom (13.0f), juce::Justification::centred, false);
            }

            ++index;
        }

        int whitesSoFar = 0;

        for (int n = low; n <= high; ++n)
        {
            if (! isBlackNote (n))
            {
                ++whitesSoFar;
                continue;
            }

            const float bw = w * 0.62f;
            auto r = juce::Rectangle<float> (b.getX() + (float) whitesSoFar * w - bw * 0.5f,
                                             b.getY(), bw, h * 0.62f);

            juce::Colour c (0xff17171a);

            if (keyOn && isAllowed (n))
                c = c.interpolatedWith (pal::gold, 0.45f);
            else if (isSwitchedOff (n))
                c = c.interpolatedWith (pal::gold, 0.15f);

            if (n == active)
                c = pal::coral.darker (0.25f);

            g.setColour (c);
            g.fillRoundedRectangle (r, 2.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = noteAt (e.position);

        if (n >= 0 && onNoteClicked)
            onNoteClicked (n);
    }

    int noteAt (juce::Point<float> p) const
    {
        auto b = getLocalBounds().toFloat();

        int whites = 0;

        for (int n = low; n <= high; ++n)
            if (! isBlackNote (n))
                ++whites;

        if (whites <= 0 || ! b.contains (p))
            return -1;

        const float w = b.getWidth() / (float) whites;
        const float h = b.getHeight();

        int whitesSoFar = 0;

        for (int n = low; n <= high; ++n)
        {
            if (! isBlackNote (n))
            {
                ++whitesSoFar;
                continue;
            }

            const float bw = w * 0.62f;
            auto r = juce::Rectangle<float> (b.getX() + (float) whitesSoFar * w - bw * 0.5f,
                                             b.getY(), bw, h * 0.62f);

            if (r.contains (p))
                return n;
        }

        const int whiteIndex = juce::jlimit (0, whites - 1, (int) ((p.x - b.getX()) / w));
        int count = 0;

        for (int n = low; n <= high; ++n)
        {
            if (isBlackNote (n))
                continue;

            if (count == whiteIndex)
                return n;

            ++count;
        }

        return -1;
    }

private:
    int low = 48, high = 84;
    int active = -1, keyRoot = 0;
    int mask = dbc::allNotesMask;
    bool keyOn = true;
    dbc::ScaleType scale = dbc::ScaleType::Major;
};

//==============================================================================
/** Caption, then a two-thumb slider bound to a min and a max parameter.
    (APVTS has no attachment for two-value sliders, so it syncs by hand.) */
class RangeRow : public juce::Component, private juce::Timer
{
public:
    RangeRow (juce::String caption, juce::Colour accent)
        : cap (std::move (caption))
    {
        slider.setSliderStyle (juce::Slider::TwoValueHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::thumbColourId, accent);
        slider.setColour (juce::Slider::trackColourId, accent.withAlpha (0.65f));
        slider.setColour (juce::Slider::backgroundColourId, pal::stroke);
        addAndMakeVisible (slider);
    }

    ~RangeRow() override { stopTimer(); }

    void attach (APVTS& state, const juce::String& minId, const juce::String& maxId)
    {
        minParam = state.getParameter (minId);
        maxParam = state.getParameter (maxId);

        if (minParam == nullptr || maxParam == nullptr)
            return;

        const auto range = state.getParameterRange (minId);
        slider.setRange (range.start, range.end, range.interval > 0.0f ? range.interval : 1.0);

        slider.onDragStart = [this] { minParam->beginChangeGesture(); maxParam->beginChangeGesture(); };
        slider.onDragEnd   = [this] { minParam->endChangeGesture();   maxParam->endChangeGesture(); };
        slider.onValueChange = [this]
        {
            minParam->setValueNotifyingHost (minParam->convertTo0to1 ((float) slider.getMinValue()));
            maxParam->setValueNotifyingHost (maxParam->convertTo0to1 ((float) slider.getMaxValue()));
            repaint();
        };

        pull();
        startTimerHz (8);   // follow host automation and preset loads
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        g.setColour (pal::dim);
        g.setFont (uiFont (11.0f));
        g.drawText (cap, r.removeFromLeft (52).toFloat(), juce::Justification::centredLeft, false);

        g.setFont (uiFont (11.0f, true));
        g.setColour (pal::text.withAlpha (0.7f));
        g.drawText (juce::String (juce::roundToInt (slider.getMinValue())) + "-"
                        + juce::String (juce::roundToInt (slider.getMaxValue())),
                    r.removeFromRight (56).toFloat(), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromLeft (52);
        r.removeFromRight (58);
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    void timerCallback() override
    {
        if (! slider.isMouseButtonDown())
            pull();
    }

    void pull()
    {
        if (minParam == nullptr || maxParam == nullptr)
            return;

        const double lo = minParam->convertFrom0to1 (minParam->getValue());
        const double hi = maxParam->convertFrom0to1 (maxParam->getValue());

        if (lo != slider.getMinValue() || hi != slider.getMaxValue())
        {
            slider.setMinAndMaxValues (juce::jmin (lo, hi), juce::jmax (lo, hi), juce::dontSendNotification);
            repaint();
        }
    }

    juce::String cap;
    juce::RangedAudioParameter* minParam = nullptr;
    juce::RangedAudioParameter* maxParam = nullptr;
};

//==============================================================================
class RingDial : public juce::Component
{
public:
    RingDial (juce::String label, juce::Colour accent)
        : text (std::move (label)), accentColour (accent)
    {
    }

    void setValue (float normalised)
    {
        normalised = juce::jlimit (0.0f, 1.0f, normalised);

        if (std::abs (normalised - value) > 0.004f)
        {
            value = normalised;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float size = juce::jmin (b.getWidth(), b.getHeight());
        const auto centre = b.getCentre();
        const float rx = size * 0.40f;
        const float start = -juce::MathConstants<float>::halfPi;
        const float full = juce::MathConstants<float>::twoPi;

        juce::Path background;
        background.addCentredArc (centre.x, centre.y, rx, rx, 0.0f, start, start + full, true);
        g.setColour (pal::stroke);
        g.strokePath (background, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));

        if (value > 0.005f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, rx, rx, 0.0f, start, start + full * value, true);
            g.setColour (accentColour);
            g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour (accentColour);
        g.fillEllipse (centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);

        g.setFont (uiFont (13.0f, true));
        g.drawText (text, juce::Rectangle<float> (b.getX(), b.getBottom() - 20.0f, b.getWidth(), 18.0f),
                    juce::Justification::centred, false);
    }

private:
    juce::String text;
    juce::Colour accentColour;
    float value = 0.0f;
};

//==============================================================================
class MonitorStrip : public juce::Component
{
public:
    void setState (const dbc::LiveState& live)
    {
        count = live.historyCount;
        write = live.historyWrite;

        for (int i = 0; i < dbc::LiveState::historySize; ++i)
            notes[i] = live.history[i];

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setColour (pal::panelSoft.withAlpha (0.8f));
        g.fillRoundedRectangle (b, 8.0f);

        for (int i = 1; i < 4; ++i)
        {
            const float y = b.getY() + b.getHeight() * (float) i / 4.0f;
            g.setColour (pal::stroke.withAlpha (0.5f));
            g.drawLine (b.getX() + 6.0f, y, b.getRight() - 6.0f, y, 1.0f);
        }

        const int shown = juce::jmin (count, 22);

        if (shown <= 0)
        {
            g.setColour (pal::dim.withAlpha (0.7f));
            g.setFont (uiFont (11.0f));
            g.drawText ("waiting for MIDI...", b, juce::Justification::centred, false);
            return;
        }

        const float cellW = (b.getWidth() - 16.0f) / 22.0f;

        for (int i = 0; i < shown; ++i)
        {
            const int idx = ((write - shown + i) % dbc::LiveState::historySize
                             + dbc::LiveState::historySize) % dbc::LiveState::historySize;
            const auto& n = notes[idx];

            const float x = b.getX() + 8.0f + cellW * (float) (22 - shown + i);
            const float noteH = 4.0f;
            const float noteY = n.drum ? b.getBottom() - 14.0f
                                       : juce::jmap ((float) n.note, 36.0f, 84.0f,
                                                     b.getBottom() - 6.0f, b.getY() + 6.0f);
            const float len = juce::jlimit (cellW * 0.6f, cellW * 2.2f, cellW * (float) n.lengthBeats);

            g.setColour (n.drum ? pal::orange.withAlpha (0.85f) : pal::coral.withAlpha (0.9f));
            g.fillRoundedRectangle (x, noteY - noteH * 0.5f, len, noteH, 1.5f);
        }

        g.setColour (pal::gold.withAlpha (0.5f));
        g.drawLine (b.getRight() - 4.0f, b.getY() + 3.0f, b.getRight() - 4.0f, b.getBottom() - 3.0f, 1.0f);
    }

private:
    dbc::HistoryNote notes[dbc::LiveState::historySize] {};
    int count = 0, write = 0;
};

} // namespace dbc::ui
