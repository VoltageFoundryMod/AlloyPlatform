#pragma once

#include "io/HardwareIO.h"

// ---------------------------------------------------------------------------
// Audrey II panel map — this module's vocabulary over the platform's
// positional I/O slots.
//
// Audrey and AlloyFlux run on the SAME hardware: same PCB, same nine pots, same
// two buttons, same jack positions. So the slot numbers here are not chosen,
// they are inherited — POT_1 is the top-left knob and CV_7 is the FM jack
// whichever firmware is flashed. Only the meaning changes. That is exactly what
// the positional HAL (M63d) exists to make possible: a module names slots, it
// never gets to move them.
//
// The shift-secondary slots are inherited too. On hardware a secondary is the
// same ADC channel read while SHIFT is held, so POT_14 *is* "shift + POT_8"
// physically, in both firmwares. Audrey uses two of the six available pairs and
// leaves POT_10..POT_13 unassigned; unassigned slots read as 0.5 and nothing
// reads them.
//
//   Physical layout (platform geometry, Audrey labels). Slot numbers are
//   row-major, matching the panel silkscreen and PanelLayout's table.
//
//        PITCH          BODY          FB GAIN        <- top row: the resonator
//      (POT_1)        (POT_2)         (POT_3)           and its feedback loop
//
//     ECHO SEND      ECHO TIME       ECHO FBK        <- mid row: the echo
//      (POT_4)        (POT_5)         (POT_6)
//
//      REV MIX       REV DECAY        FB LPF         <- low row: space + tone
//      (POT_7)        (POT_8)         (POT_9)
//                    +shift: VOL     +shift: FB HPF
//                     (POT_14)         (POT_15)
//
// Two shift pairs, both deliberate:
//   FB LPF / FB HPF   the most natural pair in the set — one knob is the
//                     feedback band, shift reaches its other edge.
//   REV DECAY / VOL   VOL sits on POT_14, which is *the same slot AlloyFlux
//                     puts VOL on*. SHIFT+centre-bottom means volume on both
//                     firmwares, so the muscle memory carries across.
//
// FB LPF gets the knob and VOL the shift because sweeping the feedback filter
// darkens the ring audibly and is played, while output level is set once.
//
// Renumbering slots is safe as far as flash goes — the preset blob is a struct of
// named fields (audrey_config.h), not a slot-indexed array, and PotTakeover's
// slot-indexed state is runtime-only. What it does affect is kShiftPairs below
// and PanelLayout's coordinate table, both of which index by slot; change them
// together or a knob moves with nothing failing.
// ---------------------------------------------------------------------------

/** Knobs. Nine physical, two SHIFT-secondaries. */
namespace Pot
{
// ---- top row: resonator + feedback loop ----
constexpr PotId PITCH  = PotId::POT_1; ///< top L — pitch, MIDI note 16–72
constexpr PotId FBBODY = PotId::POT_2; ///< top C — feedback delay 1–100 ms
constexpr PotId FBGAIN = PotId::POT_3; ///< top R — feedback gain −30…+12 dB

// ---- mid row: echo ----
constexpr PotId ECHOSEND = PotId::POT_4; ///< mid L — echo send 0–1
constexpr PotId ECHOTIME = PotId::POT_5; ///< mid C — echo time 0.05–4 s
constexpr PotId ECHOFB   = PotId::POT_6; ///< mid R — echo feedback 0–1.2

// ---- low row: space + tone ----
constexpr PotId REVMIX   = PotId::POT_7; ///< bottom L — reverb mix 0–1
constexpr PotId REVDECAY = PotId::POT_8; ///< bottom C — reverb decay 0.2–1.0
constexpr PotId FBLPF    = PotId::POT_9; ///< bottom R — feedback LPF 100–18 kHz

// ---- SHIFT-secondaries ----
/// SHIFT + bottom-centre. **The same slot AlloyFlux puts VOL on**, which is the
/// point: SHIFT+centre-bottom means volume on either firmware, so the muscle
/// memory carries across.
constexpr PotId VOL = PotId::POT_14;
/// SHIFT + bottom-right, pairing HPF with the LPF knob — one knob is the
/// feedback band, shift reaches its other edge.
constexpr PotId FBHPF = PotId::POT_15;

/// Covers slots 0..14, i.e. through POT_15. POT_10..POT_13 are the shift pairs
/// Audrey does not use and stay unassigned; readPot() returns 0.5 for those and
/// nothing reads them.
constexpr uint8_t kCount = 15;
} // namespace Pot

/**
 * Physical pot ↔ SHIFT-secondary pairs, for the ADC driver and for the SHIFT
 * edge handling that detaches both sides of a shared knob. Mirrors
 * kShiftPairs in AlloyFlux's HardwarePicoIO.
 */
namespace Pot
{
constexpr uint8_t kShiftPairCount              = 2;
constexpr PotId   kShiftPairs[kShiftPairCount][2]
    = {{REVDECAY, VOL}, {FBLPF, FBHPF}};
} // namespace Pot

