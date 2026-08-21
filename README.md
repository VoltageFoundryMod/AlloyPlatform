<img src="images/VFM_Logo_Full.svg" alt="Voltage Foundry Modular" width="280">

# Alloy Platform

A firmware platform for Eurorack synth modules.

One board, one panel family, one build system, one Alloy Controller — and a
clean seam between the parts that are the same for every module and the parts
that make a module itself. A synthesis engine drops in on one side of that seam
and gets USB and TRS MIDI, a SysEx patch protocol, nine-slot preset storage, a
serial console, an I2S audio driver, knob takeover, an LED language and a VCV
Rack build without writing any of it.

The platform started as the firmware for a single module, **Alloy Flux**, and was
factored out when a second engine — Synthux Academy's **Audrey II** — needed the
same infrastructure. That port is the proof the seam is real.

---

## Modules

| Module                               | What it is                                                                                     | Manual                                   | Status                                             |
| ------------------------------------ | ---------------------------------------------------------------------------------------------- | ---------------------------------------- | -------------------------------------------------- |
| **[Alloy Flux](modules/alloyflux/)** | Dual relation oscillator — stereo synth voice, six voice modes, 6-voice poly                   | [MANUAL.md](modules/alloyflux/MANUAL.md) | Firmware, VCV and web complete                     |
| **[Alloy Coil](modules/alloycoil/)** | Feedback resonator — Karplus-Strong string in a saturating feedback loop, with echo and reverb | [MANUAL.md](modules/alloycoil/MANUAL.md) | Engine ported and voiced; awaits its `IHardwareIO` |

Both share a PCB and a panel outline, so the same slot numbers land on the same
physical positions and each module simply names them differently.

