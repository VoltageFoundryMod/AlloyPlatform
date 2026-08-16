#pragma once

#include <stdint.h>

#include "FeedbackSynthEngine.h" // AUDREY_ECHO_MAX_S

/**
 * Audrey II — externally-controllable parameter state.
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
extern float gEchoTime;     // seconds, 0.05–AUDREY_ECHO_MAX_S
extern float gEchoFeedback; // 0–1.5 — deliberately allowed past unity

// Reverb
extern float gReverbMix;   // 0–1
extern float gReverbDecay; // 0.2–1.0

// Output
extern float gOutputLevel; // 0–1

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