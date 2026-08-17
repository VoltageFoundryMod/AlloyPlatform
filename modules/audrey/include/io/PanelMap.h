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
//                                    +shift: EXCITE
//                                      (POT_16)
//
//     ECHO TIME      ECHO SEND       ECHO FBK        <- mid row: the echo
//      (POT_4)        (POT_5)         (POT_6)
//
//     REV DECAY       REV MIX         FB LPF         <- low row: space + tone
//      (POT_7)        (POT_8)         (POT_9)
//                    +shift: VOL     +shift: FB HPF
//                     (POT_14)         (POT_15)
//
// Read it in columns and the panel teaches itself:
//
//   left    PITCH / ECHO TIME / REV DECAY   — time. Pitch is 1/time, the other
//                                             two are times outright.
//   centre  BODY / ECHO SEND / REV MIX      — how much. The wet amount of the
//                                             row below the resonator's own.
//   right   FB GAIN / ECHO FBK / FB LPF     — feedback. All three are what
//                                             comes back round; FB LPF is
//                                             literally inside the loop.
//
// ECHO SEND and ECHO TIME started the other way round and were swapped for
// this: it puts the two wet-amount controls in the same column, one above the
// other, and the columns fell out of it. BODY is the one knob that does not fit
// its column cleanly — it is a delay length sitting in the "amount" slot — and
// it stays because PITCH has to be top-left and the resonator row has its own
// logic to obey first.
//
// Three shift pairs, all deliberate:
//   FB GAIN / EXCITE  one knob for how much energy is in the loop, whether it
//                     comes from the loop itself or from the exciter jack.
//   FB LPF / FB HPF   the most natural pair in the set — one knob is the
//                     feedback band, shift reaches its other edge.
//   REV MIX / VOL     VOL sits on POT_14, which is *the same slot AlloyFlux
//                     puts VOL on*. SHIFT+centre-bottom means volume on both
//                     firmwares, so the muscle memory carries across.
//
// That second pair is positional, not thematic, and it got weaker when DECAY
// and MIX swapped places: two "how much" controls now share a knob, where
// DECAY/VOL at least separated time from level. The position wins anyway —
// VOL's entire argument is that it lives where AlloyFlux's VOL lives, and
// moving it to POT_13 to chase DECAY would trade the only cross-firmware
// habit on the panel for a tidier-sounding pairing.
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
constexpr PotId ECHOTIME = PotId::POT_4; ///< mid L — echo time 0.05–4 s
constexpr PotId ECHOSEND = PotId::POT_5; ///< mid C — echo send 0–1
constexpr PotId ECHOFB   = PotId::POT_6; ///< mid R — echo feedback 0–1.2

// ---- low row: space + tone ----
constexpr PotId REVDECAY = PotId::POT_7; ///< bottom L — reverb decay 0.2–1.0
constexpr PotId REVMIX   = PotId::POT_8; ///< bottom C — reverb mix 0–1
constexpr PotId FBLPF    = PotId::POT_9; ///< bottom R — feedback LPF 100–18 kHz

// ---- SHIFT-secondaries ----
/// SHIFT + bottom-centre, i.e. SHIFT + REV MIX. **The same slot AlloyFlux puts
/// VOL on**, which is the point: SHIFT+centre-bottom means volume on either
/// firmware, so the muscle memory carries across. It is pinned to the position,
/// not to whichever parameter currently sits there — REV DECAY and REV MIX have
/// already traded places once and VOL did not move.
constexpr PotId VOL = PotId::POT_14;
/// SHIFT + bottom-right, pairing HPF with the LPF knob — one knob is the
/// feedback band, shift reaches its other edge.
constexpr PotId FBHPF = PotId::POT_15;

/// SHIFT + top-right, i.e. SHIFT + FB GAIN — gain on the exciter jack.
///
/// ⚠ This breaks the platform's shift-numbering convention and does so on
/// purpose. Slots POT_10..POT_15 pair with POT_4..POT_9 in row-major sequence,
/// which leaves the top row (POT_1..POT_3) with no secondaries at all — an
/// artefact of AlloyFlux having needed exactly six. FB GAIN is the only
/// ergonomically right home for an exciter level: both answer "how hard is the
/// loop driven". So POT_16 is claimed as POT_3's secondary rather than putting
/// EXCITE on POT_10..POT_13, where it would have shared a knob with the echo or
/// the reverb and meant nothing. If a module ever needs the top row's other two,
/// the convention needs a proper extension rather than two more exceptions.
constexpr PotId EXCITE = PotId::POT_16;

