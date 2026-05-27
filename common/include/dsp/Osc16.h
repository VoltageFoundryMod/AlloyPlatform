#pragma once
#include <stdint.h>

/**
 * Osc16 — interpolating wavetable oscillator, int16_t output.
 *
 * Compatible with any standard Mozzi int8_t wavetable header. Uses linear
 * interpolation between adjacent table cells to eliminate amplitude
 * quantisation noise: standard int8 tables have 256 discrete levels
 * (~-48 dB SNR), which is audible and visible on a scope as grit/fuzz when
 * the signal is scaled to 16-bit output. Interpolation uses the 16-bit
 * fractional component of the phase accumulator that Mozzi's Oscil discards,
 * giving sub-integer precision and pushing the noise floor to near the
 * 16-bit limit of the PCM5102A.
 *
 * Output range : approximately ±32512  (int16_t)
 * Phase format : same as Mozzi Oscil (OSCIL_F_BITS = 16)
 * Constraint   : NUM_TABLE_CELLS must be a power of 2
 * Drop-in for  : Oscil<NUM_TABLE_CELLS, AUDIO_RATE> — same setFreq() API
 */
template <uint16_t NUM_TABLE_CELLS, uint16_t UPDATE_RATE>
class Osc16
{
  public:
    explicit Osc16(const int8_t *table)
    : _table(table), _phase(0), _phase_inc(0)
    {
    }
    Osc16() : _table(nullptr), _phase(0), _phase_inc(0) {}

    inline void setTable(const int8_t *table) { _table = table; }

    /** Set oscillator frequency in Hz.  Same formula as Mozzi Oscil::setFreq(float). */
    inline void setFreq(float freq)
    {
        _phase_inc = (uint32_t)(((float)NUM_TABLE_CELLS * freq / UPDATE_RATE)
                                * 65536.0f);
    }

    /**
     * Advance phase and return linearly-interpolated sample.
     *
     * Derivation:
     *   a   = table[floor(phase)]          // int8, -128..+127
     *   b   = table[floor(phase) + 1]      // int8
     *   frac = phase - floor(phase)        // 0..1, represented as 0..65535
     *
     *   interp = a + (b-a)*frac            // in int8 units, sub-integer precision
     *   result = interp * 256              // scale to int16 range
     *
     * Fixed-point:
     *   result = (a << 8) + ((b-a) * frac >> 8)
     *
     * Max intermediate: (127<<8) + (255*65535>>8) = 32512 + 65278 = 97790 fits int32_t.
     * Final result always within [-32768, 32511] — fits int16_t.
     */
    inline int16_t next()
    {
        _phase += _phase_inc;
        const uint16_t idx  = (uint16_t)(_phase >> 16);
        const int32_t  frac = (int32_t)(_phase & 0xFFFFu);

        const int32_t a = (int32_t)(int8_t)_table[idx & (NUM_TABLE_CELLS - 1u)];
        const int32_t b
            = (int32_t)(int8_t)_table[(idx + 1u) & (NUM_TABLE_CELLS - 1u)];

        return (int16_t)((a << 8) + (((b - a) * frac) >> 8));
    }

  private:
    const int8_t *_table;
    uint32_t      _phase;
    uint32_t      _phase_inc;
};
