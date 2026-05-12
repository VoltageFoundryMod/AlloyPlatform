#pragma once

#include <stdint.h>
#include <string.h>

/**
 * DelayEngine — Stereo ping-pong delay  (Milestone 26c)
 *
 * Max delay time is compile-time configurable via DELAY_MAX_MS (platformio.ini).
 * Default: 300ms.  Increase freely — each 100ms adds ~6.5KB SRAM.
 *
 * Ping-pong routing: even bounces → L, odd bounces → R.
 * The feedback path is cross-channel so a mono input creates stereo movement.
 *
 * Parameters
 * ----------
 *   time_ms   : delay time 10–DELAY_MAX_MS ms
 *   feedback  : 0.0 (single echo) … 0.95 (long decay)
 *   mix       : 0.0 (dry only) … 1.0 (full wet)
 *
 * Milestone 26c — currently a pass-through stub.
 * Full implementation: ring buffer + fractional read + cross-feed routing.
 */

#ifndef DELAY_MAX_MS
#define DELAY_MAX_MS 200
#endif

class DelayEngine {
  public:
    static constexpr uint32_t kMaxSamples =
        (uint32_t)((DELAY_MAX_MS / 1000.0f) * 32768.0f + 0.5f);

    DelayEngine()
        : _enabled(false), _timeSamples(0), _feedback(0.0f), _mix(0.0f),
          _writeIdx(0) {
        memset(_bufL, 0, sizeof(_bufL));
        memset(_bufR, 0, sizeof(_bufR));
    }

    /**
     * setParams() — control rate only (contains float multiply).
     * time_ms  : 10–DELAY_MAX_MS
     * feedback : 0.0–0.95
     * mix      : 0.0–1.0
     */
    void setParams(float time_ms, float feedback, float mix) {
        _enabled = (mix > 0.001f);
        if (time_ms < 10.0f)
            time_ms = 10.0f;
        if (time_ms > (float)DELAY_MAX_MS)
            time_ms = (float)DELAY_MAX_MS;
        if (feedback < 0.0f)
            feedback = 0.0f;
        if (feedback > 0.95f)
            feedback = 0.95f;
        if (mix < 0.0f)
            mix = 0.0f;
        if (mix > 1.0f)
            mix = 1.0f;
        _timeSamples = (uint32_t)(time_ms * 32.768f); // ms × 32768/1000
        _feedback = feedback;
        _mix = mix;
        _dryGain = 1.0f - mix * 0.5f; // slight dry reduction at high mix
    }

    void setEnabled(bool en) { _enabled = en; }
    bool enabled() const { return _enabled; }

    /**
     * process() — audio rate.  Ping-pong: L taps even bounces, R taps odd.
     * M26c stub: pass-through until ring buffer is implemented.
     */
    inline void process(int32_t inL, int32_t inR,
                        int32_t *outL, int32_t *outR) {
        // TODO(M26c): implement ring buffer ping-pong
        *outL = inL;
        *outR = inR;
    }

  private:
    bool _enabled;
    uint32_t _timeSamples;
    float _feedback;
    float _mix;
    float _dryGain;
    uint32_t _writeIdx;

    // Delay line buffers — statically allocated at compile time
    int32_t _bufL[kMaxSamples];
    int32_t _bufR[kMaxSamples];
};
