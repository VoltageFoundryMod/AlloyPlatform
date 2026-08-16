#include "config_store.h"
#include "ModuleHooks.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <string.h> // memcmp, memcpy, memset

// ---------------------------------------------------------------------------
// Flash layout
//
// The Earle Philhower EEPROM library maps a RAM buffer onto one or more
// 4 KB flash pages at the top of flash, with a circular-buffer scheme that
// distributes erase cycles across all pages.
//
// kEepromBytes must stay <= the RP2350 EEPROM emulation size (4096 by
// default); ConfigSlot.h static_asserts exactly that.
// ---------------------------------------------------------------------------
static constexpr int kSlotBytes   = sizeof(ConfigSlot);
static constexpr int kEepromBytes = kMaxPresets * kSlotBytes;

// Minimum milliseconds between flash commits.
// Flash is rated ~100,000 erase cycles; at 10 s minimum that's >27 years of
// continuous saving.  Adjust only if there is a concrete need.
static constexpr uint32_t kMinSaveIntervalMs = 10000;

static uint32_t sLastSaveMs  = 0;
static bool     sEepromReady = false;

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

// Ask the module to serialise itself, and wrap the result in a tagged slot.
//
// Zero-initialised: the blob's unused tail and any padding inside the module's
// own struct would otherwise be indeterminate, and the dirty check below is a
// memcmp over the whole slot — stack garbage there would report a change on
// every save and burn a flash cycle for nothing.
static void makeSlot(ConfigSlot &slot)
{
    memset(&slot, 0, sizeof(slot));
    slot.magic         = kSlotMagic;
    slot.engineId      = kEngineId;
    slot.engineVersion = kEngineVersion;
    moduleHook_packConfig(slot.blob, kSlotBlobBytes);
}

bool configStore_load(uint8_t slot)
{
    if(slot >= kMaxPresets)
        slot = 0;
    ensureEeprom();
    ConfigSlot stored;
    EEPROM.get(slotAddr(slot), stored);
    // Three gates, in widening order of specificity: is this a slot at all, was
    // it written by this module, and does its blob match the layout this build
    // understands.  Any failure leaves the compile-time defaults in place —
    // notably, a slot holding another engine's preset is skipped rather than
    // reinterpreted as this one's parameters.
    if(stored.magic != kSlotMagic || stored.engineId != kEngineId
       || stored.engineVersion != kEngineVersion)
        return false;
    moduleHook_applyConfig(stored.blob, kSlotBlobBytes);
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

    ConfigSlot newSlot;
    makeSlot(newSlot);

    // Dirty check: skip erase/program cycle if contents are identical.
    ConfigSlot stored;
    EEPROM.get(slotAddr(slot), stored);
    if(memcmp(&newSlot, &stored, kSlotBytes) == 0)
        return ConfigSaveResult::UNCHANGED;

    // Write to EEPROM buffer then commit.  commit() parks the other core for
    // the erase/program — that is the audio core — so a save costs a ~10 ms
    // dropout.  The dirty check above is what keeps that off the periodic
    // autosave path.
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
{ moduleHook_applyDefaults(); }
