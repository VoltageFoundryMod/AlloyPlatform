#pragma once

#include <stdint.h>

/**
 * SpaceEngine — mid-side stereo width processor (Milestone 13).
 *
 * Collapses or expands the stereo field using the classic mid-side identity:
 *
 *   mid  = (L + R) / 2      — centre image (mono-compatible)
 *   side = (L - R) / 2      — stereo difference
 *
 *   outL = mid + side * width
 *   outR = mid - side * width
 *
 * width 0.0  → mono:        both channels equal (L+R)/2
 * width 0.5  → half-width:  narrowed stereo field
 * width 1.0  → identity:    outL=inL, outR=inR (no processing)
 * width 1.5  → hyper-wide:  side amplified 1.5× — stronger L/R independence
 * width 2.0  → maximum:     side doubled, L and R are anti-correlated
 *
 * At width > 1.0 the output can exceed ±32512; output is clamped to prevent
 * downstream clipping in StereoOutput::from16Bit().
 *
 * Stateless — float arithmetic on M33 FPU; no state or init needed.
 * ~8 FPU operations (+ 2 clamping branches at width ≠ 1.0).
 *
 * With AlloyFlux's stereo topology (v1→L, v2→R, independent chorus LFOs),
 * width=2.0 maximally exaggerates the detune and chorus LFO independence
 * into a dramatically wide stereo image.
 */
class SpaceEngine
{
  public:
    /**
     * Process one stereo sample pair.
     *
     * @param inL   left input  sample (±32512 Mozzi range)
     * @param inR   right input sample (±32512 Mozzi range)
     * @param width stereo width 0.0–2.0 (1.0 = identity, 2.0 = maximum hyper-wide)
     * @param outL  processed left  sample (clamped to ±32512)
     * @param outR  processed right sample (clamped to ±32512)
     */
    static void __attribute__((always_inline))
    process(int32_t inL, int32_t inR, float width, int32_t *outL, int32_t *outR)
    {
        const int32_t mid  = (inL + inR) >> 1;
        const int32_t side = (inL - inR) >> 1;
        // Float multiply — eliminates the 256-step integer quantization that
        // produces zipper artifacts when sSpace changes (same pattern as VCA fix).
        // M33 FPU: two float muls cost the same as the previous integer path.
        const float fSide = (float)side;
        int32_t     L     = (int32_t)((float)mid + fSide * width);
        int32_t     R     = (int32_t)((float)mid - fSide * width);
        // Clamp: widths > 1.0 can push output beyond ±32512
        if(L > 32512)
            L = 32512;
        else if(L < -32512)
            L = -32512;
        if(R > 32512)
            R = 32512;
        else if(R < -32512)
            R = -32512;
        *outL = L;
        *outR = R;
    }
};
