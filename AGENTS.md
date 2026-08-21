# AlloyFlux — GitHub Copilot Workspace Instructions

AlloyFlux is a eurorack synthesizer module with three build targets that share a single DSP codebase:

- **Firmware** — RP2350 (Pico 2), Arduino, PlatformIO
- **VCV Rack plugin** — Rack SDK 2.6.6, one plugin carrying every module
- **Alloy Controller** — Svelte 5 + TypeScript + Vite

---

## Build Commands

All three targets build from the **root `Makefile`**. Use it rather than calling
`pio`/`make`/`npm` directly: on Windows it locates the toolchain itself — it finds
`pio.exe` under `~/.platformio` and switches `SHELL` and `PATH` to msys2 for the
Rack plugin build, which is what Rack's POSIX `plugin.mk` needs. `make help`
lists every target.

| Target     | Command                  | Notes                                                                                   |
| ---------- | ------------------------ | --------------------------------------------------------------------------------------- |
| Firmware   | `make` / `make firmware` | one image per module; `MODULE=alloycoil` for the other. Output `.pio/build/$MODULE/firmware.uf2` |
|            | `make firmware-all`      | every module's image — run after touching `platform/`                                   |
|            | `make upload`            | build + flash; `make upload-monitor` also opens the serial console                      |
| VCV plugin | `make vcv`               | **one** `plugin.dll` with *all* modules in it — there is no per-module VCV build        |
|            | `make vcv-install`       | installs into Rack's user plugin dir (`make print-plugins-dir`)                         |
|            | `make vcv-dist`          | packages the `.vcvplugin` for the VCV library                                           |
| Web        | `make web`               | **one** build for *all* modules; detects the connected one                              |
|            | `make web-dev`           | Vite at `localhost:5173`; `make web-check` runs svelte-check + tsc                      |
| Everything | `make everything`        | every firmware image + VCV + the web build                                              |
| Alloy Coil     | `make coil-host`       | host-compiles + runs the vendored Alloy Coil engine; prints its footprint                   |
| Formatting | `make format`            | clang-format over every C/C++ file; `make format-check` is the CI gate                  |

`RACK_DIR` defaults to a `Rack-SDK` checkout beside this repository. Override it
with an **absolute** path (`make RACK_DIR=D:/Rack-SDK vcv`) — a relative one would
be resolved from `vcv-plugin/`, not the repo root. Override `MSYS=D:/msys64` if
msys2 lives elsewhere.

VS Code tasks for all of the above are in `.vscode/tasks.json`, each one a wrapper
around a Makefile target. `tools/env.ps1` is optional — source it (`. .\tools\env.ps1`)
only when you want msys2, `pio` and `clang-format` on `PATH` for the PowerShell
session itself.

Always run `make everything` after changing shared headers under `platform/include/` or `modules/alloyflux/include/`
to confirm no regressions on either platform.

**Vendored trees** — [`vendor/daisysp/`](vendor/daisysp/) and [`modules/alloycoil/`](modules/alloycoil/) — each carry a recorded upstream commit in their README and are excluded from `make format`. Don't restyle them; record any local patch in the README so re-vendoring stays a matter of re-applying a known list.

---

## Architecture

### Firmware (RP2350, dual-core)

- **Core 1** — the entire audio path: owns the I2S driver (`AudioDriver::begin()` + `pump()` both run here) and renders 32-frame blocks at 48000 Hz through `renderAudio()`, reverb included
- **Core 0** — everything else: knobs, CV, buttons, LEDs, USB MIDI, serial console, flash. `updateControl()` at 128 Hz, paced by `AudioDriver::controlTicks()` so the control rate stays derived from the audio clock

Control writes engine state that audio reads with no lock. Safe only because every such value is a single aligned word and is smoothed at control rate — a torn multi-field update costs at most one sample computed from two adjacent parameter values. **Never add a `setParams()` that resizes a buffer or swaps a pointer the audio path dereferences.**

Key files and directories:

- [`modules/alloyflux/src/main.cpp`](modules/alloyflux/src/main.cpp) — thin platform shim; core split, `renderAudio()`, `updateControl()`
- [`platform/include/io/AudioDriver.h`](platform/include/io/AudioDriver.h) — block I2S output; owns the audio clock and publishes the control tick
- [`modules/alloyflux/include/SynthEngine.h`](modules/alloyflux/include/SynthEngine.h) / [`modules/alloyflux/src/SynthEngine.cpp`](modules/alloyflux/src/SynthEngine.cpp) — all DSP, platform-independent
- [`modules/alloyflux/include/io/IOBridge.h`](modules/alloyflux/include/io/IOBridge.h) — `fillSynthParams()` — the single place where hardware reads are converted to a `SynthParams` snapshot (runs on both platforms)
- [`modules/alloyflux/include/dsp/`](modules/alloyflux/include/dsp/) — individual audio engine headers (reverb, filter, chorus, delay, etc.)

