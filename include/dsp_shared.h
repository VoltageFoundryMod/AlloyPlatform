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
 * CHORUS (Milestone 12)
 * ----------------------
 * ChorusEngine runs directly in Core 0 updateAudio() ISR using a phasor LFO
 * (no trig calls in the hot path).  gChorusDepth (volatile float) is written
 * by updateControl() at 128 Hz and read atomically by the ISR.
 * No inter-core buffers or counters are needed.
 */

// ---------------------------------------------------------------------------
// Control-parameter snapshot
// (all values smoothed before write — safe to copy as a plain struct)
// ---------------------------------------------------------------------------

struct DspParams {
    float freq1;   // voice 1 Hz, post-detune, post-drift
    float freq2;   // voice 2 Hz, post-detune, post-drift
    float shape;   // 0.0 = sine … 0.5 = saw … 1.0 = hollow pulse
    float fatness; // 0.0 = no sub osc … 1.0 = sub at 50% of main level
    float motion;  // 0.0 = static … 1.0 = full drift + chorus depth
    float curve;   // 0.0 = pluck … 1.0 = swell (for chorus tail awareness)
    float volume;  // 0.0 – 1.0 master level
};

// Defined in src/main.cpp
extern DspParams gDsp;
extern mutex_t gDspMutex;

// ---------------------------------------------------------------------------
// Chorus depth — written by Core 0 updateControl() at 128 Hz, read by ISR.
// Single 32-bit float, aligned: atomic read on Cortex-M33, no mutex needed.
// ---------------------------------------------------------------------------

extern volatile float gChorusDepth;

// ---------------------------------------------------------------------------
// Read helper — any core, always safe
// ---------------------------------------------------------------------------

inline DspParams dsp_params_read() {
    mutex_enter_blocking(&gDspMutex);
    const DspParams p = gDsp;
    mutex_exit(&gDspMutex);
    return p;
}
