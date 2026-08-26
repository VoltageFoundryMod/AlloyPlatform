#pragma once
#include <stdint.h>

#include "ConfigSlot.h"

/**
 * Alloy Coil's preset payload — what goes inside a platform ConfigSlot's blob.
 *
 * The slot container, flash mechanics, dirty check and rate limit belong to
 * the platform (platform/include/config_store.h). This is only the module's
 * half: one field per params.json row, plus the platform's MIDI channel.
 *
 * Adding a parameter:
 *   1. Add a row to params.json and run `make params MODULE=alloycoil`.
 *   2. Add a field here and pack/apply lines in config_store.cpp.
 *   3. Bump kCoilEngineVersion so stored presets are discarded rather than
 *      misread.
 */

/// Identifies Alloy Coil as the engine that wrote a slot — 'A','C'. Must differ
/// from every other module's; AlloyFlux is 'A','F' (0x4146).
static constexpr uint16_t kCoilEngineId = 0x4143;

/// Layout version of the CoilConfig payload.
/// 2: added exciterLevel (M63h).
static constexpr uint16_t kCoilEngineVersion = 2;

struct CoilConfig
{
    float   stringPitch;
    float   feedbackGain;
    float   feedbackDelay;
    float   feedbackLPF;
    float   feedbackHPF;
    float   echoSend;
    float   echoTime;
    float   echoFeedback;
    float   reverbMix;
    float   reverbDecay;
    float   outputLevel;
    float   exciterLevel;
    uint8_t midiChannel; // 0 = omni, 1–16
};

static_assert(sizeof(CoilConfig) <= kSlotBlobBytes,
              "CoilConfig outgrew the platform slot blob — raise "
              "kSlotBlobBytes in platform/include/ConfigSlot.h");

/** Serialise the firmware's current parameters (gCoilParams) into a config struct. */
void packCoilConfig(CoilConfig &cfg);

/** Write a config struct back into gCoilParams. */
void applyCoilConfig(const CoilConfig &cfg);

/** Reset every parameter to its compile-time default. Touches no flash. */
void applyCoilDefaults();
