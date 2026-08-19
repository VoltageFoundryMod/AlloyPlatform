#pragma once
#ifndef INFS_ECHODELAY_H
#define INFS_ECHODELAY_H

#include "delayline.h"
#include "dsp.h"
#include "BiquadFilters.h"
#include "DSPUtils.h"

#include <stdint.h>

// ---------------------------------------------------------------------------
// M63f — decimated, fixed-point echo storage.
//
// Upstream stores 5 s of stereo echo as float at the full 48 kHz rate: two
// buffers of 960 000 bytes, 1.83 MiB between them. That is fine on a Daisy
// Seed, which has 64 MiB of external SDRAM, and impossible on an RP2350, which
// has 520 KB of SRAM in total.
//
// Two reductions, both compile-time switchable so they can be A/B'd by ear
// rather than argued about:
//
//   COIL_ECHO_DECIMATION  run the echo loop at fs/N (default 4 -> 12 kHz)
//   COIL_ECHO_Q15         store samples as int16 rather than float
//   COIL_ECHO_NOISE_SHAPE first-order error feedback on the quantiser
//   COIL_ECHO_ANTIALIAS   the 24 dB/oct filter ahead of the decimator
//
// Together they cut the storage 8x. Set COIL_ECHO_DECIMATION=1 and
// COIL_ECHO_Q15=0 to get upstream's behaviour back exactly.
//
// COIL_ECHO_ANTIALIAS exists only so the filter can be measured against its
// own absence — see modules/alloycoil/test/echo_ab.cpp and `make coil-ab`.
// Turning it off in a shipping build is not a trade-off, it is a bug.
//
// Both carry real DSP risk, and the mitigations are the interesting part:
//
//   Aliasing. The send is tapped *post-reverb* and is full-bandwidth — the
//   feedback loop's own LPF sits at 18 kHz — so decimating it raw would fold
//   everything from 6-18 kHz down into the audible band. Some of that lands at
//   low frequencies (11 kHz folds to 1 kHz) where the echo's 800 Hz bandpass
//   cannot help. Hence the 24 dB/oct anti-alias filter ahead of the decimator,
//   and linear interpolation on the way back up.
//
//   Quantisation noise. Feedback here is deliberately allowed past unity for
//   saturated swells, which means anything the quantiser adds is recirculated
//   and amplified rather than decaying. Three defences: the value is clamped
//   before quantising (SoftClip bounds the loop output to +/-1, but the sum
//   `out * feedback + in` is not bounded); samples are converted to float
//   *before* interpolation, never interpolated as integers; and the quantiser
//   carries its error into the next sample, which shifts the noise up out of
//   the band the echo's bandpass passes.
// ---------------------------------------------------------------------------

#ifndef COIL_ECHO_DECIMATION
#define COIL_ECHO_DECIMATION 4
#endif

#ifndef COIL_ECHO_Q15
#define COIL_ECHO_Q15 1
#endif

// Off by default since the M63f A/B (`make coil-ab-sweep`) measured it.
// Error feedback is the textbook improvement on a quantiser and it buys
// nothing here: SNR is 37.5 dB with it and 37.6 dB without, because the floor
// is set by SoftClip's intermodulation inside the loop, not by the quantiser.
// What it does cost is silence — the error term keeps the quantiser dithering
// around zero, leaving a permanent -119 dBFS floor where truncation decays to
// bit-exact zero. Inaudible on its own, but this echo deliberately allows
// feedback past unity, which amplifies that floor at ~1.7 dB/s. Trading a
// measurable "never truly silent" for an unmeasurable improvement is the wrong
// way round. Kept switchable because the reasoning is worth re-testing if the
// loop's nonlinearities ever change.
#ifndef COIL_ECHO_NOISE_SHAPE
#define COIL_ECHO_NOISE_SHAPE 0
#endif

#ifndef COIL_ECHO_ANTIALIAS
#define COIL_ECHO_ANTIALIAS 1
#endif

