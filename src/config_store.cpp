#include "config_store.h"
#include "params.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h> // memcmp

// ---------------------------------------------------------------------------
// Flash layout
//
// The Earle Philhower EEPROM library maps a RAM buffer onto one or more
// 4 KB flash pages at the top of flash, with a circular-buffer scheme that
// distributes erase cycles across all pages.
//
// We reserve kEepromBytes for our buffer — enough for kMaxPresets slots.
// kEepromBytes must be <= the RP2350 EEPROM emulation size (default 4096).
// ---------------------------------------------------------------------------
static constexpr int kEepromBytes = kMaxPresets * sizeof(AlloyConfig); // ~240 bytes
static constexpr int kSlotBytes = sizeof(AlloyConfig);

// Minimum milliseconds between flash commits.
// Flash is rated ~100,000 erase cycles; at 10 s minimum that's >27 years of
// continuous saving.  Adjust only if there is a concrete need.
static constexpr uint32_t kMinSaveIntervalMs = 10000;

static uint32_t sLastSaveMs = 0;
static bool sEepromReady = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void ensureEeprom() {
    if (!sEepromReady) {
        EEPROM.begin(kEepromBytes);
        sEepromReady = true;
    }
}

static int slotAddr(uint8_t slot) {
    return (int)slot * kSlotBytes;
}

// Pack all current gXxx globals into a config struct.
static void packConfig(AlloyConfig &cfg) {
    cfg.magic = kConfigMagic;
    cfg.version = kConfigVersion;
    cfg.baseFreq = gBaseFreq;
    cfg.detune = gDetune;
    cfg.relation = gRelation;
    cfg.voiceMode = (uint8_t)gVoiceMode;
    cfg.shape = gShape;
    cfg.fatness = gFatness;
    cfg.subOctave = gSubOctave;
    cfg.motion = gMotion;
    cfg.driftSpeed = gDriftSpeed;
    cfg.chorusMode = (uint8_t)gChorusMode;
    cfg.space = gSpace;
    cfg.curve = gCurve;
    cfg.curveTime = gCurveTime;
    cfg.volume = gVolume;
    cfg.midiChannel = gMidiChannel;
}

// Apply a validated config struct to all gXxx globals.
static void applyConfig(const AlloyConfig &cfg) {
    gBaseFreq = cfg.baseFreq;
    gDetune = cfg.detune;
    gRelation = cfg.relation;
    gVoiceMode = (VoiceMode)cfg.voiceMode;
    gShape = cfg.shape;
    gFatness = cfg.fatness;
    gSubOctave = (cfg.subOctave == 2) ? 2u : 1u;
    gMotion = cfg.motion;
    gDriftSpeed = cfg.driftSpeed;
    gChorusMode = (ChorusMode)cfg.chorusMode;
    gSpace = cfg.space;
    gCurve = cfg.curve;
    gCurveTime = cfg.curveTime;
    gVolume = cfg.volume;
    gMidiChannel = cfg.midiChannel;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool configStore_load() {
    ensureEeprom();
    AlloyConfig cfg;
    EEPROM.get(slotAddr(0), cfg);
    if (cfg.magic != kConfigMagic || cfg.version != kConfigVersion)
        return false; // no valid config — caller uses compile-time defaults
    applyConfig(cfg);
    return true;
}

ConfigSaveResult configStore_save() {
    const uint32_t now = millis();

    // Rate limit: protect flash from rapid repeated writes.
    if (sLastSaveMs > 0 && (now - sLastSaveMs) < kMinSaveIntervalMs)
        return ConfigSaveResult::THROTTLED;

    ensureEeprom();

    // Pack current parameters.
    AlloyConfig newCfg;
    packConfig(newCfg);

    // Dirty check: read what is stored and compare byte-for-byte.
    // If nothing changed, skip the erase/program cycle entirely.
    AlloyConfig stored;
    EEPROM.get(slotAddr(0), stored);
    if (memcmp(&newCfg, &stored, kSlotBytes) == 0)
        return ConfigSaveResult::UNCHANGED;

    // Write to EEPROM buffer then commit (this pauses Core 1 for ~10 ms).
    EEPROM.put(slotAddr(0), newCfg);
    EEPROM.commit();
    sLastSaveMs = now;
    return ConfigSaveResult::SAVED;
}

void configStore_reset() {
    ensureEeprom();
    // Wipe just the magic word — config is detected as invalid on next boot.
    uint32_t zero = 0;
    EEPROM.put(slotAddr(0), zero);
    EEPROM.commit();
    sLastSaveMs = 0;
}
