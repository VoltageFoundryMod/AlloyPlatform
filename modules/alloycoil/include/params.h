#pragma once

#include <stdint.h>

#include "FeedbackSynthEngine.h"    // COIL_ECHO_MAX_S
#include "param_struct.generated.h" // CoilParamGoals — one row per params.json

/**
 * Alloy Coil — externally-controllable parameter state.
 *
 * The engine keeps its own smoothed internals; these are the *goal* values,
 * written by MIDI CC, SysEx, the serial console and preset recall, and pushed
 * into the engine once per control tick by updateControl().
 *
 * Ranges and defaults come from upstream's registerParams() and live in
 * params.json, which generates CoilParamGoals — so a range is written down
 * once and the generated header shows every default at a glance. They were
 * repeated here as trailing comments until one of them (feedback gain, still
 * claiming −60 dB after params.json narrowed it to −30) proved the obvious
 * point about hand-kept copies.
 *
 * ---- Why a struct ------------------------------------------------------
 *
 * These were thirteen free-standing globals, which is the right shape for the
 * board: one panel, one engine, one set of parameters, and the cheapest
 * possible access from an interrupt. It is the wrong shape for VCV, where a
 * rack may hold any number of Alloy Coils. Sharing one set did not merely mean
 * the modules tracked each other — fillCoilParams() runs on the control
 * divider and ControlSmoother::Step() on the smoother divider, which are
 * different periods, so the second module's knobs reached the first module's
 * engine on whichever blocks the two phases happened to interleave.
 *
 * So the storage is a struct and every consumer takes one by reference. The
 * firmware passes gCoilParams below and is otherwise unchanged; each VCV module
 * owns a CoilParams member and passes that.
 */
struct CoilParams : CoilParamGoals
{
    /**
     * External excitation, ±1.0. Summed into both resonator channels before the
     * string, so patching anything here drives it instead of leaving it to
     * self-excite from its own −90 dBFS noise floor.
     *
     * Hand-written rather than generated because it is not a parameter: it
     * carries a *sample*, not a goal value, so it has no CC, no range and no
     * place in a preset. It belongs to the instance for the same reason the
     * goals do, which is why it sits in the same struct.
     *
     * volatile because the audio core reads it every frame while the control
     * core writes it — a single aligned float, so atomic on the M33.
     *
     * ⚠ Its bandwidth is a property of the platform, not of the engine. VCV
     * writes it once per sample from the EXCITER port, which is genuine
     * audio-rate excitation. The firmware writes it from `readCV()` at the
     * 128 Hz control tick, so on hardware it is a control voltage that pokes
     * and swells the string rather than an audio input. Giving it audio
     * bandwidth means a dedicated ADC path on the audio core; the seam is here
     * and ready for it.
     */
    volatile float exciterIn = 0.0f;
};

/**
 * The firmware's single instance, defined by param_globals.generated.h.
 *
 * Also what the generated manifest's `target` column points into, so the CC,
 * SysEx and console transports all reach it without knowing it is a member of
 * anything. That is exactly right on the board and irrelevant in VCV, where
 * those transports are Rack's and write Rack params instead — nothing in the
 * plugin dereferences a manifest target, so nothing in the plugin touches this.
 */
extern CoilParams gCoilParams;

/**
 * Doppler warp (CoilParams::warp) — non-zero halves the echo time. Upstream
 * Audrey II's one panel switch (`kDelaySwitchPin`), and the only performance
 * gesture the engine has.
 *
 * Deliberately *not* folded into echoTime. That stays the time the knob or CC
 * asked for; the halving is applied where the goal is read, in
 * ControlSmoother::Step(). Two reasons: the echo's read head is what produces
 * the pitch sweep, so the scaling belongs on the path into the engine rather
 * than on the stored value, and packCoilConfig() saves echoTime — a preset
 * captured with warp on would otherwise come back half as long.
 *
 * Latching everywhere, including the panel: Btn::WARP toggles it on the press
 * edge each control tick (io/IOBridge.h), while CC 20 and SysEx set it and
 * leave it. The panel used to be the exception, momentary while every other
 * route latched; it is not any more. Still not in CoilConfig — warp is a
 * performance state, not part of a patch. The VCV module persists it with the rack anyway,
 * because a Rack patch is a session and not a preset: what you left the module
 * set to is what should come back.
 */

/**
 * Audio-path cost switches, defined in main.cpp and read on the audio core.
 *
 * Diagnostics, not voicing. M63i put two new things inside the audio block —
 * the parameter smoother and the output limiter — and these make it possible to
 * attribute their cost on a live board with `cpu` running, instead of
 * reflashing once per hypothesis.
 *
 * gSmoothFrames: run ControlSmoother::Step() every n frames. 0 stops it
 *   entirely, which freezes the engine at whatever parameters it last had —
 *   fine for a measurement, useless for playing.
 * gLimiterEnabled: false bypasses OutputStage, leaving the engine to overshoot
 *   ±1.0 into AudioDriver::toWire()'s hard clamp.
 */
extern volatile uint32_t gSmoothFrames;
extern volatile bool     gLimiterEnabled;

/**
 * Debounced panel button state — bit 0 WARP (SW2), bit 1 SHIFT (SW3).
 *
 * Exists for `status`. The proto board has the switches but no LEDs, so there
 * is nothing on the panel to tell a miswired button from a dead one, and WARP's
 * only other symptom is an echo tail that pitches — which needs the echo to be
 * audible first. Defined in main.cpp, where the ButtonEngines live.
 */
uint8_t coilButtonsDown();

/**
 * Engine setters called on the most recent smoother step, 0–12.
 *
 * Should read 0 on a patch nobody is touching and rise only while something
 * moves. A steady non-zero reading means a deadband in ControlSmoother::Init()
 * is too tight for its parameter, and the smoother is burning transcendentals
 * to re-send values the engine already has. Defined in main.cpp.
 */
uint8_t coilSmootherPushes();

// CPU profiling — defined in main.cpp, only present when CPU_PROFILE is set.
#ifdef CPU_PROFILE
extern volatile bool     gPerformancePrintEnabled;
extern volatile uint32_t gAudioElapsedUs;
extern volatile uint32_t gAudioOverruns;
extern volatile uint32_t gAudioBudgetUs;
#endif

/// False when AudioDriver::begin() failed — no bit clock at all, and the
/// control loop is running off millis() instead of the audio clock.
/// Defined in main.cpp.
bool audioRunning();

/**
 * Drop the parameter glide and take the goal values as they stand on the next
 * audio block.
 *
 * A CoilParams' goal values reach the engine through a one-pole per parameter, with
 * upstream's per-parameter glide times — up to 1 s for the feedback body. That
 * is right when a knob or a CC moves and wrong when a whole patch changes at
 * once, so preset recall, factory reset and MIDI panic call this first.
 * Defined in main.cpp; see ControlSmoother.h.
 */
void coilControlSnap();