namespace infrasonic {

/**
 * @brief
 * Tape-ish echo delay.
 *   - Feedback is unbounded, but signal is soft-clipped
 *   - Output is full-wet, should be mixed with dry signal externally
 *
 * @tparam MaxLength Max length of delay in samples **at the full rate**.
 *         Storage is MaxLength / COIL_ECHO_DECIMATION samples, so the call
 *         site still reads as "5 s at 48 kHz" regardless of how the buffer is
 *         actually held.
 */
template<size_t MaxLength>
class EchoDelay {

    public:

        /// Decimation factor — the echo loop runs at sample_rate / kDecim.
        static constexpr size_t kDecim = COIL_ECHO_DECIMATION;

        /// Ring length at the decimated rate.
        static constexpr size_t kLen = (MaxLength + kDecim - 1) / kDecim;

        static_assert(kDecim >= 1, "decimation must be >= 1");
        static_assert(kLen > 4, "echo ring too short to interpolate");

#if COIL_ECHO_Q15
        using Sample = int16_t;
#else
        using Sample = float;
#endif

        EchoDelay() {}
        ~EchoDelay() {}

        void Init(float sample_rate)
        {
            sample_rate_ = sample_rate;
            decim_rate_  = sample_rate / static_cast<float>(kDecim);

            for (size_t i = 0; i < kLen; i++) { buf_[i] = static_cast<Sample>(0); }
            write_pos_ = 0;
            phase_     = 0;
            prev_out_  = 0.0f;
            cur_out_   = 0.0f;
            quant_err_ = 0.0f;

            // Loop filter runs at the decimated rate, so its coefficients must
            // be derived from that rate and not from sample_rate.
            bpf_.Init(decim_rate_);
            bpf_.SetParams(800.0f, 1.0f);

            // Anti-alias ahead of the decimator, at half the decimated rate's
            // Nyquist. This started at 0.35 and came down to 0.25 once the A/B
            // put numbers on it: at 0.35 (4200 Hz) an 11 kHz tone still folded
            // down to 1 kHz at -41 dBFS, which is audible, and 1 kHz is exactly
            // where the loop's own bandpass passes it through untouched.
            //
            // Dropping to 0.25 (3000 Hz) is another 12 dB of rejection for
            // almost nothing, because the bandpass below is at 800 Hz and
            // 12 dB/oct — it has already taken 23 dB out by 3 kHz, so there is
            // very little real echo content up in the band being given away.
            // Re-measure with `make coil-ab` before moving it again.
            aa_lpf_.Init(sample_rate);
            aa_lpf_.SetCutoff(decim_rate_ * 0.25f);
            aa_lpf_.SetFlatResponse();
        }

        /**
         * @brief Set the approximate lag time (smoothing) for delay time changes, in seconds
         */
        void SetLagTime(const float time_s)
        {
            // Smoothing is stepped once per decimated tick, so the coefficient
            // belongs to that rate.
            delay_smooth_coef_ = onepole_coef(time_s, decim_rate_);
        }

        /**
         * @brief Set the Delay Time in seconds
         *
         * @param time_s Delay time in seconds. Will be truncated to MaxLength.
         * @param immediately If true, sets delay time immediately with no smoothing.
         */
        void SetDelayTime(const float time_s, bool immediately = false)
        {
            delay_time_target_ = time_s;
            if (immediately) delay_time_current_ = time_s;
        }

        /**
         * @brief
         * Set the feedback amount (linear multiplier).
         * This can be >1 in magnitude for saturated swells, or negative.
         *
         * NOTE: This is not internally smoothed. Use external smoothing if desired.
         *
         * @param feedback
         */
        void SetFeedback(const float feedback)
        {
            feedback_ = feedback;
        }

