#pragma once
#include <stdint.h>

#include "ConfigSlot.h" // platform slot container: magic, engine tag, blob
#include "VoiceMode.h"
#include "dsp/ChorusEngine.h"
#include "dsp/CurveEngine.h"
#include "dsp/FilterEngine.h"
#include "dsp/FxChain.h"
#include "dsp/ReverbEngine.h"
#include "scale_quantizer.h" // ScaleId enum (M49)

/**
 * Flash config persistence — Milestone 30 (config save/load).
 *
 * Uses the Earle Philhower EEPROM emulation library which provides:
 *   - Wear levelling (circular buffer across multiple 4 KB flash pages)
 *   - Automatic pause/resume of the other core during flash erase/program
 *
 * AlloyFlux adds two more protection layers on top:
 *   - Dirty check: commit() only called when values actually changed
 *   - Rate limit: minimum 10 s between writes (see kMinSaveIntervalMs)
 *
 * The slot container (magic, engine tag, fixed-size blob) belongs to the
 * platform — see platform/include/ConfigSlot.h.  AlloyConfig below is what
 * this module stores *inside* a slot's blob, and nothing outside this module
 * knows its shape.
 *
 * Adding a new parameter:
 *   1. Add a field to AlloyConfig below.
 *   2. Bump kEngineVersion so existing stored configs are detected as
 *      incompatible and defaults are used instead (safe migration).
 *   3. Add pack/apply lines in config_store.cpp.
 */

/// Identifies AlloyFlux as the engine that wrote a slot — 'A','F'.  Any other
/// module on this platform must pick a different value.
static constexpr uint16_t kAlloyFluxEngineId = 0x4146;

/// Layout version of the AlloyConfig payload.  Continues the old
/// kConfigVersion sequence (which reached 6) rather than restarting, so no
/// stale slot from a pre-M63d build can ever match by coincidence.
static constexpr uint16_t kAlloyFluxEngineVersion = 9;

// Canonical default filter cutoff: nearest 7-bit-MIDI-representable value to 1 kHz
// on the log 20–16000 Hz scale.  CC 74 → 20 × (16000/20)^(74/127) ≈ 983.2 Hz.
// Used in both gFilterCutoff init (main.cpp) and configStore_applyDefaults so
// the in-RAM value always matches what the web configurator reads back over MIDI.
static constexpr float kDefaultFilterCutoff = 983.2f;

// The payload written into a ConfigSlot's blob.  No magic or version field of
// its own — the slot header carries both.
struct AlloyConfig
{
    // Pitch / voice
    float   baseFreq;
    float   color;    // COLOR knob 0–1 — the *internal* FM index
    float   fmAmount; // FM IN jack depth 0–1, SHIFT+ROOT (M77)
    float   relation;
    uint8_t voiceMode; // cast of VoiceMode enum
    // Timbre
    float   shape;
    float   fatness;
    uint8_t subOctave; // 1 or 2
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
    // Filter (M26a / M5x)
    float   filterCutoff;
    float   filterRes;
    uint8_t filterMode; // cast of FilterMode enum
    uint8_t filterType; // cast of FilterType enum
    // Envelope (M5x)
    uint8_t envelopeType; // cast of EnvelopeType enum
    float   adsrAttack;
    float   adsrDecay;
    float   adsrSustain;
    float   adsrRelease;
    bool    adsrLoop;
    // Reverb (M26b)
    bool  revEnabled;
    float revMix;
    float revSize;
    float revDamping;
    float revModSpeed;
    float revModDepth;
    bool  revFrozen;
    // Delay (M26c)
    float delayTime;
    float delayFeedback;
    float delayMix;
    // FxOrder (M26a)
    bool fxFilterPostChorus;
    bool fxDelayPostReverb;
    // MIDI behaviour
    bool velocitySensitive; // true = velocity scales output, false = always 1.0
    // Portamento / glide
    float glideTime;    // 0.0 = instant, 0.001–2.0 s
    bool  glideEnabled; // portamento on/off
    // Scale quantizer (M49)
    uint8_t quantizeScale; // cast of ScaleId enum; 0 = CHROMATIC (bypass)
    int8_t  transpose;     // semitone offset −24…+24; 0 = no transpose
    // Knob takeover (M62)
    uint8_t potTakeover; // cast of PotTakeoverMode; 2 = SCALE (default)
    // GATE jack note length, ms; 0 = follow the gate
    float gateLength;
};

static_assert(sizeof(AlloyConfig) <= kSlotBlobBytes,
              "AlloyConfig outgrew the platform slot blob — raise "
              "kSlotBlobBytes in platform/include/ConfigSlot.h");

// The save/load/reset API itself is the platform's — see
// platform/include/config_store.h. These three are AlloyFlux's half of it,
// called through ModuleHooks; nothing outside this module should use them.

/** Serialise the current gXxx globals into a config struct. */
void packAlloyConfig(AlloyConfig &cfg);

/** Write a config struct back into the gXxx globals. */
void applyAlloyConfig(const AlloyConfig &cfg);

/** Reset every gXxx global to its compile-time default. Touches no flash. */
void applyAlloyDefaults();
