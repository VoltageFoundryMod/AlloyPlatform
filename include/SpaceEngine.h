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
 * Stateless — all arithmetic is integer; no state or init needed.
 * ~8 integer operations (+ 2 clamping branches at width ≠ 1.0).
 *
 * With AlloyFlux's stereo topology (v1→L, v2→R, independent chorus LFOs),
 * width=2.0 maximally exaggerates the detune and chorus LFO independence
 * into a dramatically wide stereo image.
 */
class SpaceEngine {
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
    static void process(int32_t inL, int32_t inR, float width,
                        int32_t *outL, int32_t *outR) {
        const int32_t mid = (inL + inR) >> 1;
        const int32_t side = (inL - inR) >> 1;
        const int32_t iW = (int32_t)(width * 256.0f);
        int32_t L = mid + ((side * iW) >> 8);
        int32_t R = mid - ((side * iW) >> 8);
        // Clamp: widths > 1.0 can push output beyond ±32512
        if (L > 32512)
            L = 32512;
        else if (L < -32512)
            L = -32512;
        if (R > 32512)
            R = 32512;
        else if (R < -32512)
            R = -32512;
        *outL = L;
        *outR = R;
    }
};
