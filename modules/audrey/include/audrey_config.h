#pragma once
#include <stdint.h>

#include "ConfigSlot.h"

/**
 * Audrey II's preset payload — what goes inside a platform ConfigSlot's blob.
 *
 * The slot container, flash mechanics, dirty check and rate limit belong to
 * the platform (platform/include/config_store.h). This is only the module's
 * half: one field per params.json row, plus the platform's MIDI channel.
 *
 * Adding a parameter:
 *   1. Add a row to params.json and run `make params MODULE=audrey`.
 *   2. Add a field here and pack/apply lines in config_store.cpp.
 *   3. Bump kAudreyEngineVersion so stored presets are discarded rather than
 *      misread.
 */

/// Identifies Audrey as the engine that wrote a slot — 'A','U'. Must differ
/// from every other module's; AlloyFlux is 'A','F' (0x4146).
static constexpr uint16_t kAudreyEngineId = 0x4155;

/// Layout version of the AudreyConfig payload.
/// 2: added exciterLevel (M63h).
static constexpr uint16_t kAudreyEngineVersion = 2;

struct AudreyConfig
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

static_assert(sizeof(AudreyConfig) <= kSlotBlobBytes,
              "AudreyConfig outgrew the platform slot blob — raise "
              "kSlotBlobBytes in platform/include/ConfigSlot.h");

/** Serialise the current gXxx globals into a config struct. */
void packAudreyConfig(AudreyConfig &cfg);

/** Write a config struct back into the gXxx globals. */
void applyAudreyConfig(const AudreyConfig &cfg);

/** Reset every gXxx global to its compile-time default. Touches no flash. */
void applyAudreyDefaults();
