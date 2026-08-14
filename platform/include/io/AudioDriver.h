#pragma once

#include <I2S.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// AudioDriver — block-based I2S output over the arduino-pico I2S class.
//
// Owns the audio clock for the whole firmware: it renders frames in blocks,
// queues them to the I2S DMA, and counts off control periods as it goes.
// Driven by repeated pump() calls from whichever core owns audio.
//
// Pacing: check availableForWrite(), then render and queue one block.
//
// ⚠ Only I2S::write16()/write32() block.  The buffer form,
// I2S::write(const uint8_t*, size_t), forwards to AudioBufferManager::write()
// with sync=false — it returns short, or zero, when the DMA buffers are full
// and does NOT wait.  Never rely on it to pace the render loop: doing so makes
// the renderer free-run at full CPU speed and silently drop most of what it
// produces.  Always gate on space first.
//
// ⚠ begin() must be called from the same core that calls pump().  The I2S
// library installs its DMA completion handler with irq_set_enabled(DMA_IRQ_0),
// which is per-core, and its buffer bookkeeping is an SPSC pair between that
// handler and the writer.  Starting it on one core and pumping from the other
// puts the two halves on different cores and breaks that assumption.
//
// The control period is published as a counter rather than invoked as a
// callback, so control work runs on the other core and never lands inside the
// audio path.
// ---------------------------------------------------------------------------

class AudioDriver
{
  public:
    /// Frames rendered per block.  32 frames ≈ 667 µs at 48 kHz.
    static constexpr size_t kBlockFrames = 32;

    /// DMA buffers of kBlockFrames each.  4 × 32 = 128 frames ≈ 2.7 ms of
    /// buffering at 48 kHz — enough to ride out a long control tick without
    /// adding audible latency to a synth voice.
    static constexpr size_t kNumBuffers = 4;

    /// Renders one stereo frame.  Values are int32 ±32512 (the existing
    /// signal-path convention), converted to the wire format by the driver.
    using RenderFrameFn = void (*)(int32_t *outL, int32_t *outR);

    /**
     * Claims the I2S peripheral and starts the bit clock.  Call from the core
     * that will pump().
     *
     * @param sampleRate  frames/sec — arbitrary, no power-of-two constraint
     * @param controlRate control ticks/sec.  Need not divide sampleRate evenly
     *                    nor align to kBlockFrames: the tick is driven by a
     *                    phase accumulator, so the average rate is exact and
     *                    the only cost is up to one block of jitter.  At 48 kHz
     *                    / 128 Hz the period is 375 frames — not a multiple of
     *                    32 — and that is fine.
     * @param pinBCK      bit clock; word select is implicitly pinBCK + 1
     * @param pinData     serial data
     * Returns false if the I2S peripheral could not be started.
     */
    bool begin(uint32_t      sampleRate,
               uint32_t      controlRate,
               uint8_t       pinBCK,
               uint8_t       pinData,
               RenderFrameFn render)
    {
        _render        = render;
        _framesPerTick = sampleRate / controlRate;
        _frameCounter  = 0;
        // Wall-clock time one block represents; rendering must finish inside it
        // or the DMA runs dry.  ~667 µs at 48 kHz, which is also why the CPU
        // budget is measured per block: a single frame costs less than
        // time_us_32()'s 1 µs resolution and would always read zero.
        _blockPeriodUs = (kBlockFrames * 1000000u) / sampleRate;

        _i2s.setBCLK(pinBCK);
        _i2s.setDATA(pinData);
        // 32-bit frames carrying left-aligned 16-bit data.  Matches what the
        // PCM5102A expects in I2S mode and lets us write whole blocks with one
        // call instead of a write16() per frame.
        _i2s.setBitsPerSample(32);
        // Third argument is the sample the DMA emits on underflow.
        _i2s.setBuffers(kNumBuffers, kBlockFrames * 2u, kSilenceSample);
        _started = _i2s.begin(sampleRate);
        return _started;
    }

    /**
     * Renders and queues one block if the DMA has room for all of it;
     * otherwise returns immediately so the caller can try again.
     *
     * Bumps controlTicks() whenever a control period elapses.
     */
    void __attribute__((always_inline)) pump()
    {
        // availableForWrite() reports free space in bytes across all empty DMA
        // buffers.
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

        // Timed here rather than around the control tick below: that tick is
        // not part of the audio budget and runs on only a fraction of blocks.
        const uint32_t elapsed = time_us_32() - t0;
        _lastBlockUs           = elapsed;
        if(elapsed > _blockPeriodUs)
            _overruns++;

        // A long block is only a headroom warning; the DMA actually running dry
        // is the audible fault, so this is the counter worth watching.
        if(_i2s.getUnderflow())
            _underflows++;

        _frameCounter += kBlockFrames;
        if(_frameCounter >= _framesPerTick)
        {
            _frameCounter -= _framesPerTick;
            ++_controlTicks;
        }
    }

    /// Control periods elapsed since begin().  Free-running; the control core
    /// watches it for change rather than reading an absolute value, so wrap is
    /// a non-event.  A consumer that falls behind should skip the missed ticks
    /// rather than run several in a row — control work is a rate, not a queue.
    uint32_t controlTicks() const { return _controlTicks; }

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
    // Emitted by the DMA on underflow rather than a hard zero: some PCM510xA
    // parts read a run of exact zeros as "no signal", power down the analogue
    // stage, and click when output resumes.  Not reproducible on our PCM5102A,
    // so it is applied only here and not to rendered samples — substituting on
    // every zero would put a 1 LSB step on each zero crossing of a live signal.
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

    RenderFrameFn _render = nullptr;

    bool     _started       = false;
    uint32_t _framesPerTick = 1;
    uint32_t _frameCounter  = 0;
    uint32_t _blockPeriodUs = 1;

    // volatile: written on the audio core, read on the control core.  Aligned
    // 32-bit, so each is atomic on Cortex-M33 and needs no lock — but without
    // volatile the reader's poll loop hoists the load out and never advances.
    volatile uint32_t _controlTicks = 0;
    volatile uint32_t _lastBlockUs  = 0;
    volatile uint32_t _overruns     = 0;
    volatile uint32_t _underflows   = 0;

    int32_t _block[kBlockFrames * 2u] = {};
};
