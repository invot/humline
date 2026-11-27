#pragma once

#include "core/DrumClassifier.h"
#include "core/Types.h"

#include <juce_data_structures/juce_data_structures.h>

namespace dbc
{

/** The parts of a project worth carrying into the next one: the pad map, the
    takes recorded for each pad, the per-pad velocity flags and the chord
    overrides. Written as XML with a .humlinekit extension.

    A kit never touches the parameters, the key or the take, so loading one into
    a project leaves everything else where it was. */
struct Kit
{
    juce::String name { "Untitled kit" };

    int          note[numDrumPads]     {};
    juce::String padName[numDrumPads];
    bool         muted[numDrumPads]    {};
    bool         velocity[numDrumPads] {};
    ChordQuality chordOverride[12] {};

    std::vector<DrumFeatures> takes[numDrumPads];

    /** Humline's built-in map, nothing trained. */
    static Kit defaults();

    /** Takes across every pad. */
    int numTakes() const noexcept;

    juce::ValueTree toValueTree() const;

    /** Reads a kit, keeping defaults for anything the tree leaves out. False
        when the tree is not a kit at all. */
    static bool fromValueTree (const juce::ValueTree& v, Kit& out);

    juce::String toXmlString() const;
    static bool fromXmlString (const juce::String& xmlText, Kit& out);
};

/** The built-in name of a trigger (notes 35-50 style drum names). */
const char* defaultPadName (int pad) noexcept;

/** The kits that ship with Humline, in menu order. */
const juce::StringArray& factoryKitNames();

/** Fills `out` with a factory kit. False when the index is out of range. */
bool makeFactoryKit (int index, Kit& out);

/** Writes the kit over `file`, creating it when needed. */
juce::Result writeKitToFile (const Kit& kit, const juce::File& file);

/** Reads a kit file into `out`. On failure `out` is untouched. */
juce::Result readKitFromFile (const juce::File& file, Kit& out);

} // namespace dbc
