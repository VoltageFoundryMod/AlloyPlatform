#pragma once

#include <stdint.h>

/**
 * DriftEngine<N_VOICES> — per-voice slow random frequency drift.
 *
 * Simulates the subtle frequency instability of analogue oscillators.
 * Each voice independently wanders around its nominal pitch, creating gentle
 * beating, phase incoherence, and the "alive" quality of vintage hardware.
 *
 * MOTION scaling (0.0 – 1.0):
 *   0.0  — no drift; module is stable and precise
 *   0.2  — subtle warmth, faint beating between the two voices
 *   0.5  — natural ensemble feel, clearly audible on held notes
 *   1.0  — full ±kMaxDriftHz wander, rich string-machine territory
 *
 * USAGE
 * -----
 *   DriftEngine<2> drift;
 *
 *   // in updateControl() @ CONTROL_RATE Hz:
 *   drift.update(sMotion);
 *   float freq1 = baseFreq + drift.offset(0);
 *   float freq2 = baseFreq + drift.offset(1);
 *
 * DESIGN NOTES
 * ------------
 * Each voice has its own LCG seed so drift patterns are always independent.
 * Initial timers are staggered so voices don't synchronise their target changes.
 * The one-pole LP (kDriftSpeed) prevents audible steps when a new Hz target is
 * chosen — the voice glides smoothly toward it over ~0.3 seconds.
 * No float ops in the inner loop when motion=0 (target and offset both stay 0).
 */

template <uint8_t N_VOICES>
class DriftEngine {
  public:
    DriftEngine() {
        for (uint8_t i = 0; i < N_VOICES; i++) {
            // Different seed and staggered initial wake per voice
            _seed[i] = 0x12345678u ^ ((uint32_t)i * 0xDEADBEEFu);
            _freqOffset[i] = 0.0f;
            _freqTarget[i] = 0.0f;
            _timer[i] = (uint8_t)((i * 43u) % 64u) + 1u;
        }
    }

    /**
     * Set the glide speed (one-pole coefficient).
     * Range: 0.001 (very slow, τ ≈ 12 s) … 0.10 (fast, τ ≈ 0.08 s)
     * Default: kDefaultDriftSpeed (τ ≈ 0.31 s)
     */
    void setSpeed(float speed) {
        _driftSpeed = speed;
    }

    /**
     * Advance the drift engine by one control tick.
     * @param motion  MOTION parameter 0.0–1.0 — scales maximum drift amplitude.
     */
    void update(float motion) {
        const float maxHz = motion * kMaxDriftHz;
        for (uint8_t i = 0; i < N_VOICES; i++) {
            // Glide toward target (one-pole LP, time constant set by _driftSpeed)
            _freqOffset[i] += (_freqTarget[i] - _freqOffset[i]) * _driftSpeed;

            // Count down to picking a new random target
            if (_timer[i] == 0) {
                // New target: random in ±maxHz, new wait 16..63 ticks (0.12–0.5 s @ 128 Hz)
                _timer[i] = (uint8_t)(_rand(_seed[i]) % 48u) + 16u;
                _freqTarget[i] = _randf(_seed[i]) * maxHz;
            } else {
                --_timer[i];
            }
        }
    }

    /** Hz offset to add to voice i's nominal frequency. 0 when motion=0. */
    float offset(uint8_t voice) const {
        return (voice < N_VOICES) ? _freqOffset[voice] : 0.0f;
    }

    /** Reset all voices to zero offset (e.g. on note retrigger). */
    void reset() {
        for (uint8_t i = 0; i < N_VOICES; i++) {
            _freqOffset[i] = 0.0f;
            _freqTarget[i] = 0.0f;
        }
    }

  private:
    // Maximum ±Hz each voice can wander at MOTION=1.0
    // At 440Hz: 6Hz ≈ ±24 cents — clearly audible beating at MOTION=0.5+
    static constexpr float kMaxDriftHz = 6.0f;

    // Default one-pole coefficient: τ ≈ 1/(coeff * CONTROL_RATE) ≈ 0.20 s @ 128 Hz
    static constexpr float kDefaultDriftSpeed = 0.04f;

    float _driftSpeed = kDefaultDriftSpeed;

    // Numerical Recipes LCG — cheap, no stdlib dependency
    static uint32_t _rand(uint32_t &s) {
        s = s * 1664525u + 1013904223u;
        return s;
    }

    // Returns uniform float in −1.0 … +1.0
    static float _randf(uint32_t &s) {
        return (float)(int32_t)_rand(s) * (1.0f / 2147483648.0f);
    }

    float _freqOffset[N_VOICES]; // current Hz offset applied to voice
    float _freqTarget[N_VOICES]; // Hz target the voice is gliding toward
    uint8_t _timer[N_VOICES];    // ticks until next target selection
    uint32_t _seed[N_VOICES];    // per-voice LCG state
};