/**
 * CV input jacks.
 *
 * The panel numbers its four modulation jacks CV 1..CV 4 and they map onto the
 * HAL in order, CV 1..CV 4 -> CV_3..CV_6, because CV_1 and CV_2 are V/Oct and
 * Gate and no module gets to reassign those. Physically (board designators from
 * hardware/MainPCB, left to right):
 *
 *   upper row:  V/OCT   GATE    MIDI IN   CV 1     CV 2
 *               J3      J4      J2        J5       J6
 *               CV_1    CV_2    —         CV_3     CV_4
 *               —       unused  —         FBGAIN   ECHOSEND
 *
 *   lower row:  CV 3    FM IN   CV 4      OUT L    OUT R
 *               J7      J9      J8        J10      J11
 *               CV_5    CV_7    CV_6      —        —
 *               ECHOFB  EXCITER REVDECAY  —        —
 *
 * Note FM IN sits *between* CV 3 and CV 4, so the lower row is not in slot
 * order. MIDI IN is a MIDI jack, not a CV one, and has no slot at all.
 *
 * EXCITER is CV_7, AlloyFlux's FM jack. The engine sums it into both resonator
 * channels ahead of the string, so anything patched there drives the string
 * instead of leaving it to self-excite from its own −90 dBFS noise floor.
 *
 * ⚠ Its bandwidth is a property of the platform, not the engine. VCV reads the
 * port once per sample, so it is a genuine audio-rate exciter. The firmware
 * reads it from the 128 Hz control tick, so on hardware it is a control voltage
 * that pokes and swells the string but cannot excite it with audio. See
 * gExciterIn in params.h.
 *
 * GATE (CV_2) is deliberately unassigned — reserved for the VCA/envelope
 * option rather than quietly given another meaning now.
 */
namespace Cv
{
constexpr CVId VOCT     = CVId::CV_1; ///< J3, V/OCT — summed with PITCH knob
constexpr CVId FBGAIN   = CVId::CV_3; ///< J5, CV 1   — feedback gain
constexpr CVId ECHOSEND = CVId::CV_4; ///< J6, CV 2   — echo send
constexpr CVId ECHOFB   = CVId::CV_5; ///< J7, CV 3   — echo feedback
constexpr CVId REVDECAY = CVId::CV_6; ///< J8, CV 4   — reverb decay
constexpr CVId EXCITER  = CVId::CV_7; ///< J9, FM IN  — external excitation

constexpr uint8_t kCount = 7;
} // namespace Cv

/**
 * Panel buttons — the same two illuminated switches AlloyFlux has (board SW2,
 * SW3). No gesture is assigned to either yet: SHIFT's job is to select the
 * shift-secondaries, and Rack puts those in the context menu while the firmware
 * has no ADC to read a knob with. The slots are claimed anyway so the panel,
 * the widget and the HAL agree now rather than after a renumber.
 */
namespace Btn
{
constexpr ButtonId MODE  = ButtonId::BUTTON_1; ///< SW2, left
constexpr ButtonId SHIFT = ButtonId::BUTTON_2; ///< SW3, right

constexpr uint8_t kCount = 2;
} // namespace Btn

/**
 * Panel LEDs — seven RGB, at AlloyFlux's positions.
 *
 *        LEVEL_L ·  ·  · LEVEL_R      output level, per channel
 *      LOOP_L  ·  ·  ·  · LOOP_R      loop danger: green → amber → red
 *         ECHO  ·   SPACE             echo pulse / reverb, SHIFT while held
 *            · CENTRE ·               string alive / exciter
 *
 * The loop-danger pair is the reason this module has LEDs at all: Audrey can
 * run away, and a slowly building drone sounds like a drone until it isn't.
 * The colour language and the signals behind it are in io/AudreyLeds.h.
 *
 * Two orderings are in play and neither is left-to-right: the panel numbers
 * LED1..LED7 (D12..D16, D21, D22) counter-clockwise from the upper left, which
 * is also the daisy-chain order, while LightId pairs them left/right — which is
 * why the roles above come in pairs. PanelLayout's kLedMm spells out both.
 */
namespace Led
{
constexpr LightId LEVEL_L = LightId::LIGHT_1; ///< LED1 / D12 — upper left
constexpr LightId LEVEL_R = LightId::LIGHT_2; ///< LED7 / D22 — upper right
constexpr LightId LOOP_L  = LightId::LIGHT_3; ///< LED2 / D13 — left
constexpr LightId LOOP_R  = LightId::LIGHT_4; ///< LED6 / D21 — right
constexpr LightId ECHO    = LightId::LIGHT_5; ///< LED3 / D14 — lower left
constexpr LightId SPACE   = LightId::LIGHT_6; ///< LED5 / D16 — lower right
constexpr LightId CENTRE  = LightId::LIGHT_7; ///< LED4 / D15 — bottom centre

constexpr uint8_t kCount = 7;
} // namespace Led
