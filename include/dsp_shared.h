#pragma once

#include <pico/mutex.h>
#include <stdint.h>

/**
 * Inter-core DSP shared state — Core 0 writes, Core 1 reads.
 *
 * PROTOCOL
 * --------
 * Core 0  updateControl()  @ 128 Hz:
 *   Fills gDsp under gDspMutex after all parameter smoothing completes.
 *
 * Core 1  loop1()  — continuous:
 *   Calls dsp_params_read() to get a consistent snapshot, then runs DSP
 *   engines (chorus, drift, etc.) using those values.
 *
 * Core 0  updateAudio()  ISR @ 32768 Hz:
 *   MUST NOT acquire gDspMutex — ISRs must never block.
 *   Reads the Core-0-private smoothed statics (sWaveform, sVolume) directly;
 *   these are safe without a mutex because the ISR and updateControl() are
 *   both on Core 0 (no concurrent execution, only preemption between them,
 *   and 32-bit aligned float reads/writes are atomic on Cortex-M33).
 *
 * CHORUS I/O (Milestone 12)
 * -------------------------
 * updateAudio() writes gChorusIn_{L,R} after mixing, and reads gChorusOut_{L,R}
 * as the final output.  loop1() reads In, runs the BBD-inspired chorus, writes
 * Out.  One-sample latency is inaudible at 32768 Hz.
 * Volatile assignment is sufficient — 32-bit aligned stores are atomic on
 * Cortex-M33; an occasional stale sample is inaudible.
 */

// ---------------------------------------------------------------------------
// Control-parameter snapshot
// (all values smoothed before write — safe to copy as a plain struct)
// ---------------------------------------------------------------------------

struct DspParams {
    float freq1;   // voice 1 Hz, post-detune
    float freq2;   // voice 2 Hz, post-detune
    float shape;   // 0.0 = sine … 0.5 = saw … 1.0 = hollow pulse
    float fatness; // 0.0 = no sub osc … 1.0 = sub at 50% of main level
    float volume;  // 0.0 – 1.0 master level
};

// Defined in src/main.cpp
extern DspParams gDsp;
extern mutex_t gDspMutex;

// ---------------------------------------------------------------------------
// Chorus I/O  (stubs until Milestone 12)
// ---------------------------------------------------------------------------

extern volatile int32_t gChorusIn_L;
extern volatile int32_t gChorusIn_R;
extern volatile int32_t gChorusOut_L;
extern volatile int32_t gChorusOut_R;

// ---------------------------------------------------------------------------
// Read helper — any core, always safe
// ---------------------------------------------------------------------------

inline DspParams dsp_params_read() {
    mutex_enter_blocking(&gDspMutex);
    const DspParams p = gDsp;
    mutex_exit(&gDspMutex);
    return p;
}
