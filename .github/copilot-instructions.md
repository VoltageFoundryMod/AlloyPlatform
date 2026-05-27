# AlloyFlux — GitHub Copilot Workspace Instructions

AlloyFlux is a eurorack synthesizer module with three build targets that share a single DSP codebase:

- **Firmware** — RP2350 (Pico 2), Arduino/Mozzi, PlatformIO
- **VCV Rack plugin** — Rack SDK 2.6.6, shares `include/` + `src/SynthEngine.cpp`
- **Web Configurator** — Svelte 5 + TypeScript + Vite

---

## Build Commands

| Target     | Command                                | Notes                                                            |
| ---------- | -------------------------------------- | ---------------------------------------------------------------- |
| Firmware   | `platformio run`                       | env name `alloyflux`; output `.pio/build/alloyflux/firmware.uf2` |
| VCV plugin | `cd vcv-plugin && make`                | Rack SDK path hardcoded in Makefile                              |
| Web (dev)  | `cd web-configurator && npm run dev`   | Vite at `localhost:5173`                                         |
| Web (prod) | `cd web-configurator && npm run build` | Output in `dist/`                                                |

Always build firmware after changing `include/` headers shared with VCV to confirm no regressions on either platform.

---

## Architecture

### Firmware (RP2350, dual-core)

- **Core 0** — Mozzi audio ISR at 32768 Hz (`updateAudio()`), control loop at 128 Hz (`updateControl()`), all IO
- **Core 1** — Reverb engine only (`DattorroReverb`). Disabled via `-DREVERB_FORCE_CORE0=1` for debugging.

Key files:

- [`src/main.cpp`](../src/main.cpp) — thin platform shim; Mozzi hooks; inter-core ring buffer
- [`include/SynthEngine.h`](../include/SynthEngine.h) / [`src/SynthEngine.cpp`](../src/SynthEngine.cpp) — all DSP, platform-independent
- [`include/io/IOBridge.h`](../include/io/IOBridge.h) — `fillSynthParams()` — the single place where hardware reads are converted to a `SynthParams` snapshot (runs on both platforms)
- [`include/dsp/`](../include/dsp/) — individual audio engine headers (reverb, filter, chorus, delay, etc.)

### Platform abstraction

`IHardwareIO` (defined in [`include/io/HardwareIO.h`](../include/io/HardwareIO.h)) is the only boundary between DSP and hardware. Two implementations:

- **Firmware** — `HardwarePicoIO` in [`include/io/HardwarePicoIO.h`](../include/io/HardwarePicoIO.h)
- **VCV** — `VCVRackIO` in [`vcv-plugin/src/VCVRackIO.h`](../vcv-plugin/src/VCVRackIO.h)

`#ifdef ARDUINO` guards exist in a few DSP headers for RP2350-specific timer calls; keep them when editing those files.

### Config/Flash

[`include/config_store.h`](../include/config_store.h) defines `AlloyConfig`. Rules:

- **Always bump `kConfigVersion`** when adding/removing/reordering fields — old flash data is automatically discarded on mismatch.
- Magic word: `0xAF10CF01`. Slot 0 = live auto-save (10 s rate limit), slots 1–9 = user presets.

---

## ISR Safety (updateAudio)

`updateAudio()` runs at 32768 Hz. Violations cause audio dropouts or watchdog resets:

- **Never** acquire a mutex inside `updateAudio()`
- **Never** call `malloc`, `free`, `sqrt()`, `pow()`, or any blocking/File I/O function
- **Never** trigger serial/USB output from the ISR
- Hot ISR functions must be annotated `__attribute__((section(".time_critical")))` or wrapped via `IRAM_ATTR`
- All `volatile` inter-core state (gate, reverb params, chorus depth) is single aligned `float`/`bool` — atomic on Cortex-M33; larger structs need `gDspMutex`

### Inter-core reverb transport

Core 0 ISR → `gRevInQueue[]` (SPSC ring buffer, power-of-2, `volatile`) → Core 1 processes → `gRevOutBuf_L/R[]` (8-sample fixed-latency output buffer, read with `kRevReadDelay` offset). See [`include/dsp_shared.h`](../include/dsp_shared.h).

---

## Conventions

- **Multi-file edits**: use `multi_replace_string_in_file` tool; provide ≥3 lines of context above and below each change.
- **Do not commit** or run `git push` unless explicitly asked.
- **Do not modify `/c/Users/carlosedp/Rack-SDK/`** — it is a shared external dependency.
- **Do not increase `DELAY_MAX_MS`** without confirming SRAM budget (`platformio run` reports RAM usage after build).
- **Shared DSP changes**: any edit to `include/dsp/` or `src/SynthEngine.cpp` affects both firmware and VCV — validate both build targets.
- **Mark Milestones as done**: when a referenced task is complete, add an "x" to the checkbox in `references/Development_Milestones.md` (e.g. `- [x] 1. Sine wave out via PCM5102`).

---

## Reference Docs

- [`references/Development_Milestones.md`](../references/Development_Milestones.md) — project roadmap and feature breakdown
- [`references/AlloyFlux-module-reference.md`](../references/AlloyFlux-module-reference.md) — full parameter/CV specification
- [`references/AlloyFlux-MIDI-reference.md`](../references/AlloyFlux-MIDI-reference.md) — CC map and SysEx protocol
- [`references/AlloyFlux-serial-reference.md`](../references/AlloyFlux-serial-reference.md) — serial console commands
- [`references/AlloyFlux-user-manual.md`](../references/AlloyFlux-user-manual.md) — end-user manual
- [`references/ai-notes/`](../references/ai-notes/) — AI-generated notes on debugging, refactors, and optimizations (e.g. reverb concurrency, MIDI handling, etc.)
