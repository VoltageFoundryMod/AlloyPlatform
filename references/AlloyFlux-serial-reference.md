# Alloy Flux — Serial Console Reference

## Developer / Advanced Configuration

This document covers serial console commands, flash preset management, and configuration details for Alloy Flux. These features are primarily intended for development, testing, and advanced users willing to use a USB serial terminal.

For the user-facing manual (panel controls, voice modes, MIDI CC map), see [AlloyFlux-user-manual.md](AlloyFlux-user-manual.md).

---

## Connecting

Connect via USB. Alloy Flux appears as both a USB MIDI device and a USB serial CDC device simultaneously. Open a serial terminal at any baud rate (CDC ignores baud).

The serial console is gated behind `#define SERIAL_CONTROL` in the firmware — it compiles out of release builds.

---

## Commands

All commands are entered as plain text followed by Enter. Commands are parsed non-blocking in `updateControl()` at 128 Hz.

### Oscillator & Pitch

```txt
pitch 440            → ROOT frequency in Hz
note A4              → ROOT frequency by note name (A4=440, C4=261.63, etc.)
```

### Core Parameters

```txt
relation 0.3         → RELATION position (0.0–1.0)
shape 0.5            → SHAPE morph (0=sine, 1=hollow pulse)
motion 0.4           → MOTION depth (0.0–1.0)
color 0.5            → COLOR: FM depth in PAIR/CASCADE, fine Hz spread in ensemble (0.0–1.0)
curve 0.5            → CURVE envelope shape (0=pluck, 1=swell)
space 0.7            → SPACE stereo width (0.0–2.0)
vol 0.8              → master output volume (0.0–1.0)
```

### Gate & Drone

```txt
gate 1               → set gate high (arm articulation)
gate 0               → set gate low (release)
gate free            → drone mode — continuous voice without gate
```

### Voice Mode

```txt
mode pair            → PAIR mode (dual oscillator)
mode cloud           → CLOUD mode (ensemble)
mode chord           → CHORD mode (harmonic stack)
mode cascade         → CASCADE mode (FM synthesis)
mode string          → STRING mode (vintage ensemble)
mode poly            → POLY mode (4-voice polyphony)
chord <name|0-10>    → CHORD mode convenience: set chord shape by name or index
                       (unison|power|minor|major|sus2|sus4|maj7|min7|dom7|dim|octaves)
```

### Sub Oscillator

```txt
fat <0–1>            → sub oscillator level (0=off, 1=full at 50% of main)
suboct <1|2>         → sub octave: 1=one octave below (default), 2=two octaves below
```

### Drift

```txt
dspeed <0.001–0.1>   → drift glide speed (how fast voices step toward new targets)
```

### Chorus

```txt
chorus off           → bypass chorus
chorus I             → Juno type I (slow, subtle width)
chorus II            → Juno type II (faster, deeper warble)
chorus I+II          → both LFOs (maximum stereo spread, default)
```

### Filter

```txt
filter lp 2000 0.6   → SVF low-pass at 2 kHz, resonance 0.6
filter hp 400        → SVF high-pass at 400 Hz, current resonance
filter bp 1000 0.5   → SVF band-pass
filter notch 800     → SVF notch filter
filter off           → hard bypass
filter type svf      → switch to Cytomic SVF algorithm (default)
filter type ladder   → switch to OTA 4-pole ladder (LP4, self-oscillating at res=1.0)
```

### Effect Chain Ordering

```txt
fxorder filter pre   → filter before chorus (default — shapes raw voice)
fxorder filter post  → filter after chorus (sculpts chorused mix)
fxorder delay pre    → delay feeds into reverb (spacious, default)
fxorder delay post   → reverb feeds into delay (echoed reverb tail)
```

### Reverb

```txt
reverb 0.4 0.7 0.5   → mix=0.4, size=0.7, damping=0.5
reverb on            → re-enable with current settings
reverb off           → disable (Core 1 outputs zeros — zero bus traffic)
reverb freeze on     → freeze reverb tail (decay→1.0, input gated)
reverb freeze off    → resume normal reverb decay
reverb modspeed <v>  → LFO rate multiplier 0.1–4.0 (default 1.0)
reverb moddepth <v>  → LFO depth multiplier 0.0–1.0 (default 1.0)
```

### Delay

```txt
delay 0.5 150 0.6    → mix=0.5, time_ms=150, feedback=0.6
delay on             → re-enable with current settings
delay off            → hard bypass (zero CPU)
```

### Envelope

```txt
env type ar          → AR (attack + release) envelope (default)
env type adsr        → ADSR envelope
env loop on          → loop envelope (turns it into a cycling LFO)
env loop off         → normal gated mode
adsr 0.01 0.2 0.7 0.5  → attack=0.01s, decay=0.2s, sustain=0.7, release=0.5s
```

### MIDI Channel

```txt
midichan 3           → respond only to MIDI channel 3
midichan omni        → respond to all channels (default)
```

### Velocity Sensitivity

```txt
veloc on             → MIDI note velocity scales output volume (default)
veloc off            → velocity ignored — all notes play at global volume
veloc                → print current state
```

### Flash Config Presets

```txt
config save          → save current state to auto-save slot (slot 0)
config save 1        → save current state to user preset slot 1 (slots 1–9)
config load          → load from auto-save slot
config load 3        → load user preset slot 3
config reset         → reset auto-save slot to factory defaults
config reset 2       → reset user preset slot 2 to factory defaults
config reset all     → factory reset all slots (slots 0–9)
```

Slot 0 is the auto-save live state (rate-limited to once every 10 seconds). Slots 1–9 are user presets saved and recalled on demand. Presets persist across power cycles.

### Diagnostics

```txt
status               → print all current parameter values (two lines: voice + fx chain)
cpu                  → print CPU headroom report (ISR time used / available)
perf [on|off]        → enable/disable detailed CPU report printout every 5 seconds
```

---

## Flash Preset Layout

| Slot | Purpose                                                |
| ---- | ------------------------------------------------------ |
| 0    | Auto-save live state — written on change, rate-limited |
| 1–9  | User presets — explicit save/load                      |

`config reset all` wipes every slot. Next boot applies factory defaults.

The `AlloyConfig` struct covers all synthesis and effects parameters: shape, motion, curve, curvetime, relation, color, fatness, suboct, driftspeed, space, volume, midiChannel, velocitySensitive, filter (cutoff/res/mode/type), envelope type (AR/ADSR), full ADSR parameters, reverb (enabled/mix/size/damping/modSpeed/modDepth/frozen), delay (time/feedback/mix), FxOrder flags, and chorus mode.

---

## MIDI SysEx Preset Backup

Alloy Flux supports SysEx dump and restore of the full `AlloyConfig` struct. This allows preset backup/restore via any DAW or MIDI controller that supports SysEx, without needing the Web Configurator.

SysEx format details and integration instructions are available in the firmware source at `src/usb_midi.cpp`.

---

## Web Configurator

A browser-based configurator connects via Web MIDI API (Chrome/Edge). It provides:

- Real-time parameter control via MIDI CC
- Voice mode selection and display
- Preset save/load via SysEx
- MIDI channel configuration
- Visualisation of internal state (chord shape, mode)

No driver or installation required. Open in Chrome/Edge, select Alloy Flux as the MIDI device.

---

*Firmware architecture, hardware pin map, CPU profiling methods, and development milestones are documented in [AlloyFlux-module-reference.md](AlloyFlux-module-reference.md).*

---

Voltage Foundry Modular — Alloy Flux · 2026
