#pragma once

#include <math.h>
#include <stdint.h>

/**
 * CurveEngine<AUDIO_RATE> — single-knob AR envelope generator and digital VCA.
 *
 * CURVE knob (0.0–1.0) morphs both attack and release simultaneously:
 *   0.0  — pluck:   ~1 ms attack, ~80 ms release; decays regardless of gate length
 *   0.5  — natural: ~50 ms attack, ~300 ms release; sustains while gate held
 *   1.0  — swell:   ~800 ms attack, ~1 s release; slow pad-like bloom
 *
 * GATE PATCHED behaviour (controlled by gGatePatched flag, written by Core 0):
 *   false — bypass: envelope fixed at 1.0 (drone; module sounds without a gate patch)
 *   true  — AR envelope active, triggered by rising/falling edges on gGateHigh
 *
 * USAGE
 * -----
 *   CurveEngine<MOZZI_AUDIO_RATE> curveEng;
 *
 *   // in updateControl() @ 128 Hz:
 *   curveEng.setCurve(sCurve);       // recomputes A/R coefficients (may call expf)
 *   curveEng.setGate(gGateHigh);     // detects edges, advances state machine
 *
 *   // in updateAudio() @ AUDIO_RATE Hz:
 *   float env = curveEng.next();     // returns 0.0..1.0 envelope amplitude
 *
 * PLUCK MODE (curve < 0.2)
 * -------------------------
 * Below curve=0.2 the attack peak automatically triggers release — the envelope
 * decays regardless of whether the gate remains high.  This mimics the character
 * of a plucked string or percussive hit.
 *
 * Above curve=0.2, holding the gate sustains at full amplitude between the attack
 * peak and the release phase (classic ASR / gated-sustain behaviour).
 *
 * DESIGN NOTES
 * ------------
 * - setCurve() calls expf() — safe at control rate (128 Hz), never in the audio ISR.
 * - next() is pure one-pole multiply/add — no exp, no division, fast on Cortex-M33.
 * - All members are ≤32-bit and naturally aligned; reads/writes are atomic on M33,
 *   so no mutex is needed for Core-0-only shared access between two priority levels
 *   (updateControl and updateAudio ISR).
 * - Retrigger: a new gate-high edge during RELEASE or SUSTAIN restarts ATTACK cleanly.
 */
template <uint32_t SAMPLE_RATE>
class CurveEngine {
  public:
    /**
     * Pre-compute attack and release one-pole coefficients from CURVE position.
     * @param curve      0.0 = pluck … 1.0 = swell (shape)
     * @param timeScale  overall speed multiplier; 1.0 = default, 0.25 = 4× faster, 4.0 = 4× slower
     * Uses expf() — call only from updateControl(), never from updateAudio().
     */
    void setCurve(float curve, float timeScale = 1.0f) {
        _curve = curve;
        const float c2 = curve * curve;
        // Base attack: 1 ms … 800 ms; base release: 80 ms … 1000 ms
        // Both scaled uniformly by timeScale so CURVE shape is preserved.
        const float ts = (timeScale < 0.01f) ? 0.01f : timeScale; // clamp > 0
        const float attTime = (0.001f + c2 * 0.799f) * ts;
        const float relTime = (0.080f + c2 * 0.920f) * ts;
        _attCoeff = 1.0f - expf(-1.0f / (attTime * (float)SAMPLE_RATE));
        _relDecay = expf(-1.0f / (relTime * (float)SAMPLE_RATE));
    }

    /**
     * Signal a gate level change.  Detects rising/falling edges and advances
     * the envelope state machine accordingly.
     * Call once per updateControl() tick (128 Hz).
     */
    void setGate(bool high) {
        if (high && !_gateHigh) {
            // Rising edge: (re)trigger attack from any state
            _state = ATTACK;
        } else if (!high && _gateHigh) {
            // Falling edge: begin release if currently in ATTACK or SUSTAIN
            if (_state == ATTACK || _state == SUSTAIN) {
                _state = RELEASE;
            }
        }
        _gateHigh = high;
    }

    /**
     * Advance envelope by one audio sample.
     * Call from updateAudio() at AUDIO_RATE — no expf, branch-minimal.
     * Returns 0.0..1.0.
     */
    float next() {
        switch (_state) {
        case ATTACK:
            _env += _attCoeff * (1.0f - _env);
            if (_env >= 0.99f) {
                // Sustain only if curve is above the pluck threshold AND gate held
                if (_curve > 0.2f && _gateHigh) {
                    _state = SUSTAIN;
                } else {
                    _state = RELEASE; // pluck mode: auto-release at peak
                }
            }
            break;
        case SUSTAIN:
            _env = 1.0f;
            break;
        case RELEASE:
            _env *= _relDecay;
            if (_env < 0.001f) {
                _env = 0.0f;
                _state = IDLE;
            }
            break;
        case IDLE:
        default:
            break; // _env stays at 0
        }
        return _env;
    }

    /** Current envelope level 0.0..1.0 — reads without advancing state. */
    float level() const { return _env; }

    /** Reset to silent (e.g. on initialisation or mode change). */
    void reset() {
        _state = IDLE;
        _env = 0.0f;
    }

  private:
    enum State : uint8_t { IDLE,
                           ATTACK,
                           SUSTAIN,
                           RELEASE } _state = IDLE;

    float _env = 0.0f;        // current envelope amplitude
    float _attCoeff = 0.001f; // one-pole attack coefficient (toward 1.0)
    float _relDecay = 0.999f; // per-sample release multiplier (toward 0.0)
    float _curve = 0.5f;      // stored for pluck threshold check in next()
    bool _gateHigh = false;   // last gate state seen by setGate() (edge detection)
};
