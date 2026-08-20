#pragma once

#include <stdint.h>

#include "FeedbackSynthEngine.h" // COIL_ECHO_MAX_S

/**
 * Alloy Coil — externally-controllable parameter state.
 *
 * The engine keeps its own smoothed internals; these are the *goal* values,
 * written by MIDI CC, SysEx, the serial console and preset recall, and pushed
 * into the engine once per control tick by updateControl(). One global per
 * row in params.json, and nothing else writes them.
 *
 * Ranges and defaults come from upstream's registerParams(); see params.json.
 */

// Resonator
extern float gStringPitch; // MIDI note number, 16–72

// Feedback loop
extern float gFeedbackGain;  // dBFS, −60…+12
extern float gFeedbackDelay; // seconds, 0.001–0.1 ("body")
extern float gFeedbackLPF;   // Hz, 100–18000
extern float gFeedbackHPF;   // Hz, 10–4000

// Echo
extern float gEchoSend;     // 0–1
extern float gEchoTime;     // seconds, 0.05–COIL_ECHO_MAX_S
extern float gEchoFeedback; // 0–1.5 — deliberately allowed past unity

// Reverb
extern float gReverbMix;   // 0–1
extern float gReverbDecay; // 0.2–1.0

// Output
extern float gOutputLevel; // 0–1

/// Gain on the exciter jack before it enters the loop, 0–2. Not volatile: this
/// is a control-rate goal value like the rest, written by the knob/CC/preset
/// and read by updateControl(). Only gExciterIn below is touched per frame.
extern float gExciterLevel;

/**
 * External excitation, ±1.0. Summed into both resonator channels before the
 * string, so patching anything here drives it instead of leaving it to
 * self-excite from its own −90 dBFS noise floor.
 *
 * volatile because the audio core reads it every frame while the control core
 * writes it — a single aligned float, so atomic on the M33.
 *
 * ⚠ Its bandwidth is a property of the platform, not of the engine. VCV writes
 * it once per sample from the EXCITER port, which is genuine audio-rate
 * excitation. The firmware writes it from `readCV()` at the 128 Hz control
 * tick, so on hardware it is a control voltage that pokes and swells the
 * string rather than an audio input. Giving it audio bandwidth means a
 * dedicated ADC path on the audio core; the seam is here and ready for it.
 */
extern volatile float gExciterIn;

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
 * The gXxx values above reach the engine through a one-pole per parameter, with
 * upstream's per-parameter glide times — up to 1 s for the feedback body. That
 * is right when a knob or a CC moves and wrong when a whole patch changes at
 * once, so preset recall, factory reset and MIDI panic call this first.
 * Defined in main.cpp; see ControlSmoother.h.
 */
void coilControlSnap();