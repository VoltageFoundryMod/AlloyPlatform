#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// ModuleHooks — the seam between the platform's generic mechanisms and a
// particular module.
//
// USB MIDI, the SysEx patch protocol, preset slots and CC feedback are the
// same work for every module: they are driven entirely by the generated
// parameter manifest. What differs is small and specific — what a note means,
// which CCs are actions rather than parameters, what identity to enumerate as,
// and what to write into a preset blob.
//
// Every symbol here is declared by the platform and **defined by exactly one
// module**, which is what makes the compile-time engine selection work: link
// AlloyFlux's definitions and you get AlloyFlux, link Audrey's and you get
// Audrey, from the same platform sources.
//
// A module that has nothing to say for a given hook still has to define it —
// an empty body is the answer. That is deliberate: a missing definition is a
// link error naming the hook, which is a better failure than silently
// inheriting behaviour from whichever module happened to be built last.
// ---------------------------------------------------------------------------

// --- Identity -------------------------------------------------------------

/// USB descriptors, shown by the host in its device list.
extern const char *const kModuleManufacturer;
extern const char *const kModuleProduct;
extern const char *const kModuleMidiName;

/// Two-byte SysEx device signature, following the 0x7D manufacturer byte.
/// The Web Configurator reads this to decide which parameter map to load, so
/// **no two modules may share a pair**. AlloyFlux is 'A','F'.
extern const uint8_t kSysExDevId0;
extern const uint8_t kSysExDevId1;

// --- MIDI ------------------------------------------------------------------

/**
 * Note On with velocity > 0. Velocity 0 arrives as moduleHook_noteOff(), per
 * the running-status convention, so a module never has to check for it.
 */
void moduleHook_noteOn(uint8_t note, uint8_t velocity);

/** Note Off, or Note On with velocity 0. */
void moduleHook_noteOff(uint8_t note);

/**
 * A Control Change the generated tables do not cover — i.e. an action rather
 * than a parameter (sustain, panic, freeze, transpose…).
 *
 * Called only after paramMap_dispatchCC() has declined it, so a module never
 * sees a CC that the manifest already owns and cannot accidentally shadow one.
 * Return value is currently advisory; return true when handled.
 */
bool moduleHook_controlChange(uint8_t cc, uint8_t value);

/** Program Change. Modules with no program concept define an empty body. */
void moduleHook_programChange(uint8_t program);

/**
 * Append (cc, value) pairs that are not in the generated tables, so a patch
 * dump carries the module's actions and encodings as well as its parameters.
 *
 * Whatever is emitted here must be understood by moduleHook_controlChange() on
 * the way back in — an APPLY_PATCH replays the dump through the same CC path.
 * Write at most maxBytes and return the number of bytes written (always even).
 */
uint8_t moduleHook_extraPatchPairs(uint8_t *buf, uint8_t maxBytes);

// --- Config ----------------------------------------------------------------

/// Identifies the module that wrote a preset slot, and the layout version of
/// its blob. See platform/include/ConfigSlot.h.
extern const uint16_t kEngineId;
extern const uint16_t kEngineVersion;

/**
 * Serialise the module's current parameter state into a preset blob.
 * @return bytes written; must be <= kSlotBlobBytes.
 */
uint16_t moduleHook_packConfig(void *blob, uint16_t maxBytes);

/** Restore parameter state from a blob previously written by packConfig(). */
void moduleHook_applyConfig(const void *blob, uint16_t bytes);

/** Reset every parameter to its compile-time default. Touches no flash. */
void moduleHook_applyDefaults();
