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
// Renumbering slots is safe as far as flash goes — preset blobs are structs of
// named fields (see alloy_config.h), not slot-indexed arrays, and PotTakeover's
// slot-indexed state is runtime-only. What renumbering *does* affect is
// `kShiftPairs` below and `PanelLayout`'s coordinate table, both of which index
// by slot; change them together or a knob moves without anything failing.
//
// Counts are AlloyFlux's, not the platform's — the HAL sizes for the largest
// module it expects to host, and the unused slots simply go unassigned.
// ---------------------------------------------------------------------------

/**
 * Knobs. Slots 1–9 are the physical knobs, **numbered row-major to match the
 * panel silkscreen**: POT 1–3 across the top, 4–6 the middle, 7–9 the bottom.
 * Slots 10–15 are their SHIFT-secondaries.
 *
 * The row-major ordering is not cosmetic. `platform/vcv/PanelLayout.h` indexes
 * its coordinate table by these very slots, so `PanelLayout::pot(Pot::ROOT)`
 * lands on the knob the panel calls POT 1. They were previously numbered in the
 * order the parameters happened to be written, which put POT_2 at top-right in
 * the firmware and top-centre on the panel — a disagreement nothing could catch.
 */
namespace Pot
{
// ---- 9 physical panel knobs (same on hardware and VCV) ----
constexpr PotId ROOT  = PotId::POT_1; ///< top L    — root pitch (V/Oct centre)
constexpr PotId COLOR = PotId::POT_2; ///< top C    — FM depth / Hz spread
constexpr PotId RELATION = PotId::POT_3; ///< top R    — voice 2 offset, 0–24 st
constexpr PotId SHAPE  = PotId::POT_4; ///< mid L    — sine→tri→saw→pulse→hollow
constexpr PotId CURVE  = PotId::POT_5; ///< mid C    — envelope pluck ↔ swell
constexpr PotId MOTION = PotId::POT_6; ///< mid R    — drift + chorus depth
constexpr PotId DELAY  = PotId::POT_7; ///< bottom L — delay mix, 0 = bypass
constexpr PotId SPACE  = PotId::POT_8; ///< bottom C — stereo width
constexpr PotId REVERB = PotId::POT_9; ///< bottom R — reverb mix, 0 = bypass

// ---- SHIFT-secondaries. Hardware: the same physical knob, read while SHIFT is
//      held, so a secondary has no position of its own. VCV: context-menu
//      sliders. Numbered in row-major order of their primary, so POT_10..POT_15
//      pair with POT_4..POT_9 in sequence. ----
constexpr PotId FATNESS    = PotId::POT_10; ///< SHIFT+SHAPE  — sub level
constexpr PotId CURVETIME  = PotId::POT_11; ///< SHIFT+CURVE  — env time 0.25–4×
constexpr PotId DRIFTSPEED = PotId::POT_12; ///< SHIFT+MOTION — drift rate
constexpr PotId DELAYTIME  = PotId::POT_13; ///< SHIFT+DELAY  — delay time
constexpr PotId VOL        = PotId::POT_14; ///< SHIFT+SPACE  — master volume
constexpr PotId REVERBSIZE = PotId::POT_15; ///< SHIFT+REVERB — plate size

constexpr uint8_t kCount = 15;
} // namespace Pot

/**
 * CV input jacks.
 *
 * The panel numbers its four modulation jacks CV 1..CV 4 and they map onto the
 * HAL in order, CV 1..CV 4 -> CV_3..CV_6; CV_1 and CV_2 are V/Oct and Gate.
 * Physically, left to right as the panel reads:
 *
 *   upper row:  V/OCT   GATE   MIDI IN   CV 1      CV 2
 *               J3      J4     J2        J5        J6
 *               CV_1    CV_2   —         CV_3      CV_4
 *               —       —      —         RELATION  SHAPE
 *
 *   lower row:  FM IN   CV 3     CV 4     OUT L     OUT R
 *               (J7)    (J9)     J8       J10       J11
 *               CV_7    CV_5     CV_6     —         —
 *               FM      MOTION   SPACE    —         —
 *
 * FM IN is at the LEFT end of the lower row; the row is otherwise in slot
 * order. MIDI IN is a MIDI jack, not a CV one, and has no slot at all.
 *
 * ⚠ The two parenthesised designators are the one place the board and the
 * panel disagree, and it is not a silkscreen fix. Electrically J9 *is* FM IN —
 * the only jack on a direct ADC pin (GP27, deliberately off the analogue mux
 * so the exciter can be sampled at audio rate) — and J7 is an ordinary mux
 * channel. But `MainPCB.kicad_pcb` still places J7 at the left end and J9 one
 * position to its right, so the board has the two swapped relative to the art.
 * Correcting it means moving that net and its conditioning stage, which is not
 * the generic one: FM IN is ±8 V with a 100 pF cap, the CV jacks are ±5 V
 * through the mux. Until the board moves, Rack and the panel agree with each
 * other and not with the PCB. `platform/vcv/PanelLayout.h` owns the
 * coordinates and carries the full note.
 */
namespace Cv
{
constexpr CVId VOCT = CVId::CV_1; ///< J3, V/OCT — readCV() returns volts
constexpr CVId GATE = CVId::CV_2; ///< J4, GATE — 0.0 or raw voltage when high
constexpr CVId RELATION = CVId::CV_3; ///< J5, CV 1  — bipolar, ±1.0
constexpr CVId SHAPE    = CVId::CV_4; ///< J6, CV 2  — bipolar, ±1.0
constexpr CVId MOTION   = CVId::CV_5; ///< J7, CV 3  — bipolar, ±1.0
constexpr CVId SPACE    = CVId::CV_6; ///< J8, CV 4  — bipolar, ±1.0
constexpr CVId FM       = CVId::CV_7; ///< J9, FM IN — FM / COLOR, ±1.0

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
 *
 * This role order pairs the LEDs left/right, which is **not** how the panel
 * numbers them: the silkscreen's LED1..LED7 (board D12, D13, D14, D15, D16,
 * D21, D22) run counter-clockwise from the upper left, which is also the
 * daisy-chain order. Both columns are spelled out below; the third mapping,
 * LightId -> chain position, is kChainPos in HardwarePicoIO::writeLight().
 */
namespace Led
{
constexpr LightId VOICE_L = LightId::LIGHT_1; ///< LED1 / D12 — upper left
constexpr LightId VOICE_R = LightId::LIGHT_2; ///< LED7 / D22 — upper right
constexpr LightId MOD_L   = LightId::LIGHT_3; ///< LED2 / D13 — left
constexpr LightId MOD_R   = LightId::LIGHT_4; ///< LED6 / D21 — right
constexpr LightId MODE    = LightId::LIGHT_5; ///< LED3 / D14 — lower left
constexpr LightId SHIFT   = LightId::LIGHT_6; ///< LED5 / D16 — lower right
constexpr LightId CENTRE  = LightId::LIGHT_7; ///< LED4 / D15 — bottom centre

constexpr uint8_t kCount = 7;
} // namespace Led