### Platform abstraction

`IHardwareIO` (defined in [`platform/include/io/HardwareIO.h`](platform/include/io/HardwareIO.h)) is the only boundary between DSP and hardware. Two implementations:

- **Firmware** — `HardwarePicoIO` in [`modules/alloyflux/include/io/HardwarePicoIO.h`](modules/alloyflux/include/io/HardwarePicoIO.h). Alloy Coil has none yet, so its knobs and CV are VCV-only.
- **VCV** — `VCVRackIO` in [`platform/vcv/VCVRackIO.h`](platform/vcv/VCVRackIO.h), shared by every module. `SubMenuSlider.hpp` sits beside it for shift-secondaries.

Its identifiers are **positional** — `PotId::POT_1`, `CVId::CV_3`, `LightId::LIGHT_5`. Each module names them in its own `io/PanelMap.h` — AlloyFlux as `Pot::ROOT`/`Cv::VOCT`, Alloy Coil as `Pot::PITCH`/`Cv::EXCITER` — and **both map the same slot numbers to the same physical positions**, because they share a PCB. Use the `Pot::`/`Cv::` names in module code; never add a semantic name to the HAL. **Slot order is the flash format** — append, never insert.

The platform's audio boundary is **float ±1.0**. AlloyFlux's `int32 ±kSignalFullScale` is an internal convention and converts only at its own edge (`renderAudio()` on hardware, `setVoltage()` in VCV) — see `kSignalToFloat` / `kFloatToSignal` in `SynthEngine.h`.

There is **no engine singleton**. The firmware owns one `SynthEngine` at file scope in `main.cpp`; VCV owns one per `Module`. Code that needs to start a note calls `polyNoteOn()` (declared in `params.h`) rather than reaching for an engine.

`#ifdef ARDUINO` guards exist in a few DSP headers for RP2350-specific timer calls; keep them when editing those files.

### Alloy Controller

[`web-configurator/src/`](web-configurator/src/) — Svelte 5 + TypeScript frontend. Connects to the module via two channels:

- **Web MIDI SysEx** (primary) — full patch dump/restore, preset save/load; see [`references/AlloyFlux-MIDI-reference.md`](references/AlloyFlux-MIDI-reference.md) for the protocol (manufacturer ID `0x7D`, device signature `0x41 0x46` for AlloyFlux, `0x41 0x43` for Alloy Coil, `0x7F 0x7F` = wildcard/discovery)
- **Web Serial CDC** (fallback) — text command interface; see [`references/AlloyFlux-serial-reference.md`](references/AlloyFlux-serial-reference.md)

Key library modules: `src/lib/serial.ts` (Web Serial), `src/lib/midi.ts` (Web MIDI), `src/lib/patchSync.ts` (SysEx build/parse), `src/lib/paramMap.ts` (CC ↔ param mapping).

Which module the page shows is **detected at runtime**: on connect it sends REQUEST\_DUMP addressed to the wildcard signature `7F 7F`, which every module answers, and `src/lib/activeModule.ts` reads the module out of the reply's header (`parseSysExBody` returns the sender's identity alongside the CC pairs). One build drives every module. `src/lib/paramMap.ts` exposes the tables as a store (`params`) plus a non-reactive `currentParams()`; `App.svelte` keys the parameter panel on the module id so a switch rebuilds every control.

**`make web` takes no `MODULE`** — there is nothing per-module to build. The last detected module is remembered in localStorage; a browser that has never connected opens on AlloyFlux with the header badge muted to say the choice is a guess. `VITE_MODULE` used to set that first-run default and is gone: it looked like a build flag selecting a target while selecting nothing but a name.

The UI is the **panel view** (`src/components/PanelView.svelte`): a single-screen control surface of rotary knobs, segmented switches and LED ladders, styled from the physical panel artwork. The palette lives as CSS custom properties in `src/app.css` and nothing below it should hardcode a colour. `App.svelte` owns the patch state (`paramValues`/`selectValues`) and pushes inbound CC into each control through its exported `applyCC`; the components own no state of their own.

The original scrolling `ParamSlider` grid — the "List" view — has been removed now the panel covers everything it did. Two leftovers of it survive on purpose: the generated tables still export `PARAM_CATEGORIES`/`PARAMS_BY_CATEGORY` (nothing reads them; the panel places controls by name via `panelLayout.ts`), and browsers may still hold an `alloy-view-mode` localStorage key, which is now ignored.

Where a control sits is authored in [`src/lib/panelLayout.ts`](web-configurator/src/lib/panelLayout.ts), against the parameter's `name` — deliberately **not** in `params.json`, which is the firmware contract and would otherwise need the C++ table regenerated to move a knob one column. `layoutFor()` appends any parameter no section places into a per-category catch-all, so forgetting to place a new one costs an ugly trailing group rather than a missing control.

