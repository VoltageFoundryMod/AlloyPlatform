# AlloyFlux — GitHub Copilot Workspace Instructions

AlloyFlux is a eurorack synthesizer module with three build targets that share a single DSP codebase:

- **Firmware** — RP2350 (Pico 2), Arduino/Mozzi, PlatformIO
- **VCV Rack plugin** — Rack SDK 2.6.6, shares `common/include/` + `common/src/SynthEngine.cpp`
- **Web Configurator** — Svelte 5 + TypeScript + Vite

---

## Build Commands

All three targets build from the **root `Makefile`**. Use it rather than calling
`pio`/`make`/`npm` directly: on Windows it locates the toolchain itself — it finds
`pio.exe` under `~/.platformio` and switches `SHELL` and `PATH` to msys2 for the
Rack plugin build, which is what Rack's POSIX `plugin.mk` needs. `make help`
lists every target.

| Target     | Command                  | Notes                                                                  |
| ---------- | ------------------------ | ---------------------------------------------------------------------- |
| Firmware   | `make` / `make firmware` | env `alloyflux`; output `.pio/build/alloyflux/firmware.uf2`            |
|            | `make upload`            | build + flash; `make upload-monitor` also opens the serial console     |
| VCV plugin | `make vcv`               | `vcv-plugin/plugin.dll`; no MSYS shell needed, the Makefile finds one  |
|            | `make vcv-install`       | installs into Rack's user plugin dir (`make print-plugins-dir`)        |
|            | `make vcv-dist`          | packages the `.vcvplugin` for the VCV library                          |
| Web        | `make web`               | production build into `web-configurator/dist`                          |
|            | `make web-dev`           | Vite at `localhost:5173`; `make web-check` runs svelte-check + tsc     |
| Everything | `make everything`        | firmware + VCV + web                                                   |
| Formatting | `make format`            | clang-format over every C/C++ file; `make format-check` is the CI gate |

`RACK_DIR` defaults to a `Rack-SDK` checkout beside this repository. Override it
with an **absolute** path (`make RACK_DIR=D:/Rack-SDK vcv`) — a relative one would
be resolved from `vcv-plugin/`, not the repo root. Override `MSYS=D:/msys64` if
msys2 lives elsewhere.

VS Code tasks for all of the above are in `.vscode/tasks.json`, each one a wrapper
around a Makefile target. `tools/env.ps1` is optional — source it (`. .\tools\env.ps1`)
only when you want msys2, `pio` and `clang-format` on `PATH` for the PowerShell
session itself.

Always run `make everything` after changing shared headers under `common/include/`
to confirm no regressions on either platform.

---

## Architecture

### Firmware (RP2350, dual-core)

- **Core 0** — Mozzi audio ISR at 32768 Hz (`updateAudio()`), control loop at 128 Hz (`updateControl()`), all IO
- **Core 1** — Reverb engine only (`DattorroReverb`). Disabled via `-DREVERB_FORCE_CORE0=1` for debugging.

Key files and directories:

- [`firmware/src/main.cpp`](../firmware/src/main.cpp) — thin platform shim; Mozzi hooks; inter-core ring buffer
- [`common/include/SynthEngine.h`](../common/include/SynthEngine.h) / [`common/src/SynthEngine.cpp`](../common/src/SynthEngine.cpp) — all DSP, platform-independent
- [`common/include/io/IOBridge.h`](../common/include/io/IOBridge.h) — `fillSynthParams()` — the single place where hardware reads are converted to a `SynthParams` snapshot (runs on both platforms)
- [`common/include/dsp/`](../common/include/dsp/) — individual audio engine headers (reverb, filter, chorus, delay, etc.)

### Platform abstraction

`IHardwareIO` (defined in [`common/include/io/HardwareIO.h`](../common/include/io/HardwareIO.h)) is the only boundary between DSP and hardware. Two implementations:

- **Firmware** — `HardwarePicoIO` in [`firmware/include/io/HardwarePicoIO.h`](../firmware/include/io/HardwarePicoIO.h)
- **VCV** — `VCVRackIO` in [`vcv-plugin/src/VCVRackIO.h`](../vcv-plugin/src/VCVRackIO.h)

`#ifdef ARDUINO` guards exist in a few DSP headers for RP2350-specific timer calls; keep them when editing those files.

### Web Configurator

[`web-configurator/src/`](../web-configurator/src/) — Svelte 5 + TypeScript frontend. Connects to the module via two channels:

- **Web MIDI SysEx** (primary) — full patch dump/restore, preset save/load; see [`references/AlloyFlux-MIDI-reference.md`](../references/AlloyFlux-MIDI-reference.md) for the protocol (manufacturer ID `0x7D`, device signature `0x41 0x46`)
- **Web Serial CDC** (fallback) — text command interface; see [`references/AlloyFlux-serial-reference.md`](../references/AlloyFlux-serial-reference.md)

Key library modules: `src/lib/serial.ts` (Web Serial), `src/lib/midi.ts` (Web MIDI), `src/lib/patchSync.ts` (SysEx build/parse), `src/lib/paramMap.ts` (CC ↔ param mapping).

**Requirement**: Chrome or Edge only — Web Serial and Web MIDI APIs are not supported in Firefox/Safari.

