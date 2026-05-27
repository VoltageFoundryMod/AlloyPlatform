#pragma once

#include <math.h>
#include <stdint.h>

// Chorus mode selector — stored as uint8_t in gChorusMode.
enum class ChorusMode : uint8_t {
    OFF = 0,  // pass-through (phasors keep running)
    I = 1,    // both channels: slow LFO (0.513 Hz) — subtle width
    II = 2,   // both channels: fast LFO (0.618 Hz) — deeper warble
    I_II = 3, // L=slow, R=fast — maximum stereo spread (Juno I+II)
};

/**
 * ChorusEngine<SAMPLE_RATE> — BBD-inspired stereo chorus (Milestone 12)
 *
 * Designed to run INSIDE Core 0 updateAudio() ISR — no trig calls in process().
 * The LFOs use a quadrature-phasor recurrence (multiply-only per sample) so
 * there is no sinf/cosf in the hot path.  sinf/cosf are called only once each
 * at init() to seed the two phasors.
 *
 * Juno-60 inspired: modulated delay lines, stereo spread via offset LFO phases
 * and slightly detuned rates.  Depth driven by gChorusDepth (alias: sMotion).
 *
 * Chorus modes (ChorusMode enum):
 *   OFF  (0) — pass-through; phasors keep running for glitch-free re-enable
 *   I    (1) — both channels driven by slow phasor (0.513 Hz), subtle width
 *   II   (2) — both channels driven by fast phasor (0.618 Hz), deeper warble
 *   I_II (3) — L=slow, R=fast; maximum stereo independence (default)
 *
 * Mix law:  wet = depth × 0.6   dry = 1.0 − wet
 *   depth=0.0 → 100% dry (transparent)
 *   depth=0.5 → 70% dry / 30% wet
 *   depth=1.0 → 40% dry / 60% wet
 *
 * Delay parameters (@ SAMPLE_RATE = 32768 Hz):
 *   Centre delay : 7 ms  = ~229 samples
 *   LFO mod depth: ±3 ms = ±98  samples  →  max read ~327 samples behind write
 *   Buffer size  : 1024 samples (power-of-2 for cheap masking)
 *
 * LFO rates:
 *   L : 0.513 Hz   (slightly asymmetric from R for organic spread)
 *   R : 0.618 Hz   (golden-ratio step above L)
 *   Initial phases: L=0°, R=90° for maximum L/R independence
 *
 * Memory: two int32_t[1024] delay buffers → 8 KB BSS.
 *
 * CPU cost in ISR (M33 @ 150 MHz, FPU):
 *   2 × phasor step   :  ~10 cycles
 *   2 × delay compute :  ~10 cycles
 *   2 × linear interp :  ~20 cycles
 *   4 × mix muls+adds :   ~8 cycles
 *   Total             : ~50 cycles  (<2 % of 4587-cycle ISR budget)
 */
template <uint32_t SAMPLE_RATE>
class ChorusEngine {
  public:
    static constexpr uint32_t BUFFER_SAMPLES = 1024;
    static constexpr uint32_t BUFFER_MASK = BUFFER_SAMPLES - 1u;

    static constexpr float CENTER_DELAY_S = 0.007f; // 7 ms
    static constexpr float MAX_MOD_S = 0.003f;      // ±3 ms

    // LFO rates and default compile-time delay lengths (informational; actual
    // values are computed at runtime in init() from the effective sample rate).
    static constexpr float LFO_RATE_L = 0.513f; // Hz
    static constexpr float LFO_RATE_R = 0.618f; // Hz

    void init(uint32_t sampleRate = SAMPLE_RATE) {
        _centerSamples = CENTER_DELAY_S * (float)sampleRate;
        _maxModSamples = MAX_MOD_S * (float)sampleRate;
        for (uint32_t i = 0; i < BUFFER_SAMPLES; i++) {
            _bufL[i] = 0;
            _bufR[i] = 0;
        }
        _writePos = 0;

        // Pre-compute phasor increments — only trig calls ever made.
        const float incL = LFO_RATE_L / (float)sampleRate * 6.28318f;
        const float incR = LFO_RATE_R / (float)sampleRate * 6.28318f;
        _cosIncL = cosf(incL);
        _sinIncL = sinf(incL);
        _cosIncR = cosf(incR);
        _sinIncR = sinf(incR);

        // Seed phasors: L at 0°, R at 90° (sin=1, cos=0)
        _sinL = 0.0f;
        _cosL = 1.0f;
        _sinR = 1.0f;
        _cosR = 0.0f;
    }

    /** Runtime sample-rate change — re-initialises phasors and delay params. */
    void setSampleRate(uint32_t sr) { init(sr); }