The panel is a **fixed-width stage that zooms**, not a reflowing layout: each module declares a `stageWidth` (AlloyFlux 1700, Alloy Coil 1050), sections take `span` columns of twelve on that stage, and PanelView transform-scales the whole thing to fit both axes of the window. Hand-tuned spans are only safe because of this — a reflowing grid made the same spans read as half-empty outlines on a wide monitor and a column stacked down the left on a narrow one. Aim a stage at roughly 2:1, which is about the window's usable aspect once the chrome and the dock are off; the tighter axis picks the zoom and the other shows slack.

Presets, Keyboard and Settings **dock to a right-hand rail** (`panel/DockPanel.svelte`), several at once. The rail takes its width out of the panel column, and since PanelView measures the room it is given, opening one shrinks the control surface instead of covering it — no overlay, nothing to move out of the way.

The quantizer indicator (`panel/ScaleKeys.svelte`) is **read-only by necessity**. The `scale` CC carries a `ScaleId`, not a mask, so there is no way to send an arbitrary set of semitones; making the keys editable needs a new firmware parameter first. Its masks in `lib/scales.ts` are **mirrored by hand from `modules/alloyflux/include/scale_quantizer.h`** — that header is the authority, and the two must be kept in step. They are not generated because the masks live in a hand-written C++ header rather than in `params.json`, which only ever sees the option labels.

Knob readouts derive their decimal places from the parameter's range *and its CC resolution*, never from the current value. The resolution half matters: `root` is ±48 semitones over 128 CC steps, so exact centre would need CC 63.5 and an initialised patch round-trips to CC 64 = 0.378 st. Printed to one decimal that reads as a module 38 cents sharp out of the box; one CC step there is 0.76 st, so whole semitones is the honest rendering. Don't "fix" that by adding decimals.

Three per-control affordances worth knowing: `size` (`sm`/`md`/`lg`, and the spread is the main tool for saying which control matters), `icons` (glyphs from `panel/Glyph.svelte` — on a knob, a strip across the travel with the nearest one lit, as the wave morph and the unison spread use it; on a segmented switch, one glyph per option above its label, as the filter mode and algorithm switches use it), and `disabledParams`, a set of names App.svelte derives from the current mode. Disabled controls are greyed and refuse to put CC on the wire, which is how the AR/ADSR split is shown: AR greys the four ADSR knobs, ADSR greys TIME SCALE. Sections size their dial slots to their largest knob so mixed sizes keep one centre line and one readout baseline.

**Requirement**: Chrome or Edge only — Web Serial and Web MIDI APIs are not supported in Firefox/Safari.

### Config/Flash

[`modules/alloyflux/include/alloy_config.h`](modules/alloyflux/include/alloy_config.h) defines `AlloyConfig`. Rules:

- **Always bump `kEngineVersion`** when adding/removing/reordering fields — old flash data is automatically discarded on mismatch. Current value: `7`.
- A slot is `{magic, engineId, engineVersion, blob[192]}` — the container belongs to the platform ([`platform/include/ConfigSlot.h`](platform/include/ConfigSlot.h)), the blob to the module. `engineId` (`0x4146` for AlloyFlux) means another module's preset in the same slot is skipped rather than reinterpreted as AlloyFlux floats.
- Magic word: `0xAF10CF01`. Slot 0 = live auto-save (10 s rate limit), slots 1–9 = user presets.
- Current SRAM usage: ~359 KB of 512 KB (68.5%), flash 4.5%; check after any change that increases buffer sizes. The delay buffers alone are ~187 KB (`DELAY_MAX_MS` 500 ms sized at `DelayEngine::kNativeRate`).

---

## Audio path safety (renderAudio)

`renderAudio()` runs 32 times per block, 1500 blocks/sec at 48000 Hz. The render loop is driven from `loop1()` by `AudioDriver::pump()`, which paces itself on the I2S DMA. The budget is one block's wall-clock period, ~667 µs, reported as `gAudioBudgetUs`. Violations cause audio dropouts or watchdog resets:

- **Never** acquire a mutex inside `renderAudio()`
- **Never** call `malloc`, `free`, `sqrt()`, `pow()`, or any blocking/File I/O function
- **Never** trigger serial/USB output from `renderAudio()`
- Hot render-path functions must be annotated `__attribute__((section(".time_critical")))` or wrapped via `IRAM_ATTR`
- All `volatile` cross-core state (gate, reverb params, chorus depth) is a single aligned `float`/`bool` — atomic on Cortex-M33. Nothing wider crosses cores; if something needs to, redesign rather than adding a mutex to the audio path
- A flash write (`EEPROM.commit()`) parks Core 1 for its erase/program, i.e. stops audio for ~10 ms. Keep it off any periodic path

