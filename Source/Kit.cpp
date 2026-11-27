#include "Kit.h"

#include <iterator>

namespace dbc
{

namespace
{
const char* const padNames[numDrumPads] {
    "Really Low Bassdrum", "Snare", "Clap", "Bassdrum",
    "Closed Hat", "Open Hat", "Low Tom", "High Tom"
};

// The factory takes were captured by running synthesised kick / snare / clap /
// hat / tom hits through the real analysis path at 48 kHz, so they are shaped
// like the takes a user records. They are a starting point: training with your
// own mouth and mic still matches better.
const DrumFeatures trainedReallyLow[] {
    { 0.9165f, 0.0595f, 0.0239f, 0.0001f, 0.00394f, 0.4682f, 0.7004f },
    { 0.9557f, 0.0314f, 0.0127f, 0.0001f, 0.00000f, 0.2678f, 0.7556f },
    { 0.9272f, 0.0518f, 0.0209f, 0.0001f, 0.00394f, 0.5984f, 0.6973f },
    { 0.9294f, 0.0503f, 0.0203f, 0.0001f, 0.00394f, 0.5065f, 0.7398f }
};

const DrumFeatures trainedSnare[] {
    { 0.0945f, 0.1969f, 0.1941f, 0.5145f, 0.54724f, 0.1299f, 0.0000f },
    { 0.1444f, 0.1429f, 0.1902f, 0.5226f, 0.52362f, 0.2087f, 0.0000f },
    { 0.1331f, 0.1370f, 0.1703f, 0.5596f, 0.52362f, 0.2786f, 0.0000f },
    { 0.1124f, 0.1656f, 0.2058f, 0.5161f, 0.52756f, 0.3789f, 0.0000f }
};

const DrumFeatures trainedClap[] {
    { 0.0474f, 0.2157f, 0.2486f, 0.4883f, 0.51969f, 0.0000f, 0.0000f },
    { 0.0438f, 0.1881f, 0.3039f, 0.4642f, 0.48031f, 0.0000f, 0.0000f },
    { 0.0377f, 0.2180f, 0.3042f, 0.4400f, 0.40157f, 0.0000f, 0.0000f },
    { 0.0297f, 0.2216f, 0.2953f, 0.4534f, 0.41732f, 0.0000f, 0.0000f }
};

const DrumFeatures trainedKick[] {
    { 0.9009f, 0.0707f, 0.0283f, 0.0001f, 0.00394f, 0.0000f, 0.9546f },
    { 0.9058f, 0.0672f, 0.0268f, 0.0002f, 0.00394f, 0.0450f, 0.9669f },
    { 0.9428f, 0.0407f, 0.0164f, 0.0001f, 0.00000f, 0.1094f, 0.9747f },
    { 0.9523f, 0.0339f, 0.0138f, 0.0001f, 0.00000f, 0.0357f, 0.9789f }
};

const DrumFeatures trainedClosedHat[] {
    { 0.0061f, 0.0592f, 0.1287f, 0.8060f, 0.62992f, 0.0000f, 0.0000f },
    { 0.0080f, 0.0574f, 0.1333f, 0.8013f, 0.67323f, 0.0000f, 0.0000f },
    { 0.0031f, 0.0544f, 0.1222f, 0.8203f, 0.58661f, 0.0000f, 0.0000f },
    { 0.0014f, 0.0541f, 0.1324f, 0.8120f, 0.46457f, 0.0000f, 0.0000f }
};

const DrumFeatures trainedOpenHat[] {
    { 0.0054f, 0.0599f, 0.1330f, 0.8017f, 0.62992f, 0.1662f, 0.0000f },
    { 0.0122f, 0.0502f, 0.1157f, 0.8219f, 0.66142f, 0.2375f, 0.0000f },
    { 0.0060f, 0.0520f, 0.1214f, 0.8206f, 0.65354f, 0.3198f, 0.0000f },
    { 0.0140f, 0.0538f, 0.1200f, 0.8121f, 0.70472f, 0.4479f, 0.0000f }
};

const DrumFeatures trainedLowTom[] {
    { 0.8132f, 0.1339f, 0.0525f, 0.0004f, 0.00394f, 0.2679f, 0.9950f },
    { 0.8019f, 0.1419f, 0.0558f, 0.0004f, 0.00787f, 0.2505f, 0.9956f },
    { 0.7935f, 0.1475f, 0.0585f, 0.0004f, 0.00394f, 0.2404f, 0.9957f },
    { 0.7876f, 0.1520f, 0.0600f, 0.0004f, 0.00787f, 0.2502f, 0.9962f }
};

const DrumFeatures trainedHighTom[] {
    { 0.6196f, 0.2717f, 0.1052f, 0.0035f, 0.00394f, 0.1037f, 0.9964f },
    { 0.5808f, 0.3007f, 0.1169f, 0.0016f, 0.00787f, 0.0905f, 0.9965f },
    { 0.5835f, 0.2997f, 0.1152f, 0.0016f, 0.00787f, 0.0926f, 0.9965f },
    { 0.5819f, 0.3004f, 0.1160f, 0.0017f, 0.00787f, 0.0782f, 0.9971f }
};

struct TrainedBank
{
    const DrumFeatures* takes;
    int count;
};

const TrainedBank trainedBanks[numDrumPads] {
    { trainedReallyLow, (int) std::size (trainedReallyLow) },
    { trainedSnare,     (int) std::size (trainedSnare) },
    { trainedClap,      (int) std::size (trainedClap) },
    { trainedKick,      (int) std::size (trainedKick) },
    { trainedClosedHat, (int) std::size (trainedClosedHat) },
    { trainedOpenHat,   (int) std::size (trainedOpenHat) },
    { trainedLowTom,    (int) std::size (trainedLowTom) },
    { trainedHighTom,   (int) std::size (trainedHighTom) }
};

int readInt (const juce::ValueTree& v, const juce::Identifier& id, int fallback)
{
    return v.hasProperty (id) ? (int) v.getProperty (id) : fallback;
}

float readShare (const juce::ValueTree& v, const juce::Identifier& id)
{
    return juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty (id, 0.0));
}
} // namespace

