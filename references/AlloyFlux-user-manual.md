# Alloy Flux — User Manual

> **Firmware status: M1–M15** (PAIR mode, serial console, drift, chorus, stereo width, envelope/VCA)
> Hardware: Raspberry Pi Pico 2 (RP2350) + PCM5102A DAC

---

## Table of Contents

- [Alloy Flux — User Manual](#alloy-flux--user-manual)
  - [Table of Contents](#table-of-contents)
  - [What Alloy Flux Is](#what-alloy-flux-is)
  - [Connecting](#connecting)
  - [Serial Console](#serial-console)
  - [Parameters](#parameters)
    - [Pitch and Tuning](#pitch-and-tuning)
      - [`pitch <hz>` — Base frequency](#pitch-hz--base-frequency)
      - [`detune <hz>` — Stereo spread](#detune-hz--stereo-spread)
    - [SHAPE — Waveform](#shape--waveform)
      - [`shape <0–1>` — Waveform morph](#shape-01--waveform-morph)
    - [FATNESS — Sub Oscillator](#fatness--sub-oscillator)
      - [`fat <0–1>` — Sub oscillator level](#fat-01--sub-oscillator-level)
    - [MOTION — Drift and Animation](#motion--drift-and-animation)
      - [`motion <0–1>` — Drift + chorus depth](#motion-01--drift--chorus-depth)
      - [`dspeed <0.001–0.1>` — Drift glide speed](#dspeed-000101--drift-glide-speed)
      - [`chorus <off | I | II | I+II>` — Chorus mode](#chorus-off--i--ii--iii--chorus-mode)
    - [SPACE — Stereo Width](#space--stereo-width)
      - [`space <0–1>` — Stereo width](#space-01--stereo-width)
      - [`gate <1 | 0 | free>` — Gate control](#gate-1--0--free--gate-control)
      - [`curve <0–1>` — Envelope shape](#curve-01--envelope-shape)
      - [`curvetime <0.25–4>` — Envelope time scale](#curvetime-0254--envelope-time-scale)
    - [Volume](#volume)
      - [`vol <0–1>` — Master volume](#vol-01--master-volume)
  - [Knob Shift Functions](#knob-shift-functions)
  - [Gate and Envelope Modes](#gate-and-envelope-modes)
    - [Drone mode (default)](#drone-mode-default)
    - [Triggered mode (via serial for now)](#triggered-mode-via-serial-for-now)
  - [Status](#status)
  - [Sound Design Recipes](#sound-design-recipes)
    - [Juno-style string pad](#juno-style-string-pad)
    - [Plucky synth bass](#plucky-synth-bass)
    - [Warm detuned pad (drone)](#warm-detuned-pad-drone)
    - [Slow cinematic swell](#slow-cinematic-swell)
    - [Hollow nasal lead](#hollow-nasal-lead)
    - [Unstable vintage synth](#unstable-vintage-synth)
  - [Diagnostic Commands](#diagnostic-commands)
    - [`cpu`](#cpu)
    - [`perf on` / `perf off`](#perf-on--perf-off)
  - [Command Quick Reference](#command-quick-reference)
  - [Default Values](#default-values)

---

## What Alloy Flux Is

Alloy Flux is a stereo dual-oscillator voice for Eurorack. It generates two detuned voices in stereo with:

- **5-shape continuous wavetable morphing** (sine → triangle → saw → pulse → hollow)
- **Sub oscillator** one octave below each voice, adding warmth and body
- **Drift engine** — per-voice random frequency wander simulating analogue instability
- **AR envelope + VCA** — gate-controlled articulation; unpatched runs as a continuous drone

With a V/OCT input and a gate source you have a self-contained melodic voice. With just audio out it plays continuously as a drone or pad.

---

## Connecting

| Connection            | Purpose                      |
| --------------------- | ---------------------------- |
| USB to computer       | Serial console (115200 baud) |
| DAC left/right out    | Stereo audio output          |
| *(future)* V/OCT jack | Pitch CV — 1V/octave         |
| *(future)* GATE jack  | Envelope trigger             |

Open any serial terminal (Arduino Serial Monitor, `tio`, PuTTY, etc.) at **115200 baud**. You should see:

```txt
AlloyFlux ready. Type 'help' for commands.
```

---

## Serial Console

Commands are typed as `name value` followed by Enter. Examples:

```txt
pitch 440
shape 0.5
gate 1
```

- Names are case-sensitive, all lowercase
- Values can be integers or decimals
- Sending `status` prints all current parameters at once
- Sending `help` lists every available command

---

## Parameters

### Pitch and Tuning

#### `pitch <hz>` — Base frequency

Range: 20–8000 Hz

Sets the root pitch of both voices. Both voices share this pitch; RELATION (detune) spreads them apart.

```txt
pitch 220         # A3
pitch 261.63      # C4 (middle C)
pitch 440         # A4 (concert A)
pitch 523.25      # C5
```

**MIDI note to Hz reference:**

| Note | Hz     |
| ---- | ------ |
| C3   | 130.81 |
| A3   | 220.00 |
| C4   | 261.63 |
| E4   | 329.63 |
| A4   | 440.00 |
| C5   | 523.25 |

#### `detune <hz>` — Stereo spread

Range: 0–200 Hz

Spreads voice 1 (left) and voice 2 (right) symmetrically around the base pitch. Voice 1 goes down by half the detune amount, voice 2 goes up. The beating rate between the two voices equals the detune value.

```txt
detune 0          # mono, both voices at the same pitch
detune 2          # very subtle chorus-like thickening
detune 8          # warm ensemble, ~8 Hz beating rate
detune 20         # xylophone-thick spread
detune 50         # wide stereo, clearly two pitches
```

---

### SHAPE — Waveform

#### `shape <0–1>` — Waveform morph

Range: 0.0–1.0

Continuously morphs between five band-limited wavetables. Intermediate values crossfade smoothly — there are no steps or clicks.

| Value | Waveform           | Character                    |
| ----- | ------------------ | ---------------------------- |
| 0.00  | Sine               | Pure, no harmonics           |
| 0.25  | Triangle           | Warm, soft, gentle harmonics |
| 0.50  | Saw                | Bright, full harmonic series |
| 0.75  | Pulse (50%)        | Hollow, reedy                |
| 1.00  | Narrow pulse (25%) | Nasal, oboe-like             |

```txt
shape 0           # pure sine — clean but thin
shape 0.25        # triangle — most useful for pads and strings
shape 0.5         # saw — Juno-style, synth bass, leads
shape 0.75        # pulse — reedy pad character
shape 0.35        # between tri and saw — classic polysynth pad
```

---

### FATNESS — Sub Oscillator

#### `fat <0–1>` — Sub oscillator level

Range: 0.0–1.0

Adds a square wave sub oscillator one octave below each voice. At `fat 1.0` the sub contributes 50% of the main oscillator's amplitude. The sub is always square-shaped regardless of the SHAPE setting.

```txt
fat 0             # no sub — thinner, clearer
fat 0.3           # subtle warmth, good for most sounds
fat 0.6           # significant body — synth bass territory
fat 1.0           # maximum sub — full-bodied bass/pad
```

> Note: high FATNESS values add significant low-frequency energy. Reduce volume if needed to prevent output clipping.

---

### MOTION — Drift and Animation

`motion` is a shared depth control: it drives **both** the frequency drift engine and the **chorus** simultaneously. At zero, the module is static and the chorus is fully bypassed (dry signal). As motion increases, voices begin to wander and the stereo chorus thickens the image — both effects scale together, giving a single knob that moves from "cold and precise" to "warm and alive."

#### `motion <0–1>` — Drift + chorus depth

Range: 0.0–1.0

Controls how much each voice wanders away from its nominal pitch, and simultaneously sets the chorus wet mix.

| Value | Drift   | Chorus wet | Character                |
| ----- | ------- | ---------- | ------------------------ |
| 0.0   | none    | 0%         | Static, precise          |
| 0.2   | subtle  | 12%        | Gentle warmth            |
| 0.4   | natural | 24%        | Ensemble feel            |
| 0.7   | audible | 42%        | String machine character |
| 1.0   | heavy   | 60%        | Full Juno-style stereo   |

At `motion=1.0` the mix is 40% dry / 60% chorus — the fundamental stays present while the modulated delays add width and shimmer.

```txt
motion 0          # static, clean — good for bass or exact tuning
motion 0.2        # subtle warmth, barely perceptible on short notes
motion 0.4        # natural ensemble feel — recommended starting point
motion 0.7        # clearly audible wander + chorus width — string machine character
motion 1.0        # full drift + full chorus — heavy vintage ensemble
```

#### `dspeed <0.001–0.1>` — Drift glide speed

Range: 0.001–0.10, default: 0.04

Controls how quickly each voice glides toward its new random target frequency. This is independent from how far it drifts (that is set by `motion`).

| Value | Time constant | Character                      |
| ----- | ------------- | ------------------------------ |
| 0.005 | ~1.6 s        | Glacial, very slow wander      |
| 0.04  | ~0.20 s       | Default, natural analogue feel |
| 0.08  | ~0.10 s       | Fast, slightly jittery         |

```txt
dspeed 0.01       # slow wander — smooth gentle drift
dspeed 0.04       # default
dspeed 0.08       # faster — more restless
```

#### `chorus <off | I | II | I+II>` — Chorus mode

Default: `I+II`

Selects the Juno-inspired chorus character. `motion` sets the wet depth; `chorus` selects which LFO configuration is active. Phasors always keep running even when `off`, so switching modes is click-free.

| Mode   | LFO config                        | Character                                      |
| ------ | --------------------------------- | ---------------------------------------------- |
| `off`  | pass-through                      | Dry — pure oscillator sound, no width          |
| `I`    | both channels: slow LFO (0.51 Hz) | Subtle thickening, gentle width                |
| `II`   | both channels: fast LFO (0.62 Hz) | Deeper warble, more pronounced movement        |
| `I+II` | L=slow, R=fast                    | Maximum stereo spread — classic Juno character |

```txt
chorus off        # bypass — dry signal
chorus I          # subtle Juno type I
chorus II         # deeper warble type II
chorus I+II       # full stereo spread (default)
```

> `chorus off` with `motion 0` = completely dry, static, precise.
> `chorus I+II` with `motion 0.6` = classic Juno string ensemble character.

---

### SPACE — Stereo Width

`space` controls the width of the final stereo image using a mid-side processor. At the default (`1.0`) the signal passes through unchanged — full independent L/R from detune and chorus. Reducing it narrows the field progressively toward mono.

#### `space <0–1>` — Stereo width

Range: 0.0–1.0, default: 1.0

| Value | Character                                          |
| ----- | -------------------------------------------------- |
| 0.0   | Mono — both channels carry the summed centre image |
| 0.5   | Narrowed — stereo information halved               |
| 1.0   | Full stereo — identity, no processing (default)    |
| 1.5   | Hyper-wide — side signal amplified 1.5×            |
| 2.0   | Maximum — side doubled, L/R fully anti-correlated  |

```txt
space 1.0         # default — full stereo
space 0.5         # narrower field — useful in dense mixes
space 0.0         # mono — both channels identical
space 1.5         # hyper-wide — exaggerates detune and chorus spread
space 2.0         # maximum anti-correlation — dramatic spatial effect
```

> SPACE operates after chorus in the signal chain: `Osc → Drift → VCA → Chorus → SPACE → Output`. At `space > 1.0` output is clamped to ±32512 to prevent clipping. Mono compatibility decreases above 1.0 — ideal for full-stereo patches.

---

The CURVE system gives the module basic articulation without an external envelope/VCA. When a gate is active, the module shapes each note with an Attack–Release (AR) envelope.

#### `gate <1 | 0 | free>` — Gate control

| Value  | Behaviour                                                                              |
| ------ | -------------------------------------------------------------------------------------- |
| `free` | **Drone mode** (default) — envelope bypassed, module sounds continuously at full level |
| `1`    | Gate high — attack phase begins. Enables envelope mode.                                |
| `0`    | Gate low — release phase begins.                                                       |

Sending `gate 1` then `gate 0` triggers a complete note event.

#### `curve <0–1>` — Envelope shape

Range: 0.0–1.0

Morphs both attack and release times simultaneously.

| Value | Attack  | Release | Best for                       |
| ----- | ------- | ------- | ------------------------------ |
| 0.0   | ~1 ms   | ~80 ms  | Pluck, marimba, click          |
| 0.2   | ~6 ms   | ~120 ms | Short staccato                 |
| 0.5   | ~50 ms  | ~300 ms | Natural, sustaining pads       |
| 0.75  | ~200 ms | ~500 ms | Slow pad, soft strings         |
| 1.0   | ~800 ms | ~1 s    | Swell, evolving pad, cinematic |

**Pluck mode:** below `curve 0.2` the envelope decays automatically at its peak even if the gate is still held. This gives percussive one-shot behaviour regardless of gate length.

**Sustain mode:** above `curve 0.2`, holding the gate sustains the note at full level between attack peak and release.

#### `curvetime <0.25–4>` — Envelope time scale

Range: 0.25–4.0, default: 1.0

Scales both attack and release times uniformly. The CURVE shape is preserved — only the overall speed changes. The range is intentionally symmetric in log space — each doubling of `curvetime` takes the same number of steps whether you are going faster or slower, giving fine-grained control over short times while still reaching very slow swells.

The future hardware knob will use `powf(4.0f, (knob - 0.5f) * 2.0f)` so that the center detent always locks to 1.0×.

| Value | Effect                                                             |
| ----- | ------------------------------------------------------------------ |
| 0.25  | 4× faster — great for fast staccato sequences                      |
| 1.0   | Default                                                            |
| 2.0   | 2× slower — longer swells                                          |
| 4.0   | 4× slower — very slow cinematic blooms (3.2 s attack at curve=1.0) |

```txt
# Tight plucky bass:
curve 0.1
curvetime 0.5
gate 1
gate 0

# Slow string swell:
curve 1.0
curvetime 2.0
gate 1

# Natural voiced pad:
curve 0.5
curvetime 1.0
gate 1
gate 0
```

---

### Volume

#### `vol <0–1>` — Master volume

Range: 0.0–1.0, default: 0.8

```txt
vol 0.5           # quieter
vol 0.8           # default
vol 1.0           # maximum (caution with high fatness)
```

---

## Knob Shift Functions

The panel has a single button that doubles as a **shift key**. Holding it while turning a knob accesses a secondary parameter — four knobs have shift functions, giving eight parameters from seven knobs.

| Knob  | Primary parameter               | Shift parameter (hold button)     |
| ----- | ------------------------------- | --------------------------------- |
| SHAPE | `shape` — waveform morph        | `fat` — sub oscillator level      |
| MOTN  | `motion` — drift + chorus depth | `dspeed` — drift glide rate       |
| CURVE | `curve` — envelope shape        | `curvetime` — envelope time scale |
| SPACE | `space` — stereo width          | `vol` — master output volume      |

The LED indicator dims white while shift mode is active. ROOT, RELATION, and FM knobs have no shift function.

> **Current firmware:** shift is not yet implemented (Milestone 31). All secondary parameters are accessed via serial commands in the meantime.

---

## Gate and Envelope Modes

### Drone mode (default)

No gate patching required. The module sounds continuously. Useful for:

- Testing timbre without an envelope
- Pads and drones that sustain indefinitely
- Tuning and sound design exploration

```txt
# Factory default — just plug in audio and hear sound:
gate free         # (this is the default on power-up)
pitch 220
detune 4
shape 0.25
fat 0.4
motion 0.4
```

### Triggered mode (via serial for now)

Enable the envelope by sending any gate command other than `free`.

```txt
# Simple note sequence at A3:
pitch 220
curve 0.4
curvetime 1.0
gate 1            # note on — attack begins
gate 0            # note off — release begins

# Retrigger a new note (change pitch between gate 1 events):
gate 0
pitch 261.63
gate 1
gate 0
pitch 329.63
gate 1
gate 0
```

> When hardware gate jack (GP12) is connected in a future milestone, it will write the same `gGateHigh` flag directly — the serial `gate` command and the hardware jack are identical in effect.

---

## Status

`status` prints all current parameters in a single line:

```txt
pitch=220.00 detune=4.00 shape=0.250 fat=0.400 motion=0.400 dspeed=0.0400 curve=0.500 ctime=1.00 gate=free chorus=I+II vol=0.800 space=1.000
```

---

## Sound Design Recipes

### Juno-style string pad

```txt
pitch 220
detune 5
shape 0.25
fat 0.5
motion 0.5
dspeed 0.04
curve 0.75
curvetime 1.5
gate free
```

### Plucky synth bass

```txt
pitch 110
detune 1
shape 0.5
fat 0.7
motion 0.1
curve 0.0
curvetime 0.8
gate 1
gate 0
```

### Warm detuned pad (drone)

```txt
pitch 130.81
detune 8
shape 0.3
fat 0.4
motion 0.6
dspeed 0.03
gate free
```

### Slow cinematic swell

```txt
pitch 261.63
detune 3
shape 0.2
fat 0.3
motion 0.5
curve 1.0
curvetime 3.0
gate 1
```

### Hollow nasal lead

```txt
pitch 440
detune 0
shape 0.9
fat 0.0
motion 0.15
curve 0.3
curvetime 0.7
gate 1
gate 0
```

### Unstable vintage synth

```txt
pitch 220
detune 6
shape 0.5
fat 0.3
motion 0.9
dspeed 0.06
gate free
```

---

## Diagnostic Commands

### `cpu`

Prints audio ISR timing, CPU headroom, and overrun count (requires `CPU_PROFILE` build flag).

```txt
audio ISR: 2us / 30us  headroom: 93.3%  overruns: 0
```

### `perf on` / `perf off`

Enables or disables automatic CPU reporting every 5 seconds to the serial console.

---

## Command Quick Reference

| Command             | Range         | Description                                                    |
| ------------------- | ------------- | -------------------------------------------------------------- |
| `pitch <hz>`        | 20–8000       | Base frequency                                                 |
| `detune <hz>`       | 0–200         | Symmetric stereo spread between voices                         |
| `shape <0–1>`       | 0–1           | Waveform: 0=sine 0.25=tri 0.5=saw 0.75=pulse 1=hollow          |
| `fat <0–1>`         | 0–1           | Sub oscillator level (0=off, 1=50% of main)                    |
| `motion <0–1>`      | 0–1           | Frequency drift + chorus depth (0=dry/static, 1=full)          |
| `dspeed <n>`        | 0.001–0.1     | Drift glide speed (τ coefficient)                              |
| `chorus <mode>`     | off/I/II/I+II | Chorus mode (default: I+II)                                    |
| `space <0–2>`       | 0–2           | Stereo width (0=mono, 1=full stereo, 2=hyper-wide, default: 1) |
| `curve <0–1>`       | 0–1           | Envelope shape (0=pluck, 1=swell)                              |
| `curvetime <n>`     | 0.25–4        | Envelope time scale (1=default)                                |
| `gate <1\|0\|free>` | —             | Gate high / low / bypass (drone)                               |
| `trig [ms]`         | —             | One-shot gate pulse (default 100 ms)                           |
| `vol <0–1>`         | 0–1           | Master volume                                                  |
| `status`            | —             | Print all current parameters                                   |
| `cpu`               | —             | Audio ISR timing and headroom                                  |
| `perf on\|off`      | —             | Auto CPU reporting every 5 s                                   |
| `help`              | —             | List all commands                                              |

---

## Default Values

| Parameter   | Default | Notes                             |
| ----------- | ------- | --------------------------------- |
| `pitch`     | 440 Hz  | A4                                |
| `detune`    | 0 Hz    | No spread                         |
| `shape`     | 0.0     | Sine                              |
| `fat`       | 0.4     | Sub slightly audible              |
| `motion`    | 0.0     | Static                            |
| `dspeed`    | 0.04    | ~0.20 s drift glide               |
| `chorus`    | I+II    | Juno I+II stereo spread           |
| `space`     | 1.0     | Full stereo (identity, range 0–2) |
| `curve`     | 0.5     | Natural AR                        |
| `curvetime` | 1.0     | Normal speed                      |
| `gate`      | free    | Drone, envelope bypassed          |
| `vol`       | 0.8     |                                   |
