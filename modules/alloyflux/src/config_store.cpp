#include "config_store.h"
#include "params.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h> // memcmp, memcpy

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
static constexpr int kSlotBytes   = sizeof(ConfigSlot);
static constexpr int kEepromBytes = kMaxPresets * kSlotBytes;

// Minimum milliseconds between flash commits.
// Flash is rated ~100,000 erase cycles; at 10 s minimum that's >27 years of
// continuous saving.  Adjust only if there is a concrete need.
static constexpr uint32_t kMinSaveIntervalMs = 10000;

static uint32_t sLastSaveMs  = 0;
static bool     sEepromReady = false;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void ensureEeprom()
{
    if(!sEepromReady)
    {
        EEPROM.begin(kEepromBytes);
        sEepromReady = true;
    }
}

static int slotAddr(uint8_t slot)
{ return (int)slot * kSlotBytes; }

// Wrap a packed payload in a slot, tagged as ours.
// Zero-initialised: the blob's unused tail and any padding inside AlloyConfig
// would otherwise be indeterminate, and the dirty check below is a memcmp over
// the whole slot — stack garbage there would report a change on every save and
// burn a flash cycle for nothing.
static void makeSlot(ConfigSlot &slot, const AlloyConfig &cfg)
{
    memset(&slot, 0, sizeof(slot));
    slot.magic         = kSlotMagic;
    slot.engineId      = kEngineId;
    slot.engineVersion = kEngineVersion;
    memcpy(slot.blob, &cfg, sizeof(cfg));
}

// Pack all current gXxx globals into a config struct.
static void packConfig(AlloyConfig &cfg)
{
    memset(&cfg, 0, sizeof(cfg));
    cfg.baseFreq    = gBaseFreq;
    cfg.color       = gColor;
    cfg.relation    = gRelation;
    cfg.voiceMode   = (uint8_t)gVoiceMode;
    cfg.shape       = gShape;
    cfg.fatness     = gFatness;
    cfg.subOctave   = gSubOctave;
    cfg.motion      = gMotion;
    cfg.driftSpeed  = gDriftSpeed;
    cfg.chorusMode  = (uint8_t)gChorusMode;
    cfg.space       = gSpace;
    cfg.curve       = gCurve;
    cfg.curveTime   = gCurveTime;
    cfg.volume      = gVolume;
    cfg.midiChannel = gMidiChannel;
    // Filter
    cfg.filterCutoff = gFilterCutoff;
    cfg.filterRes    = gFilterRes;
    cfg.filterMode   = (uint8_t)gFilterMode;
    cfg.filterType   = (uint8_t)gFilterType;
    // Envelope
    cfg.envelopeType = (uint8_t)gEnvelopeType;
    cfg.adsrAttack   = gAdsrAttack;
    cfg.adsrDecay    = gAdsrDecay;
    cfg.adsrSustain  = gAdsrSustain;
    cfg.adsrRelease  = gAdsrRelease;
    cfg.adsrLoop     = gAdsrLoop;
    // Reverb
    cfg.revEnabled  = gRevEnabled;
    cfg.revMix      = gRevMix;
    cfg.revSize     = gRevSize;
    cfg.revDamping  = gRevDamping;
    cfg.revModSpeed = gRevModSpeed;
    cfg.revModDepth = gRevModDepth;
    cfg.revFrozen   = gRevFrozen;
    // Delay
    cfg.delayTime     = gDelayTime;
    cfg.delayFeedback = gDelayFeedback;
    cfg.delayMix      = gDelayMix;
    // FxOrder
    cfg.fxFilterPostChorus = gFxOrder.filterPostChorus;
    cfg.fxDelayPostReverb  = gFxOrder.delayPostReverb;
    // MIDI behaviour
    cfg.velocitySensitive = gVelocitySensitive;
    // Portamento / glide
    cfg.glideTime    = gGlideTime;
    cfg.glideEnabled = gGlideEnabled;
    // Scale quantizer (M49)
    cfg.quantizeScale = (uint8_t)gQuantizeScale;
    cfg.transpose     = gTranspose;
    // Knob takeover (M62)
    cfg.potTakeover = (uint8_t)gPotTakeoverMode;
}

