#pragma once

#include "dsp/FilterEngine.h"
#include <math.h>
#include <stdint.h>

/**
 * OTALadder — Stereo 4-pole Moog-style OTA ladder filter.
 *
 * Topology
 * --------
 * Four cascaded one-pole stages with tanh nonlinearity and resonance
 * feedback.  Implements the Huovilainen/Välimäki zero-delay-feedback (ZDF)
 * variant, which gives accurate self-oscillation without coefficient
 * pre-warping errors.  Adapted from the Valley Audio (Dale Johnson) OTA
 * filter (GPLv3) — ported to scalar float, ARM-friendly, no SSE.
 *
 * Character vs SVFFilter
 * ----------------------
 *   SVFFilter (Cytomic SVF) — clean, linear until near self-oscillation;
 *   perfect for transparent tone shaping and notch effects.
 *
 *   OTALadder — 4-pole LP only; saturates as resonance increases giving
 *   the warm, slightly "hairy" Moog character; self-oscillates cleanly at
 *   resonance ≥ 0.95 producing a pitched sine at the cutoff frequency.
 *
 * Mode support
 * ------------
 * Only LP4 (4-pole low-pass) is implemented.  The `mode` argument to
 * setParams() is accepted but only LP4 / LP / OFF are honoured; other
 * modes fall back to LP4.
 *
 * Tanh approximation
 * ------------------
 * Uses the piecewise approximation from NonLinear.hpp (Valley source):
 *   |x| ≤ 0.75  → x  (linear)
 *   0.75 < |x| ≤ 1.25 → softclip blend
 *   |x| > 1.25  → ±1.0 (hard clip)
 * No libm tanhf() in the audio hot path — ~4-6 cycles per call on M33.
 *
 * Parameters
 * ----------
 *   cutoff_hz  : 20–8000 Hz  (ladder is more aliased above Nyquist/4)
 *   resonance  : 0.0 (flat) … 1.0 (self-oscillation at 1.0)
 *
 * Signal levels
 * -------------
 * Input: ±32512 int32 (Mozzi pipeline scale) → normalised to ±1.0 internally.
 * Output: ±32512 int32.  At high resonance the self-oscillation output
 * is a sine at ~0.7 amplitude — no output clipping added so the
 * oscillation sustains cleanly.
 *
 * CPU cost (estimate, M33 @ 150 MHz FPU)
 * ----------------------------------------
 * 4 stages × 2 channels × ~15 FPU ops ≈ 120 FPU cycles ≈ 0.8 µs.
 * Budget is 30µs per audio sample — safe.
 *
 * Milestone 5x (runtime-selectable filter type).
 */

class OTALadder : public FilterEngine
{
  public:
    OTALadder() : _G(0.5f), _k(0.0f), _mode(FilterMode::LP4) { _reset(); }

    void setParams(float      cutoff_hz,
                   float      resonance,
                   FilterMode mode,
                   float      sampleRate = 32768.0f) override
    {
        // OTA ladder is LP4 only — anything non-OFF is treated as LP4
        _mode = (mode == FilterMode::OFF) ? FilterMode::OFF : FilterMode::LP4;
        if(_mode == FilterMode::OFF)
            return;

        if(cutoff_hz < 20.0f)
            cutoff_hz = 20.0f;
        if(cutoff_hz > 8000.0f)
            cutoff_hz = 8000.0f; // ladder aliases above sr/4
        if(resonance < 0.0f)
            resonance = 0.0f;
        if(resonance > 1.0f)
            resonance = 1.0f;

        // Pre-warped integrator gain g (same ZDF formula as SVF)
        const float g = tanf(3.14159265f * cutoff_hz / sampleRate);
        _G            = g / (1.0f + g); // one-pole stage gain (trapezoidal)
        _k = resonance
             * 4.0f; // Moog ladder resonance scale: 0=flat, 4=self-osc
    }

    inline void
    process(int32_t inL, int32_t inR, int32_t *outL, int32_t *outR) override
    {
        if(_mode == FilterMode::OFF)
        {
            *outL = inL;
            *outR = inR;
            return;
        }
        *outL = _chan(inL * (1.0f / 32512.0f), _zL);
        *outR = _chan(inR * (1.0f / 32512.0f), _zR);
    }

    FilterMode mode() const override { return _mode; }

    void reset() override { _reset(); }

  private:
    // Fast piecewise tanh approximation (Valley NonLinear.hpp, BSD-style)
    static inline float _tanh(float x)
    {
        if(x < -1.25f)
            return -1.0f;
        else if(x < -0.75f)
            return 1.0f - (x * (-2.5f - x) - 0.5625f) - 1.0f;
        else if(x > 1.25f)
            return 1.0f;
        else if(x > 0.75f)
            return x * (2.5f - x) - 0.5625f;
        return x;
    }

    // Process one channel with independent state array z[4].
    // Uses the Huovilainen predictor-corrector zero-delay feedback formulation
    // (simplified to one corrector pass — a good accuracy/cost trade-off):
    //
    //   u  = tanh(in*0.5 - k*z[3])  / (1 + k*gamma)
    //   y1 = G*(tanh(u) - z[0]) + z[0];   z[0] = 2*y1 - z[0]
    //   …repeat for y2, y3, y4
    //
    // sigma = G^3*z[0] + G^2*z[1] + G*z[2] + z[3]  (feedback predictor)
    inline int32_t _chan(float in, float *z)
    {
        // Feedback predictor
        const float G2    = _G * _G;
        const float G3    = G2 * _G;
        const float sigma = G3 * z[0] + G2 * z[1] + _G * z[2] + z[3];
        const float gamma
            = G3 * _G; // G^4 — overall 4-pole gain in feedback path

        // Input with feedback subtracted (normalise 0.5 input headroom)
        const float u = (_tanh(in * 0.5f - _k * sigma)) / (1.0f + _k * gamma);

        // Four cascaded one-pole stages
        float x = u;
        float y
            = 0.0f; // last stage output; declared outside loop so it survives
        for(int s = 0; s < 4; s++)
        {
            const float v = _G * (_tanh(x) - z[s]);
            y             = v + z[s]; // stage output (LP tap)
            z[s]          = y + v;    // state update: z[s] := 2v + z[s]_old
            x             = _tanh(y);
        }

        // Output is the 4th-stage output y (not state z[3]).
        // z[3] = y + v = 2v + z_old which is TWICE the correct level.
        // Clamp to rail (self-oscillation can reach ~0.9, well within int32).
        float out = y;
        if(out > 1.0f)
            out = 1.0f;
        if(out < -1.0f)
            out = -1.0f;
        return (int32_t)(out * 32512.0f);
    }

    void _reset()
    {
        for(int i = 0; i < 4; i++)
        {
            _zL[i] = 0.0f;
            _zR[i] = 0.0f;
        }
    }

    float      _G;     // per-stage integrator gain (from cutoff)
    float      _k;     // resonance feedback gain (0–4)
    float      _zL[4]; // left channel integrator states
    float      _zR[4]; // right channel integrator states
    FilterMode _mode;
};