        inline float Process(const float in)
        {
            // Band-limit the send before it is decimated. This is the whole
            // defence against fold-down: the tap is post-reverb and carries
            // content well above the decimated Nyquist.
#if COIL_ECHO_ANTIALIAS
            const float band_limited = aa_lpf_.Process(in);
#else
            const float band_limited = in; // measurement only — see the header
#endif

            if (++phase_ >= kDecim) {
                phase_    = 0;
                prev_out_ = cur_out_;
                cur_out_  = tick(band_limited);
            }

            // Linear interpolation back up to the full rate. Costs one
            // decimated sample of latency (~83 us at 12 kHz), constant.
            const float t = static_cast<float>(phase_) / static_cast<float>(kDecim);
            return prev_out_ + (cur_out_ - prev_out_) * t;
        }

    private:

        EchoDelay(const EchoDelay &other) = delete;
        EchoDelay(EchoDelay &&other) = delete;
        EchoDelay& operator=(const EchoDelay &other) = delete;
        EchoDelay& operator=(EchoDelay &&other) = delete;

        /// One tick of the echo loop, at the decimated rate.
        inline float tick(const float in)
        {
            daisysp::fonepole(delay_time_current_, delay_time_target_, delay_smooth_coef_);

            float delay_samp = delay_time_current_ * decim_rate_;
            // Leave room for the interpolator's second tap and never let the
            // read head reach the write head.
            delay_samp = daisysp::fclamp(delay_samp, 1.0f, static_cast<float>(kLen - 2));

            float out = readFrac(delay_samp);
            out = bpf_.Process(out);
            out = daisysp::SoftClip(out);
            write(out * feedback_ + in);
            return out;
        }

        /// Fractional read. Storage is dequantised to float *first*, then
        /// interpolated — interpolating integers would round twice.
        inline float readFrac(const float delay_samp) const
        {
            const size_t i0   = static_cast<size_t>(delay_samp);
            const float  frac = delay_samp - static_cast<float>(i0);

            const size_t r0 = (write_pos_ + kLen - i0) % kLen;
            const size_t r1 = (r0 + kLen - 1) % kLen;

            const float s0 = toFloat(buf_[r0]);
            const float s1 = toFloat(buf_[r1]);
            return s0 + (s1 - s0) * frac;
        }

        inline void write(float v)
        {
#if COIL_ECHO_Q15
#if COIL_ECHO_NOISE_SHAPE
            v += quant_err_;
#endif
            // Clamp before quantising. SoftClip bounds the loop output, but
            // `out * feedback + in` is a sum of two unbounded terms and can
            // leave the representable range; wrapping an int16 there would be
            // a full-scale discontinuity fed straight back into the loop.
            const float c = daisysp::fclamp(v, -kQ15Max, kQ15Max);
            const float scaled = c * 32767.0f;
            const int32_t q = static_cast<int32_t>(scaled >= 0.0f ? scaled + 0.5f
                                                                  : scaled - 0.5f);
            buf_[write_pos_] = static_cast<int16_t>(q);
#if COIL_ECHO_NOISE_SHAPE
            // Error is taken against the *clamped* value, so a hard clip does
            // not accumulate into the feedback path and wind up.
            quant_err_ = c - static_cast<float>(q) * (1.0f / 32767.0f);
#endif
#else
            buf_[write_pos_] = v;
#endif
            if (++write_pos_ >= kLen) { write_pos_ = 0; }
        }

#if COIL_ECHO_Q15
        static constexpr float kQ15Max = 0.999969f; // 32767 / 32768
        static inline float toFloat(const Sample s)
        {
            return static_cast<float>(s) * (1.0f / 32767.0f);
        }
#else
        static inline float toFloat(const Sample s) { return s; }
#endif

        float sample_rate_ = 48000.0f;
        float decim_rate_  = 12000.0f;
        float delay_time_current_ = 0.0f;
        float delay_time_target_  = 0.0f;
        float delay_smooth_coef_  = 1.0f;

        float feedback_ = 0.0f;

        size_t write_pos_ = 0;
        size_t phase_     = 0;
        float  prev_out_  = 0.0f;
        float  cur_out_   = 0.0f;
        float  quant_err_ = 0.0f;

        Sample buf_[kLen];

        LPF24 aa_lpf_;
        BPF12 bpf_;
};

}

#endif
