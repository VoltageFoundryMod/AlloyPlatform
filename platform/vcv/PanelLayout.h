#pragma once

#include "io/HardwareIO.h"
#include <rack.hpp>

// ---------------------------------------------------------------------------
// PanelLayout — where things physically are on an Alloy platform panel.
//
// Every module on this platform shares one PCB, so they share one panel
// geometry: nine pots, two buttons, seven LEDs, eight jacks in, two out. Only
// the labels differ. These coordinates are therefore platform data, not module
// data, and they live here so the two widgets cannot drift apart — which is
// exactly what happened before: AlloyFlux was still on a 71.12 mm layout while
// Alloy Coil had moved to 70.76 mm, and the only way to notice was to look at both.
//
// Source of truth is the `components` layer of `panel-src/AlloyPlatform.svg`.
// Regenerate these tables with:
//
//     make panel-coords
//
// Cross-checked against hardware/MainPCB/MainPCB.kicad_pcb: every footprint
// below sits at exactly (pcb_x − 190.370, pcb_y − 39.234) mm, for all 9 pots,
// 7 LEDs, 2 switches and 10 jacks. The SVG guides and the board agree, so the
// designators quoted here (POTn / Dn / SWn / Jn) are the board's, and a board
// revision can be re-checked by re-deriving that offset from the jack rows.
//
// ⚠ The board's *designators* are not in panel order — POT1/POT3/POT2 across
// the top, POT8/POT7/POT9 across the bottom. The slot numbering here is
// positional (row-major), which is what the firmware and the panel silkscreen
// use. Only the ADC-mux driver ever needs the designators, and it does not
// exist yet (POT_ADC_PRESENT == 0).
//
// ⚠ Do NOT transcribe from Rack's module-helper stub. That stub reads raw cx/cy
// and ignores the `transform="translate(120.35222,-1592.0584)"` Inkscape puts on
// the layer, and the viewBox here is 1 uu = 0.01 mm rather than 1 uu = 1 mm. The
// result was every widget 1.20 mm left and 15.92 mm low — plausible-looking
// numbers, visibly wrong panel. tools/panel_coords.py accumulates the ancestor
// transforms and the viewBox scale, which is why it is a script and not a habit.
//
// The pot and LED tables are indexed by the platform's **positional** slot ids,
// so `kPotMm[(int)Pot::ROOT]` and `kPotMm[(int)Pot::PITCH]` reach the same
// knob — the layout and the HAL agree by construction rather than by comment.
// ---------------------------------------------------------------------------

