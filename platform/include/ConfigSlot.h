#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// ConfigSlot — the platform's flash preset container.
//
// The platform owns the slot: how many there are, how big each is, and how to
// tell whose it is.  What goes *inside* one is entirely the module's business —
// the platform never interprets a byte of the payload.
//
// Why the engine tag: one board runs one firmware image at a time, but they all
// share the same flash region, so an Alloy Coil preset and an AlloyFlux preset can
// occupy the same slot on the same hardware across a reflash.  The tag does not
// preserve both — whichever engine saves last wins that slot — but it does
// guarantee the other one is *recognised as foreign and skipped* rather than
// applied as if its floats meant something.  Without it, loading a slot written
// by a different engine sets every parameter to noise.
//
// A stale slot needs no cleanup: engineId or engineVersion simply fails to
// match and the module falls back to its compile-time defaults.
// ---------------------------------------------------------------------------

/// "This is an Alloy platform slot."  Historic value, kept so an existing flash
/// dump is still recognisable; it identifies the container, not the engine.
static constexpr uint32_t kSlotMagic = 0xAF10CF01;

/// Payload bytes per slot.  Sized for the largest module payload plus room to
/// grow — a module that outgrows it should raise this rather than pack tighter,
/// but note that kMaxPresets × sizeof(ConfigSlot) must stay inside the RP2350
/// EEPROM emulation region (4096 bytes by default).
static constexpr uint16_t kSlotBlobBytes = 192;

/// Slot 0 is the auto-saved live state; 1…kMaxPresets-1 are user presets.
static constexpr uint8_t kMaxPresets = 10;

struct ConfigSlot
{
    uint32_t magic;         ///< kSlotMagic
    uint16_t engineId;      ///< which module wrote this — see the module header
    uint16_t engineVersion; ///< that module's payload layout version
    uint8_t  blob[kSlotBlobBytes]; ///< opaque to the platform
};

static_assert(sizeof(ConfigSlot) * kMaxPresets <= 4096,
              "preset slots exceed the RP2350 EEPROM emulation region");
