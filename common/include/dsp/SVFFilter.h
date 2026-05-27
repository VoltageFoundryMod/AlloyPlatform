#pragma once

#include "dsp/FilterEngine.h"
#include <math.h>

// ---------------------------------------------------------------------------
// SVFFilter — Cytomic trapezoidal state-variable filter (M26a)
//
// Modes: OFF / LP / HP / BP / NOTCH
// Coefficients computed once per control cycle (tanf at 128 Hz).
// Audio path is pure float multiply/add — no trig in the ISR.
// Stable at all cutoffs and resonances including near self-oscillation.
//
// Stereo: L and R channels share coefficients but have independent
// integrator state (ic1L/ic2L, ic1R/ic2R) for true stereo response.
// ---------------------------------------------------------------------------

class SVFFilter : public FilterEngine
{
  public:
    SVFFilter()
    : _a1(1.0f),
      _a2(0.0f),
      _a3(0.0f),
      _k(2.0f),
      _ic1L(0.0f),
      _ic2L(0.0f),
      _ic1R(0.0f),
      _ic2R(0.0f),
      _mode(FilterMode::OFF)
    {
    }

    void setParams(float      cutoff_hz,
                   float      resonance,
                   FilterMode mode,
                   float      sampleRate = 32768.0f) override
    {
        _mode = mode;
        if(mode == FilterMode::OFF)
            return;

        if(cutoff_hz < 20.0f)
            cutoff_hz = 20.0f;
        if(cutoff_hz > 16000.0f)
            cutoff_hz = 16000.0f;
        if(resonance < 0.0f)
            resonance = 0.0f;
        if(resonance > 0.999f)
            resonance = 0.999f;

        const float g = tanf(3.14159265f * cutoff_hz / sampleRate);
        _k            = 2.0f * (1.0f - resonance);
        _a1           = 1.0f / (1.0f + g * (g + _k));
        _a2           = g * _a1;
        _a3           = g * _a2;
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
        *outL = _chan(inL, &_ic1L, &_ic2L);
        *outR = _chan(inR, &_ic1R, &_ic2R);
    }

    FilterMode mode() const override { return _mode; }

    void reset() override { _ic1L = _ic2L = _ic1R = _ic2R = 0.0f; }

  private:
    inline int32_t _chan(int32_t in, float *ic1, float *ic2)
    {
        const float v0 = in * (1.0f / 32512.0f);
        const float v3 = v0 - *ic2;
        const float v1 = _a1 * (*ic1) + _a2 * v3;
        const float v2 = *ic2 + _a2 * (*ic1) + _a3 * v3;
        *ic1           = 2.0f * v1 - *ic1;
        *ic2           = 2.0f * v2 - *ic2;
        float out;
        switch(_mode)
        {
            case FilterMode::LP: out = v2; break;
            case FilterMode::HP: out = v0 - _k * v1 - v2; break;
            case FilterMode::BP: out = v1; break;
            case FilterMode::NOTCH: out = v0 - _k * v1; break;
            default: out = v2; break;
        }
        if(out > 1.0f)
            out = 1.0f;
        if(out < -1.0f)
            out = -1.0f;
        return (int32_t)(out * 32512.0f);
    }

    float      _a1, _a2, _a3, _k;
    float      _ic1L, _ic2L, _ic1R, _ic2R;
    FilterMode _mode;
};
