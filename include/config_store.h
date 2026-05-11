#pragma once
#include <stdint.h>

#include "ChorusEngine.h"
#include "VoiceMode.h"

/**
 * Flash config persistence — Milestone 30 (config save/load).
 *
 * Uses the Earle Philhower EEPROM emulation library which provides:
 *   - Wear levelling (circular buffer across multiple 4 KB flash pages)
 *   - Automatic Core 1 pause/resume during flash erase/program
 *
 * AlloyFlux adds two more protection layers on top:
 *   - Dirty check: commit() only called when values actually changed
 *   - Rate limit: minimum 10 s between writes (configurable via kMinSaveIntervalMs)
 *
 * Preset layout: the EEPROM buffer is divided into kMaxPresets slots of
 * sizeof(AlloyConfig) each.  Slot 0 = auto-saved "live" state.
 * Slots 1–(kMaxPresets-1) are named presets (future use).
 *
 * Adding a new parameter:
 *   1. Add a field to AlloyConfig below.
 *   2. Bump kVersion so existing stored configs are detected as incompatible
 *      and defaults are used instead (safe migration).
 *   3. Add pack/apply lines in config_store.cpp.
 */

static constexpr uint32_t kConfigMagic = 0xAF10CF01; // "AlloyFlux Config v1"
static constexpr uint8_t kConfigVersion = 1;
static constexpr uint8_t kMaxPresets = 4; // future: slots 1–3 for presets

struct AlloyConfig {
    uint32_t magic;
    uint8_t version;
    // Pitch / voice
    float baseFreq;
    float detune;
    float relation;
    uint8_t voiceMode; // cast of VoiceMode enum
    // Timbre
    float shape;
    float fatness;
    // Animation
    float motion;
    float driftSpeed;
    // Chorus
    uint8_t chorusMode; // cast of ChorusMode enum
    // Space / envelope
    float space;
    float curve;
    float curveTime;
    // Level
    float volume;
    // MIDI
    uint8_t midiChannel; // 0 = omni, 1–16 = specific channel
};

enum class ConfigSaveResult : uint8_t {
    SAVED,     // parameters written + committed to flash
    UNCHANGED, // stored data matched — no write performed (flash protected)
    THROTTLED, // too soon since last save — rate limit enforced
};

/**
 * Load config from flash into gXxx globals.
 * Call once at startup, before startMozzi().
 * Returns true when a valid config was found and applied; false = using defaults.
 */
bool configStore_load();

/**
 * Save current gXxx globals to flash.
 * Enforces dirty-check + 10 s rate limit.  Returns result code.
 * Note: a successful commit() pauses audio for ~10 ms (Core 1 halted for flash erase).
 */
ConfigSaveResult configStore_save();

/**
 * Invalidate the stored config by writing zero magic.
 * Next boot falls back to compile-time defaults.
 */
void configStore_reset();
