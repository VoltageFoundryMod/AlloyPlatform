# Alloy Flux Web Configurator

A browser-based editor for the **Alloy Flux** Eurorack module by Voltage Foundry Modular.  
Connects to the module over **Web MIDI** and **Web Serial** to read and write all parameters in real time, manage presets, and monitor the serial console — no drivers or native app required.

## Features

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
- A Chromium-based browser (Chrome, Edge, Opera) for Web MIDI + Web Serial support  
  — Firefox does not support either API

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

The `dist/` folder is a self-contained static site — serve it from any web server or open `index.html` directly (note: Web Serial requires a secure context, so `file://` won't work; use `npm run preview` or a local server).

## Type-check

```bash
npm run check      # svelte-check + tsc — reports TS/Svelte errors without building
```

## Project layout

```txt
src/
  App.svelte              — root component; patch state, serial console drawer
  components/
    ConnectionBar.svelte  — MIDI + Serial connect/disconnect controls
    ParamSlider.svelte    — labelled range slider (linear & log scale)
    ParamSelect.svelte    — labelled enum selector
    MidiKeyboard.svelte   — QWERTY piano keyboard
    MidiMonitor.svelte    — MIDI traffic drawer (both directions, decoded)
    FxChainVisual.svelte  — signal-flow diagram
    PresetManager.svelte  — preset save/load/reset/import/export
  lib/
    paramMap.ts           — CC parameter descriptors + ccToFloat/floatToCC helpers
    midi.ts               — Web MIDI store + connect/disconnect/send helpers
    midiDecode.ts         — raw MIDI bytes → readable text for the monitor
    serial.ts             — Web Serial store + connect/disconnect/send/onLine helpers
    patchSync.ts          — SysEx protocol encode/decode, .syx file I/O
    presets.ts            — preset SysEx commands + serial fallback
```

## Recommended IDE

[VS Code](https://code.visualstudio.com/) with the [Svelte extension](https://marketplace.visualstudio.com/items?itemName=svelte.svelte-vscode).