/// Covers slots 0..15, i.e. through POT_16. POT_10..POT_13 are the shift pairs
/// Audrey does not use and stay unassigned; readPot() returns 0.5 for those and
/// nothing reads them.
constexpr uint8_t kCount = 16;
} // namespace Pot

/**
 * Physical pot ↔ SHIFT-secondary pairs, for the ADC driver and for the SHIFT
 * edge handling that detaches both sides of a shared knob. Mirrors
 * kShiftPairs in AlloyFlux's HardwarePicoIO.
 */
namespace Pot
{
constexpr uint8_t kShiftPairCount              = 3;
constexpr PotId   kShiftPairs[kShiftPairCount][2]
    = {{REVMIX, VOL}, {FBLPF, FBHPF}, {FBGAIN, EXCITE}};
} // namespace Pot

/**
 * CV input jacks.
 *
 * The panel numbers its four modulation jacks CV 1..CV 4 and they map onto the
 * HAL in order, CV 1..CV 4 -> CV_3..CV_6, because CV_1 and CV_2 are V/Oct and
 * Gate and no module gets to reassign those. Left to right, as the panel reads:
 *
 *   upper row:  V/OCT    GATE      MIDI IN   CV 1     CV 2
 *               CV_1     CV_2      —         CV_3     CV_4
 *               —        unused    —         FBBODY   FBGAIN
 *               "Pitch"  "Gate"    "MIDI In" "Body"   "FB Gain"
 *
 *   lower row:  EXC IN   CV 3      CV 4      OUT L    OUT R
 *               CV_7     CV_5      CV_6      —        —
 *               EXCITER  ECHOSEND  REVMIX    —        —
 *               "Exc In" "Delay Snd" "Rev. Mix"
 *
 * Bottom row is left to right as the panel reads it, with the exciter at the
 * left end. MIDI IN is a MIDI jack, not a CV one, and has no slot at all.
 *
 * ⚠ Two parameters lost their CV here and it was a choice, not an oversight:
 * **echo feedback** and **reverb decay** are no longer modulated, while **body**
 * and **reverb mix** now are. There are four generic jacks, and they are spent
 * on the resonator's two shaping controls and the two wet/dry amounts rather
 * than on the two coefficients that can run away. Both dropped controls are
 * still reachable by MIDI CC (87 and 92).
 *
 * ⚠ EXC IN's position is a board problem, not a firmware one: the exciter needs
 * the direct GP27 ADC to be read at audio rate, and on the current PCB that pin
 * is J9 in the *middle* of this row. See platform/vcv/PanelLayout.h.
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
constexpr CVId VOCT     = CVId::CV_1; ///< "Pitch"     — summed with PITCH knob
constexpr CVId FBBODY   = CVId::CV_3; ///< "Body",  CV 1, top row
constexpr CVId FBGAIN   = CVId::CV_4; ///< "FB Gain", CV 2, top row
constexpr CVId ECHOSEND = CVId::CV_5; ///< "Delay Snd", CV 3, lower row
constexpr CVId REVMIX   = CVId::CV_6; ///< "Rev. Mix", CV 4, lower row
constexpr CVId EXCITER  = CVId::CV_7; ///< "Exc In"    — external excitation

constexpr uint8_t kCount = 7;
} // namespace Cv

/**
 * Panel buttons — the same two illuminated switches AlloyFlux has (board SW2,
 * SW3).
 *
 * WARP is the doppler warp: held, it halves the echo time, which drags the read
 * head toward the write head and pitches the whole tail up before it settles.
 * This is upstream Audrey II's one panel switch (`kDelaySwitchPin`), and it is
 * the only performance gesture the engine has. See io/IOBridge.h.
 *
 * SHIFT selects the shift-secondaries. On hardware that means "read this knob
 * as its other parameter"; Rack has no key to hold, so it puts them in the
 * context menu and the button is inert there.
 *
 * BUTTON_1 is AlloyFlux's MODE button and the silkscreen there says MODE. Audrey
 * calls the same slot WARP because Audrey's panel says WARP — which is the whole
 * job of this header. A module names slots in its own vocabulary; it does not
 * inherit the other module's.
 */
namespace Btn
{
constexpr ButtonId WARP  = ButtonId::BUTTON_1; ///< SW2, left  — doppler warp
constexpr ButtonId SHIFT = ButtonId::BUTTON_2; ///< SW3, right — secondaries

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