### Config/Flash

[`firmware/include/config_store.h`](../firmware/include/config_store.h) defines `AlloyConfig`. Rules:

- **Always bump `kConfigVersion`** when adding/removing/reordering fields — old flash data is automatically discarded on mismatch. Current value: `5`.
- Magic word: `0xAF10CF01`. Slot 0 = live auto-save (10 s rate limit), slots 1–9 = user presets.
- Current SRAM usage: ~290 KB of 512 KB (56.5%), flash 4.5%; check after any change that increases buffer sizes. The jump from the previously documented 236 KB is `-DDELAY_MAX_MS=500` in `platformio.ini` (~65 KB of delay buffer), not a regression.

---

## ISR Safety (updateAudio)

`updateAudio()` runs at 32768 Hz. Violations cause audio dropouts or watchdog resets:

- **Never** acquire a mutex inside `updateAudio()`
- **Never** call `malloc`, `free`, `sqrt()`, `pow()`, or any blocking/File I/O function
- **Never** trigger serial/USB output from the ISR
- Hot ISR functions must be annotated `__attribute__((section(".time_critical")))` or wrapped via `IRAM_ATTR`
- All `volatile` inter-core state (gate, reverb params, chorus depth) is single aligned `float`/`bool` — atomic on Cortex-M33; larger structs need `gDspMutex`

### Inter-core reverb transport

Core 0 ISR → `gRevInQueue[]` (SPSC ring buffer, power-of-2, `volatile`) → Core 1 processes → `gRevOutBuf_L/R[]` (8-sample fixed-latency output buffer, read with `kRevReadDelay` offset). See [`firmware/include/dsp_shared.h`](../firmware/include/dsp_shared.h).

---

## Conventions

- **Multi-file edits**: use `multi_replace_string_in_file` tool; provide ≥3 lines of context above and below each change.
- **Do not commit** or run `git push` unless explicitly asked.
- **Do not modify `/c/Users/carlosedp/Rack-SDK/`** — it is a shared external dependency.
- **Do not increase `DELAY_MAX_MS`** without confirming SRAM budget (`platformio run` reports RAM usage after build).
- **Shared DSP changes**: any edit to `common/include/dsp/` or `common/src/SynthEngine.cpp` affects both firmware and VCV — validate both build targets.
- **Windows VCV build**: run `make vcv` from the repo root — the root Makefile picks up the msys2 shell and toolchain itself, so no MinGW64 shell is needed.
- **VCV warnings on GCC**: keep `vcv-plugin/Makefile` filtering out `-Wno-vla-extension` from `CXXFLAGS` (Clang-only flag from Rack SDK).
- **Mark Milestones as done**: when a referenced task is complete, add an "x" to the checkbox in `references/Development_Milestones.md` (e.g. `- [x] 1. Sine wave out via PCM5102`).

### Code Style & Naming

- **Headers**: always `#pragma once` — never `#ifndef` include guards.
- **Formatting**: 4-space indent, 80-column limit, Allman brace style (after class/struct), no tabs. Governed by `.clang-format`.
  - Check: `make format-check` — Fix: `make format`. Both cover untracked-but-not-ignored files too, so a newly added header is formatted before its first commit. On Windows the Makefile falls back to the clang-format shipped with the VS Code C/C++ extension when none is on `PATH`.
- **Global naming**: `gXxx` = goal/target values (updated from hardware reads), `sXxx` = smoothed/current values (updated each control tick). Inter-core volatile state follows the same convention with `volatile` qualifier.
- **Template DSP engines**: parameterized by `SAMPLE_RATE` at compile time; call `engine.init(sampleRate)` at instantiation. Firmware uses 32768 Hz; VCV uses host sample rate (`args.sampleRate`).
- **DSP integer samples**: `int32_t ±32512` throughout the signal path; float conversion only at DAC output boundary.

### Data Flow (params → audio)

```txt
Hardware / Rack params
        │
        ▼
IHardwareIO::readPot/readCV/isPatched   ← HardwarePicoIO (firmware) or VCVRackIO (VCV)
        │
        ▼
fillSynthParams()  [common/include/io/IOBridge.h]
        │ produces SynthParams snapshot
        ▼
SynthEngine::control()  @ 128 Hz   ← smoothing, voice pitch, effect coefficients
        │
        ▼
SynthEngine::audio()   @ 32768 Hz  ← oscillators, filters, reverb, output summation
```

---

## Reference Docs

- [`references/Development_Milestones.md`](../references/Development_Milestones.md) — project roadmap and feature breakdown
- [`references/AlloyFlux-module-reference.md`](../references/AlloyFlux-module-reference.md) — full parameter/CV specification
- [`references/AlloyFlux-MIDI-reference.md`](../references/AlloyFlux-MIDI-reference.md) — CC map and SysEx protocol
- [`references/AlloyFlux-serial-reference.md`](../references/AlloyFlux-serial-reference.md) — serial console commands
- [`references/AlloyFlux-user-manual.md`](../references/AlloyFlux-user-manual.md) — end-user manual
- [`references/ai-notes/`](../references/ai-notes/) — AI-generated notes on debugging, refactors, and optimizations (e.g. reverb concurrency, MIDI handling, etc.)
