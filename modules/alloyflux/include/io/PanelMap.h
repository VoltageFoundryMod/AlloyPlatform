#pragma once

#include "io/HardwareIO.h"

// ---------------------------------------------------------------------------
// AlloyFlux panel map — this module's vocabulary, mapped onto the platform's
// positional I/O slots.
//
// The HAL names slots (POT_1, CV_3, LIGHT_5…) and knows nothing about what
// they do.  This header is the one place that says slot 1 is the pitch knob,
// and it is the only file that has to change if the panel is re-laid out.
//
// ⚠ **Slot order is the flash format.**  Preset blobs and PotTakeover's state
// array are indexed by slot number, so inserting an entry in the middle
// renumbers everything after it and silently reinterprets stored presets.
// Append new entries at the end of each group and bump kEngineVersion.
//
// Counts are AlloyFlux's, not the platform's — the HAL sizes for the largest
// module it expects to host, and the unused slots simply go unassigned.
// ---------------------------------------------------------------------------

/** Knobs.  Slots 1–9 are physical; 10–15 are their SHIFT-secondaries. */
namespace Pot
{
// ---- 9 physical panel knobs (same on hardware and VCV) ----
constexpr PotId ROOT     = PotId::POT_1; ///< Root pitch (V/Oct centre)
constexpr PotId RELATION = PotId::POT_2; ///< Semitone offset, voice 2 (0–24 st)
constexpr PotId SHAPE    = PotId::POT_3; ///< sine → tri → saw → pulse → hollow
constexpr PotId MOTION   = PotId::POT_4; ///< Drift + chorus depth [0–1]
constexpr PotId COLOR  = PotId::POT_5; ///< FM depth / ensemble Hz spread [0–1]
constexpr PotId CURVE  = PotId::POT_6; ///< Envelope character (pluck ↔ swell)
constexpr PotId SPACE  = PotId::POT_7; ///< Stereo width [0–1]
constexpr PotId DELAY  = PotId::POT_8; ///< Delay wet mix — 0 = bypass (M56)
constexpr PotId REVERB = PotId::POT_9; ///< Reverb wet mix — 0 = bypass (M56)

// ---- SHIFT-secondaries.  Hardware: the same physical knob, read while SHIFT
//      is held.  VCV: hidden params / context-menu sliders. ----
constexpr PotId FATNESS    = PotId::POT_10; ///< Sub level      (SHIFT+SHAPE)
constexpr PotId DRIFTSPEED = PotId::POT_11; ///< Drift rate     (SHIFT+MOTION)
constexpr PotId CURVETIME  = PotId::POT_12; ///< Env time 0.25–4× (SHIFT+CURVE)
constexpr PotId VOL        = PotId::POT_13; ///< Master volume  (SHIFT+SPACE)
constexpr PotId DELAYTIME  = PotId::POT_14; ///< Delay time     (SHIFT+DELAY)
constexpr PotId REVERBSIZE = PotId::POT_15; ///< Plate size     (SHIFT+REVERB)

constexpr uint8_t kCount = 15;
} // namespace Pot

/** CV input jacks. */
namespace Cv
{
constexpr CVId VOCT = CVId::CV_1; ///< V/Oct pitch — readCV() returns volts
constexpr CVId GATE = CVId::CV_2; ///< Gate — 0.0 or the raw voltage when high
constexpr CVId RELATION = CVId::CV_3; ///< bipolar, normalised to ±1.0
constexpr CVId SHAPE    = CVId::CV_4; ///< bipolar, normalised to ±1.0
constexpr CVId MOTION   = CVId::CV_5; ///< bipolar, normalised to ±1.0
constexpr CVId SPACE    = CVId::CV_6; ///< bipolar, normalised to ±1.0
constexpr CVId FM       = CVId::CV_7; ///< FM / COLOR — bipolar, ±1.0

constexpr uint8_t kCount = 7;
} // namespace Cv

/** Panel buttons. */
namespace Btn
{
constexpr ButtonId MODE  = ButtonId::BUTTON_1; ///< Mode cycle   (GP10)
constexpr ButtonId SHIFT = ButtonId::BUTTON_2; ///< Shift / trig (GP11)

constexpr uint8_t kCount = 2;
} // namespace Btn

/**
 * Panel LEDs.  Order matches LedId in io/LedEngine.h one-for-one —
 * LedEngine::writeTo() casts an index between them, so the two must stay
 * in step.
 *
 *      VOICE_L ·  ·  · VOICE_R      top:        voice activity
 *     MOD_L  ·  ·  ·  · MOD_R       mid-top:    motion / modulation
 *        MODE  ·   SHIFT            mid-bottom: mode / shift-drone
 *           · CENTRE ·              bottom:     heartbeat / global
 */
namespace Led
{
constexpr LightId VOICE_L = LightId::LIGHT_1; ///< D12 / VCV LED1
constexpr LightId VOICE_R = LightId::LIGHT_2; ///< D22 / VCV LED7
constexpr LightId MOD_L   = LightId::LIGHT_3; ///< D13 / VCV LED2
constexpr LightId MOD_R   = LightId::LIGHT_4; ///< D21 / VCV LED6
constexpr LightId MODE    = LightId::LIGHT_5; ///< D14 / VCV LED3
constexpr LightId SHIFT   = LightId::LIGHT_6; ///< D16 / VCV LED5
constexpr LightId CENTRE  = LightId::LIGHT_7; ///< D15 / VCV LED4

constexpr uint8_t kCount = 7;
} // namespace Led
