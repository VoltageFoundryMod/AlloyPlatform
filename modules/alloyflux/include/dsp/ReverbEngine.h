#pragma once

#include <stdint.h>

/**
 * ReverbEngine — Abstract reverb interface for AlloyFlux M26b.
 *
 * Processed inline in the audio path, one sample behind the dry signal so the
 * dry/wet comb stays stationary.  On hardware that path owns Core 1; in VCV it
 * is the host's audio thread.  Either way the engine sees a single caller.
 *
 * ALGORITHM SWAPPING
 * ------------------
 * ReverbEngine is a pure-virtual base class.  Concrete implementations:
 *   DattorroReverb  — lush plate, floating tails (M26b)
 *   (future)        — spring, hall, room, convolution IR
 *
 * To swap algorithm: point SynthEngine::reverb at another instance; the render
 * path and all command routing are algorithm-agnostic.
 */

// ---------------------------------------------------------------------------
// Wet-mix state — defined in main.cpp, written at control rate, read per frame
// ---------------------------------------------------------------------------
extern volatile float gRevMix;     // 0.0 = dry … 1.0 = full wet
extern volatile bool  gRevEnabled; // false = reverb skipped entirely

// ---------------------------------------------------------------------------
// Abstract algorithm interface
// ---------------------------------------------------------------------------

class ReverbEngine
{
  public:
    virtual ~ReverbEngine() {}

    /**
     * setParams() — call at control rate when parameters change.  May contain
     * expf/sqrtf; it is off the audio path.
     *
     * Implementations must confine themselves to scalar coefficient writes.
     * Control runs on the other core from audio on hardware, so anything that
     * resized a buffer here could be observed half-applied by a render in
     * flight.
     *
     * size    : 0.0 (small room) … 1.0 (long plate)
     * damping : 0.0 (bright, no damping) … 1.0 (dark, heavy HF loss)
     */
    virtual void setParams(float size, float damping) = 0;

    /**
     * process() — audio hot path, once per frame.
     * Must be float-only; no heavy allocation; no blocking.
     *
     * inL/inR   : normalised float ±1.0 input
     * outL/outR : normalised float ±1.0 wet output (dry NOT added here)
     */
    virtual void process(float inL, float inR, float *outL, float *outR) = 0;

    /** reset() — clear all delay lines and state (mode switch, mute). */
    virtual void reset() = 0;

    /** setModulation() — M40: LFO speed and depth, called at control rate. */
    virtual void setModulation(float /*speed*/, float /*depth*/) {}

    /** freeze() — M41: hold current reverb tail indefinitely. */
    virtual void freeze(bool /*frozen*/) {}
};