//==============================================================================
const char* defaultPadName (int pad) noexcept
{
    return (pad >= 0 && pad < numDrumPads) ? padNames[pad] : "";
}

//==============================================================================
Kit Kit::defaults()
{
    Kit k;

    for (int i = 0; i < numDrumPads; ++i)
    {
        k.note[i] = Settings().padNote[i];
        k.padName[i] = defaultPadName (i);
        k.muted[i] = false;
        k.velocity[i] = true;
    }

    for (auto& q : k.chordOverride)
        q = ChordQuality::Preset;

    return k;
}

int Kit::numTakes() const noexcept
{
    int n = 0;

    for (const auto& bank : takes)
        n += (int) bank.size();

    return n;
}

juce::ValueTree Kit::toValueTree() const
{
    juce::ValueTree v ("KIT");
    v.setProperty ("version", 1, nullptr);
    v.setProperty ("name", name, nullptr);

    for (int i = 0; i < numDrumPads; ++i)
    {
        juce::ValueTree pad ("PAD");
        pad.setProperty ("index", i, nullptr);
        pad.setProperty ("note", note[i], nullptr);
        pad.setProperty ("name", padName[i], nullptr);
        pad.setProperty ("muted", muted[i], nullptr);
        pad.setProperty ("velocity", velocity[i], nullptr);
        v.appendChild (pad, nullptr);

        for (const auto& t : takes[i])
        {
            juce::ValueTree take ("TAKE");
            take.setProperty ("pad", i, nullptr);
            take.setProperty ("low", t.low, nullptr);
            take.setProperty ("f1", t.f1, nullptr);
            take.setProperty ("f2", t.f2, nullptr);
            take.setProperty ("high", t.high, nullptr);
            take.setProperty ("zcr", t.zcr, nullptr);
            take.setProperty ("sustain", t.sustain, nullptr);
            take.setProperty ("pitched", t.pitched, nullptr);
            v.appendChild (take, nullptr);
        }
    }

    juce::StringArray overrides;

    for (auto q : chordOverride)
        overrides.add (juce::String ((int) q));

    juce::ValueTree chords ("CHORDS");
    chords.setProperty ("values", overrides.joinIntoString (","), nullptr);
    v.appendChild (chords, nullptr);

    return v;
}

