#pragma once
#include <stdint.h>

#include "ConfigSlot.h"

/**
 * Flash preset persistence — Milestone 30, promoted to the platform in M63f.
 *
 * Uses the Earle Philhower EEPROM emulation library, which provides:
 *   - Wear levelling (circular buffer across multiple 4 KB flash pages)
 *   - Automatic pause/resume of the other core during erase/program
 *
 * Two more protection layers on top:
 *   - Dirty check: commit() only called when the slot actually changed
 *   - Rate limit: minimum 10 s between auto-saves
 *
 * The platform owns the slot container and the flash mechanics; the *contents*
 * of a slot's blob are entirely the module's, packed and applied through
 * ModuleHooks. Nothing here interprets a byte of it.
 */

enum class ConfigSaveResult : uint8_t
{
    SAVED,     // parameters written + committed to flash
    UNCHANGED, // stored data matched — no write performed (flash protected)
    THROTTLED, // too soon since last save — rate limit enforced
};

/**
 * Load a slot and apply it.
 * slot 0 = auto-save (called at startup); slots 1–9 = user presets.
 * Returns true when a valid slot written by *this* module at *this* layout
 * version was found and applied; false leaves compile-time defaults in place.
 */
bool configStore_load(uint8_t slot = 0);

/**
 * Save current parameter state to a slot.
 * slot 0 = auto-save (dirty check + 10 s rate limit enforced).
 * slots 1–9 = explicit preset saves (rate limit bypassed, dirty check kept).
 * Note: a commit parks the audio core for ~10 ms — an audible dropout.
 */
ConfigSaveResult configStore_save(uint8_t slot = 0);

/**
 * Invalidate stored slots. slot 0 = wipe the live slot; 255 = wipe all.
 * Next boot (slot 0 wiped) falls back to compile-time defaults.
 */
void configStore_reset(uint8_t slot = 0);

/**
 * Apply compile-time factory defaults immediately.
 * Does not touch flash — call configStore_reset() separately to wipe it.
 */
void configStore_applyDefaults();
