#pragma once

#include <math.h>
#include <stdint.h>

/**
 * FilterEngine — Stereo State Variable Filter (Cytomic trapezoidal SVF)
 *
 * Uses the topology-preserving, trapezoidal-integration form described by
 * Andrew Simper (Cytomic) — no frequency warping, stable at all cutoffs and
 * resonances including near-self-oscillation.
 *
 * Coefficients (_a1, _a2, _a3, _k) are computed at control rate (128 Hz)
 * in setParams() — the only place tanf() is called.
 * process() is called at audio rate (32768 Hz) — integer I/O, float
 * integrator state, no trig functions.
 *
 * FILTER MODES
 * ------------
 *   OFF   — bypass (zero CPU, integrators preserved for glitch-free enable)
 *   LP    — low-pass  (rolls off highs, warm, good for pads)
 *   HP    — high-pass (removes lows, good for clearing mud)
 *   BP    — band-pass (peaks around cutoff, good for wah/formant)
 *   NOTCH — band-reject (scoops a notch, good for phaser-like effects)
 *
 * PARAMETERS
 * ----------
 *   cutoff     : Hz — frequency where the filter takes effect (20–16000 Hz)
 *   resonance  : 0.0 (none) … 1.0 (near self-oscillation) — be musical here
 *
 * SIGNAL LEVELS
 * -------------
 * Expects ±32512 int32 input (Mozzi pipeline scale).
 * Internally normalised to ±1.0f for the float SVF, then back to ±32512.
 *
 * Milestone 26a.
 */

enum class FilterMode : uint8_t {
    OFF = 0,
    LP = 1,
    HP = 2,
    BP = 3,
    NOTCH = 4,
};

class FilterEngine {
  public:
    FilterEngine()
        : _a1(1.0f), _a2(0.0f), _a3(0.0f), _k(2.0f),
          _ic1L(0.0f), _ic2L(0.0f), _ic1R(0.0f), _ic2R(0.0f),
          _mode(FilterMode::OFF) {}

    /**
     * setParams() — call at control rate (128 Hz) when cutoff or resonance changes.
     * Never call from updateAudio() — contains tanf().
     *
     * cutoff_hz : 20.0 – 16000.0 Hz
     * resonance : 0.0 (flat) – 1.0 (near self-oscillation)
     */
    void setParams(float cutoff_hz, float resonance,
                   FilterMode mode, float sampleRate = 32768.0f) {
        _mode = mode;
        if (mode == FilterMode::OFF)
            return;

        if (cutoff_hz < 20.0f)
            cutoff_hz = 20.0f;
        if (cutoff_hz > 16000.0f)
            cutoff_hz = 16000.0f;
        if (resonance < 0.0f)
            resonance = 0.0f;
        if (resonance > 0.999f)
            resonance = 0.999f;

        // Cytomic TVA-SVF coefficients
        const float g = tanf(3.14159265f * cutoff_hz / sampleRate);
        _k = 2.0f * (1.0f - resonance); // k=2 → no resonance; k→0 → self-osc
        _a1 = 1.0f / (1.0f + g * (g + _k));
        _a2 = g * _a1;
        _a3 = g * _a2;
    }

    /**
     * process() — call at audio rate from updateAudio() ISR.
     * No trig. Two channels processed independently with shared coefficients.
     */
    inline void process(int32_t inL, int32_t inR,
                        int32_t *outL, int32_t *outR) {
        if (_mode == FilterMode::OFF) {
            *outL = inL;
            *outR = inR;
            return;
        }
        *outL = _processChannel(inL, &_ic1L, &_ic2L);
        *outR = _processChannel(inR, &_ic1R, &_ic2R);
    }

    FilterMode mode() const { return _mode; }

  private:
    inline int32_t _processChannel(int32_t in, float *ic1, float *ic2) {
        // Normalise ±32512 → ±1.0
        const float v0 = in * (1.0f / 32512.0f);

        // Trapezoidal integrators (Cytomic eq. 4)
        const float v3 = v0 - *ic2;
        const float v1 = _a1 * (*ic1) + _a2 * v3;
        const float v2 = *ic2 + _a2 * (*ic1) + _a3 * v3;

        // Update integrator history (double-delay update)
        *ic1 = 2.0f * v1 - *ic1;
        *ic2 = 2.0f * v2 - *ic2;

        // Select output topology
        float out;
        switch (_mode) {
        case FilterMode::LP:
            out = v2;
            break;
        case FilterMode::HP:
            out = v0 - _k * v1 - v2;
            break;
        case FilterMode::BP:
            out = v1;
            break;
        case FilterMode::NOTCH:
            out = v0 - _k * v1;
            break;
        default:
            out = v2;
            break;
        }

        // Soft-clip to ±1.0 before converting back (prevents int32 overflow
        // at high resonance where BP gain can exceed unity)
        if (out > 1.0f)
            out = 1.0f;
        if (out < -1.0f)
            out = -1.0f;

        return (int32_t)(out * 32512.0f);
    }

    // Shared coefficients — same for both channels, updated at control rate
    float _a1, _a2, _a3, _k;

    // Per-channel integrator state — maintained between samples
    float _ic1L, _ic2L;
    float _ic1R, _ic2R;

    FilterMode _mode;
};
