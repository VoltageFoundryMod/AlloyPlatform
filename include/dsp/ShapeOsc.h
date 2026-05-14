#pragma once

#include <stdint.h>

/**
 * ShapeOsc<UPDATE_RATE> — single-phase wavetable oscillator with 5-shape morph.
 *
 * One 32-bit phase accumulator is shared across all five shapes, so:
 *   - no phase discontinuity when SHAPE changes (smooth timbral transitions)
 *   - one setFreq() call covers all waveforms simultaneously
 *
 * SHAPE spectrum (0.0 → 1.0):
 *   0.00      0.25      0.50      0.75      1.00
 *   sine ─── tri ─── saw ─── pulse ─── hollow pulse
 *
 * All tables must be exactly TABLE_CELLS (2048) int8 samples, band-limited via
 * additive synthesis with Lanczos sigma smoothing — see generateWavetables() in
 * main.cpp for the derivation of each table.
 *
 * setFreq() and setShape() are called at control rate (128 Hz) from updateControl().
 * next() is called at audio rate (32768 Hz) from updateAudio() — no float arithmetic
 * in the hot path; all float work is done in setShape().
 *
 * Output range: ≈ ±32512, compatible with the Mozzi 16-bit stereo pipeline.
 */

template <uint16_t UPDATE_RATE>
class ShapeOsc {
  public:
    static constexpr uint16_t TABLE_CELLS = 2048;
    static constexpr uint8_t N_SHAPES = 5;

    ShapeOsc(const int16_t *sine,
             const int16_t *tri,
             const int16_t *saw,
             const int16_t *pulse,
             const int16_t *hollow)
        : _phase(0), _phaseInc(0), _tA(0), _blend(0) {
        _tables[0] = sine;
        _tables[1] = tri;
        _tables[2] = saw;
        _tables[3] = pulse;
        _tables[4] = hollow;
    }

    void setFreq(float freq) {
        _phaseInc = (uint32_t)((TABLE_CELLS * freq / UPDATE_RATE) * 65536.0f);
    }

    // shape: 0.0 (sine) … 1.0 (hollow pulse).  Clamped internally.
    // Pre-computes the table-pair index and blend factor so next() is float-free.
    void setShape(float shape) {
        if (shape < 0.0f)
            shape = 0.0f;
        if (shape > 1.0f)
            shape = 1.0f;

        const float pos = shape * (float)(N_SHAPES - 1); // 0.0 … 4.0
        uint8_t tA = (uint8_t)pos;
        if (tA >= N_SHAPES - 1)
            tA = N_SHAPES - 2; // clamp to 0..3
        const float fpos = pos - (float)tA;

        _tA = tA;
        _blend = (fpos >= 1.0f) ? (uint8_t)255 : (uint8_t)(fpos * 256.0f);
    }

    // Returns the next interpolated, crossfaded sample ≈ ±32512.
    // Integer-only hot path: one table lookup + linear interpolation + crossfade.
    // Index masking (& TABLE_CELLS-1) is REQUIRED — same as Osc16 — because
    // (phase >> 16) ranges 0..65535 while the table is only TABLE_CELLS cells.
    inline int16_t next() {
        // Phase → masked table index + 16-bit fractional part (same as Osc16)
        const uint16_t mask = TABLE_CELLS - 1;
        const uint16_t idx = (uint16_t)(_phase >> 16) & mask;
        const uint16_t nxt = (idx + 1u) & mask;
        const int32_t frac = (int32_t)(_phase & 0xFFFFu); // 0..65535

        // Read two adjacent cells from each crossfade table; sign-extend int16→int32
        const int32_t a0 = (int32_t)_tables[_tA][idx];
        const int32_t b0 = (int32_t)_tables[_tA][nxt];
        const int32_t a1 = (int32_t)_tables[_tA + 1][idx];
        const int32_t b1 = (int32_t)_tables[_tA + 1][nxt];

        // Linear interpolation within each table.
        // int16 range is ±32767; interpolate in 32-bit then shift down by 16
        // to get output in ±32767 (compatible with Mozzi 16-bit pipeline).
        const int32_t s0 = a0 + (((b0 - a0) * frac) >> 16);
        const int32_t s1 = a1 + (((b1 - a1) * frac) >> 16);

        // Crossfade between the two adjacent tables
        const int16_t out = (int16_t)(((s0 * (256 - _blend)) + (s1 * _blend)) >> 8);

        _phase += _phaseInc;
        return out;
    }

  private:
    const int16_t *_tables[N_SHAPES];
    uint32_t _phase;
    uint32_t _phaseInc;
    uint8_t _tA;    // lower table index (0..3), updated in setShape()
    uint8_t _blend; // crossfade 0=100%tA … 255=~100%tA+1, updated in setShape()
};
