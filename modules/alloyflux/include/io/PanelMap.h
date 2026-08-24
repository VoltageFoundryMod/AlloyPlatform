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
 *
 * POT_2 is the panel's **large** knob — top-centre, physically bigger than the
 * other eight, and Davies1900hLarge in Rack. RELATION sits there: it is the
 * control that decides what every voice mode does, so it gets the middle of
 * the top row and the bigger cap. COLOR moved out to POT_3 in the same swap.
 * Nothing indexes by slot to find the large knob — the widget table in
 * `vcv/AlloyFlux.cpp` names Pot::RELATION directly — so the pair can be
 * swapped back by editing these two lines and that one.
 */
namespace Pot
{
// ---- 9 physical panel knobs (same on hardware and VCV) ----
constexpr PotId ROOT     = PotId::POT_1; ///< top L — root pitch (V/Oct centre)
constexpr PotId RELATION = PotId::POT_2; ///< top C — voice 2 offset, 0–24 st
constexpr PotId COLOR    = PotId::POT_3; ///< top R — FM depth / Hz spread
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
 * The four modulation jacks map onto the HAL in panel order, CV_3..CV_6;
 * CV_1 and CV_2 are V/Oct and Gate. Physically, left to right as the panel
 * reads:
 *
 *   upper row:  V/OCT   GATE   MIDI IN   RELATION  COLOR
 *               J3      J4     J2        J21       J6
 *               CV_1    CV_2   —         CV_3      CV_4
 *               —       CH0    —         CH1       CH2
 *
 *   lower row:  FM IN   SHAPE    MOTION   OUT L     OUT R
 *               J9      J7       J8       J10       J11
 *               CV_7    CV_5     CV_6     —         —
 *               GP27    CH3      CH4      —         —
 *
 * FM IN is at the LEFT end of the lower row; the row is otherwise in slot
 * order. MIDI IN is a MIDI jack, not a CV one, and has no slot at all.
 *
 * The fourth line is the analogue mux channel (U9, read through GP28); FM IN
 * and V/Oct are on direct ADC pins instead, GP27 and GP26. Board designators
 * are NOT in panel order and **there is no J5** — the row-1 position-4 jack is
 * J21. All of it is verified against `hardware/MainPCB/`, and
 * `references/AlloyFlux-hardware-design.md` carries the full table.
 *
 * ⚠ `Inputs.kicad_sch` still names the four modulation nets REL_CV, SHAPE_CV,
 * MOTION_CV and SPACE_CV, from the layout in which SPACE had a jack. By
 * position they now carry RELATION, COLOR, SHAPE and MOTION — so the net
 * called SHAPE_CV is the COLOR jack, and so on down. Nothing is mis-wired and
 * this file needs no change for it: the mux order is positional, so CV_3..CV_6
 * land on CH1..CH4 in panel order whatever the nets are called. Read the panel
 * table, not the net label, until the schematic is renamed.
 *
 * The jacks are silkscreened with the parameter they modulate rather than
 * numbered CV 1..CV 4, so this table *is* the panel. Two consequences worth
 * knowing:
 *
 *   * **SPACE has no CV jack.** It had CV_6 and lost it to MOTION when COLOR
 *     was given a jack of its own — COLOR reaches further into the sound in
 *     every voice mode than stereo width does, so it earns the panel space.
 *     SPACE is still a knob, a CC and a preset field; only the jack is gone.
 *   * **FM IN is pitch FM**, not a second COLOR input. It used to sum into
 *     COLOR because COLOR is the internal FM depth and there was no other
 *     route in; now that COLOR has CV_4, the jack does what it is named for
 *     and modulates the oscillator pitch (`SynthParams::fmIn`). See
 *     `io/IOBridge.h` and `kFmInOctPerVolt` in `SynthEngine.h`.
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
constexpr CVId VOCT = CVId::CV_1; ///< J3, V/OCT, GP26 — readCV() gives volts
constexpr CVId GATE = CVId::CV_2; ///< J4, GATE, mux CH0 — 0 or raw volts
constexpr CVId RELATION = CVId::CV_3; ///< J21, upper row, mux CH1 — ±1.0
constexpr CVId COLOR    = CVId::CV_4; ///< J6,  upper row, mux CH2 — ±1.0
constexpr CVId SHAPE    = CVId::CV_5; ///< J7,  lower row, mux CH3 — ±1.0
constexpr CVId MOTION   = CVId::CV_6; ///< J8,  lower row, mux CH4 — ±1.0
constexpr CVId FM       = CVId::CV_7; ///< J9,  FM IN, GP27 — pitch FM, volts

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
