#pragma once

#include "ReverbEngine.h"
#include <stdint.h>
#include <string.h>
#ifdef ARDUINO
#include <hardware/timer.h> // time_us_32() on RP2350
#endif

/**
 * DattorroReverb — Plate reverb based on Jon Dattorro's 1997 algorithm.
 *
 * Reference: "Effect Design, Part 1: Reverberator and Other Filters"
 *            J. Audio Eng. Soc., Vol. 45, No. 9, September 1997.
 *
 * TOPOLOGY
 * --------
 *                        INPUT (stereo → mono sum)
 *                              │
 *                      [pre-delay 30ms]
 *                              │
 *                      [pre-LPF bandwidth]
 *                              │
 *         ┌──── APF1 ─── APF2 ─── APF3 ─── APF4 ────┐
 *         │          (input diffuser)                  │
 *         ▼                                            ▼
 * ┌── TANK LEFT ──────────────────────────────────────────────┐
 * │  APF5(lfo1) → D5 → [LPF damp] → [HPF bass-cut]           │◄─ cross-inject right
 * │                     → APF6(static) → D6                   │
 * └───────────────────────────────────────────────────────────┘
 * ┌── TANK RIGHT ─────────────────────────────────────────────┐
 * │  APF7(lfo3) → D7 → [LPF damp] → [HPF bass-cut]           │◄─ cross-inject left
 * │                     → APF8(static) → D8                   │
 * └───────────────────────────────────────────────────────────┘
 *         │                            │
 *    7 output taps L              7 output taps R
 *         → [DC block L]               → [DC block R]
 *
 * Improvements from Plateau/Valley VCV Rack study (M26b revision):
 *   • 2 bipolar LFOs at 0.10/0.12 Hz on APF5/APF7 only (original Dattorro
 *     modulates only the entry APFs; bipolar removes net pitch drift).
 *   • Output tap positions scaled from 29761→32768 Hz for correct spectral char.
 *   • Output DC blockers (~10 Hz HP) — prevents tail DC offset at high decay.
 *   • Tank HP filter (~30 Hz) after each LPF — arrests bass accumulation.
 *   • Seventh output tap per channel completing Dattorro Table 1.
 *   • Freeze mode — decay → 1.0, new input gated out (M41).
 *   • Modulation speed and depth parameters (M40).
 *
 * All delay lines are float (avoids Q15 quantization noise in long reverb tails).
 * Total buffer RAM: ~108 KB (static allocation, scaled from 29761→32768 Hz).
 *
 * Runs on Core 1 only. No shared mutable state with Core 0. Milestone 26b.
 */

// ---------------------------------------------------------------------------
// Delay-line helper — int16_t circular buffer with float I/O (Q15 scale)
// ---------------------------------------------------------------------------

template <uint32_t N>
class DLine
{
  public:
    DLine() : _w(0) { memset(_buf, 0, sizeof(_buf)); }

    void write(float v)
    {
        _buf[_w] = v;
        if(++_w >= N)
            _w = 0;
    }

    float read(uint32_t offset) const
    {
        // offset=0 → most-recently written sample
        uint32_t idx = (_w + N - 1 - offset) % N;
        return _buf[idx];
    }

    // Linear interpolation for fractional offsets (LFO modulation)
    float readFrac(float offset) const
    {
        const uint32_t i0 = (uint32_t)offset;
        const float    fr = offset - (float)i0;
        return read(i0) + fr * (read(i0 + 1) - read(i0));
    }

    void clear()
    {
        memset(_buf, 0, sizeof(_buf));
        _w = 0;
    }

  private:
    float _buf
        [N]; // float storage — avoids Q15 quantization noise in reverb tails
    uint32_t _w;
};

// ---------------------------------------------------------------------------
// All-pass filter built on top of a delay line
// ---------------------------------------------------------------------------

template <uint32_t N>
class APF
{
  public:
    APF() : _coeff(0.75f) {}

    void setCoeff(float c) { _coeff = c; }

    // Standard Schroeder/Moorer all-pass structure.
    float process(float in)
    {
        const float delayed = _dline.read(N - 1);
        const float fwd     = in - _coeff * delayed;
        _dline.write(fwd);
        return delayed + _coeff * fwd;
    }

