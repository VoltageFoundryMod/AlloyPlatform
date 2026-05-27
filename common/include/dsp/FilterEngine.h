#pragma once

#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// FilterMode — shared enum used by both FilterEngine subclasses and commands.
// ---------------------------------------------------------------------------

enum class FilterMode : uint8_t {
    OFF = 0,
    LP = 1,
    HP = 2,
    BP = 3,
    NOTCH = 4,
    // LP4 (ladder) — intentionally shares LP topology selector for OTALadder,
    // but is a separate enum so the display / command layer can distinguish.
    LP4 = 5,
};

// ---------------------------------------------------------------------------
// FilterType — selects which concrete algorithm is active.
// ---------------------------------------------------------------------------

enum class FilterType : uint8_t {
    SVF = 0,    // Cytomic trapezoidal state-variable (default, clean)
    LADDER = 1, // OTA 4-pole Moog-style ladder (saturating, self-oscillating)
};

// ---------------------------------------------------------------------------
// FilterEngine — abstract stereo filter interface (Milestone 26a, M5x refactor)
//
// Concrete subclasses: SVFFilter, OTALadder.
// Runtime selection: change gFilterInst pointer in main.cpp — no recompile.
//
// Input/output: ±32512 int32 (Mozzi pipeline scale).
// setParams() : call at control rate (128 Hz); may contain tanf()/tanhf().
// process()   : call at audio rate (32768 Hz ISR); must be trig-free.
// ---------------------------------------------------------------------------

class FilterEngine {
  public:
    virtual ~FilterEngine() {}

    /**
     * setParams() — control rate.  May call tanf(), tanhf() etc.
     * cutoff_hz : 20–16000 Hz
     * resonance : 0.0 (flat) – 1.0 (near/at self-oscillation)
     * mode      : LP / HP / BP / NOTCH / OFF / LP4
     */
    virtual void setParams(float cutoff_hz, float resonance, FilterMode mode,
                           float sampleRate = 32768.0f) = 0;

    /**
     * process() — audio rate ISR.  No trig; integer I/O.
     */
    virtual void process(int32_t inL, int32_t inR,
                         int32_t *outL, int32_t *outR) = 0;

    /** Current mode — readable by commands for `status` output. */
    virtual FilterMode mode() const = 0;

    /** Reset integrator state (called on type switch to silence glitch). */
    virtual void reset() {}
};
