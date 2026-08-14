#pragma once

#include "ParamDescriptor.h"
#include <stdint.h>

/**
 * Parameter dispatch — every transport (USB MIDI, hardware TRS MIDI, serial
 * console, SysEx, the Web Configurator, future I2C) reads the same table
 * instead of keeping its own switch statement.
 *
 * The table itself is generated: declare a parameter once in
 * `modules/alloyflux/params.json`, run `make params`, and the CC number, range,
 * curve, default, label, category and unit all follow from that one row.
 * Nothing here is hand-maintained.
 *
 * Not covered, because they are actions rather than parameters:
 *   Note On / Note Off   — monophonic pitch + gate
 *   CC 64 (Sustain)      — gate hold
 *   CC 119               — drone return
 *   CC 123               — All Notes Off / panic
 *   Program Change 1–6   — VoiceMode select
 * CC 16 (root pitch) is also hand-written: it carries a V/Oct encoding rather
 * than a plain range.
 */

/// All float parameters, ordered by CC. Generated from params.json.
/// A pointer rather than an array so the generated table stays in one
/// translation unit instead of being copied into every includer.
extern const ParamDescriptor *const kParamTable;
extern const uint8_t                kParamCount;

/// All discrete parameters (enums, bools, small selects), ordered by CC.
extern const EnumParamDescriptor *const kEnumTable;
extern const uint8_t                    kEnumCount;

/// Look up by CC number, or by serial-command name. nullptr when absent.
const ParamDescriptor     *paramMap_findByCC(uint8_t cc);
const ParamDescriptor     *paramMap_findByName(const char *name);
const EnumParamDescriptor *paramMap_findEnumByCC(uint8_t cc);
const EnumParamDescriptor *paramMap_findEnumByName(const char *name);

/**
 * Decode a raw CC 0–127 into its parameter and write it.
 * Returns false when the CC is not a table parameter, leaving the caller to
 * handle it as a special case.
 */
bool paramMap_dispatchCC(uint8_t cc, uint8_t value);
