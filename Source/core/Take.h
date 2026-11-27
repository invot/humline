#pragma once

// The take: every note Humline has finished since the take was started (or
// cleared), kept so it can be shown on the Notes page and exported as a .mid.
// JUCE-free, like the rest of core/.

#include "Types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dbc
{

/** Pulls newly finished notes out of LiveState's 32-note history ring.

    VoiceEngine bumps LiveState::historySeq once per pushed note, so the
    difference to the last value seen is the number of new notes, and they sit
    just behind historyWrite. Call capture() once per audio block (the ring only
    holds 32 notes, so it must be drained at least that often). */
class TakeRecorder
{
public:
    /** Hard cap on the take, so the audio thread never has to allocate:
        reserve this many in the destination vector up front. */
    static constexpr size_t maxNotes = 16384;

    /** Appends the notes pushed since the previous call to `out` (up to
        maxNotes in total) and returns how many were new. */
    int capture (const LiveState& live, std::vector<HistoryNote>& out) noexcept
    {
        const uint32_t seq = live.historySeq;

        // VoiceEngine::reset() starts the counter again from zero.
        if (seq < lastSeq)
            lastSeq = 0;

        uint32_t fresh = seq - lastSeq;
        lastSeq = seq;

        fresh = std::min<uint32_t> (fresh, (uint32_t) std::max (0, live.historyCount));
        fresh = std::min<uint32_t> (fresh, (uint32_t) LiveState::historySize);

        const int n = (int) fresh;
        constexpr int size = LiveState::historySize;

        for (int i = 0; i < n; ++i)
        {
            if (out.size() >= maxNotes)
                break;

            const int idx = ((live.historyWrite - n + i) % size + size) % size;
            out.push_back (live.history[idx]);
        }

        return n;
    }

private:
    uint32_t lastSeq = 0;
};

//==============================================================================
enum class TakeFilter
{
    All = 0,
    Voice,
    Drums
};

inline bool takeFilterAccepts (TakeFilter f, const HistoryNote& n) noexcept
{
    return f == TakeFilter::All || (f == TakeFilter::Drums) == n.drum;
}

/** One note of a take laid out for a MIDI file: positions in ticks from the
    start of the bar the take begins in. */
struct TakeExportNote
{
    int64_t startTick  = 0;
    int64_t lengthTick = 1;
    int     note       = 60;
    bool    drum       = false;
};

/** First beat of the bar that holds the earliest note: the export starts
    there, so the take keeps its position inside the bar without dragging along
    every empty bar before it. */
inline double takeOriginBeats (const std::vector<HistoryNote>& notes, TakeFilter filter,
                               double beatsPerBar = 4.0) noexcept
{
    double first = 0.0;
    bool any = false;

    for (const auto& n : notes)
    {
        if (! takeFilterAccepts (filter, n))
            continue;

        first = any ? std::min (first, (double) n.startBeats) : (double) n.startBeats;
        any = true;
    }

    if (! any || beatsPerBar <= 0.0)
        return 0.0;

    // A hair of tolerance so a note sitting exactly on a barline is not pushed
    // into the previous bar by float rounding.
    return std::floor (first / beatsPerBar + 1.0e-6) * beatsPerBar;
}

/** Turns a take into notes for a MIDI file: filtered, sorted by start, in ticks
    from takeOriginBeats(), at least one tick long, and with overlapping repeats
    of the same note shortened so every note-on has its own note-off. */
inline std::vector<TakeExportNote> layoutTakeForExport (const std::vector<HistoryNote>& notes,
                                                        TakeFilter filter, int ticksPerQuarter,
                                                        double beatsPerBar = 4.0)
{
    std::vector<TakeExportNote> out;
    out.reserve (notes.size());

    const double origin = takeOriginBeats (notes, filter, beatsPerBar);
    const double tpq = (double) std::max (1, ticksPerQuarter);

    for (const auto& n : notes)
    {
        if (! takeFilterAccepts (filter, n))
            continue;

        TakeExportNote e;
        e.startTick  = std::max<int64_t> (0, (int64_t) std::llround (((double) n.startBeats - origin) * tpq));
        e.lengthTick = std::max<int64_t> (1, (int64_t) std::llround ((double) n.lengthBeats * tpq));
        e.note       = std::clamp (n.note, 0, 127);
        e.drum       = n.drum;
        out.push_back (e);
    }

    // By start, then lane and key, longest first: duplicates end up side by side.
    std::sort (out.begin(), out.end(), [] (const TakeExportNote& a, const TakeExportNote& b)
    {
        if (a.startTick != b.startTick)   return a.startTick < b.startTick;
        if (a.drum != b.drum)             return ! a.drum;
        if (a.note != b.note)             return a.note < b.note;
        return a.lengthTick > b.lengthTick;
    });

    // Two notes on the same key starting on the same tick: keep the longest.
    out.erase (std::unique (out.begin(), out.end(), [] (const TakeExportNote& a, const TakeExportNote& b)
    {
        return a.startTick == b.startTick && a.note == b.note && a.drum == b.drum;
    }), out.end());

    // A note that is still sounding when the same key (on the same lane) starts
    // again ends where the next one begins.
    for (size_t i = 0; i < out.size(); ++i)
    {
        for (size_t j = i + 1; j < out.size(); ++j)
        {
            if (out[j].startTick >= out[i].startTick + out[i].lengthTick)
                break;

            if (out[j].note == out[i].note && out[j].drum == out[i].drum)
            {
                out[i].lengthTick = std::max<int64_t> (1, out[j].startTick - out[i].startTick);
                break;
            }
        }
    }

    return out;
}

} // namespace dbc
