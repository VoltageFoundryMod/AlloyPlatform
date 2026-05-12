#pragma once

#include <stdint.h>

/**
 * ReverbEngine — Abstract reverb interface for AlloyFlux M26b.
 *
 * Runs on Core 1 (loop1()) to offload CPU from the Core 0 audio ISR.
 * Core 0 ISR deposits dry stereo signal; Core 1 processes it and writes
 * the wet return; Core 0 ISR reads the return and mixes it in.
 *
 * INTER-CORE PROTOCOL (artifact-free by design)
 * ----------------------------------------------
 * gRevIn_L/R  : written by Core 0 ISR,  read by Core 1 loop
 * gRevOut_L/R : written by Core 1 loop, read by Core 0 ISR
 *
 * All four are volatile int32_t — 32-bit aligned atomic on Cortex-M33.
 * Core 0 never waits on Core 1; reading gRevOut gives the last computed
 * reverb sample (at most 1 audio frame = 30µs old — inaudible for a reverb
 * tail of 0.5–3 seconds).  No mutex, no spin-lock in the ISR path.
 *
 * This avoids the artifact problem that forced chorus back onto Core 0:
 * chorus is in-line (a blocked read would corrupt the sample); reverb is
 * additive (a 1-sample-old read is acoustically transparent).
 *
 * ALGORITHM SWAPPING
 * ------------------
 * ReverbEngine is a pure-virtual base class.  Concrete implementations:
 *   DattorroReverb  — lush plate, floating tails (M26b)
 *   (future)        — spring, hall, room, convolution IR
 *
 * To swap algorithm: change `gReverb` instantiation in main.cpp; the Core 1
 * loop and all command routing are algorithm-agnostic.
 *
 * Milestone 26b — Core 1 infrastructure present now; algorithm added in M26b.
 */

// ---------------------------------------------------------------------------
// Inter-core shared state — defined in main.cpp
// ---------------------------------------------------------------------------
extern volatile int32_t gRevIn_L; // Core 0 writes, Core 1 reads
extern volatile int32_t gRevIn_R;
extern volatile int32_t gRevOut_L; // Core 1 writes, Core 0 reads
extern volatile int32_t gRevOut_R;

extern volatile float gRevMix;    // 0.0=dry, 1.0=full wet — written by Core 0 updateControl()
extern volatile bool gRevEnabled; // false = Core 1 passes through zeros

// ---------------------------------------------------------------------------
// Abstract algorithm interface
// ---------------------------------------------------------------------------

class ReverbEngine {
  public:
    virtual ~ReverbEngine() {}

    /**
     * setParams() — call at control rate from Core 1 when parameters change.
     * May contain expf/sqrtf — safe; Core 1 is not time-critical.
     *
     * size    : 0.0 (small room) … 1.0 (long plate)
     * damping : 0.0 (bright, no damping) … 1.0 (dark, heavy HF loss)
     */
    virtual void setParams(float size, float damping) = 0;

    /**
     * process() — Core 1 hot path.  Called once per audio sample deposited
     * by Core 0.  Must be float-only; no heavy allocation; no blocking.
     *
     * inL/inR   : normalised float ±1.0 input
     * outL/outR : normalised float ±1.0 wet output (dry NOT added here)
     */
    virtual void process(float inL, float inR,
                         float *outL, float *outR) = 0;

    /** reset() — clear all delay lines and state (mode switch, mute). */
    virtual void reset() = 0;
};

// ---------------------------------------------------------------------------
// Null / pass-through implementation — M26 stub until DattorroReverb lands
// ---------------------------------------------------------------------------

class NullReverb final : public ReverbEngine {
  public:
    void setParams(float /*size*/, float /*damping*/) override {}
    void process(float /*inL*/, float /*inR*/,
                 float *outL, float *outR) override {
        *outL = 0.0f;
        *outR = 0.0f;
    }
    void reset() override {}
};
