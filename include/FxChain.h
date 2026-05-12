#pragma once

#include <stdint.h>

/**
 * FxChain — Effect ordering and bypass flags for the M26 post-effects section.
 *
 * SIGNAL CHAIN (default ordering):
 *
 *   Oscs → VCA → [Filter] → Chorus → [Delay] → [Reverb] → Space → Out
 *
 * TWO CONFIGURABLE POSITIONS:
 *
 *   filterPos  PRE_CHORUS (default) : Filter shapes the raw voice before chorus adds dimension
 *              POST_CHORUS          : Chorus runs first, filter tone-sculpts the full wet mix
 *
 *   delayPos   PRE_REVERB (default) : Delays bloom into the reverb — most spacious
 *              POST_REVERB          : Reverb is echoed — tighter, rhythmic
 *
 * This gives 4 orderings, all musically distinct:
 *
 *   filterPos=PRE,  delayPos=PRE  → Filter→Chorus→Delay→Reverb  (default)
 *   filterPos=POST, delayPos=PRE  → Chorus→Filter→Delay→Reverb
 *   filterPos=PRE,  delayPos=POST → Filter→Chorus→Reverb→Delay
 *   filterPos=POST, delayPos=POST → Chorus→Filter→Reverb→Delay
 *
 * Each flag is a plain bool stored in gFx — written by updateControl() / commands,
 * read by updateAudio() ISR.  No mutex needed: 8-bit aligned bool reads are
 * atomic on Cortex-M33.
 *
 * Designed for future web configurator control (one CC or websocket message per flag).
 *
 * Milestone 26a.
 */

struct FxOrder {
    bool filterPostChorus; // false = PRE_CHORUS (default), true = POST_CHORUS
    bool delayPostReverb;  // false = PRE_REVERB (default), true = POST_REVERB
};

// Defined in main.cpp
extern FxOrder gFxOrder;
