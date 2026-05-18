#pragma once

#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// EnvelopeType — selects which concrete algorithm is active at runtime.
// ---------------------------------------------------------------------------

enum class EnvelopeType : uint8_t {
    AR = 0,   // Single-knob AR with pluck mode (default — original CurveEngine)
    ADSR = 1, // Full ADSR with separate A/D/S/R params + optional loop mode
};

// ---------------------------------------------------------------------------
// EnvelopeEngine — abstract envelope + VCA interface
//
// Concrete subclasses: AREnvelope (original CurveEngine), ADSREnvelope.
// Runtime selection: change gEnvInst pointer in main.cpp.
//
// setGate()  : call every control tick (128 Hz); may call expf().
// next()     : call every audio sample; no expf, branch-minimal.
// level()    : read current value without advancing.
// reset()    : silence immediately (mode switch, mute).
// ---------------------------------------------------------------------------

class EnvelopeEngine {
  public:
    virtual ~EnvelopeEngine() {}

    /** Detect gate edge and advance state machine.  Call at 128 Hz. */
    virtual void setGate(bool high) = 0;

    /** Advance by one sample.  Returns 0.0–1.0.  Call at audio rate. */
    virtual float next() = 0;

    /** Current level without advancing. */
    virtual float level() const = 0;

    /** Reset to silent idle (e.g. on type switch). */
    virtual void reset() = 0;
};

// ---------------------------------------------------------------------------
// AREnvelope — original single-knob AR envelope (= former CurveEngine).
//
// CURVE knob (0.0–1.0) morphs attack and release simultaneously.
// Below curve=0.2 → pluck mode: auto-releases at peak regardless of gate.
// setCurve() calls expf() — safe at 128 Hz; next() is multiply-only.
// ---------------------------------------------------------------------------

template <uint32_t SAMPLE_RATE>
class AREnvelope : public EnvelopeEngine {
  public:
    void setCurve(float curve, float timeScale = 1.0f) {
        _curve = curve;
        const float c2 = curve * curve;
        const float ts = (timeScale < 0.01f) ? 0.01f : timeScale;
        const float attTime = (0.001f + c2 * 0.799f) * ts;
        const float relTime = (0.080f + c2 * 0.920f) * ts;
        _attCoeff = 1.0f - expf(-1.0f / (attTime * (float)SAMPLE_RATE));
        _relDecay = expf(-1.0f / (relTime * (float)SAMPLE_RATE));
    }

    void setGate(bool high) override {
        if (high && !_gateHigh)
            _state = ATTACK;
        else if (!high && _gateHigh)
            if (_state == ATTACK || _state == SUSTAIN)
                _state = RELEASE;
        _gateHigh = high;
    }

    float next() override {
        switch (_state) {
        case ATTACK:
            _env += _attCoeff * (1.0f - _env);
            if (_env >= 0.99f) {
                _state = (_curve > 0.2f && _gateHigh) ? SUSTAIN : RELEASE;
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
        default:
            break;
        }
        return _env;
    }

    float level() const override { return _env; }

    void reset() override {
        _state = IDLE;
        _env = 0.0f;
    }

  private:
    enum State : uint8_t { IDLE,
                           ATTACK,
                           SUSTAIN,
                           RELEASE } _state = IDLE;
    float _env = 0.0f;
    float _attCoeff = 0.001f;
    float _relDecay = 0.999f;
    float _curve = 0.5f;
    bool _gateHigh = false;
};

// Backwards-compatibility alias — existing code that uses CurveEngine<RATE> still compiles.
template <uint32_t SAMPLE_RATE>
using CurveEngine = AREnvelope<SAMPLE_RATE>;

// ---------------------------------------------------------------------------
// ADSREnvelope — full Attack / Decay / Sustain / Release envelope.
//
// All four times are independent.  Optional loop mode makes the envelope
// cycle continuously as an LFO (restarts attack automatically after release).
//
// setADSR()   : set times + sustain level + loop flag; calls expf() × 3.
// setGate()   : edge detection.  In loop mode gate is ignored.
// next()      : audio-rate; no expf; one-pole multiply-only per tick.
// ---------------------------------------------------------------------------

template <uint32_t SAMPLE_RATE>
class ADSREnvelope : public EnvelopeEngine {
  public:
    /**
     * Configure ADSR parameters.  Call at control rate when values change.
     * @param attackTime   seconds  (0.001–10.0)
     * @param decayTime    seconds  (0.001–10.0)
     * @param sustainLevel 0.0–1.0
     * @param releaseTime  seconds  (0.001–10.0)
     * @param loop         true = re-trigger automatically after release reaches 0
     */
    void setADSR(float attackTime, float decayTime, float sustainLevel,
                 float releaseTime, bool loop = false) {
        _sustain = (sustainLevel < 0.0f) ? 0.0f : (sustainLevel > 1.0f ? 1.0f : sustainLevel);
        _loop = loop;
        const float sr = (float)SAMPLE_RATE;
        _attCoeff = _coeff(attackTime, sr);
        _decCoeff = _coeff(decayTime, sr);
        _relCoeff = _coeff(releaseTime, sr);
    }

    void setGate(bool high) override {
        if (_loop)
            return; // loop mode ignores external gate
        if (high && !_gateHigh)
            _state = ATTACK;
        else if (!high && _gateHigh)
            if (_state == ATTACK || _state == DECAY || _state == SUSTAIN)
                _state = RELEASE;
        _gateHigh = high;
    }

    float next() override {
        switch (_state) {
        case ATTACK:
            _env += _attCoeff * (1.0f - _env);
            if (_env >= 0.99f) {
                _env = 1.0f;
                _state = DECAY;
            }
            break;
        case DECAY:
            _env += _decCoeff * (_sustain - _env);
            if (fabsf(_env - _sustain) < 0.001f) {
                _env = _sustain;
                _state = (_sustain < 0.001f) ? RELEASE : SUSTAIN;
            }
            break;
        case SUSTAIN:
            _env = _sustain;
            break;
        case RELEASE:
            _env *= _relCoeff;
            if (_env < 0.001f) {
                _env = 0.0f;
                _state = IDLE;
                if (_loop)
                    _state = ATTACK; // loop: auto-restart
            }
            break;
        default:
            break;
        }
        return _env;
    }

    float level() const override { return _env; }

    void reset() override {
        _state = IDLE;
        _env = 0.0f;
    }

    /** Start a one-shot attack (useful for trig commands and SHIFT button). */
    void trigger() { _state = ATTACK; }

  private:
    // Continuous one-pole decay coefficient toward a target.
    // attCoeff = 1 - exp(-1/(time*sr)):  env += coeff*(target - env) per sample.
    // relCoeff = exp(-1/(time*sr)):      env *= coeff per sample.
    static float _coeff(float time_s, float sr) {
        if (time_s < 0.001f)
            time_s = 0.001f;
        if (time_s > 10.0f)
            time_s = 10.0f;
        return expf(-1.0f / (time_s * sr));
    }

    enum State : uint8_t { IDLE,
                           ATTACK,
                           DECAY,
                           SUSTAIN,
                           RELEASE } _state = IDLE;
    float _env = 0.0f;
    float _sustain = 0.8f;
    float _attCoeff = 0.0f;   // toward 1.0: env += coeff*(1-env)
    float _decCoeff = 0.0f;   // toward sustain
    float _relCoeff = 0.999f; // decay multiplier (per-sample)
    bool _loop = false;
    bool _gateHigh = false;
};
