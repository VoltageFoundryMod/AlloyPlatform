# Alloy Controller

An editor for the **Alloy Flux** and **Alloy Coil** Eurorack modules by Voltage Foundry Modular.  
Connects over **USB MIDI**, **serial** or **Bluetooth LE** to read and write all parameters in real time, manage presets, and monitor the serial console — no drivers required.

**Live at <https://alloy.vfmod.com/>** — published from `main` automatically.

Ships three ways from one build: a browser page that installs as a **PWA** and
works offline, and native **iOS** and **Android** apps that reach the module
over Bluetooth. See **[BUILDING.md](BUILDING.md)** for how to build and publish
each.

## Features

- **Single-screen panel** — the whole module at once: rotary knobs, segmented switches and LED ladders drawn in the hardware panel's own visual language, rather than a page of sliders you scroll. Drag a knob to set it, hold <kbd>Shift</kbd> for fine adjustment, double-click to return it to its default; wheel and arrow keys work too.
  - **Zooms to fit** — the panel is composed at a fixed design size and scaled to the window, so it keeps its proportions instead of reflowing into a different layout at every width
  - Controls the current mode ignores are **greyed and inert** (in AR the four ADSR knobs; in ADSR, Time Scale)
  - Waveform glyphs under a morphing control light the shape nearest the knob's position
  - Readouts show only the precision a 7-bit CC actually carries, so a value that is one CC step from zero reads as zero rather than as a module that is slightly out of tune
- **Utility rail** — Presets, Keyboard and Settings dock to the right; any number open at once, and the panel re-zooms to the space left rather than being covered
- **Quantizer indicator** — a one-octave strip showing which semitones the selected scale lets through (read-only; see below)
- Real-time parameter control via MIDI CC (CC map mirrors `src/param_map.cpp`)
- Full patch dump / restore over SysEx (AlloyFlux protocol: `7D 41 46 ...`)
- Preset save/load/reset (10 slots, stored in module flash)
- Log-scale filter cutoff slider; per-parameter unit display
- FX chain signal-flow visualiser
- Interactive MIDI keyboard (QWERTY mapping + velocity presets)
- Web Serial console drawer — send commands, view output, command history (↑/↓)
- MIDI monitor drawer — decoded traffic both directions, with port names, TX/RX byte counters, hex view, pause and per-type filters
- Live mirroring of the module — knob moves, button combos and preset recalls on the device appear in the UI via CC feedback
- Import / export patches as `.syx` SysEx files

## Stack

| Layer        | Technology                                        |
| ------------ | ------------------------------------------------- |
| UI framework | [Svelte 5](https://svelte.dev) (runes API)        |
| Language     | TypeScript                                        |
| Build tool   | [Vite 8](https://vite.dev)                        |
| Device I/O   | Web MIDI API + Web Serial API (Chrome / Edge 89+) |

No backend, no server-side code — the built output is a fully static site.

## Requirements

- **Node.js** 18 or later
- **npm** 9 or later
- A Chromium-based browser (Chrome, Edge, Opera) for Web MIDI, Web Serial and Web Bluetooth  
  — Firefox supports none of the three
- **iOS**: the app, not the browser. WebKit ships none of those APIs, every iOS
  browser is WebKit underneath, and an installed PWA there is still WebKit — so
  no browser on an iPhone can reach a module. See [BUILDING.md](BUILDING.md).

Building the native apps additionally needs a JDK and the Android SDK, or a Mac
with Xcode. Full toolchain setup is in **[BUILDING.md](BUILDING.md)**.

## Development

```bash
npm install        # install dependencies
npm run dev        # start Vite dev server with HMR at http://localhost:5173
```

## Build

```bash
npm run build      # production build → dist/
npm run preview    # serve the dist/ folder locally to verify before deploy
```

The `dist/` folder is a self-contained static site — serve it from any web server (note: Web Serial, Web MIDI and Web Bluetooth all require a secure context, so `file://` won't work and neither will a plain-HTTP LAN address; use `npm run preview`, or `make web-host` for HTTPS on the LAN).

The same `dist/` is what Capacitor wraps for the iOS and Android apps:

```bash
npm run sync           # copy the current dist/ into both native projects
npm run open:android   # open in Android Studio
npm run open:ios       # open in Xcode (macOS only)
```

⚠️ `sync` copies, it does not build — run `npm run build` first, or use the
`make app-*` targets from the repository root, which depend on it. Full
instructions: **[BUILDING.md](BUILDING.md)**.

## Type-check

```bash
npm run check      # svelte-check + tsc — reports TS/Svelte errors without building
```

## Project layout

```txt
src/
  app.css                 — global reset + the panel palette, as CSS custom
                            properties; components reference these, never hex
  App.svelte              — root component; patch state, utility rail, serial
                            console drawer
  components/
    ConnectionBar.svelte  — MIDI + Serial connect/disconnect controls
    PanelView.svelte      — panel view: the layout, drawn
    panel/
      Knob.svelte         — rotary control
      PanelSelect.svelte  — enum as segmented switch, LED ladder or dropdown
      PanelSection.svelte — one outlined functional group
      Glyph.svelte        — silkscreen glyphs (waveforms, filter responses,
                            unison spread, stereo width, …)
      DockPanel.svelte    — one panel in the right-hand utility rail
      ScaleKeys.svelte    — quantizer scale indicator
    MidiKeyboard.svelte   — QWERTY piano keyboard
    MidiMonitor.svelte    — MIDI traffic drawer (both directions, decoded)
    FxChainVisual.svelte  — signal-flow diagram
    EnvelopeGraph.svelte  — envelope shape on a log time axis
    FilterResponse.svelte — filter magnitude response on a log frequency axis
    PresetManager.svelte  — preset save/load/reset/import/export
  lib/
    scales.ts             — scale masks, mirrored from scale_quantizer.h
    panelLayout.ts        — panel layout: stage width, section spans, per-control
                            size / glyphs, all keyed by param name
    paramMap.ts           — CC parameter descriptors + ccToFloat/floatToCC helpers
    midi.ts               — Web MIDI store + connect/disconnect/send helpers
    midiDecode.ts         — raw MIDI bytes → readable text for the monitor
    serial.ts             — Web Serial store + connect/disconnect/send/onLine helpers
    patchSync.ts          — SysEx protocol encode/decode, .syx file I/O
    presets.ts            — preset SysEx commands + serial fallback
```

## Recommended IDE

[VS Code](https://code.visualstudio.com/) with the [Svelte extension](https://marketplace.visualstudio.com/items?itemName=svelte.svelte-vscode).
