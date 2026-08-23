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
 * next() is called at audio rate from renderAudio() — no float arithmetic
 * in the hot path; all float work is done in setShape().
 *
 * Output range: ≈ ±32512, matching the 16-bit stereo signal path.
 */

template <uint16_t UPDATE_RATE>
class ShapeOsc
{
  public:
    static constexpr uint16_t TABLE_CELLS = 2048;
    static constexpr uint8_t  N_SHAPES    = 5;

    /** Default constructor — call setTables() before use. */
    ShapeOsc()
    : _phase(0), _phaseInc(0), _tA(0), _blend(0), _sampleRate(UPDATE_RATE)
    {
        for(uint8_t i = 0; i < N_SHAPES; i++)
            _tables[i] = nullptr;
    }

    ShapeOsc(const int16_t *sine,
             const int16_t *tri,
             const int16_t *saw,
             const int16_t *pulse,
             const int16_t *hollow)
    : _phase(0), _phaseInc(0), _tA(0), _blend(0), _sampleRate(UPDATE_RATE)
    {
        _tables[0] = sine;
        _tables[1] = tri;
        _tables[2] = saw;
        _tables[3] = pulse;
        _tables[4] = hollow;
    }

    /** Assign wavetable pointers after default construction. */
    void setTables(const int16_t *sine,
                   const int16_t *tri,
                   const int16_t *saw,
                   const int16_t *pulse,
                   const int16_t *hollow)
    {
        _tables[0] = sine;
        _tables[1] = tri;
        _tables[2] = saw;
        _tables[3] = pulse;
        _tables[4] = hollow;
    }

    void setFreq(float freq)
    {
        _phaseInc
            = (uint32_t)((TABLE_CELLS * freq / (float)_sampleRate) * 65536.0f);
    }

    /** Runtime sample-rate override — call when VCV host changes rate. */
    void setSampleRate(uint32_t sr) { _sampleRate = sr; }

    // shape: 0.0 (sine) … 1.0 (hollow pulse).  Clamped internally.
    // Pre-computes the table-pair index and blend factor so next() is float-free.
    /** Reset phase accumulator to zero — call on note retrigger to avoid random-phase clicks. */
    void resetPhase() { _phase = 0; }

    /**
     * Force the phase accumulator to an arbitrary position.
     *
     * The counterpart to resetPhase(), for the case where a *known* phase is
     * the wrong answer: CLOUD's supersaw randomises all seven phases on note
     * attack, which is what gives each stab its own character and — just as
     * importantly — stops seven near-identical oscillators from summing
     * coherently into a peak the limiter has to catch.
     */
    void setPhase(uint32_t phase) { _phase = phase; }

    // Phase-modulated sample: reads at (_phase + phaseOffset) but advances _phase normally.
    // Use for FM/PM in CASCADE mode — the carrier calls this with the modulator sample
    // scaled to Q16 phase units (phaseOffset = modSample × sFmDepth precomputed at ctrl rate).
    inline int16_t nextPM(int32_t phaseOffset)
    {
        const uint32_t phaseMod = _phase + (uint32_t)phaseOffset;
        const uint16_t mask     = TABLE_CELLS - 1;
        const uint16_t idx      = (uint16_t)(phaseMod >> 16) & mask;
        const uint16_t nxt      = (idx + 1u) & mask;
        const int32_t  frac     = (int32_t)(phaseMod & 0xFFFFu);

        const int32_t a0 = (int32_t)_tables[_tA][idx];
        const int32_t b0 = (int32_t)_tables[_tA][nxt];
        const int32_t a1 = (int32_t)_tables[_tA + 1][idx];
        const int32_t b1 = (int32_t)_tables[_tA + 1][nxt];

        const int32_t s0 = a0 + (((b0 - a0) * frac) >> 16);
        const int32_t s1 = a1 + (((b1 - a1) * frac) >> 16);

        const int16_t out
            = (int16_t)(((s0 * (256 - _blend)) + (s1 * _blend)) >> 8);
        _phase += _phaseInc;
        return out;
    }

    void setShape(float shape)
    {
        if(shape < 0.0f)
            shape = 0.0f;
        if(shape > 1.0f)
            shape = 1.0f;

        const float pos = shape * (float)(N_SHAPES - 1); // 0.0 … 4.0
        uint8_t     tA  = (uint8_t)pos;
        if(tA >= N_SHAPES - 1)
            tA = N_SHAPES - 2; // clamp to 0..3
        const float fpos = pos - (float)tA;

        _tA    = tA;
        _blend = (fpos >= 1.0f) ? (uint8_t)255 : (uint8_t)(fpos * 256.0f);
    }

    // Returns the next interpolated, crossfaded sample ≈ ±32512.
    // Integer-only hot path: one table lookup + linear interpolation + crossfade.
    // Index masking (& TABLE_CELLS-1) is REQUIRED because
    // (phase >> 16) ranges 0..65535 while the table is only TABLE_CELLS cells.
    inline int16_t next()
    {
        // Phase → masked table index + 16-bit fractional part
        const uint16_t mask = TABLE_CELLS - 1;
        const uint16_t idx  = (uint16_t)(_phase >> 16) & mask;
        const uint16_t nxt  = (idx + 1u) & mask;
        const int32_t  frac = (int32_t)(_phase & 0xFFFFu); // 0..65535

        // Read two adjacent cells from each crossfade table; sign-extend int16→int32
        const int32_t a0 = (int32_t)_tables[_tA][idx];
        const int32_t b0 = (int32_t)_tables[_tA][nxt];
        const int32_t a1 = (int32_t)_tables[_tA + 1][idx];
        const int32_t b1 = (int32_t)_tables[_tA + 1][nxt];

        // Linear interpolation within each table.
        // int16 range is ±32767; interpolate in 32-bit then shift down by 16
        // to get output in ±32767 (16-bit signal path).
        const int32_t s0 = a0 + (((b0 - a0) * frac) >> 16);
        const int32_t s1 = a1 + (((b1 - a1) * frac) >> 16);

        // Crossfade between the two adjacent tables
        const int16_t out
            = (int16_t)(((s0 * (256 - _blend)) + (s1 * _blend)) >> 8);

        _phase += _phaseInc;
        return out;
    }

  private:
    const int16_t *_tables[N_SHAPES];
    uint32_t       _phase;
    uint32_t       _phaseInc;
    uint8_t        _tA; // lower table index (0..3), updated in setShape()
    uint8_t _blend; // crossfade 0=100%tA … 255=~100%tA+1, updated in setShape()
    uint32_t
        _sampleRate; // effective sample rate — default = UPDATE_RATE template param
};