namespace PanelLayout
{

/// Panel size, in mm. 14 HP.
static constexpr float kWidthMm  = 70.76f;
static constexpr float kHeightMm = 128.5f;

/**
 * The nine physical knobs, row-major: POT_1..POT_3 across the top, POT_4..POT_6
 * the middle, POT_7..POT_9 the bottom.
 *
 * Only nine entries. Slots POT_10 and up are SHIFT-secondaries — on hardware
 * they are the same physical knob read while SHIFT is held, so they have no
 * position of their own, and in Rack they are context-menu sliders.
 */
static constexpr int   kPotCount            = 9;
static constexpr float kPotMm[kPotCount][2] = {
    {13.786f, 21.268f}, // POT_1  top    left    — board POT1
    {35.386f, 25.768f}, // POT_2  top    centre  — board POT3, sits lower
    {56.986f, 21.268f}, // POT_3  top    right   — board POT2
    {13.786f, 40.368f}, // POT_4  middle left    — board POT4
    {35.386f, 44.068f}, // POT_5  middle centre  — board POT5
    {56.986f, 40.768f}, // POT_6  middle right   — board POT6
    {19.636f, 62.365f}, // POT_7  bottom left    — board POT8
    {35.430f, 62.361f}, // POT_8  bottom centre  — board POT7
    {51.124f, 62.361f}, // POT_9  bottom right   — board POT9
};

/// MODE and SHIFT, indexed by ButtonId.
static constexpr int   kButtonCount               = 2;
static constexpr float kButtonMm[kButtonCount][2] = {
    {22.133f, 77.476f}, // BUTTON_1  MODE   — board SW2
    {48.580f, 77.476f}, // BUTTON_2  SHIFT  — board SW3
};

/**
 * Seven RGB LEDs, indexed by LightId.
 *
 * Two numbering schemes meet here and neither is left-to-right, so read the
 * designators, not the row order:
 *
 *   * The **panel/board** numbers LED1..LED7 (D12, D13, D14, D15, D16, D21,
 *     D22) as one counter-clockwise ring: LED1 upper-left, down the left side
 *     to LED4 at bottom-centre, back up the right side to LED7 upper-right.
 *     This is also the order they are daisy-chained in.
 *   * **LightId** pairs them left/right instead — LIGHT_1/LIGHT_2 are the outer
 *     pair, LIGHT_3/LIGHT_4 the next in, and LIGHT_7 the lone centre one —
 *     because the roles that drive them are symmetric (VOICE_L/VOICE_R,
 *     MOD_L/MOD_R). That is why this table reads LED1, LED7, LED2, LED6, LED3,
 *     LED5, LED4 rather than 1..7.
 *
 * HardwarePicoIO::writeLight()'s kChainPos table converts LightId back to chain
 * position; it must stay in step with the second column below.
 */
static constexpr int   kLedCount            = 7;
static constexpr float kLedMm[kLedCount][2] = {
    {13.786f, 52.766f}, // LIGHT_1  LED1 / D12  upper left
    {56.980f, 52.716f}, // LIGHT_2  LED7 / D22  upper right
    {9.330f, 66.816f},  // LIGHT_3  LED2 / D13  left
    {61.830f, 66.816f}, // LIGHT_4  LED6 / D21  right
    {13.580f, 77.616f}, // LIGHT_5  LED3 / D14  lower left
    {57.130f, 77.534f}, // LIGHT_6  LED5 / D16  lower right
    {35.430f, 79.716f}, // LIGHT_7  LED4 / D15  bottom centre
};

// --- Jacks ----------------------------------------------------------------
//
// Ten jacks in two rows of five, left to right on the panel:
//
//   upper (y 93.714):  V/OCT   GATE    MIDI IN   CV 1    CV 2
//                      J3      J4      J2        J21     J6
//   lower (y 107.732): FM IN   CV 3    CV 4      OUT L   OUT R
//                      J9      J7      J8        J10     J11
//
// CV 1..CV 4 above are *positions*, not necessarily what is printed on the
// panel — this table is shared by every module on the PCB and what a jack
// modulates is the module's business. Alloy Coil does number them CV 1..CV 4;
// AlloyFlux silkscreens each with the knob it drives (RELATION, COLOR / SHAPE,
// MOTION). Either way they map onto the HAL as CV 1..CV 4 -> CV_3..CV_6 in
// order — CV_1 and CV_2 are taken by V/Oct and Gate, which are not free for a
// module to reassign.
//
// Nothing here assumes a module uses all four: AlloyFlux's SPACE has no jack,
// so `kCv4Mm` carries MOTION there and its lower row reads one position left
// of Alloy Coil's. Each module's widget table decides; see `io/PanelMap.h`.
//
// FM IN is at the LEFT end of the lower row. Both panel arts say so (AlloyFlux
// "FM In", Alloy Coil "Exc In", both at x 9.3), the guide layer agrees, and so
// does the board: J9 sits at 9.343 and J7 at 22.117.
//
// That last clause used to be the opposite, and this comment used to carry a
// long warning about it — an earlier revision had J7 at the left end with J9
// one position right, which mattered because **J9 is the only jack on a direct
// ADC pin (GP27, deliberately off the analogue mux so it can be sampled at
// audio rate)**. The two are not interchangeable: FM IN is ±8 V with a 100 pF
// cap, the generic CV jacks are ±5 V through the mux, so correcting it meant
// moving a net *and* its conditioning stage. It has been corrected. Re-derive
// it from `MainPCB.kicad_pcb` rather than trusting this comment if the board
// moves again — `references/AlloyFlux-hardware-design.md` carries the full
// jack table with designators and mux channels.
//
// ⚠ The designators are NOT in panel order, and there is no J5: the row-1
// position-4 jack is **J21**. These two lines said "J5" for a while and sent
// readers looking for a footprint that does not exist.
static constexpr float kVOctMm[2] = {9.443f, 93.714f};   // J3  V/Oct  -> CV_1
static constexpr float kGateMm[2] = {22.217f, 93.714f};  // J4  Gate   -> CV_2
static constexpr float kMidiMm[2] = {35.390f, 93.714f};  // J2  TRS MIDI IN
static constexpr float kCv1Mm[2]  = {48.663f, 93.714f};  // J21 CV 1   -> CV_3
static constexpr float kCv2Mm[2]  = {61.437f, 93.714f};  // J6  CV 2   -> CV_4
static constexpr float kFmInMm[2] = {9.343f, 107.732f};  // J9  FM IN  -> CV_7
static constexpr float kCv3Mm[2]  = {22.116f, 107.732f}; // J7  CV 3  -> CV_5
static constexpr float kCv4Mm[2]  = {35.340f, 107.732f}; // J8  CV 4  -> CV_6
static constexpr float kOutLMm[2] = {48.563f, 107.732f}; // J10 Out L
static constexpr float kOutRMm[2] = {61.337f, 107.732f}; // J11 Out R

/// mm pair -> Rack pixel Vec. Named `at` so call sites read as positions.
inline rack::math::Vec at(const float mm[2])
{ return rack::mm2px(rack::math::Vec(mm[0], mm[1])); }

/// Position of a pot by its positional slot. A SHIFT-secondary has no position
/// of its own, so it is clamped to POT_1 rather than read off the end of the
/// table — a widget stacked on the top-left knob is a visible mistake, which is
/// the point.
inline rack::math::Vec pot(PotId id)
{
    const int i = (int)id;
    return at(kPotMm[i < kPotCount ? i : 0]);
}

inline rack::math::Vec button(ButtonId id)
{ return at(kButtonMm[(int)id < kButtonCount ? (int)id : 0]); }

inline rack::math::Vec led(LightId id)
{ return at(kLedMm[(int)id < kLedCount ? (int)id : 0]); }

} // namespace PanelLayout
