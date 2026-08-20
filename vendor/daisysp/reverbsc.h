#pragma once
#ifndef DSYSP_REVERBSC_H
#define DSYSP_REVERBSC_H

// ⚠ LICENCE: this file is NOT under DaisySP's MIT licence like the rest of
// vendor/daisysp. It comes from electro-smith/DaisySP-LGPL and is LGPL-2.1 —
// see LICENSE.LGPL-2.1 and the "Licensing" section of README.md. Used here as
// GPL-3.0-or-later under LGPL-2.1 section 3. Keep the attribution block below.

// LOCAL PATCH. The pool is now a count of FLOATS — which is what it was always
// used as — and is *derived from the highest sample rate the build must
// support* rather than hard-coded. See the units bug fixed in reverbsc.cpp's
// Init().
//
// The eight delay lines need 24 726 samples at 48 kHz (2543 + 2842 + 3325 +
// 3605 + 3977 + 4202 + 2251 + 1981, from DelayLineMaxSamples), and that scales
// linearly with the rate. A fixed 24 800 was right for the firmware, which runs
// at exactly 48 kHz, and wrong for the VCV plugin, which re-inits at whatever
// rate Rack is set to: at 96 kHz only four of the eight lines fit, Init()
// returned 1, and the other four were left holding indeterminate `buf`
// pointers that Process() then wrote through.
//
// So the size is computed instead. Summing the eight lines exactly needs
// floating point and eight floor()s, neither of which the preprocessor has, so
// this is a closed-form upper bound on that sum:
//
//   sum_i floor(d_i * sr + 16.5)  <=  0.5124834 * sr + 132
//
// where sum(d_i) = 24124/48000 + 0.0088 * 1.125 = 0.5124834 s is the total of
// the eight delay times plus their pitch-modulation headroom. 5125/10000 is
// that constant rounded *up*, and +140 covers both the eight roundings and the
// integer division's truncation, so the bound holds at every rate.
//
//   48 kHz  -> 24 740 floats (  96.6 KiB) — the firmware
//  192 kHz  -> 98 540 floats ( 385.0 KiB) — the VCV plugin's worst case
#ifndef DSY_REVERBSC_MAX_SRATE
#define DSY_REVERBSC_MAX_SRATE 48000
#endif

#define DSY_REVERBSC_MAX_SIZE (((DSY_REVERBSC_MAX_SRATE) * 5125) / 10000 + 140)

namespace daisysp
{
/**Delay line for internal reverb use
*/
typedef struct
{
    int    write_pos;         /**< write position */
    int    buffer_size;       /**< buffer size */
    int    read_pos;          /**< read position */
    int    read_pos_frac;     /**< fractional component of read pos */
    int    read_pos_frac_inc; /**< increment for fractional */
    int    dummy;             /**<  dummy var */
    int    seed_val;          /**< randseed */
    int    rand_line_cnt;     /**< number of random lines */
    float  filter_state;      /**< state of filter */
    float *buf;               /**< buffer ptr */
} ReverbScDl;

/** Stereo Reverb

Reverb SC:               Ported from csound/soundpipe

Original author(s):        Sean Costello, Istvan Varga

Year:                    1999, 2005

Ported to soundpipe by:  Paul Batchelor

Ported by:                Stephen Hensley
*/
class ReverbSc
{
  public:
    ReverbSc() {}
    ~ReverbSc() {}
    /** Initializes the reverb module, and sets the sample_rate at which the Process function will be called.
        Returns 0 if all good, or 1 if it runs out of delay times exceed maximum allowed.
    */
    int Init(float sample_rate);

    /** Process the input through the reverb, and updates values of out1, and out2 with the new processed signal.
    */
    int Process(const float &in1, const float &in2, float *out1, float *out2);

    /** controls the reverb time. reverb tail becomes infinite when set to 1.0
        \param fb - sets reverb time. range: 0.0 to 1.0
    */
    inline void SetFeedback(const float &fb) { feedback_ = fb; }
    /** controls the internal dampening filter's cutoff frequency.
        \param freq - low pass frequency. range: 0.0 to sample_rate / 2
    */
    inline void SetLpFreq(const float &freq) { lpfreq_ = freq; }

  private:
    void       NextRandomLineseg(ReverbScDl *lp, int n);
    int        InitDelayLine(ReverbScDl *lp, int n);
    float      feedback_, lpfreq_;
    float      i_sample_rate_, i_pitch_mod_, i_skip_init_;
    float      sample_rate_;
    float      damp_fact_;
    float      prv_lpfreq_;
    int        init_done_;
    ReverbScDl delay_lines_[8];
    float      aux_[DSY_REVERBSC_MAX_SIZE];

    // DSY_REVERBSC_MAX_SRATE * 5125 must not overflow a signed int during
    // preprocessing. 419 kHz is far past anything a module will ask for, but a
    // silent wrap here would size the pool at a negative number.
    static_assert(DSY_REVERBSC_MAX_SRATE <= 419000,
                  "DSY_REVERBSC_MAX_SRATE too large — the size expression "
                  "overflows; widen it before raising this");
};


} // namespace daisysp
#endif
