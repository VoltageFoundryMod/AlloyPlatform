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
 *   the warm, slightly "hairy" Moog character; self-oscillates cleanly from
 *   resonance ≈ 0.89 (where k reaches 4) producing a pitched sine at the
 *   cutoff frequency.
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
 * Input: ±32512 int32 (shared signal-path scale) → normalised to ±1.0 internally.
 * Output: ±32512 int32.  At high resonance the self-oscillation output is a
 * sine at ~0.79 FS, level and pitch steady across the whole cutoff range, and
 * clear of the clamp so it stays a sine rather than squaring off.
 *
 * Level-matched to SVFFilter at resonance 0, so switching filter type does not
 * move the patch.  Above that the ladder still thins its low end as resonance
 * rises — 12.1 dB down at res 1.0 — which is what a ladder does; _makeup gives
 * back a quarter of that loss (in dB) so the knob needs less of a volume ride.
 * See setParams() for the two terms and why the depth stops where it does.
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
    OTALadder() : _G(0.5f), _k(0.0f), _makeup(2.0f), _mode(FilterMode::LP4)
    { _reset(); }

    void setParams(float      cutoff_hz,
                   float      resonance,
                   FilterMode mode,
                   float      sampleRate) override
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
        // Moog ladder resonance scale.  The self-oscillation threshold of a
        // 4-pole ladder is k = 4, so a 0-4 map puts res = 1.0 exactly *on* it,
        // where the oscillation is marginal (measured 0.001-0.026 FS) and its
        // amplitude drifts with cutoff.  4.5 carries res = 1.0 just past the
        // threshold: a stable 0.25 FS sine at every cutoff, with the taper
        // below it unchanged.  Do not raise this much further - at 6.0 the
        // filter self-oscillates from res = 0.75 and the top of the knob dies.
        _k = resonance * 4.5f;

        // Makeup gain, computed here so the audio path stays one multiply.
        //
        // Two terms:
        //   2.0            undoes the 0.5 drive scale in _chan(), which is tanh
        //                  headroom and was never meant to change level.
        //                  Without it the ladder ran 6.02 dB under SVFFilter.
        //   (1+k)^0.25     gives back a quarter (in dB) of the ladder's own
        //                  passband loss.  Negative feedback pulls DC gain to
        //                  1/(1+k), so resonance thins the low end by up to
        //                  15.8 dB at res 1.0 — real ladder behaviour, but a
        //                  steep level ride on a knob the SVF holds flat.
        //
        // The exponent is 0.25 because that is the deepest makeup that costs no
        // headroom anywhere.  Measured, 110 Hz saw at 0.5 FS into fc 2000:
        //
        //   expo   loss at res .5/1.0    self-osc FS    clamp hits at res .9/1.0
        //   0.00     -10.2  -15.8           0.51           0.0%   0.0%
        //   0.25      -7.7  -12.1           0.79           0.0%   0.0%
        //   0.35      -6.7  -10.7           0.93           0.3%   4.7%
        //   0.50      -5.1  (self-osc)      1.00 (rail)    9.4%  35.7%
        //
        // Raising it further does not buy level, it buys clipping: the makeup
        // lifts the resonant peak too, and the peak is already the loudest part
        // of the signal.  At 0.5 the self-oscillation squares off against the
        // clamp instead of staying a sine.  Do not raise this without re-running
        // the headroom rows above.
        //
        // Nested sqrtf, not powf: two VSQRT instructions on the M33 FPU against
        // ~90 us for powf, which at 128 Hz control rate would be 1.2% of a core.
        _makeup = 2.0f * sqrtf(sqrtf(1.0f + _k));
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
    // Feedback predictor: the value the 4th stage would output with no input.
    // A TPT one-pole is y = G*x + (1-G)*z, so chaining four of them gives
    //   y4 = (1-G) * (G^3*z[0] + G^2*z[1] + G*z[2] + z[3])
    // The (1-G) is not optional.  Dropping it overestimates the feedback by
    // 1/(1-G), which rises with cutoff (negligible at 100 Hz, 1.6x at 8 kHz),
    // so resonance tracks cutoff instead of the knob: at res = 0.8 the peak
    // ran -0.5 dB at 100 Hz but +14 dB at 4 kHz, already past self-oscillation,
    // and sweeping the cutoff up dropped the passband 6.7 dB instead of
    // holding it flat.  Guarded by ladder_response.cpp.
    inline int32_t _chan(float in, float *z)
    {
        const float G2 = _G * _G;
        const float G3 = G2 * _G;
        const float sigma
            = (1.0f - _G) * (G3 * z[0] + G2 * z[1] + _G * z[2] + z[3]);
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
            x             = y; // stage input is tanh()-ed at the top of the
                               // next iteration - do not saturate twice
        }

        // Output is the 4th-stage output y (not state z[3]).
        // z[3] = y + v = 2v + z_old which is TWICE the correct level.
        //
        // _makeup is applied here rather than to the input, so the tanh stages
        // still see the drive level they were tuned at and the character is
        // unchanged.  See setParams() for what the two terms in it are.
        //
        // Clamped after makeup: self-oscillation reaches ~0.60 FS at res 1.0
        // and a full-scale input lands on the rail at res 0, so the clamp is a
        // guard rather than something the filter runs into.
        float out = y * _makeup;
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

    float      _G;      // per-stage integrator gain (from cutoff)
    float      _k;      // resonance feedback gain (0–4.5)
    float      _makeup; // output makeup: 2 * (1+k)^0.25, see setParams()
    float      _zL[4];  // left channel integrator states
    float      _zR[4];  // right channel integrator states
    FilterMode _mode;
};
