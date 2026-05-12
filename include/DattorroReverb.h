#pragma once

#include "ReverbEngine.h"
#include <stdint.h>
#include <string.h>

/**
 * DattorroReverb — Plate reverb based on Jon Dattorro's 1997 algorithm.
 *
 * Reference: "Effect Design, Part 1: Reverberator and Other Filters"
 *            J. Audio Eng. Soc., Vol. 45, No. 9, September 1997.
 *
 * TOPOLOGY
 * --------
 *                           INPUT
 *                             │
 *                     [pre-delay 30ms]
 *                             │
 *                     [pre-LPF bandwidth]
 *                             │
 *          ┌──── APF1─── APF2 ─── APF3 ─── APF4 ────┐
 *          │         (input diffuser)                  │
 *          ▼                                           ▼
 *  ┌── TANK LEFT ──────────────────────────────────────┐
 *  │  APF5(mod) → delay → [LPF damping] → APF6 → delay │◄─ cross-inject right tank
 *  └───────────────────────────────────────────────────┘
 *  ┌── TANK RIGHT ─────────────────────────────────────┐
 *  │  APF7(mod) → delay → [LPF damping] → APF8 → delay │◄─ cross-inject left tank
 *  └───────────────────────────────────────────────────┘
 *                    │                 │
 *              tap left            tap right
 *                (sum of           (sum of
 *                6 taps)           6 taps)
 *
 * All delay lines are stored as float for full numerical precision in reverb tails.
 * Total buffer RAM: ~107 KB (12 delay lines + pre-delay, scaled from 29.761→32768 Hz).
 *
 * PARAMETERS
 * ----------
 *   size    : 0.0 (small room) … 1.0 (large plate)
 *             Maps to tank feedback decay (0.50—0.97) and APF5/7/6/8 coeff.
 *   damping : 0.0 (bright) … 1.0 (very dark)
 *             Maps to tank low-pass coefficient.
 *
 * Core 1 usage: called from loop1() at one sample per iteration.
 * No blocking, no allocation, no trig (LFO is triangle accumulator).
 * ISR-safe: runs entirely on Core 1 — no shared mutable state with Core 0.
 *
 * Milestone 26b.
 */

// ---------------------------------------------------------------------------
// Delay-line helper — int16_t circular buffer with float I/O (Q15 scale)
// ---------------------------------------------------------------------------

template <uint32_t N>
class DLine {
  public:
    DLine() : _w(0) { memset(_buf, 0, sizeof(_buf)); }

    void write(float v) {
        _buf[_w] = v;
        if (++_w >= N) _w = 0;
    }

    float read(uint32_t offset) const {
        // offset=0 → most-recently written sample
        uint32_t idx = (_w + N - 1 - offset) % N;
        return _buf[idx];
    }

    // Linear interpolation for fractional offsets (LFO modulation)
    float readFrac(float offset) const {
        const uint32_t i0 = (uint32_t)offset;
        const float    fr = offset - (float)i0;
        return read(i0) + fr * (read(i0 + 1) - read(i0));
    }

    void clear() { memset(_buf, 0, sizeof(_buf)); _w = 0; }

  private:
    float    _buf[N]; // float storage — avoids Q15 quantization noise in reverb tails
    uint32_t _w;
};

// ---------------------------------------------------------------------------
// All-pass filter built on top of a delay line
// ---------------------------------------------------------------------------

template <uint32_t N>
class APF {
  public:
    APF() : _coeff(0.75f) {}

    void setCoeff(float c) { _coeff = c; }

    // Standard Schroeder/Moorer all-pass structure.
    float process(float in) {
        const float delayed = _dline.read(N - 1);
        const float fwd = in - _coeff * delayed;
        _dline.write(fwd);
        return delayed + _coeff * fwd;
    }

    // Modulated read: read pointer varies ±depth samples around the
    // nominal output tap (used for the two tank APFs to create pitch shimmer).
    float processModRead(float in, float modOffset) {
        const float delayed = _dline.readFrac(modOffset);
        const float fwd = in - _coeff * delayed;
        _dline.write(fwd);
        return delayed + _coeff * fwd;
    }

    void clear() { _dline.clear(); }

  private:
    DLine<N> _dline;
    float    _coeff;
};

// ---------------------------------------------------------------------------
// One-pole low-pass filter
// ---------------------------------------------------------------------------

struct OnePole {
    float _z = 0.0f;
    // coeff: 1.0=transparent, 0.0=frozen (maximum damping)
    inline float process(float in, float coeff) {
        _z = coeff * in + (1.0f - coeff) * _z;
        return _z;
    }
    void clear() { _z = 0.0f; }
};

// ---------------------------------------------------------------------------
// Dattorro plate — delay line sizes scaled from 29761 → 32768 Hz
// ---------------------------------------------------------------------------

