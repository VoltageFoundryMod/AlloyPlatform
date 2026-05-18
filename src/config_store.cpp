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
static constexpr int kEepromBytes = kMaxPresets * sizeof(AlloyConfig); // ~1200 bytes (10 slots)
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
    // Filter
    cfg.filterCutoff = gFilterCutoff;
    cfg.filterRes = gFilterRes;
    cfg.filterMode = (uint8_t)gFilterMode;
    cfg.filterType = (uint8_t)gFilterType;
    // Envelope
    cfg.envelopeType = (uint8_t)gEnvelopeType;
    cfg.adsrAttack = gAdsrAttack;
    cfg.adsrDecay = gAdsrDecay;
    cfg.adsrSustain = gAdsrSustain;
    cfg.adsrRelease = gAdsrRelease;
    cfg.adsrLoop = gAdsrLoop;
    // Reverb
    cfg.revEnabled = gRevEnabled;
    cfg.revMix = gRevMix;
    cfg.revSize = gRevSize;
    cfg.revDamping = gRevDamping;
    cfg.revModSpeed = gRevModSpeed;
    cfg.revModDepth = gRevModDepth;
    cfg.revFrozen = gRevFrozen;
    // Delay
    cfg.delayTime = gDelayTime;
    cfg.delayFeedback = gDelayFeedback;
    cfg.delayMix = gDelayMix;
    // FxOrder
    cfg.fxFilterPostChorus = gFxOrder.filterPostChorus;
    cfg.fxDelayPostReverb = gFxOrder.delayPostReverb;
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
    // Filter
    gFilterCutoff = cfg.filterCutoff;
    gFilterRes = cfg.filterRes;
    gFilterMode = (FilterMode)cfg.filterMode;
    gFilterType = (FilterType)cfg.filterType;
    // Envelope
    gEnvelopeType = (EnvelopeType)cfg.envelopeType;
    gAdsrAttack = cfg.adsrAttack;
    gAdsrDecay = cfg.adsrDecay;
    gAdsrSustain = cfg.adsrSustain;
    gAdsrRelease = cfg.adsrRelease;
    gAdsrLoop = cfg.adsrLoop;
    // Reverb
    gRevEnabled = cfg.revEnabled;
    gRevMix = cfg.revMix;
    gRevSize = cfg.revSize;
    gRevDamping = cfg.revDamping;
    gRevModSpeed = cfg.revModSpeed;
    gRevModDepth = cfg.revModDepth;
    gRevFrozen = cfg.revFrozen;
    // Delay
    gDelayTime = cfg.delayTime;
    gDelayFeedback = cfg.delayFeedback;
    gDelayMix = cfg.delayMix;
    // FxOrder
    gFxOrder.filterPostChorus = cfg.fxFilterPostChorus;
    gFxOrder.delayPostReverb = cfg.fxDelayPostReverb;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool configStore_load(uint8_t slot) {
    if (slot >= kMaxPresets)
        slot = 0;
    ensureEeprom();
    AlloyConfig cfg;
    EEPROM.get(slotAddr(slot), cfg);
    if (cfg.magic != kConfigMagic || cfg.version != kConfigVersion)
        return false; // no valid config — caller uses compile-time defaults
    applyConfig(cfg);
    return true;
}

ConfigSaveResult configStore_save(uint8_t slot) {
    if (slot >= kMaxPresets)
        slot = 0;
    const uint32_t now = millis();

    // Rate limit applies only to the auto-save slot (slot 0) to protect flash.
    // Explicit preset saves (slots 1–9) bypass the rate limit.
    if (slot == 0 && sLastSaveMs > 0 && (now - sLastSaveMs) < kMinSaveIntervalMs)
        return ConfigSaveResult::THROTTLED;

    ensureEeprom();

    // Pack current parameters.
    AlloyConfig newCfg;
    packConfig(newCfg);

    // Dirty check: skip erase/program cycle if contents are identical.
    AlloyConfig stored;
    EEPROM.get(slotAddr(slot), stored);
    if (memcmp(&newCfg, &stored, kSlotBytes) == 0)
        return ConfigSaveResult::UNCHANGED;

    // Write to EEPROM buffer then commit (pauses Core 1 for ~10 ms).
    EEPROM.put(slotAddr(slot), newCfg);
    EEPROM.commit();
    if (slot == 0)
        sLastSaveMs = now;
    return ConfigSaveResult::SAVED;
}

void configStore_reset(uint8_t slot) {
    ensureEeprom();
    uint32_t zero = 0;
    if (slot == 255) {
        // Wipe all slots.
        for (uint8_t i = 0; i < kMaxPresets; i++)
            EEPROM.put(slotAddr(i), zero);
    } else {
        if (slot >= kMaxPresets)
            slot = 0;
        EEPROM.put(slotAddr(slot), zero);
    }
    EEPROM.commit();
    sLastSaveMs = 0;
}