// Apply a validated config struct to all gXxx globals.
static void applyConfig(const AlloyConfig &cfg)
{
    gBaseFreq    = cfg.baseFreq;
    gColor       = cfg.color;
    gRelation    = cfg.relation;
    gVoiceMode   = (VoiceMode)cfg.voiceMode;
    gShape       = cfg.shape;
    gFatness     = cfg.fatness;
    gSubOctave   = (cfg.subOctave == 2) ? 2u : 1u;
    gMotion      = cfg.motion;
    gDriftSpeed  = cfg.driftSpeed;
    gChorusMode  = (ChorusMode)cfg.chorusMode;
    gSpace       = cfg.space;
    gCurve       = cfg.curve;
    gCurveTime   = cfg.curveTime;
    gVolume      = cfg.volume;
    gMidiChannel = cfg.midiChannel;
    // Filter
    gFilterCutoff = cfg.filterCutoff;
    gFilterRes    = cfg.filterRes;
    gFilterMode   = (FilterMode)cfg.filterMode;
    gFilterType   = (FilterType)cfg.filterType;
    // Envelope
    gEnvelopeType = (EnvelopeType)cfg.envelopeType;
    gAdsrAttack   = cfg.adsrAttack;
    gAdsrDecay    = cfg.adsrDecay;
    gAdsrSustain  = cfg.adsrSustain;
    gAdsrRelease  = cfg.adsrRelease;
    gAdsrLoop     = cfg.adsrLoop;
    // Reverb
    gRevEnabled  = cfg.revEnabled;
    gRevMix      = cfg.revMix;
    gRevSize     = cfg.revSize;
    gRevDamping  = cfg.revDamping;
    gRevModSpeed = cfg.revModSpeed;
    gRevModDepth = cfg.revModDepth;
    gRevFrozen   = cfg.revFrozen;
    // Delay
    gDelayTime     = cfg.delayTime;
    gDelayFeedback = cfg.delayFeedback;
    gDelayMix      = cfg.delayMix;
    // FxOrder
    gFxOrder.filterPostChorus = cfg.fxFilterPostChorus;
    gFxOrder.delayPostReverb  = cfg.fxDelayPostReverb;
    // MIDI behaviour
    gVelocitySensitive = cfg.velocitySensitive;
    // Portamento / glide
    gGlideTime    = cfg.glideTime;
    gGlideEnabled = cfg.glideEnabled;
    // Scale quantizer (M49)
    gQuantizeScale = (cfg.quantizeScale < (uint8_t)ScaleId::COUNT)
                         ? (ScaleId)cfg.quantizeScale
                         : ScaleId::CHROMATIC;
    gTranspose     = (int8_t)constrain((int)cfg.transpose, -24, 24);
    // Knob takeover (M62) — unknown values fall back to the SCALE default.
    gPotTakeoverMode = (cfg.potTakeover <= (uint8_t)PotTakeoverMode::SCALE)
                           ? (PotTakeoverMode)cfg.potTakeover
                           : PotTakeoverMode::SCALE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool configStore_load(uint8_t slot)
{
    if(slot >= kMaxPresets)
        slot = 0;
    ensureEeprom();
    ConfigSlot stored;
    EEPROM.get(slotAddr(slot), stored);
    // Three gates, in widening order of specificity: is this a slot at all, was
    // it written by AlloyFlux, and does its payload match the layout this build
    // understands.  Any failure leaves the compile-time defaults in place —
    // notably, a slot holding another engine's preset is skipped rather than
    // reinterpreted as AlloyFlux floats.
    if(stored.magic != kSlotMagic || stored.engineId != kEngineId
       || stored.engineVersion != kEngineVersion)
        return false;
    AlloyConfig cfg;
    memcpy(&cfg, stored.blob, sizeof(cfg));
    applyConfig(cfg);
    return true;
}

ConfigSaveResult configStore_save(uint8_t slot)
{
    if(slot >= kMaxPresets)
        slot = 0;
    const uint32_t now = millis();

    // Rate limit applies only to the auto-save slot (slot 0) to protect flash.
    // Explicit preset saves (slots 1–9) bypass the rate limit.
    if(slot == 0 && sLastSaveMs > 0 && (now - sLastSaveMs) < kMinSaveIntervalMs)
        return ConfigSaveResult::THROTTLED;

    ensureEeprom();

    // Pack current parameters into a tagged slot.
    AlloyConfig newCfg;
    packConfig(newCfg);
    ConfigSlot newSlot;
    makeSlot(newSlot, newCfg);

    // Dirty check: skip erase/program cycle if contents are identical.
    ConfigSlot stored;
    EEPROM.get(slotAddr(slot), stored);
    if(memcmp(&newSlot, &stored, kSlotBytes) == 0)
        return ConfigSaveResult::UNCHANGED;

    // Write to EEPROM buffer then commit.  commit() parks the other core for
    // the erase/program — that is Core 1, the audio core — so a save costs a
    // ~10 ms dropout.  The dirty check above is what keeps that off the
    // periodic autosave path.
    EEPROM.put(slotAddr(slot), newSlot);
    EEPROM.commit();
    if(slot == 0)
        sLastSaveMs = now;
    return ConfigSaveResult::SAVED;
}

void configStore_reset(uint8_t slot)
{
    ensureEeprom();
    uint32_t zero = 0;
    if(slot == 255)
    {
        // Wipe all slots.
        for(uint8_t i = 0; i < kMaxPresets; i++)
            EEPROM.put(slotAddr(i), zero);
    }
    else
    {
        if(slot >= kMaxPresets)
            slot = 0;
        EEPROM.put(slotAddr(slot), zero);
    }
    EEPROM.commit();
    sLastSaveMs = 0;
}

void configStore_applyDefaults()
{
    AlloyConfig d        = {};
    d.baseFreq           = 440.0f;
    d.color              = 0.0f;
    d.relation           = 0.0f;
    d.voiceMode          = (uint8_t)VoiceMode::PAIR;
    d.shape              = 0.0f;
    d.fatness            = 0.4f;
    d.subOctave          = 1;
    d.motion             = 0.0f;
    d.driftSpeed         = 0.04f;
    d.chorusMode         = (uint8_t)ChorusMode::I_II;
    d.space              = 1.0f;
    d.curve              = 0.5f;
    d.curveTime          = 1.0f;
    d.volume             = 1.0f;
    d.midiChannel        = 0;
    d.filterCutoff       = kDefaultFilterCutoff;
    d.filterRes          = 0.0f;
    d.filterMode         = (uint8_t)FilterMode::OFF;
    d.filterType         = (uint8_t)FilterType::SVF;
    d.envelopeType       = (uint8_t)EnvelopeType::AR;
    d.adsrAttack         = 0.05f;
    d.adsrDecay          = 0.10f;
    d.adsrSustain        = 0.8f;
    d.adsrRelease        = 0.30f;
    d.adsrLoop           = false;
    d.revEnabled         = false;
    d.revMix             = 0.0f;
    d.revSize            = 0.5f;
    d.revDamping         = 0.5f;
    d.revModSpeed        = 1.0f;
    d.revModDepth        = 1.0f;
    d.revFrozen          = false;
    d.delayTime          = 100.0f;
    d.delayFeedback      = 0.5f;
    d.delayMix           = 0.0f;
    d.fxFilterPostChorus = false;
    d.fxDelayPostReverb  = false;
    d.velocitySensitive  = true;
    d.glideTime          = 0.0f;
    d.glideEnabled       = false;
    d.quantizeScale      = (uint8_t)ScaleId::CHROMATIC;
    d.transpose          = 0;
    d.potTakeover        = (uint8_t)PotTakeoverMode::SCALE;
    applyConfig(d);
}
