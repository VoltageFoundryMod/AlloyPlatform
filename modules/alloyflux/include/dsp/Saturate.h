#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Soft saturation — how a polysynth is supposed to handle a chord.
//
// This module has tried both of the obvious answers and rejected both, for good
// reasons that are recorded where they were rejected:
//
//  - A **hard clamp** is transparent right up to the wall and catastrophic at
//    it. Where it really bites is the delay: DelayEngine clamps its own
//    feedback path, so once the input reaches the rail the loop recirculates
//    clipped content and the result is heard as ringing rather than as
//    distortion.
//  - An **always-on Padé soft clip** was worse in the other direction — 6–7%
//    distortion on clean single-oscillator sines and on reverb tails at
//    s≈0.4–0.6, nowhere near the ceiling. It was removed twice.
//
// The answer neither tried is a curve with a **provably linear region**:
//
//     |x| <= T     y = x                              (identity, not "close")
//     |x| >  T     y = T + (C-T)·v/(1+v),  v = (|x|-T)/(C-T)
//
// Below T there is no arithmetic at all, so the Padé objection cannot apply: a
// signal that stays under the knee is bit-identical to what the hard clamp gave
// it. Above it the curve is continuous in value *and* in slope — dy/dx is
// exactly 1 on both sides of the knee, which is what keeps the knee itself
// inaudible — and it asymptotes to C without ever reaching it. One divide, and
// only when it actually bites.
//
// This is what lets voices sum the way a real polysynth's do. A JUNO or a
// JP-8000 does not attenuate a voice because five more might arrive: each voice
// runs at its own fixed level, the sum is allowed to grow with the chord, and
// the output stage rounds off what comes out the top. Pre-attenuating every
// voice for the worst-case chord instead is what makes a single note weak.
// ---------------------------------------------------------------------------

/// Knee, as a fraction of full scale. Nothing below this is touched at all.
/// 0.80 leaves everything quieter than −1.9 dBFS exactly as it was — which is
/// normal single-voice playing in every mode — and gives the curve room above
/// to round a chord off gently instead of cornering it.
static constexpr float kSatKnee = 0.80f * 32767.0f;

/// Ceiling the curve approaches and never reaches.
static constexpr float kSatCeil = 32767.0f;

/// Soft-saturate one sample. Identity below the knee.
static inline int32_t softSaturate(int32_t x)
{
    const float ax = (float)(x < 0 ? -x : x);
    if(ax <= kSatKnee)
        return x; // untouched, and provably so
    const float v = (ax - kSatKnee) / (kSatCeil - kSatKnee);
    const float y = kSatKnee + (kSatCeil - kSatKnee) * (v / (1.0f + v));
    return (x < 0) ? -(int32_t)y : (int32_t)y;
}
