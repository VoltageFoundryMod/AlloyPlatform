#pragma once

#include <I2S.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// AudioDriver — block-based I2S output, replacing Mozzi (M63a).
//
// Mozzi supplied three things: the I2S DAC output, a per-sample updateAudio()
// callback, and a control-rate updateControl() tick.  This replaces all three
// with a thin layer over the arduino-pico I2S class — which is what Mozzi's own
// RP2040 backend wrapped anyway (setBCLK / setBuffers / begin / write16).
//
// Why replace it at all (see references/Alloy-Platform-Design.md §2):
//   1. Mozzi is per-sample only.  The platform hosts engines that want blocks —
//      DaisySP has block APIs and Audrey's output limiter is block-only.
//   2. MOZZI_AUDIO_RATE leaked into common/include/params.h and SynthEngine.h,
//      which blocks making the sample rate a per-module property.
//
// Pacing:  gate on availableForWrite(), then render and queue one block.
//
// NOTE: only I2S::write16()/write32() block.  The buffer form,
// I2S::write(const uint8_t*, size_t), forwards to AudioBufferManager::write()
// with sync=false — it returns short (or zero) when the DMA buffers are full
// and does NOT wait.  Relying on it to pace the loop makes the renderer
// free-run and silently drop most of what it produces.  Checking for space
// first is the same shape as Mozzi's canBufferAudioOutput() gate.
//
// Control ticks are derived by counting frames actually queued, which keeps the
// control callback in thread context.  That matters: it does serial and USB
// work and must not run from an interrupt.
//
// A later refinement (M63b, once audio moves to Core 1) is to drive this from
// i2s.onTransmit() instead of polling.  Deliberately not done here — that
// callback runs in interrupt context, and step M63a changes one thing only.
// ---------------------------------------------------------------------------

class AudioDriver
{
  public:
    /// Frames rendered per block.  32 frames ≈ 977 µs at 32768 Hz, and divides
    /// evenly into the 256-frame control period (32768 / 128).
    static constexpr size_t kBlockFrames = 32;

    /// DMA buffers of kBlockFrames each.  4 × 32 = 128 frames ≈ 3.9 ms of
    /// buffering at 32768 Hz — enough to ride out a long control tick without
    /// adding audible latency to a synth voice.
    static constexpr size_t kNumBuffers = 4;

    /// Renders one stereo frame.  Values are int32 ±32512 (the existing
    /// signal-path convention), converted to the wire format by the driver.
    using RenderFrameFn = void (*)(int32_t *outL, int32_t *outR);

    /// Called once per controlRate ticks, in thread context.
    using ControlTickFn = void (*)();

    /**
     * Claims the I2S peripheral and starts the bit clock.
     *
     * @param sampleRate  frames/sec — arbitrary, no power-of-two constraint
     * @param controlRate control ticks/sec (must divide sampleRate evenly)
     * @param pinBCK      bit clock; word select is implicitly pinBCK + 1
     * @param pinData     serial data
     * Returns false if the I2S peripheral could not be started.
     */
    bool begin(uint32_t      sampleRate,
               uint32_t      controlRate,
               uint8_t       pinBCK,
               uint8_t       pinData,
               RenderFrameFn render,
               ControlTickFn control)
    {
        _render        = render;
        _control       = control;
        _framesPerTick = sampleRate / controlRate;
        _frameCounter  = 0;
        // Wall-clock time one block represents.  Rendering must finish inside
        // this or the DMA runs dry — the block-level equivalent of the old
        // per-sample budget, and with far better timer resolution: 977 µs at
        // 32768 Hz versus 30 µs, against a time_us_32() granularity of 1 µs.
        _blockPeriodUs = (kBlockFrames * 1000000u) / sampleRate;

        _i2s.setBCLK(pinBCK);
        _i2s.setDATA(pinData);
        // 32-bit frames carrying left-aligned 16-bit data.  Matches what the
        // PCM5102A expects in I2S mode and lets us write whole blocks with one
        // call instead of a write16() per frame.
        _i2s.setBitsPerSample(32);
        // Third argument is the sample the DMA emits on underflow.  Left at 1
        // rather than 0 for the same reason as kSilenceSample below.
        _i2s.setBuffers(kNumBuffers, kBlockFrames * 2u, kSilenceSample);
        _started = _i2s.begin(sampleRate);
        return _started;
    }

    /// False when begin() failed — the PIO state machine never started, so
    /// there is no bit clock on BCK/WS at all and the DAC sees nothing.
    bool started() const { return _started; }

