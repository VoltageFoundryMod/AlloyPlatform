# Reverb Core 1 Debug Notes

- Hardware-only shimmer was tied primarily to Core0↔Core1 reverb transport/concurrency, not just algorithm math.
- Cleanest isolation: `REVERB_FORCE_CORE0=1` removes shimmer but can overrun/crackle under load.
- Current Core1 hardening in `src/main.cpp`: SPSC input queue, queue depth 128, drop-oldest policy with `gRevInDrops` counter, burst dequeue (up to 8), non-blocking wet read fallback to last coherent frame.
- Firmware path applies reverb params on Core1 only; `SynthEngine` firmware build no longer mutates reverb object on Core0.
- Telemetry now prints `revdrops +X/5s` to correlate transport pressure with audible artifacts.
- New tuning (2026-05-26): Core1 reverb worker now processes one frame per pass (no burst drain) to reduce inter-core flash/bus contention spikes that can raise Core0 ISR overruns even when revdrops stay at zero.
- Added Core1 reverb parameter deadbands in `loop1()`: `setParams` only if `|Δsize|` or `|Δdamping|` > 0.0025, `setModulation` only if `|ΔmodSpeed|` or `|ΔmodDepth|` > 0.01 — avoids 128 Hz jitter-driven reconfiguration.
- Shimmer root cause (confirmed 2026-05-26): Core 0 was reading the ring buffer slot using `(gRevOutWriteIdx - kRevReadDelay) & mask` — evaluating Core 1's live write index at ISR time. If Core 1 increments between two ISR ticks, the read slot jumps → variable wet latency → swept comb = shimmer.
- **FINAL FIX**: Core 0 uses independent static `sRevReadIdx` counter, advances exactly +1 per ISR tick. Never derived from Core 1's write index at read time. Re-anchored to `writeIdx - kRevReadDelay` only on reverb enable/re-enable.
- Ring buffer: `kRevOutBufDepth=8`, `kRevReadDelay=3` (90 µs headroom). `gRevOutWriteIdx` pre-seeded to `kRevReadDelay` so silent slots [0..2] cover the initial delay.
- LFO triangle bug fixed: formula used factor 2 → unipolar range `-halfDepth..0` (always pitch-sharp). Correct is factor 4 → bipolar `-halfDepth..+halfDepth`.
- `loop1()` and `DattorroReverb::process()` placed in `.time_critical` SRAM (Arduino builds) to reduce XIP cache-miss latency jitter on Core 1.
- Output LPF added at reverb wet output: OnePole coeff=0.61 → fc≈8 kHz, smooths HF tail density without affecting dry signal.
