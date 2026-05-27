#pragma once

#include <stdint.h>
#include <string.h>

/**
 * DelayEngine — Stereo ping-pong delay  (Milestone 26c)
 *
 * Max delay time is compile-time configurable via DELAY_MAX_MS (platformio.ini).
 * Default: 300ms.  Increase freely — each 100ms adds ~13KB SRAM (int32 buffers).
 *
 * Ping-pong routing: cross-channel feedback — echoes alternate L / R / L / R.
 *   L delay line ← inL + feedback × delayedR
 *   R delay line ← inR + feedback × delayedL
 * Linear interpolation for accurate fractional delay time (no zipper artefacts).
 *
 * Parameters
 * ----------
 *   time_ms   : delay time 10–DELAY_MAX_MS ms
 *   feedback  : 0.0 (single echo) … 0.95 (long decay)
 *   mix       : 0.0 (dry only) … 1.0 (full wet)
 */

#ifndef DELAY_MAX_MS
#define DELAY_MAX_MS 300
#endif

class DelayEngine
{
  public:
    static constexpr uint32_t kMaxSamples
        = (uint32_t)((DELAY_MAX_MS / 1000.0f) * 32768.0f + 0.5f);

    DelayEngine()
    : _enabled(false),
      _timeSamples(0),
      _timeFrac(0.0f),
      _feedback(0.0f),
      _mix(0.0f),
      _dryGain(1.0f),
      _writeIdx(0)
    {
        memset(_bufL, 0, sizeof(_bufL));
        memset(_bufR, 0, sizeof(_bufR));
    }

    /**
     * setParams() — control rate only (contains float multiply).
     * time_ms  : 10–DELAY_MAX_MS
     * feedback : 0.0–0.95
     * mix      : 0.0–1.0
     */
    void setParams(float time_ms, float feedback, float mix)
    {
        _enabled = (mix > 0.001f);
        if(time_ms < 10.0f)
            time_ms = 10.0f;
        if(time_ms > (float)DELAY_MAX_MS)
            time_ms = (float)DELAY_MAX_MS;
        if(feedback < 0.0f)
            feedback = 0.0f;
        if(feedback > 0.95f)
            feedback = 0.95f;
        if(mix < 0.0f)
            mix = 0.0f;
        if(mix > 1.0f)
            mix = 1.0f;
        const float fsamples = time_ms * (32768.0f / 1000.0f);
        _timeSamples         = (uint32_t)fsamples;
        if(_timeSamples >= kMaxSamples)
            _timeSamples = kMaxSamples - 1;
        _timeFrac = fsamples - (float)_timeSamples;
        _feedback = feedback;
        _mix      = mix;
        _dryGain  = 1.0f - mix * 0.5f; // slight dry reduction at high mix
    }

    void setEnabled(bool en) { _enabled = en; }
    bool enabled() const { return _enabled; }

    /**
     * process() — audio rate, stereo ping-pong delay.
     *
     * Cross-channel feedback creates the ping-pong effect:
     *   L delay line ← inL + feedback × delayedR  (right echo feeds left)
     *   R delay line ← inR + feedback × delayedL  (left echo feeds right)
     * A centred mono sound will alternate L / R / L / R on successive echoes.
     *
     * Linear interpolation between adjacent samples gives accurate fractional
     * delay time without audible stepped zipper artefacts.
     */
    __attribute__((always_inline)) inline void
    process(int32_t inL, int32_t inR, int32_t *outL, int32_t *outR)
    {
        if(!_enabled)
        {
            *outL = inL;
            *outR = inR;
            return;
        }

        // Read index: _timeSamples samples back from the write head.
        // Avoid modulo (expensive on M33) — use conditional subtract.
        const uint32_t ri0 = (_writeIdx >= _timeSamples)
                                 ? _writeIdx - _timeSamples
                                 : _writeIdx + kMaxSamples - _timeSamples;
        // One additional sample back for linear interpolation.
        const uint32_t ri1 = (ri0 == 0) ? kMaxSamples - 1 : ri0 - 1;

        // Fractional linear interpolation.
        const int32_t dL0 = _bufL[ri0];
        const int32_t delayedL
            = dL0 + (int32_t)(_timeFrac * (float)(_bufL[ri1] - dL0));
        const int32_t dR0 = _bufR[ri0];
        const int32_t delayedR
            = dR0 + (int32_t)(_timeFrac * (float)(_bufR[ri1] - dR0));

        // Write to delay lines with cross-channel feedback.
        int32_t fbL = inL + (int32_t)((float)delayedR * _feedback);
        int32_t fbR = inR + (int32_t)((float)delayedL * _feedback);
        // Clamp to prevent feedback accumulation beyond rail.
        if(fbL > 32767)
            fbL = 32767;
        else if(fbL < -32767)
            fbL = -32767;
        if(fbR > 32767)
            fbR = 32767;
        else if(fbR < -32767)
            fbR = -32767;

        _bufL[_writeIdx] = fbL;
        _bufR[_writeIdx] = fbR;

        // Advance write head.
        if(++_writeIdx >= kMaxSamples)
            _writeIdx = 0;

        // Dry + wet mix; clamp output.
        int32_t oL = (int32_t)((float)inL * _dryGain + (float)delayedL * _mix);
        int32_t oR = (int32_t)((float)inR * _dryGain + (float)delayedR * _mix);
        if(oL > 32767)
            oL = 32767;
        else if(oL < -32767)
            oL = -32767;
        if(oR > 32767)
            oR = 32767;
        else if(oR < -32767)
            oR = -32767;
        *outL = oL;
        *outR = oR;
    }

  private:
    bool     _enabled;
    uint32_t _timeSamples;
    float    _timeFrac;
    float    _feedback;
    float    _mix;
    float    _dryGain;
    uint32_t _writeIdx;

    // Delay line buffers — statically allocated at compile time
    int32_t _bufL[kMaxSamples];
    int32_t _bufR[kMaxSamples];
};