    /**
     * Process one stereo sample pair in the audio ISR.
     *
     * inL / inR   — input samples (16-bit range ±32768)
     * depth       — 0.0 (dry) … 1.0 (full chorus), driven by sMotion
     * mode        — ChorusMode: OFF / I / II / I_II
     * *outL / *outR — wet+dry mix
     *
     * No trig, no division, no branches (other than buffer wrap — masked).
     * Both phasors always advance regardless of mode so re-enabling is glitch-free.
     */
    void __attribute__((always_inline)) process(int32_t inL, int32_t inR, float depth, ChorusMode mode,
                                                int32_t *outL, int32_t *outR) {
        // Write new samples into delay buffers.
        _bufL[_writePos] = inL;
        _bufR[_writePos] = inR;

        if (mode == ChorusMode::OFF) {
            *outL = inL;
            *outR = inR;
        } else {
            // Select which phasor drives each channel.
            const float sinL = (mode == ChorusMode::II) ? _sinR : _sinL;
            const float sinR = (mode == ChorusMode::I) ? _sinL : _sinR;

            // LFO-modulated fractional delays (samples).
            const float delayL = _centerSamples + _maxModSamples * sinL;
            const float delayR = _centerSamples + _maxModSamples * sinR;

            // Linear-interpolated reads.
            const int32_t wetL = _readInterp(_bufL, delayL);
            const int32_t wetR = _readInterp(_bufR, delayR);

            // Wet/dry mix — wet capped at 60% so the fundamental always stays.
            const float wet = depth * 0.6f;
            const float dry = 1.0f - wet;
            *outL = (int32_t)(dry * (float)inL + wet * (float)wetL);
            *outR = (int32_t)(dry * (float)inR + wet * (float)wetR);
        }

        // Advance write pointer.
        _writePos = (_writePos + 1u) & BUFFER_MASK;

        // Advance both phasors always — no transient on mode switch.
        //   sin(θ+Δ) = sin θ · cos Δ + cos θ · sin Δ
        //   cos(θ+Δ) = cos θ · cos Δ − sin θ · sin Δ
        const float newSinL = _sinL * _cosIncL + _cosL * _sinIncL;
        const float newCosL = _cosL * _cosIncL - _sinL * _sinIncL;
        _sinL = newSinL;
        _cosL = newCosL;

        const float newSinR = _sinR * _cosIncR + _cosR * _sinIncR;
        const float newCosR = _cosR * _cosIncR - _sinR * _sinIncR;
        _sinR = newSinR;
        _cosR = newCosR;

        // Phasor renormalization — the quadrature recurrence accumulates float
        // rounding error: sin²+cos² drifts from 1.0 at ~1e-7 per sample, reaching
        // ~0.2 magnitude error after a minute.  Every 512 samples (~16 ms) apply
        // fast inverse-sqrt approximation: r = 1.5 - 0.5*(sin²+cos²) ≈ 1/|v|.
        // 8 FPU ops, negligible cost; keeps error bounded at < 5e-5.
        if ((_writePos & 511u) == 0u) {
            float rL = 1.5f - 0.5f * (_sinL * _sinL + _cosL * _cosL);
            _sinL *= rL;
            _cosL *= rL;
            float rR = 1.5f - 0.5f * (_sinR * _sinR + _cosR * _cosR);
            _sinR *= rR;
            _cosR *= rR;
        }
    }

  private:
    int32_t _bufL[BUFFER_SAMPLES];
    int32_t _bufR[BUFFER_SAMPLES];
    uint32_t _writePos = 0;

    // Runtime-computed delay lengths (set in init()).
    float _centerSamples = CENTER_DELAY_S * (float)SAMPLE_RATE;
    float _maxModSamples = MAX_MOD_S * (float)SAMPLE_RATE;

    // Phasor state — magnitude-1 quadrature pairs.
    float _sinL = 0.0f, _cosL = 1.0f;
    float _sinR = 1.0f, _cosR = 0.0f;

    // Phasor increments — set once in init().
    float _cosIncL = 1.0f, _sinIncL = 0.0f;
    float _cosIncR = 1.0f, _sinIncR = 0.0f;

    /**
     * Linear-interpolated circular-buffer read.
     * delaySamples — fractional samples behind the current write position.
     */
    inline int32_t _readInterp(const int32_t *buf, float delaySamples) const {
        const uint32_t d = (uint32_t)delaySamples;
        const float frac = delaySamples - (float)d;
        const uint32_t i0 = (_writePos - d) & BUFFER_MASK;
        const uint32_t i1 = (_writePos - d - 1u) & BUFFER_MASK;
        return buf[i0] + (int32_t)(frac * (float)(buf[i1] - buf[i0]));
    }
};
