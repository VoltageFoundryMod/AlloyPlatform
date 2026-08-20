#pragma once
#ifndef INFS_COIL_OUTPUT_STAGE_H
#define INFS_COIL_OUTPUT_STAGE_H

#include "dsp.h"
#include "limiter.h"

// ---------------------------------------------------------------------------
// The module's output edge — everything between Engine::Process() and the DAC.
//
// Upstream keeps this *outside* the engine, in its audio callback:
//
//     engine.Process(IN_L[i], OUT_L[i], OUT_R[i]);   // per frame
//     limiter[0].ProcessBlock(OUT_L, size, 0.7f);    // per block
//     limiter[1].ProcessBlock(OUT_R, size, 0.7f);
//
// so it stays outside the engine here too, and the engine's Process() remains
// line-for-line identical to upstream's. What changes is that there are three
// call sites in this tree — the firmware, the VCV module and the host harness —
// and they must not drift, so the stage lives in this header rather than being
// written out three times.
//
// ⚠ This replaces the bare `SoftClip` the port originally shipped, which was
// not the same thing and was the single largest departure from Audrey II's
// voicing. daisysp::Limiter applies 0.7 pre-gain and 0.7 post-gain — 0.49
// static — with peak-tracked reduction (attack 0.05, release 0.00002) on top,
// and only engages that reduction once |x * 0.7| exceeds 1. The 0.49 is what
// keeps the signal inside SoftLimit's near-linear region at ordinary levels:
//
//     engine     Limiter(0.7)    bare SoftClip
//      0.20        0.0977          0.1977        +6.1 dB
//      1.00        0.4577          0.7778        +4.6 dB
//      1.34        0.5833          0.8940        +3.7 dB   (the engine's peak
//                                                           at useful settings)
//
// So the difference was not only ~6 dB of level. Pushed through SoftLimit at
// full scale the port was waveshaping *every* loud sample — 3.4 dB of
// compression between 0.20 and 1.34, against upstream's 1.0 dB — which on an
// instrument that is already a distorting feedback box reads as grit that
// Audrey II does not have.
//
// The original rationale said upstream "has the same headroom problem; on a
// Daisy the codec clips it just as hard". It does not: the limiter is between.
// ---------------------------------------------------------------------------

/// 1 = upstream's Limiter (default). 0 = the bare SoftClip the port shipped
/// before this, kept so the two can still be compared by ear:
///   make coil-host HOST_EXTRA="-DCOIL_OUTPUT_LIMITER=0"
#ifndef COIL_OUTPUT_LIMITER
#define COIL_OUTPUT_LIMITER 1
#endif

namespace infrasonic {
namespace FeedbackSynth {

class OutputStage {

    public:

        /// Upstream's pre_gain argument. Not a voicing knob — changing it
        /// changes both where the limiter engages and the static output level,
        /// because daisysp::Limiter multiplies by it twice.
        static constexpr float kPreGain = 0.7f;

        void Init()
        {
            limiter_[0].Init();
            limiter_[1].Init();
        }

        /// One stereo frame, in place.
        ///
        /// Upstream calls ProcessBlock() once per 4-frame block; its loop body
        /// is per-sample with the peak follower carried across calls, so
        /// stepping it a frame at a time produces the identical sequence and
        /// lets this sit in a per-frame render callback unchanged.
        inline void Process(float &l, float &r)
        {
#if COIL_OUTPUT_LIMITER
            limiter_[0].ProcessBlock(&l, 1, kPreGain);
            limiter_[1].ProcessBlock(&r, 1, kPreGain);
#else
            l = daisysp::SoftClip(l);
            r = daisysp::SoftClip(r);
#endif
        }

    private:
        daisysp::Limiter limiter_[2];
};

}
}

#endif