    // Modulated read: read pointer varies ±depth samples around the
    // nominal output tap (used for the two tank APFs to create pitch shimmer).
    float processModRead(float in, float modOffset)
    {
        const float delayed = _dline.readFrac(modOffset);
        const float fwd     = in - _coeff * delayed;
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

struct OnePole
{
    float _z = 0.0f;
    // coeff: 1.0 = transparent, ~0.0 = maximum damping
    inline float process(float in, float coeff)
    {
        _z = coeff * in + (1.0f - coeff) * _z;
        return _z;
    }
    void clear() { _z = 0.0f; }
};

// ---------------------------------------------------------------------------
// One-pole high-pass filter — y[n] = R * (y[n-1] + x[n] - x[n-1])
// R = 1 - 2π*fc/fs.  Default: R ≈ 0.99616 → fc ≈ 20 Hz at 32768 Hz.
// ---------------------------------------------------------------------------

struct OnePoleHP
{
    float        _x = 0.0f, _y = 0.0f, _R = 0.99616f;
    void         setR(float R) { _R = R; }
    inline float process(float in)
    {
        _y = _R * (_y + in - _x);
        _x = in;
        return _y;
    }
    void clear() { _x = _y = 0.0f; }
};

// ---------------------------------------------------------------------------
// Dattorro plate — delay line sizes scaled from 29761 → 32768 Hz
// ---------------------------------------------------------------------------

// Input diffuser APF lengths (samples at 32768 Hz)
static constexpr uint32_t kAP1 = 156;
static constexpr uint32_t kAP2 = 118;
static constexpr uint32_t kAP3 = 417;
static constexpr uint32_t kAP4 = 305;

// Tank APF + delay lengths (samples at 32768 Hz)
static constexpr uint32_t kAP5 = 740;  // left  — modulated (nominal)
static constexpr uint32_t kD5  = 4903; // left  — long delay
static constexpr uint32_t kAP6 = 1982; // left  — modulated (nominal)
static constexpr uint32_t kD6  = 4096; // left  — medium delay

static constexpr uint32_t kAP7 = 1000; // right — modulated (nominal)
static constexpr uint32_t kD7  = 4643; // right — long delay
static constexpr uint32_t kAP8 = 2924; // right — modulated (nominal)
static constexpr uint32_t kD8  = 3483; // right — medium delay

// Modulated APF buffers: nominal + LFO depth (8) + guard (4)
// All 4 tank APFs are modulated for richer tail diffusion.
static constexpr uint32_t kAP5_BUF = kAP5 + 12; //  752
static constexpr uint32_t kAP6_BUF = kAP6 + 12; // 1994
static constexpr uint32_t kAP7_BUF = kAP7 + 12; // 1012
static constexpr uint32_t kAP8_BUF = kAP8 + 12; // 2936

// LFO parameters — 4 independent triangle oscillators, Plateau/Valley frequencies.
// ~10× slower than the original 1 Hz; longer sweep period produces richer diffusion
// without audible pitch wobble.  90° phase offsets cover the full cycle immediately.
static constexpr float kLfoDepth = 8.0f; // ±8 samples nominal excursion
static constexpr float kLfoRate1 = 0.100f / 32768.0f; // 0.10 Hz — ~10 s period
static constexpr float kLfoRate2 = 0.150f / 32768.0f; // 0.15 Hz — ~6.7 s period
static constexpr float kLfoRate3 = 0.120f / 32768.0f; // 0.12 Hz — ~8.3 s period
static constexpr float kLfoRate4 = 0.180f / 32768.0f; // 0.18 Hz — ~5.6 s period

class DattorroReverb final : public ReverbEngine
{
  public:
    DattorroReverb()
    : _preDelayPos(0),
      _decay(0.75f),
      _bandwidth(0.9995f),
      _damping(0.0005f),
      _modSpeed(1.0f),
      _modDepth(1.0f),
      _frozen(false),
      _lfoPhase1(0.0f),
      _lfoPhase2(0.25f),
      _lfoPhase3(0.5f),
      _lfoPhase4(0.75f),
      _lastProcessUs(0)
    {
        memset(_preDelay, 0, sizeof(_preDelay));
        // Tank HP: ~30 Hz removes bass accumulation in long tails.
        // R = 1 - 2π*30/32768 ≈ 0.99425
        _tankHPL.setR(0.99425f);
        _tankHPR.setR(0.99425f);
        // Output DC block: ~10 Hz removes true DC offset at high decay.
        // R = 1 - 2π*10/32768 ≈ 0.99808
        _dcBlockL.setR(0.99808f);
        _dcBlockR.setR(0.99808f);
    }

    // -----------------------------------------------------------------------
    // setParams() — call at control rate (128 Hz). Safe on Core 1.
    // size:    0.0–1.0 → decay 0.50–0.97, diffuser coeff 0.625–0.725
    // damping: 0.0–1.0 → tank LPF coeff 0.9995 (bright) … 0.005 (dark)
    // -----------------------------------------------------------------------
    void setParams(float size, float damping) override
    {
        _decay = 0.50f + size * 0.47f;

        const float d1 = 0.75f;
        const float d2 = 0.625f + size * 0.10f;
        _apf1.setCoeff(d1);
        _apf2.setCoeff(d2);
        _apf3.setCoeff(d1);
        _apf4.setCoeff(d2);

        _apf5.setCoeff(0.70f);
        _apf6.setCoeff(0.70f);
        _apf7.setCoeff(0.70f);
        _apf8.setCoeff(0.70f);

        _bandwidth = 0.9990f + size * 0.0009f;
        _damping   = 0.9995f - damping * 0.9495f;
        if(_damping < 0.005f)
            _damping = 0.005f;
    }

    // -----------------------------------------------------------------------
    // setModulation() — M40: LFO speed and depth at control rate.
    // speed: 0.1 (glacial, barely moving) … 4.0 (fast shimmer)
    // depth: 0.0 (static APFs, no pitch variation) … 1.0 (full ±8 sample swing)
    // -----------------------------------------------------------------------
    void setModulation(float speed, float depth) override
    {
        _modSpeed = speed;
        _modDepth = depth;
    }

    // -----------------------------------------------------------------------
    // freeze() — M41: hold current reverb tail indefinitely.
    // frozen=true : decay clamped to 1.0; new input gated out.
    // frozen=false: normal decay + input resumed (tail is at full level,
    //               so re-introducing input mixes in cleanly at next onset).
    // -----------------------------------------------------------------------
    void freeze(bool frozen) override { _frozen = frozen; }

    // -----------------------------------------------------------------------
    // process() — Core 1 hot path, one sample per call.
    // Input/output: float ±1.0. Wet-only out; dry+wet mix done by Core 0.
    // Placed in SRAM on Arduino/Pico builds to eliminate XIP cache-miss jitter
    // that would otherwise make Core 1's execution time variable and reintroduce
    // the variable-comb shimmer the ring-buffer transport was designed to prevent.
    // -----------------------------------------------------------------------
#ifdef ARDUINO
    __attribute__((section(".time_critical.DattorroProcess")))
#endif
    void process(float inL, float inR, float *outL, float *outR) override
    {
        // --- Pre-delay (30ms = ~983 samples at 32768 Hz) ---
        const uint32_t preLen   = 983;
        _preDelay[_preDelayPos] = (inL + inR) * 0.5f;
        const uint32_t readPos  = (_preDelayPos + 1024 - preLen) % 1024;
        const float    preOut   = _preDelay[readPos];
        if(++_preDelayPos >= 1024)
            _preDelayPos = 0;

        // --- Bandwidth pre-filter ---
        const float filtered = _bwFilter.process(preOut, _bandwidth);

        // --- Input diffuser (4 all-pass sections) ---
        const float diff = _apf4.process(
            _apf3.process(_apf2.process(_apf1.process(filtered))));

        // --- Freeze: gate input and clamp decay for infinite sustain ---
        const float activeInput = _frozen ? 0.0f : diff;
        const float activeDecay = _frozen ? 1.0f : _decay;

        // --- 4 independent triangle LFOs (Plateau frequencies, 90° apart) ---
        // Rates scaled by _modSpeed; amplitude scaled by _modDepth.
        // Phase advances by wall-clock elapsed time so LFO rate stays accurate
        // regardless of irregular Core 1 call spacing.
        const float depth = _modDepth * kLfoDepth;
        {
#ifdef ARDUINO
            const uint32_t nowUs     = time_us_32();
            const uint32_t elapsedUs = nowUs - _lastProcessUs;
            _lastProcessUs           = nowUs;
            // Cap elapsed to 1 ms to avoid a jump on first call or after reset.
            const float dtSec = (elapsedUs > 1000u ? 1000u : elapsedUs) * 1e-6f;
#else
            // VCV / non-Arduino: constant rate at nominal sample period.
            constexpr float dtSec = 1.0f / 32768.0f;
#endif
            const float tick = _modSpeed * 32768.0f * dtSec;
            _lfoPhase1 += kLfoRate1 * tick;
            if(_lfoPhase1 >= 1.0f)
                _lfoPhase1 -= 1.0f;
            _lfoPhase2 += kLfoRate2 * tick;
            if(_lfoPhase2 >= 1.0f)
                _lfoPhase2 -= 1.0f;
            _lfoPhase3 += kLfoRate3 * tick;
            if(_lfoPhase3 >= 1.0f)
                _lfoPhase3 -= 1.0f;
            _lfoPhase4 += kLfoRate4 * tick;
            if(_lfoPhase4 >= 1.0f)
                _lfoPhase4 -= 1.0f;
        }

        // Triangle: ramp 0→depth for phase 0..0.5, depth→0 for 0.5..1.0
        const float lfo1 = _lfoPhase1 < 0.5f
                               ? _lfoPhase1 * 2.0f * depth
                               : (1.0f - _lfoPhase1) * 2.0f * depth;
        const float lfo2 = _lfoPhase2 < 0.5f
                               ? _lfoPhase2 * 2.0f * depth
                               : (1.0f - _lfoPhase2) * 2.0f * depth;
        const float lfo3 = _lfoPhase3 < 0.5f
                               ? _lfoPhase3 * 2.0f * depth
                               : (1.0f - _lfoPhase3) * 2.0f * depth;
        const float lfo4 = _lfoPhase4 < 0.5f
                               ? _lfoPhase4 * 2.0f * depth
                               : (1.0f - _lfoPhase4) * 2.0f * depth;

        // --- Cross-inject: read previous-sample feeds before touching either tank ---
        // One-sample latency is inaudible in a reverb tail of 0.5–5 s.
        const float feedForLeft  = _tankDelayMR.read(kD8 - 1) * activeDecay;
        const float feedForRight = _tankDelayML.read(kD6 - 1) * activeDecay;

        // --- Left tank ---
        const float inTankL = activeInput + feedForLeft;
        const float ap5out  = _apf5.processModRead(inTankL, (kAP5 - 1) + lfo1);
        _tankDelayL.write(ap5out);
        const float dampL = _dampL.process(_tankDelayL.read(kD5 - 1), _damping);
        const float hp6in = _tankHPL.process(dampL); // bass-cut before APF6
        const float ap6out = _apf6.processModRead(hp6in, (kAP6 - 1) + lfo2);
        _tankDelayML.write(ap6out);

        // --- Right tank ---
        const float inTankR = activeInput + feedForRight;
        const float ap7out  = _apf7.processModRead(inTankR, (kAP7 - 1) + lfo3);
        _tankDelayR.write(ap7out);
        const float dampR = _dampR.process(_tankDelayR.read(kD7 - 1), _damping);
        const float hp8in = _tankHPR.process(dampR); // bass-cut before APF8
        const float ap8out = _apf8.processModRead(hp8in, (kAP8 - 1) + lfo4);
        _tankDelayMR.write(ap8out);

        // --- Output taps (Dattorro Table 1, 7 taps per channel) ---
        // Tap positions scaled from 29761 Hz → 32768 Hz (×1.10107) so that the
        // comb/all-pass resonances match the original algorithm's spectral character.
        // Left — primary from right tank (D7/D8), cross-taps from left (D5/D6):
        float oL = _tankDelayR.read(293) + _tankDelayR.read(3274)
                   - _tankDelayMR.read(2107) + _tankDelayL.read(2198)
                   - _tankDelayML.read(2191) - _tankDelayR.read(206)
                   - _tankDelayMR.read(1174);

        // Right — primary from left tank (D5/D6), cross-taps from right (D7/D8):
        float oR = _tankDelayL.read(389) + _tankDelayL.read(3994)
                   - _tankDelayML.read(1352) + _tankDelayR.read(2943)
                   - _tankDelayMR.read(2325) - _tankDelayL.read(306)
                   - _tankDelayML.read(1174);

        // Scale (1/7 taps), gentle output HF roll-off, then DC block.
        // coeff 0.61 → fc ≈ (0.61/(1-0.61))×(Fs/2π) ≈ 8.1 kHz: smooths the
        // high-frequency density of the reverb tail without killing air.
        constexpr float kOutLpf = 0.75f;
        *outL = _dcBlockL.process(_outLpfL.process(oL * 0.143f, kOutLpf));
        *outR = _dcBlockR.process(_outLpfR.process(oR * 0.143f, kOutLpf));
    }

    void reset() override
    {
        _apf1.clear();
        _apf2.clear();
        _apf3.clear();
        _apf4.clear();
        _apf5.clear();
        _apf6.clear();
        _apf7.clear();
        _apf8.clear();
        _tankDelayL.clear();
        _tankDelayML.clear();
        _tankDelayR.clear();
        _tankDelayMR.clear();
        _bwFilter.clear();
        _dampL.clear();
        _dampR.clear();
        _tankHPL.clear();
        _tankHPR.clear();
        _dcBlockL.clear();
        _dcBlockR.clear();
        _outLpfL.clear();
        _outLpfR.clear();
        _preDelayPos = 0;
        memset(_preDelay, 0, sizeof(_preDelay));
        _lfoPhase1     = 0.0f;
        _lfoPhase2     = 0.25f;
        _lfoPhase3     = 0.5f;
        _lfoPhase4     = 0.75f;
        _frozen        = false;
        _lastProcessUs = 0;
    }

  private:
    // Input diffuser (static APFs)
    APF<kAP1> _apf1;
    APF<kAP2> _apf2;
    APF<kAP3> _apf3;
    APF<kAP4> _apf4;

    // Tank left — all APFs modulated; _BUF variants add LFO headroom
    APF<kAP5_BUF> _apf5;
    DLine<kD5>    _tankDelayL;
    DLine<kD6>    _tankDelayML;
    APF<kAP6_BUF> _apf6;

    // Tank right
    APF<kAP7_BUF> _apf7;
    DLine<kD7>    _tankDelayR;
    DLine<kD8>    _tankDelayMR;
    APF<kAP8_BUF> _apf8;

    // Pre-delay (fixed 1024 samples; 30ms at 32768 Hz)
    float    _preDelay[1024];
    uint32_t _preDelayPos;

    // Filters
    OnePole   _bwFilter; // input bandwidth LPF
    OnePole   _dampL;    // tank L HF damping LPF
    OnePole   _dampR;    // tank R HF damping LPF
    OnePoleHP _tankHPL;  // tank L bass-cut HP (~30 Hz)
    OnePoleHP _tankHPR;  // tank R bass-cut HP (~30 Hz)
    OnePoleHP _dcBlockL; // output L DC block (~10 Hz)
    OnePoleHP _dcBlockR; // output R DC block (~10 Hz)
    OnePole   _outLpfL; // output L gentle HF soft-roll (fc ≈ 8 kHz at 32768 Hz)
    OnePole   _outLpfR; // output R gentle HF soft-roll

    // Core coefficients
    float _decay;
    float _bandwidth;
    float _damping;

    // Modulation (M40)
    float _modSpeed; // LFO rate multiplier: 1.0 = nominal Plateau rates
    float _modDepth; // LFO amplitude multiplier: 1.0 = nominal ±8 samples

    // Freeze (M41)
    bool _frozen; // true = decay -> 1.0 + new input gated

    // 4 LFO phases — initialised 90° apart to immediately span the full cycle
    float _lfoPhase1, _lfoPhase2, _lfoPhase3, _lfoPhase4;
    // Wall-clock timestamp of the last process() call (µs). Used to advance LFO
    // phases by actual elapsed time rather than a fixed per-call increment, so
    // the LFO rate is stable regardless of irregular Core 1 call spacing.
    uint32_t _lastProcessUs;
};
