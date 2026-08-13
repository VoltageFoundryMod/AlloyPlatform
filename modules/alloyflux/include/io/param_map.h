#pragma once
#include <stdint.h>

/**
 * Parameter map — single source of truth for all control interfaces.
 *
 * Every continuous synthesis parameter that can be driven externally is
 * listed here with its serial command name, MIDI CC number, and value range.
 * All transports (USB MIDI, hardware TRS MIDI, future I2C, Web configurator)
 * call paramMap_dispatchCC() instead of maintaining their own switch statements.
 *
 * Special messages not in this table (they are not simple float parameters):
 *   Note On / Note Off   — monophonic pitch + gate
 *   CC 64 (Sustain)      — gate hold
 *   CC 123               — All Notes Off / panic
 *   Program Change 1–5   — VoiceMode select
 *
 * Adding a new parameter:
 *   1. Declare extern float gXxx in include/params.h
 *   2. Add one row to kCCParams[] in src/param_map.cpp — nothing else needed.
 */

struct CCParam
{
    uint8_t         cc;     // MIDI CC number
    float           valMin; // parameter value when CC = 0
    float           valMax; // parameter value when CC = 127
    volatile float *target; // pointer to the gXxx global to write
    const char *
        name; // matches the serial command name — for Web UI labels, I2C NRPN, etc.
    bool
        logScale; // true = log interpolation (e.g. filter cutoff); false = linear
};

// Defined in src/param_map.cpp — iterable by any transport.
extern const CCParam kCCParams[];
extern const uint8_t kCCParamCount;

/**
 * Map a raw CC 0–127 value to the matching parameter and write it.
 * Returns true if the CC was in the table, false if it is a special case
 * (handled by the caller: CC 64 sustain, CC 123 panic, etc.).
 */
bool paramMap_dispatchCC(uint8_t cc, uint8_t value);