bool Kit::fromValueTree (const juce::ValueTree& v, Kit& out)
{
    if (! v.hasType ("KIT"))
        return false;

    Kit kit = defaults();
    kit.name = v.getProperty ("name", "Untitled kit").toString();

    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        const auto child = v.getChild (i);

        if (child.hasType ("PAD"))
        {
            const int index = readInt (child, "index", -1);

            if (index < 0 || index >= numDrumPads)
                continue;

            kit.note[index] = juce::jlimit (0, 127, readInt (child, "note", kit.note[index]));
            kit.padName[index] = child.getProperty ("name", kit.padName[index]).toString();
            kit.muted[index] = readInt (child, "muted", kit.muted[index] ? 1 : 0) != 0;
            kit.velocity[index] = readInt (child, "velocity", kit.velocity[index] ? 1 : 0) != 0;
        }
        else if (child.hasType ("TAKE"))
        {
            const int pad = readInt (child, "pad", -1);

            if (pad < 0 || pad >= numDrumPads
                || (int) kit.takes[pad].size() >= DrumClassifier::maxTakes)
                continue;

            DrumFeatures f;
            f.low = readShare (child, "low");
            f.f1 = readShare (child, "f1");
            f.f2 = readShare (child, "f2");
            f.high = readShare (child, "high");
            f.zcr = readShare (child, "zcr");
            f.sustain = readShare (child, "sustain");
            f.pitched = readShare (child, "pitched");
            kit.takes[pad].push_back (f);
        }
        else if (child.hasType ("CHORDS"))
        {
            const auto values = juce::StringArray::fromTokens (child.getProperty ("values", "").toString(),
                                                               ",", {});

            for (int d = 0; d < 12; ++d)
                kit.chordOverride[d] = d < values.size()
                    ? (ChordQuality) juce::jlimit (0, (int) ChordQuality::NumQualities - 1,
                                                   values[d].getIntValue())
                    : ChordQuality::Preset;
        }
    }

    out = std::move (kit);
    return true;
}

juce::String Kit::toXmlString() const
{
    if (auto xml = toValueTree().createXml())
        return xml->toString();

    return {};
}

bool Kit::fromXmlString (const juce::String& xmlText, Kit& out)
{
    auto xml = juce::XmlDocument::parse (xmlText);

    if (xml == nullptr)
        return false;

    return fromValueTree (juce::ValueTree::fromXml (*xml), out);
}

//==============================================================================
const juce::StringArray& factoryKitNames()
{
    static const juce::StringArray names { "GM starter", "808 / trap" };
    return names;
}

bool makeFactoryKit (int index, Kit& out)
{
    if (index < 0 || index >= factoryKitNames().size())
        return false;

    Kit kit = Kit::defaults();
    kit.name = factoryKitNames()[index];

    for (int pad = 0; pad < numDrumPads; ++pad)
        kit.takes[pad].assign (trainedBanks[pad].takes,
                               trainedBanks[pad].takes + trainedBanks[pad].count);

    if (index == 1)
    {
        // The trap map: sub, snare 2, clap, kick, hats, two toms.
        static constexpr int trapNotes[numDrumPads] { 35, 40, 39, 36, 42, 46, 45, 47 };

        for (int pad = 0; pad < numDrumPads; ++pad)
            kit.note[pad] = trapNotes[pad];

        // Sevenths where a trap progression usually wants them.
        kit.chordOverride[0] = ChordQuality::Minor7;
        kit.chordOverride[2] = ChordQuality::Major7;
        kit.chordOverride[3] = ChordQuality::Minor7;
        kit.chordOverride[4] = ChordQuality::Dominant7;
        kit.chordOverride[5] = ChordQuality::Major7;
    }

    out = std::move (kit);
    return true;
}

//==============================================================================
juce::Result writeKitToFile (const Kit& kit, const juce::File& file)
{
    const auto xml = kit.toXmlString();

    if (xml.isEmpty())
        return juce::Result::fail ("The kit could not be written.");

    if (! file.replaceWithText (xml))
        return juce::Result::fail ("Could not write " + file.getFullPathName());

    return juce::Result::ok();
}

juce::Result readKitFromFile (const juce::File& file, Kit& out)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("There is no file at " + file.getFullPathName());

    // A kit is a few kilobytes; anything this big is not one, and loading it
    // would only waste memory.
    if (file.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail ("\"" + file.getFileName() + "\" is too big to be a Humline kit.");

    Kit kit;

    if (! Kit::fromXmlString (file.loadFileAsString(), kit))
        return juce::Result::fail ("\"" + file.getFileName() + "\" is not a Humline kit.");

    out = std::move (kit);
    return juce::Result::ok();
}

} // namespace dbc