// Input diffuser APF lengths (samples at 32768 Hz)
static constexpr uint32_t kAP1 = 156;
static constexpr uint32_t kAP2 = 118;
static constexpr uint32_t kAP3 = 417;
static constexpr uint32_t kAP4 = 305;

// Tank APF + delay lengths
static constexpr uint32_t kAP5  = 740;   // left  — modulated (nominal)
static constexpr uint32_t kD5   = 4903;  // left  — long delay
static constexpr uint32_t kAP6  = 1982;  // left  — static
static constexpr uint32_t kD6   = 4096;  // left  — medium delay

static constexpr uint32_t kAP7  = 1000;  // right — modulated (nominal)
static constexpr uint32_t kD7   = 4643;  // right — long delay
static constexpr uint32_t kAP8  = 2924;  // right — static
static constexpr uint32_t kD8   = 3483;  // right — medium delay

// Modulation: triangle LFO at ~1 Hz, ±8 samples depth (Dattorro spec)
static constexpr float    kLfoDepth = 8.0f;
static constexpr float    kLfoRate  = 1.0f / 32768.0f; // 1 Hz at 32768 Hz sr

// Modulated APF buffer sizes — must be larger than nominal + LFO depth
static constexpr uint32_t kAP5_BUF = kAP5 + 12; // 752: kAP5(740) + depth(8) + guard(4)
static constexpr uint32_t kAP7_BUF = kAP7 + 12; // 1012: kAP7(1000) + depth(8) + guard(4)

class DattorroReverb final : public ReverbEngine {
  public:
    DattorroReverb()
        : _decay(0.75f), _bandwidth(0.9995f), _damping(0.0005f),
          _preDelayPos(0), _lfoPhaseL(0.0f), _lfoPhaseR(0.5f) {
        memset(_preDelay, 0, sizeof(_preDelay));
    }

    // -----------------------------------------------------------------------
    // setParams() — call at control rate (128 Hz) when parameters change.
    // May call expf / arithmetic — safe on Core 1, never in ISR.
    // size:    0.0–1.0 → decay 0.50–0.97, input diffuser coeff 0.625–0.785
    // damping: 0.0–1.0 → tank LPF coeff 0.9995–0.001 (bright to very dark)
    // -----------------------------------------------------------------------
    void setParams(float size, float damping) override {
        // Decay: map size 0–1 → 0.50–0.97 (exponential feel at the top)
        _decay = 0.50f + size * 0.47f;

        // Input diffuser APF coefficients: Dattorro uses 0.75 and 0.625;
        // we let size vary them gently to open/close the diffusion texture.
        const float d1 = 0.75f;
        const float d2 = 0.625f + size * 0.10f; // 0.625 (small) … 0.725 (large)
        _apf1.setCoeff(d1);
        _apf2.setCoeff(d2);
        _apf3.setCoeff(d1);
        _apf4.setCoeff(d2);

        // Tank modulated APF coefficients: Dattorro uses 0.70 fixed;
        // we keep it fixed — modulation wanders the delay length, not the coeff.
        _apf5.setCoeff(0.70f);
        _apf6.setCoeff(0.70f);
        _apf7.setCoeff(0.70f);
        _apf8.setCoeff(0.70f);

        // Bandwidth: pre-filter before diffuser (higher = brighter input)
        // Map size 0–1 → bandwidth 0.9990–0.9999 (subtle input colour)
        _bandwidth = 0.9990f + size * 0.0009f;

        // Tank LPF damping: map damping 0–1 → coeff 0.9995 (bright) … 0.050 (very dark)
        // Use linear-on-log scale so the knob feels even
        _damping = 0.9995f - damping * 0.9495f;
        if (_damping < 0.005f) _damping = 0.005f;
    }