    /**
     * Renders and queues one block if the DMA has room for all of it;
     * otherwise returns immediately so the caller can try again.
     *
     * Runs the control callback inline whenever a control period elapses —
     * always on a block boundary, so the control rate is exact as long as
     * controlRate divides sampleRate.
     */
    void __attribute__((always_inline)) pump()
    {
        // availableForWrite() reports free space in bytes across all empty DMA
        // buffers.  Waiting for a whole block keeps the render loop aligned to
        // block boundaries, so the control tick below stays exact.
        if(!_started || _i2s.availableForWrite() < (int)sizeof(_block))
            return;

        const uint32_t t0 = time_us_32();

        for(size_t i = 0; i < kBlockFrames; ++i)
        {
            int32_t l = 0, r = 0;
            _render(&l, &r);
            _block[i * 2u]      = toWire(l);
            _block[i * 2u + 1u] = toWire(r);
        }

        _i2s.write((const uint8_t *)_block, sizeof(_block));

        // Two timer reads per block (~1000/s) — negligible, so this is not
        // compiled out.  The control tick below is excluded deliberately: it is
        // not part of the audio budget and runs only 1 block in 8.
        _lastBlockUs = time_us_32() - t0;
        if(_lastBlockUs > _blockPeriodUs)
            _overruns++;

        // Ground truth for an audible dropout: the DMA actually ran dry and
        // emitted silence.  A block merely running long is not audible as long
        // as the buffers cover it, so this is the counter worth watching —
        // _overruns is only a headroom warning.
        if(_i2s.getUnderflow())
            _underflows++;

        _frameCounter += kBlockFrames;
        if(_frameCounter >= _framesPerTick)
        {
            _frameCounter -= _framesPerTick;
            _control();
        }
    }

    /// True when the DMA ran dry since the last call.
    bool underflowed() { return _i2s.getUnderflow(); }

    /// Microseconds spent rendering and queueing the most recent block.
    uint32_t lastBlockUs() const { return _lastBlockUs; }

    /// Wall-clock microseconds one block represents — the budget lastBlockUs()
    /// is measured against.
    uint32_t blockPeriodUs() const { return _blockPeriodUs; }

    /// Blocks that took longer than blockPeriodUs() to produce.  A warning
    /// about headroom, not necessarily an audible fault.
    uint32_t overruns() const { return _overruns; }

    /// Times the DMA ran dry and emitted the silence sample — an actual,
    /// audible dropout.  This is the number that matters.
    uint32_t underflows() const { return _underflows; }

    /// Clears both counts — call once after start-up so DMA/PIO init costs are
    /// not charged against steady-state performance.
    void resetOverruns()
    {
        _overruns   = 0;
        _underflows = 0;
    }

  private:
    // Emitted by the DMA on underflow rather than a hard zero.  Some PCM510xA
    // parts read a run of exact zeros as "no signal" and power down the
    // analogue stage, clicking when output resumes — observed on the PCM5100A
    // by the pico-audio project, and ours is the same family.
    //
    // M63a bring-up: tested on our PCM5102A and **no click was audible** when
    // triggering a note out of digital silence.  So the per-sample variant of
    // this workaround (substituting 1 for every zero in toWire()) was dropped —
    // it put a 1 LSB step on every zero crossing of a live signal, a small
    // crossover nonlinearity for no measured benefit.  Kept only here, on the
    // underflow path, where it costs nothing and there is no signal to distort.
    static constexpr int32_t kSilenceSample = 1;

    /// int32 ±32512 → 16-bit sample left-aligned in a 32-bit I2S frame.
    static inline int32_t toWire(int32_t s)
    {
        // Defensive clamp: the signal path is nominally ±32512, but effect
        // sends and the reverb return can overshoot on transients, and wrapping
        // a sample sounds like a gunshot.
        if(s > 32767)
            s = 32767;
        else if(s < -32768)
            s = -32768;
        return s << 16;
    }

    I2S _i2s{OUTPUT};

    RenderFrameFn _render  = nullptr;
    ControlTickFn _control = nullptr;

    bool     _started       = false;
    uint32_t _framesPerTick = 1;
    uint32_t _frameCounter  = 0;
    uint32_t _blockPeriodUs = 1;
    uint32_t _lastBlockUs   = 0;
    uint32_t _overruns      = 0;
    uint32_t _underflows    = 0;

    int32_t _block[kBlockFrames * 2u] = {};
};