---

## Conventions

- **Multi-file edits**: use `multi_replace_string_in_file` tool; provide ≥3 lines of context above and below each change.
- **Do not commit** or run `git push` unless explicitly asked.
- **Do not modify `/c/Users/carlosedp/Rack-SDK/`** — it is a shared external dependency.
- **Do not increase `DELAY_MAX_MS`** without confirming SRAM budget (`platformio run` reports RAM usage after build).
- **Adding or changing a parameter**: edit `modules/<module>/params.json` and run `make params`, then commit the regenerated files. That one row supplies the CC number, range, curve, default, label, category, unit and (for discrete params) the option bands to both `param_manifest.generated.h` and the Alloy Controller's `paramMap<Module>.ts` (`paramMap.ts` itself is a hand-written shim that picks between them). Do not hand-edit a generated file; `make params-check` is the CI gate. A module that sets `"globals_output": true` (Alloy Coil does) also gets `param_globals.generated.h`, which *defines* the parameter globals initialised to the `default` column — include it in exactly one TU per binary — and `applyParamDefaults()` in the manifest for factory reset. Between those two, changing a `default` needs no C++ edit at all. Only the module's config pack/apply (the struct field list, not its defaults) and the VCV param list are still hand-maintained.
- **Shared DSP changes**: any edit to `modules/<module>/include/dsp/` or `src/SynthEngine.cpp` affects both firmware and VCV — validate both build targets.
- **Windows VCV build**: run `make vcv` from the repo root — the root Makefile picks up the msys2 shell and toolchain itself, so no MinGW64 shell is needed.
- **VCV warnings on GCC**: keep `vcv-plugin/Makefile` filtering out `-Wno-vla-extension` from `CXXFLAGS` (Clang-only flag from Rack SDK).
- **Mark Milestones as done**: when a referenced task is complete, add an "x" to the checkbox in `references/Development_Milestones.md` (e.g. `- [x] 1. Sine wave out via PCM5102`).

### Code Style & Naming

- **Headers**: always `#pragma once` — never `#ifndef` include guards.
- **Formatting**: 4-space indent, 80-column limit, Allman brace style (after class/struct), no tabs. Governed by `.clang-format`.
  - Check: `make format-check` — Fix: `make format`. Both cover untracked-but-not-ignored files too, so a newly added header is formatted before its first commit. On Windows the Makefile falls back to the clang-format shipped with the VS Code C/C++ extension when none is on `PATH`.
- **Global naming**: `gXxx` = goal/target values (updated from hardware reads), `sXxx` = smoothed/current values (updated each control tick). Inter-core volatile state follows the same convention with `volatile` qualifier.
- **Template DSP engines**: the `SAMPLE_RATE` template argument is only a *default* — `setSampleRate()` / `init(rate)` is what actually sets the rate, and `SynthEngine::init()` calls it on every engine. Firmware uses 48000 Hz (M63g); VCV uses host sample rate (`args.sampleRate`). Note `DattorroReverb` is deliberately absent from `SynthEngine::setSampleRate()` — its delay lines are fixed sample counts, so the plate scales with rate on both platforms alike.
- **DSP integer samples**: `int32_t ±32512` throughout the signal path; float conversion only at DAC output boundary.

### Data Flow (params → audio)

```txt
Hardware / Rack params
        │
        ▼
IHardwareIO::readPot/readCV/isPatched   ← HardwarePicoIO (firmware) or VCVRackIO (VCV)
        │
        ▼
fillSynthParams()  [modules/alloyflux/include/io/IOBridge.h]
        │ produces SynthParams snapshot
        ▼
SynthEngine::control()  @ 128 Hz   ← smoothing, voice pitch, effect coefficients
        │
        ▼
SynthEngine::audio()   @ 48000 Hz  ← oscillators, filters, reverb, output summation
```

---

## Reference Docs

- [`references/Development_Milestones.md`](references/AlloyFlux-Development_Milestones.md) — project roadmap and feature breakdown
- [`references/AlloyFlux-hardware-design.md`](references/AlloyFlux-hardware-design.md) — pin map, analog front end, power, panel, BOM
- [`references/AlloyFlux-dsp-design.md`](references/AlloyFlux-dsp-design.md) — the DSP engines and their algorithms
- [`references/AlloyFlux-MIDI-reference.md`](references/AlloyFlux-MIDI-reference.md) — SysEx protocol (the CC map is generated into the manual)
- [`references/AlloyFlux-serial-reference.md`](references/AlloyFlux-serial-reference.md) — serial console commands
- [`modules/alloyflux/MANUAL.md`](modules/alloyflux/MANUAL.md) — Alloy Flux end-user manual
- [`modules/alloycoil/MANUAL.md`](modules/alloycoil/MANUAL.md) — Alloy Coil end-user manual