    // -----------------------------------------------------------------------
    // process() — Core 1 hot path, one sample per call.
    // Input/output: float ±1.0 (dry signal in, wet-only out — Core 0 mixes)
    // -----------------------------------------------------------------------
    void process(float inL, float inR,
                 float *outL, float *outR) override {
        // --- Pre-delay (30ms = ~983 samples at 32768 Hz) ---
        const uint32_t preLen = 983;
        _preDelay[_preDelayPos] = (inL + inR) * 0.5f;
        const uint32_t readPos = (_preDelayPos + 1024 - preLen) % 1024;
        const float preOut = _preDelay[readPos];
        if (++_preDelayPos >= 1024) _preDelayPos = 0;

        // --- Pre-filter (bandwidth LPF) ---
        const float filtered = _bwFilter.process(preOut, _bandwidth);

        // --- Input diffuser (4 all-pass sections) ---
        const float diff = _apf4.process(
                           _apf3.process(
                           _apf2.process(
                           _apf1.process(filtered))));

        // --- LFO triangle oscillators (L at 0.977 Hz, R at 1.153 Hz) ---
        // Two slightly-detuned LFOs reduce metallic colouration in the tails.
        _lfoPhaseL += kLfoRate * 0.977f;
        if (_lfoPhaseL >= 1.0f) _lfoPhaseL -= 1.0f;
        _lfoPhaseR += kLfoRate * 1.153f;
        if (_lfoPhaseR >= 1.0f) _lfoPhaseR -= 1.0f;

        // Triangle: 0→+depth for phase 0..0.5, +depth→0 for 0.5..1.0
        const float lfoL = (_lfoPhaseL < 0.5f)
                         ? (_lfoPhaseL * 2.0f * kLfoDepth)
                         : ((1.0f - _lfoPhaseL) * 2.0f * kLfoDepth);
        const float lfoR = (_lfoPhaseR < 0.5f)
                         ? (_lfoPhaseR * 2.0f * kLfoDepth)
                         : ((1.0f - _lfoPhaseR) * 2.0f * kLfoDepth);

        // --- Tank cross-inject (proper Dattorro cross-coupled topology) ---
        // Capture BOTH cross-feeds from the PREVIOUS sample's medium-delay outputs
        // before processing either tank.  One-sample cross-latency: inaudible at 32768 Hz.
        //   Left  tank input = diffuser + decay × END of right tank's D8
        //   Right tank input = diffuser + decay × END of left  tank's D6
        const float feedForLeft  = _tankDelayMR.read(kD8 - 1) * _decay;
        const float feedForRight = _tankDelayML.read(kD6 - 1) * _decay;

        // --- Left tank ---
        const float inTankL = diff + feedForLeft;
        const float ap5out  = _apf5.processModRead(inTankL, (kAP5 - 1) + lfoL);
        _tankDelayL.write(ap5out);
        const float dampL   = _dampL.process(_tankDelayL.read(kD5 - 1), _damping);
        const float ap6out  = _apf6.process(dampL);
        _tankDelayML.write(ap6out);

        // --- Right tank ---
        const float inTankR = diff + feedForRight;
        const float ap7out  = _apf7.processModRead(inTankR, (kAP7 - 1) + lfoR);
        _tankDelayR.write(ap7out);
        const float dampR   = _dampR.process(_tankDelayR.read(kD7 - 1), _damping);
        const float ap8out  = _apf8.process(dampR);
        _tankDelayMR.write(ap8out);

        // --- Output taps (Dattorro Table 1) ---
        // Left output taps (from right tank + right portion of left tank):
        float oL =  _tankDelayR.read(266)
                  + _tankDelayR.read(2974)
                  - _tankDelayMR.read(1913)
                  + _tankDelayL.read(1996)
                  - _tankDelayML.read(1990)
                  - _tankDelayR.read(187);

        // Right output taps (from left tank + left portion of right tank):
        float oR =  _tankDelayL.read(353)
                  + _tankDelayL.read(3627)
                  - _tankDelayML.read(1228)
                  + _tankDelayR.read(2673)
                  - _tankDelayMR.read(2111)
                  - _tankDelayL.read(278);

        // Scale output to keep ±1.0 range (6 taps, each ≤1.0)
        *outL = oL * 0.167f;
        *outR = oR * 0.167f;
    }

    void reset() override {
        _apf1.clear(); _apf2.clear(); _apf3.clear(); _apf4.clear();
        _apf5.clear(); _apf6.clear(); _apf7.clear(); _apf8.clear();
        _tankDelayL.clear(); _tankDelayML.clear();
        _tankDelayR.clear(); _tankDelayMR.clear();
        _bwFilter.clear(); _dampL.clear(); _dampR.clear();
        _preDelayPos = 0;
        memset(_preDelay, 0, sizeof(_preDelay));
        _lfoPhaseL = 0.0f;
        _lfoPhaseR = 0.5f;
    }

  private:
    // Input diffuser
    APF<kAP1> _apf1;
    APF<kAP2> _apf2;
    APF<kAP3> _apf3;
    APF<kAP4> _apf4;

    // Tank left
    APF<kAP5_BUF> _apf5;  // oversized by 12 to accommodate LFO read excursion
    DLine<kD5> _tankDelayL;
    DLine<kD6> _tankDelayML;
    APF<kAP6>  _apf6;

    // Tank right
    APF<kAP7_BUF> _apf7;  // oversized by 12 to accommodate LFO read excursion
    DLine<kD7> _tankDelayR;
    DLine<kD8> _tankDelayMR;
    APF<kAP8>  _apf8;

    // Pre-delay (fixed 1024 samples; 30ms at 32768 Hz)
    float    _preDelay[1024];
    uint32_t _preDelayPos;

    // Pre-filter and tank damping
    OnePole _bwFilter; // bandwidth (input pre-filter)
    OnePole _dampL;    // tank L HF damping
    OnePole _dampR;    // tank R HF damping

    // Coefficients (set in setParams)
    float _decay;
    float _bandwidth;
    float _damping;

    // LFO phases
    float _lfoPhaseL;
    float _lfoPhaseR;
};
