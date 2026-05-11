#pragma once

/**
 * Shared synthesis parameters — defined in main.cpp.
 *
 * Naming convention:
 *   gXxx  goal values: written by serial console / knobs / MIDI in updateControl()
 *   sXxx  smoothed values: read by updateAudio(), one-pole LPF applied in updateControl()
 */

// Voice pitch & detune
extern float gBaseFreq; // Hz, voice 1 root pitch (20–8000)
extern float gDetune;   // Hz, symmetric spread (v1 = base-d/2, v2 = base+d/2)

// Timbre
extern float gShape;   // 0.0 = sine  0.25 = tri  0.50 = saw  0.75 = pulse  1.0 = hollow
extern float gFatness; // 0.0 = no sub osc  …  1.0 = sub at 50% of main level

// Level
extern float gVolume; // 0.0 – 1.0 master output

// CPU profiling — defined in main.cpp, only present when CPU_PROFILE is set.
// gAudioElapsedUs : µs spent inside the last updateAudio() call
// gAudioOverruns  : calls that exceeded the 30µs audio budget
#ifdef CPU_PROFILE
extern volatile bool gPerformancePrintEnabled; // set by cmd_performance_print, read by updateAudio()
extern volatile uint32_t gAudioElapsedUs;
extern volatile uint32_t gAudioOverruns;
#endif