Alloy Coil's engine is the work of [Nick Donaldson](https://github.com/ndonald2)
and [Roey Tsemah](https://github.com/roeytsemah) at
[Synthux Academy](https://github.com/Synthux-Academy/Audrey-II), where it powers
their module **Audrey II**. It is MIT-licensed and vendored here with its
credits intact — see [`modules/alloycoil/README.md`](modules/alloycoil/README.md)
for what was taken, what was left behind, and how a 2.36 MiB engine was made to
fit in 520 KB.

**Alloy Coil is not Audrey II.** The port was made with the original author's
blessing, and ships under its own name at their request, so that questions and
bug reports land with whoever actually owns the code in front of the user. Ask
about Alloy Coil here; ask about Audrey II
[upstream](https://github.com/Synthux-Academy/Audrey-II).

---

## Three targets, one codebase

| Target                                                   | Build                           | Output                                   |
| -------------------------------------------------------- | ------------------------------- | ---------------------------------------- |
| **Firmware** — RP2350 (Pico 2), PlatformIO, arduino-pico | `make firmware MODULE=<module>` | `.pio/build/<module>/firmware.uf2`       |
| **VCV Rack plugin** — Rack SDK 2.6.6                     | `make vcv`                      | one `plugin.dll` carrying _every_ module |
| **Alloy Controller** — Svelte 5 + TypeScript + Vite      | `make web`                      | `web-configurator/dist`                  |

```sh
make help              # every target, with the current ENV / MODULE
make                   # firmware for the default module
make upload            # build and flash over USB
make vcv-install       # build the plugin and install it into Rack
make web-dev           # configurator on localhost:5173
make everything        # every firmware image + VCV + the web build
```

Build through the **root Makefile** rather than calling `pio` / `make` / `npm`
directly. On Windows it locates the toolchain itself: it finds `pio.exe` under
`~/.platformio` and switches `SHELL` and `PATH` to msys2 for the Rack plugin
build, which is what Rack's POSIX `plugin.mk` needs.

The Alloy Controller needs **Chrome or Edge** — Web Serial and Web MIDI are not
available in Firefox or Safari.

---

## How the seam works

### Engine selection is compile-time

Memory forces it. Alloy Flux and Alloy Coil each fill most of the RP2350's 520 KB, so
they cannot be co-resident: **one firmware image per module**. That is a
constraint worth having, because it also lets each module pick its own sample
rate and block size, and it means the dispatch cost is zero — a build-flag
typedef rather than a vtable.

The platform declares a set of hooks in
[`platform/include/ModuleHooks.h`](platform/include/ModuleHooks.h) and **exactly
one module defines them**: what a note means, which CCs are actions rather than
parameters, what to enumerate as over USB, what to write into a preset blob. Link
Alloy Flux's definitions and you get Alloy Flux; link Alloy Coil's and you get Alloy Coil,
from the same platform sources. A module with nothing to say for a hook still has
to define it — an empty body — so a missing one is a link error naming the hook
rather than silently inherited behaviour.

### Hardware sits behind one interface

[`IHardwareIO`](platform/include/io/HardwareIO.h) is the only boundary between DSP
and hardware, with two implementations: `HardwarePicoIO` on the board and
`VCVRackIO` in Rack. Its identifiers are **positional** — `PotId::POT_1`,
`CVId::CV_3`, `LightId::LIGHT_5` — and each module names them in its own
`io/PanelMap.h`. Alloy Flux calls slot 1 `Pot::ROOT`; Alloy Coil calls it `Pot::PITCH`.
No semantic name ever enters the HAL.

The platform's audio boundary is **float ±1.0**. Alloy Flux's fixed-point
`int32 ±32512` path is an internal convention that converts only at its own edge.

### Parameters are declared once

A parameter used to touch ten sites across firmware, VCV, flash and the web UI —
and the three "adding a new parameter" checklists in the tree did not agree with
each other. Now a module declares it once in `modules/<name>/params.json`:

```sh
make params            # regenerate every table derived from it
make params-check      # CI gate: fail if the committed tables are stale
```

One row supplies the CC number, range, curve, default, label, category, unit and
discrete option bands to both the firmware's parameter manifest and the web
configurator's parameter map. The generated files are committed, so an ordinary
build never needs Python — only editing `params.json` does.

### Presets are engine-tagged

A flash slot is `{magic, engineId, engineVersion, blob}`. The container belongs to
the platform, the blob to the module, so an Alloy Coil preset sitting in a slot is
_skipped_ by an Alloy Flux build rather than reinterpreted as Alloy Flux floats.
Slot 0 is a rate-limited live auto-save; slots 1–9 are user presets.

### The audio path owns a core

Core 1 runs the entire audio path — it owns the I2S driver and renders 32-frame
blocks at 48 kHz. Core 0 does everything else: knobs, CV, buttons, LEDs, USB MIDI,
serial, flash, at a 128 Hz control tick paced by `AudioDriver::controlTicks()` so
the control rate stays derived from the audio clock.

Control writes engine state that audio reads with no lock, which is safe only
because every such value is a single aligned word smoothed at control rate. The
rules that keep it safe are in [`AGENTS.md`](AGENTS.md) — read them before
touching `renderAudio()`.

---

## Repository layout

```txt
platform/         engine-agnostic: HAL, MIDI/SysEx, config store, serial console,
                  audio driver, VCV scaffolding, LED and panel geometry
modules/
  alloyflux/      the dual relation oscillator — engine, params.json, panel map,
                  VCV module, manual
  alloycoil/         the vendored Audrey II engine and its integration
vendor/daisysp/   the DaisySP subset Alloy Coil needs, with its patches recorded
vcv-plugin/       the Rack plugin build — one plugin, all modules
web-configurator/ Svelte 5 configurator, one build for every module
hardware/         KiCad schematics and PCB (CERN-OHL-S v2)
panel-src/        panel artwork (CC BY-NC-ND 4.0)
tools/            parameter generation, panel prep, SVG → NanoVG
references/       design docs, module/MIDI/serial specs, milestones
```

---

## Connecting to a module

Two channels, both driverless:

- **Web MIDI SysEx** (primary) — full patch dump and restore, preset save and
  load. Manufacturer ID `0x7D`, then a two-byte device signature per module
  (`0x41 0x46` for Alloy Flux, `0x41 0x43` for Alloy Coil).
- **Web Serial CDC** (fallback) — a text command console.

The configurator **detects which module it is talking to**. On connect it sends
a patch request addressed to the wildcard signature `7F 7F`, which every module
answers, and reads the module out of the reply's header — one round trip that
identifies the module and delivers its patch. So a single build drives every
module, and swapping modules on the port swaps the controls. With nothing
connected it shows the last module it saw.

---

## Documentation

|                                                                                                    |                                                             |
| -------------------------------------------------------------------------------------------------- | ----------------------------------------------------------- |
| [`AGENTS.md`](AGENTS.md)                                                                           | architecture, build commands, conventions, audio-path rules |
| [`LICENSING.md`](LICENSING.md)                                                                     | what is licensed how, and why the combination works         |
| [`references/Alloy-Platform-Design.md`](references/Alloy-Platform-Design.md)                       | the platform design and the decisions behind it             |
| [`references/AlloyFlux-Development_Milestones.md`](references/AlloyFlux-Development_Milestones.md) | the roadmap, and the record of what was measured            |
| [`references/AlloyFlux-module-reference.md`](references/AlloyFlux-module-reference.md)             | full parameter/CV spec and LED language                     |
| [`references/AlloyFlux-MIDI-reference.md`](references/AlloyFlux-MIDI-reference.md)                 | SysEx protocol                                              |
| [`references/AlloyFlux-serial-reference.md`](references/AlloyFlux-serial-reference.md)             | serial console commands                                     |

---

## Licensing

Four licences, scoped by directory. The intent is the one Mutable Instruments
settled on: **everything needed to build, study, fork and improve the module is
open; the names and the artwork are not.** Fork the module; draw your own front.

| What                                            | Licence          |
| ----------------------------------------------- | ---------------- |
| Firmware, VCV plugin, Alloy Controller, tooling | GPL-3.0-or-later |
| Hardware design (`hardware/`)                   | CERN-OHL-S v2    |
| Panel artwork (`panel-src/`, `vcv-plugin/res/`) | CC BY-NC-ND 4.0  |
| Names, marks, logos                             | not licensed     |

Third-party code keeps its own licence and notices. Full detail, including why
MIT and LGPL-2.1 combine into GPLv3 here, is in [`LICENSING.md`](LICENSING.md).

---

Voltage Foundry Modular — 2026
