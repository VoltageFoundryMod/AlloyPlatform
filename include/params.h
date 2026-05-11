#pragma once

#include "ChorusEngine.h" // ChorusMode enum
#include "VoiceMode.h"    // VoiceMode enum

/**
 * Shared synthesis parameters — defined in main.cpp.
 *
 * Naming convention:
 *   gXxx  goal values: written by serial console / knobs / MIDI in updateControl()
 *   sXxx  smoothed values: read by updateAudio(), one-pole LPF applied in updateControl()
 */

// Voice pitch & detune
extern float gBaseFreq; // Hz, voice 1 root pitch (20–8000)
extern float gDetune;   // Hz, symmetric fine spread (v1 = base−d/2, v2 = base+d/2)

// Voice mode and RELATION
extern VoiceMode gVoiceMode; // synthesis personality (default: PAIR)
extern float gRelation;      // semitones above ROOT for voice 2: 0=unison, 7=fifth, 12=octave, 24=max (PAIR mode)

// Timbre
extern float gShape;   // 0.0 = sine  0.25 = tri  0.50 = saw  0.75 = pulse  1.0 = hollow
extern float gFatness; // 0.0 = no sub osc  …  1.0 = sub at 50% of main level

// Animation
extern float gMotion;     // 0.0 = static  …  1.0 = full drift + chorus depth
extern float gDriftSpeed; // one-pole glide coeff: 0.001 (slow) … 0.10 (fast), default 0.025

// Chorus
extern ChorusMode gChorusMode; // OFF / I / II / I_II  (default: I_II)

// Space
extern float gSpace; // stereo width: 0.0 = mono, 1.0 = identity, 2.0 = hyper-wide (default: 1.0)

// Envelope / VCA (Milestone 15)
extern float gCurve;               // 0.0 = pluck … 1.0 = swell
extern float gCurveTime;           // envelope time scale: 0.25=4×faster  1.0=default  4.0=4×slower
extern volatile bool gGateHigh;    // true while gate is asserted (attack phase)
extern volatile bool gGatePatched; // false = drone (bypass VCA); true = AR envelope active

// Level
extern float gVolume; // 0.0 – 1.0 master output

// MIDI configuration
extern uint8_t gMidiChannel; // 0 = omni (all channels), 1–16 = specific channel

// CPU profiling — defined in main.cpp, only present when CPU_PROFILE is set.
// gAudioElapsedUs : µs spent inside the last updateAudio() call
// gAudioOverruns  : calls that exceeded the 30µs audio budget
#ifdef CPU_PROFILE
extern volatile bool gPerformancePrintEnabled; // set by cmd_performance_print, read by updateAudio()
extern volatile uint32_t gAudioElapsedUs;
extern volatile uint32_t gAudioOverruns;
#endif
