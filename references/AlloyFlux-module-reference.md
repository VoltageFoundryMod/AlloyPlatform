# Alloy Flux

## Dual Relation Oscillator — Juno Inspired Stereo Voice

---

## Table of Contents

- [Alloy Flux](#alloy-flux)
  - [Dual Relation Oscillator — Juno Inspired Stereo Voice](#dual-relation-oscillator--juno-inspired-stereo-voice)
  - [Table of Contents](#table-of-contents)
  - [Vision](#vision)
  - [Core Philosophy](#core-philosophy)
    - [Relationship Synthesis](#relationship-synthesis)
  - [Sound Character Goals](#sound-character-goals)
  - [Technical Overview](#technical-overview)
  - [Hardware Stack](#hardware-stack)
    - [RP2350 Peripheral Usage](#rp2350-peripheral-usage)
    - [RP2350 Pin Usage](#rp2350-pin-usage)
  - [Power Architecture](#power-architecture)
  - [Signal Chain](#signal-chain)
  - [Voice Modes](#voice-modes)
    - [PAIR (default)](#pair-default)
    - [CLOUD](#cloud)
    - [CHORD](#chord)
    - [CASCADE](#cascade)
    - [STRING](#string)
  - [Oscillator Architecture](#oscillator-architecture)
  - [Voice and Polyphony Architecture](#voice-and-polyphony-architecture)
    - [Voice Slot Definition](#voice-slot-definition)
    - [Per-Mode Slot Layout](#per-mode-slot-layout)
    - [Polyphony from MIDI / I2C](#polyphony-from-midi--i2c)
    - [Summing and Normalisation](#summing-and-normalisation)
  - [Wave Morphing — SHAPE](#wave-morphing--shape)
    - [Implementation — Wavetable Crossfade (Milestone 10)](#implementation--wavetable-crossfade-milestone-10)
  - [Sub Oscillator — FATNESS](#sub-oscillator--fatness)
    - [Sub Oscillator Characteristics](#sub-oscillator-characteristics)
    - [FATNESS Hardware Interaction — Button Shift](#fatness-hardware-interaction--button-shift)
    - [Mixing and Clip Safety](#mixing-and-clip-safety)
  - [FM Philosophy](#fm-philosophy)
  - [Drift and Motion System](#drift-and-motion-system)
    - [Implementation — Frequency Drift (Milestone 11)](#implementation--frequency-drift-milestone-11)
  - [Chorus Philosophy](#chorus-philosophy)
    - [Implementation — ChorusEngine (Milestone 12)](#implementation--chorusengine-milestone-12)
  - [SPACE Engine — Stereo Width](#space-engine--stereo-width)
    - [Implementation — SpaceEngine (Milestone 13)](#implementation--spaceengine-milestone-13)
  - [CURVE Engine — Envelope and Amplitude](#curve-engine--envelope-and-amplitude)
    - [Gate Sources](#gate-sources)
    - [Envelope Shape](#envelope-shape)
    - [VCA Placement — Before Chorus](#vca-placement--before-chorus)
    - [Implementation Notes](#implementation-notes)
    - [Implementation — CurveEngine (Milestone 15)](#implementation--curveengine-milestone-15)
  - [Front Panel Controls](#front-panel-controls)
    - [ROOT](#root)
    - [RELATION *(signature control — largest knob)*](#relation-signature-control--largest-knob)
    - [SHAPE](#shape)
    - [MOTION](#motion)
    - [FM](#fm)
    - [CURVE](#curve)
    - [SPACE](#space)
    - [Shift Function Summary](#shift-function-summary)
  - [Panel Layout — 14HP](#panel-layout--14hp)
  - [Jack Assignment](#jack-assignment)
    - [Input Row 1 — Primary Inputs](#input-row-1--primary-inputs)
    - [Input Row 2 — Modulation Inputs](#input-row-2--modulation-inputs)
    - [Output Row](#output-row)
  - [ADC Philosophy \& CV Input Conditioning](#adc-philosophy--cv-input-conditioning)
    - [ADC Strategy](#adc-strategy)
    - [V/OCT Input Conditioning](#voct-input-conditioning)
    - [Modulation CV Inputs (through mux)](#modulation-cv-inputs-through-mux)
    - [FM IN Conditioning](#fm-in-conditioning)
    - [Gate Input](#gate-input)
    - [V/Oct Calibration Routine](#voct-calibration-routine)
  - [Analog Multiplexer](#analog-multiplexer)
    - [Mux Channel Map](#mux-channel-map)
    - [Attenuverter Logic via Jack Switch Detection](#attenuverter-logic-via-jack-switch-detection)
  - [Audio Output Stage](#audio-output-stage)
    - [PCM5102A](#pcm5102a)
    - [Op-Amp Gain Stage](#op-amp-gain-stage)
    - [Passive Mono Sum](#passive-mono-sum)
  - [MIDI Implementation](#midi-implementation)
    - [Connections](#connections)
    - [Supported Messages](#supported-messages)
    - [MIDI Channel](#midi-channel)
  - [User Interface — Screenless](#user-interface--screenless)
    - [LED Language](#led-language)
    - [LED Behavior Per Mode](#led-behavior-per-mode)
    - [Button Interaction Map](#button-interaction-map)
  - [Internal Modulation Philosophy](#internal-modulation-philosophy)
  - [Default Behavior](#default-behavior)
  - [Firmware Architecture](#firmware-architecture)
    - [Dual Core DSP Split](#dual-core-dsp-split)
    - [DSP Components](#dsp-components)
    - [Mozzi Configuration](#mozzi-configuration)
    - [Golden Rules](#golden-rules)
    - [Parameter Smoothing](#parameter-smoothing)
  - [CPU Profiling \& Headroom](#cpu-profiling--headroom)
    - [The Core Problem](#the-core-problem)
    - [Method 1 — Timing Pin Toggle](#method-1--timing-pin-toggle)
    - [Method 2 — Hardware Cycle Counter (Recommended)](#method-2--hardware-cycle-counter-recommended)
    - [Method 3 — Idle Core Load Meter](#method-3--idle-core-load-meter)
    - [Method 4 — Audio Glitch / Overrun Counter](#method-4--audio-glitch--overrun-counter)
    - [Voice Budget Estimates — RP2350](#voice-budget-estimates--rp2350)
    - [Dual Core Offload Strategy](#dual-core-offload-strategy)
  - [Component BOM](#component-bom)
  - [PlatformIO Setup](#platformio-setup)
    - [Pico 2 / RP2350 Production Target](#pico-2--rp2350-production-target)
  - [Prototyping Plan](#prototyping-plan)
    - [Phase 1 — Minimal Protoboard](#phase-1--minimal-protoboard)
    - [Serial Console Control](#serial-console-control)
    - [Teletype I2C Integration](#teletype-i2c-integration)
    - [Web USB Configurator (future expansion)](#web-usb-configurator-future-expansion)
  - [Development Milestones](#development-milestones)
  - [Project Refinement](#project-refinement)
    - [Software](#software)
    - [Hardware](#hardware)
  - [Future Expansion](#future-expansion)
  - [Full Feature Summary](#full-feature-summary)

---

## Vision

Alloy Flux is not a traditional dual oscillator, a feature-heavy digital voice, or an experimental FM noise machine.

Its identity is centered around:

- harmonic relationships
- stereo movement
- ensemble-like behavior
- playable immediacy
- lush and emotionally musical sound design
- internally animated voices

The module combines inspiration from:

- classic Roland Juno synthesizers
- Mannequins Just Friends relationship-based modular philosophy
- ensemble and chorus synthesis
- polyphonic harmonic distribution
- digitally controlled but musically restrained oscillator interaction

The goal is a Eurorack instrument that sounds rich and dimensional even when minimally patched.

---

## Core Philosophy

### Relationship Synthesis

Instead of exposing two entirely independent oscillators, Alloy Flux treats the second oscillator as a dynamic relationship to the first.

The module is conceptually organized as **ROOT** and **RELATION** — not Oscillator 1 and Oscillator 2.

This allows harmonic movement, interval behavior, stereo divergence, controlled detune, ensemble voicing, and FM interaction to feel coherent and musical rather than arbitrary.

The RELATION knob is the signature control of the module. It is the most expressive and defining element of the panel — physically larger than the others, positioned prominently.

---

## Sound Character Goals

**The sonic identity should prioritize:**

- lush stereo imaging
- smooth ensemble movement
- warm digital oscillators
- restrained and musical FM
- animated drift
- soft phase instability
- playable harmonic intervals
- comfortable and emotional timbres

**The module should actively avoid:**

- harsh metallic FM by default
- chaotic digital aliasing behavior
- excessive menu-driven complexity
- overly technical interaction paradigms
- anything that requires reading to perform

---

## Technical Overview

| Parameter   | Value                                                                              |
| ----------- | ---------------------------------------------------------------------------------- |
| Format      | Eurorack                                                                           |
| Width       | 14HP                                                                               |
| MCU         | Raspberry Pi Pico 2 — RP2350 (dual Cortex-M33 @ 150MHz)                            |
| DAC         | PCM5102A (I2S, 16-bit, 112dB SNR)                                                  |
| Output      | Stereo L/R — passive mono normalled on Left when R unplugged                       |
| Voice modes | PAIR / CLOUD / CHORD / CASCADE / STRING                                            |
| Framework   | Arduino + Mozzi 2.x                                                                |
| Build tool  | PlatformIO                                                                         |
| Knobs       | 7 (ROOT, RELATION, SHAPE, MOTION, FM, CURVE, SPACE)                                |
| Jacks       | 10 (V/OCT, GATE, MIDI, REL CV, SHAPE CV, MOTION CV, FM IN, SPACE CV, L OUT, R OUT) |
| Buttons     | 1                                                                                  |
| LEDs        | 3× WS2812B RGB                                                                     |
| Power draw  | ~100mA +12V, ~5mA −12V (estimate)                                                  |

---

## Hardware Stack

```txt
┌──────────────────────────────────────────────────────┐
│             RASPBERRY PI PICO 2 (RP2350)             │
│             Dual Cortex-M33 @ 150MHz                 │
│             4MB Flash / 520KB RAM                    │
│                                                      │
│  PIO I2S ────────────────────────→ PCM5102A          │
│  ADC GP26 ←──────────────────────── V/OCT pitch CV  │
│  ADC GP27 ←──────────────────────── FM IN (audio)   │
│  ADC GP28 ←──── 74HC4067 ←────────── all knobs +    │
│                                       slow CVs       │
│  UART1 RX (GP9) ←────────────────── MIDI TRS in     │
│  USB ←────────────────────────────── USB MIDI       │
│  PIO GP7 ────────────────────────→ WS2812B LEDs     │
│  GP10 ←───────────────────────────── Button         │
│  GP12 ←───────────────────────────── Gate input     │
│  GP13 PWM ───────────────────────→ (spare / future) │
│  GP3–GP6 ────────────────────────→ 74HC4067 select  │
└──────────────────────────────────────────────────────┘
```

### RP2350 Peripheral Usage

| Peripheral | Usage                                                      |
| ---------- | ---------------------------------------------------------- |
| PIO 0      | I2S audio output to PCM5102A (BCK=26, LCK=27, DATA=28)     |
| PIO 1      | WS2812B LED data (GP7)                                     |
| PIO 2      | Spare — future use                                         |
| ADC GP26   | V/OCT pitch CV — direct, fast reads                        |
| ADC GP27   | FM IN — direct, audio-rate reads in updateAudio()          |
| ADC GP28   | 74HC4067 mux signal — all knobs + slow CVs + jack switches |
| UART1 RX   | Hardware MIDI in on GP9                                    |
| USB        | USB MIDI device — native, no pins consumed                 |
| Core 0     | UI, ADC scanning, MIDI, modulation routing                 |
| Core 1     | Audio DSP — oscillators, chorus, drift, spatializer        |
| Timer (hw) | Mozzi audio rate interrupt                                 |

---

### RP2350 Pin Usage

All pins accounted for. No pin used twice.

| GPIO | Pico Pin | Function              | In/Out | Notes                                                 |
| ---- | -------- | --------------------- | ------ | ----------------------------------------------------- |
| GP0  | 1        | I2S DATA (SD)         | Out    | PCM5102 serial data — PIO 0                           |
| GP1  | 2        | I2S BCK               | Out    | PCM5102 bit clock — PIO 0                             |
| GP2  | 4        | I2S LRCLK             | Out    | PCM5102 LR clock — PIO 0                              |
| GP3  | 5        | Mux S0                | Out    | 74HC4067 select bit 0                                 |
| GP4  | 6        | Mux S1                | Out    | 74HC4067 select bit 1                                 |
| GP5  | 7        | Mux S2                | Out    | 74HC4067 select bit 2                                 |
| GP6  | 9        | Mux S3                | Out    | 74HC4067 select bit 3                                 |
| GP7  | 10       | WS2812B data          | Out    | LED chain (all 3 LEDs) — PIO 1                        |
| GP8  | 11       | UART1 TX              | Out    | Spare / debug serial                                  |
| GP9  | 12       | UART1 RX              | In     | Hardware MIDI in (TRS jack)                           |
| GP10 | 14       | Button                | In     | Internal pull-up — single button                      |
| GP11 | 15       | Spare                 | —      | Future expansion                                      |
| GP12 | 16       | Gate input            | In     | Note trigger — direct digital read                    |
| GP13 | 17       | Spare / LFO CV future | Out    | PWM → RC filter → op-amp if LFO CV output added later |
| GP14 | 19       | I2C External          | —      | SDA 1 for I2C external comm                           |
| GP15 | 20       | I2C External          | —      | SCL 1 for I2C external comm                           |
| GP16 | 21       | Spare                 | —      | Future expansion                                      |
| GP17 | 22       | Spare                 | —      | Future expansion                                      |
| GP18 | 24       | Spare                 | —      | Future expansion                                      |
| GP19 | 25       | Spare                 | —      | Future expansion                                      |
| GP20 | 26       | Spare                 | —      | Future expansion                                      |
| GP21 | 27       | Spare                 | —      | Future expansion                                      |
| GP22 | 29       | Spare                 | —      | Future expansion                                      |
| GP26 | 31       | ADC0 — V/OCT pitch    | In     | Direct ADC, fast reads, 1V/oct tracking               |
| GP27 | 32       | ADC1 — FM IN          | In     | Direct ADC, audio-rate reads in updateAudio()         |
| GP28 | 34       | ADC2 — Mux signal     | In     | 74HC4067 SIG — all knobs + slow CVs + jack switches   |
| GP25 | internal | Onboard LED           | Out    | Debug only                                            |
| —    | 36       | 3.3V out              | Pwr    | Powers PCM5102, 74HC4067                              |
| —    | 39       | VSYS                  | Pwr    | System power from Eurorack via LDO                    |
| —    | 40       | VBUS                  | Pwr    | USB 5V                                                |

**Spare GPIO: GP11, GP13–GP22 — 11 pins available for future features.**

---

## Power Architecture

```txt
Eurorack +12V ──→ LDO (MCP1700-3302) ──→ 3.3V ──→ Pico 2 VSYS
                                                    PCM5102A VCC
                                                    74HC4067 VCC
Eurorack +12V / −12V ───────────────────────────→ TL072 op-amps
Eurorack +12V ──→ ferrite bead + 100µF ─────────→ clean analog rail
```

- Pico 2 runs from 3.3V via VSYS — cleaner for audio than 5V via VBUS
- Check if MCP1700-3302 can supply enough current for both Pico 2 and PCM5102A + 74HC4067; if not, consider a higher current LDO or separate regulators
- Check if better to use rail-to-rail op-amps to run from a separate 0-6V to provide full 0-5V headroom for the output
- PCM5102A and 74HC4067 both on the same 3.3V LDO rail
- Op-amps on ±12V directly for full Eurorack output swing
- Add ferrite bead + 100µF electrolytic + 100nF ceramic on each rail before the circuit
- Add P-channel MOSFET reverse polarity protection on the power header
- Keep Pico 2 SMPS switching node away from analog signal traces on PCB — use ground plane separation

---

## Signal Chain

```txt
MIDI TRS / USB MIDI / V/OCT + GATE
            │
            ▼
     Pitch calculation
     (1V/oct, MIDI note → freq, oversampled ADC)
            │
            ▼
┌───────────────────────────┐
│  ROOT oscillator          │
│  SHAPE morph engine       │◄── SHAPE CV (mux)
│  + FM from RELATION       │◄── FM IN (GP27 direct)
│  + drift engine           │
└──────────┬────────────────┘
           │
┌──────────▼────────────────┐
│  RELATION engine          │◄── REL CV (mux)
│  interval / detune /      │
│  spread / ratio           │
│  (mode-dependent)         │
└──────────┬────────────────┘
           │
┌──────────▼────────────────┐
│  Drift & Motion engine    │◄── MOTION CV (mux)
│  phase drift / stereo     │
│  animation / chorus mod   │
└──────────┬────────────────┘
           │
┌──────────▼────────────────┐
│  VCA / CURVE envelope     │◄── GATE jack / MIDI / I2C
│  AR envelope, audio rate  │◄── CURVE knob (mux)
│  pluck ↔ swell morph      │
└──────────┬────────────────┘
           │
   ┌───────┴───────┐
   ▼               ▼
Chorus L        Chorus R
(BBD-inspired)  (different phase/rate)
   │               │
   ▼               ▼
SPACE engine    SPACE engine    ◄── SPACE CV (mux)
(stereo width,  (stereo width,
 placement)      placement)
   │               │
   ▼               ▼
TL072 gain      TL072 gain
(±12V rails)    (±12V rails)
   │               │
   ▼               ▼
L OUT jack      R OUT jack
(mono norm.)    (stereo R)
```

---

## Voice Modes

Mode is selected by tapping the single button. LED pattern confirms the current mode. Five modes cycle in sequence.

### PAIR (default)

Primary dual-oscillator mode. ROOT and RELATION as two voices.

- RELATION follows ROOT with interval relationships
- controlled detune and stereo divergence
- normalized internal FM routing (RELATION → ROOT)
- the foundation mode — most direct and immediate

### CLOUD

Multi-voice detuned ensemble mode. Inspired by vintage polysynths and string machines.

- internally distributed detune across multiple virtual voices
- animated stereo positioning — voices drift slowly in the field
- slow phase drift between voices
- supersaw-like density without aggressive harshness
- RELATION knob controls the ensemble spread and density

### CHORD

Interval and harmonic stack mode.

- internally generated chord intervals from a chord table
- RELATION knob sweeps through chord shapes continuously
- REL CV morphs chord character from an external source
- stereo harmonic placement — intervals distributed across the field
- dynamic spread behavior controlled by SPACE

Chord table (RELATION knob position → intervals in semitones):

| Position | Chord      | Intervals     |
| -------- | ---------- | ------------- |
| 0.0      | Unison     | 0, 0, 0, 0    |
| 0.1      | Power      | 0, 7, 12, 19  |
| 0.2      | Minor      | 0, 3, 7, 12   |
| 0.3      | Major      | 0, 4, 7, 12   |
| 0.4      | Sus2       | 0, 2, 7, 12   |
| 0.5      | Sus4       | 0, 5, 7, 12   |
| 0.6      | Major 7    | 0, 4, 7, 11   |
| 0.7      | Minor 7    | 0, 3, 7, 10   |
| 0.8      | Dominant 7 | 0, 4, 7, 10   |
| 0.9      | Diminished | 0, 3, 6, 9    |
| 1.0      | Octaves    | 0, 12, 24, 36 |

Future: alternate chord tables, scale quantization, adaptive harmony.

### CASCADE

Oscillator interaction mode. Musical and restrained FM.

- chained modulation relationships between ROOT and RELATION
- FM interaction with soft-clipped depth
- phase influence and animated harmonic motion
- RELATION knob controls interaction depth and character
- deliberately bounded to avoid metallic FM harshness
- soft-clipped internally — FM depth ranges are musically scaled

> Implementation note: depth limiting, ratio constraints, and soft clipping must be in place before this mode is considered complete. The goal is harmonic warmth, not noise.

### STRING

Vintage ensemble inspired mode. The most atmospheric mode.

- distributed microdetune across all internal voices
- animated chorus interaction — movement never fully stops
- voice drift and slow phase instability
- stereo width enhancement — SPACE has the strongest effect here
- slow ensemble movement inspired by vintage string machines
- Juno chorus character embedded into the synthesis layer

---

## Oscillator Architecture

Oscillators should:

- remain anti-aliased across the full pitch range
- preserve low-end warmth
- support soft waveform transitions without zipper noise
- maintain tuning stability — no drift from temperature or load
- support smooth interpolation between shapes

**Implementation approaches (choose or combine):**

- polyBLEP for anti-aliasing at waveform discontinuities
- wavetable interpolation for morphing between shapes
- band-limited additive for specific timbres

---

## Voice and Polyphony Architecture

### Voice Slot Definition

Each voice slot is one `ShapeOsc` instance — a single-phase oscillator that morphs
continuously across all five waveforms. Voice slots are the fundamental building block
of every mode.

```txt
Voice slot = 1 ShapeOsc instance
           = 1 shared phase accumulator (32-bit)
           + 5 wavetable pointers + pre-computed crossfade position
           ≈ 24 bytes RAM
```

All active voice slots run simultaneously, every audio sample. Their outputs are
summed and scaled before reaching the stereo output.

### Per-Mode Slot Layout

Modes differ only in how voice slots are pitched and panned — the synthesis engine
(ShapeOsc morph, CURVE envelope, chorus) is identical across all modes.

**PAIR** (default, 2 voices):

```txt
slot[0]  freq = ROOT − DETUNE/2   →  L output
slot[1]  freq = ROOT + DETUNE/2   →  R output
```

**CHORD** (4 voices, Milestone 23):

```txt
slot[0]  ROOT + interval[0]  →  hard L
slot[1]  ROOT + interval[1]  →  soft L
slot[2]  ROOT + interval[2]  →  soft R
slot[3]  ROOT + interval[3]  →  hard R

Interval set from chord table; RELATION knob sweeps chord shape.
All slots share the same SHAPE, CURVE, and MOTION settings.
```

**CLOUD** (4–8 voices, Milestone 22):

```txt
slot[0..7]  ROOT ± micro-detune (animated by MOTION)
            stereo position drifts slowly per slot
```

### Polyphony from MIDI / I2C

A voice allocator maps incoming note events to ROOT pitch:

```txt
MIDI Note On  (note=60)  ─┬───────────────────────────────┐
I2C command   (note=67)  ─┤  voice allocator → set ROOT freq      │
V/OCT + GATE            ─┼───────────────────────────────┤
MIDI Note Off (note=60)  ─┘  → release (trigger CURVE release phase)

PAIR mode:  one logical note fans to [ROOT] and [ROOT + RELATION interval]
CHORD mode: one logical note fans to 4 calculated interval slots
CLOUD mode: one logical note fans to N micro-detuned, drifting slots
```

### Summing and Normalisation

With N active voice slots, sum and scale to prevent clipping:

```cpp
int32_t sum = 0;
for (int i = 0; i < N; i++) sum += voices[i].next();
sum /= N;  // normalise to ±32512 range
```

8-voice CLOUD and 2-voice PAIR produce identical output levels — consistent with
Eurorack expectations.

---

## Wave Morphing — SHAPE

Instead of a waveform selector switch, Alloy Flux uses continuous waveform morphing.

The SHAPE control sweeps through a timbral spectrum:

```txt
fully CCW ──────────────────────────────────── fully CW
  sine     triangle     saw     pulse     hollow pulse
```

Benefits:

- one knob covers all timbral territory
- SHAPE CV enables real-time timbral animation
- no stepped waveform switching — everything is a blend
- harmonic brightness increases from left to right

### Implementation — Wavetable Crossfade (Milestone 10)

Five band-limited wavetables are generated at startup via additive synthesis with
Lanczos sigma smoothing (same technique as the original saw table).
`maxH = 37` at 32768 Hz, band-limited from 440 Hz upward.

| SHAPE | Table        | Harmonics        | Amplitude law                |
| ----- | ------------ | ---------------- | ---------------------------- |
| 0.00  | Sine         | fundamental only | Mozzi constant array (flash) |
| 0.25  | Triangle     | odd: 1, 3, 5, …  | σ · (−1)^((h−1)/2) / h²      |
| 0.50  | Saw          | all: 1, 2, 3, …  | σ / h                        |
| 0.75  | Pulse (50%)  | odd: 1, 3, 5, …  | σ / h                        |
| 1.00  | Hollow (25%) | all: 1, 2, 3, …  | σ · sin(h·π/4) / h           |

σ = Lanczos sigma: `sinc(h·π / (maxH+1))`, where h is the harmonic number.

`ShapeOsc<UPDATE_RATE>` holds five table pointers and one phase accumulator:

- `setFreq(f)` and `setShape(s)` called at control rate (128 Hz) from `updateControl()`
- `next()` called at audio rate (32768 Hz) from `updateAudio()` — **no float ops**;
  crossfade pair and blend factor are pre-computed in `setShape()`
- Both adjacent tables are read at the **same phase** — no phase discontinuity
  when SHAPE changes mid-note
- `_blend` is `uint8_t` 0–255; integer crossfade is `(s0*(256-b) + s1*b) >> 8`

---

## Sub Oscillator — FATNESS

The classic Juno sound is never just one waveform. The original Juno-106 sums a saw
oscillator, a pulse oscillator (with PWM), and a sub oscillator (one or two octaves
below, fixed square) through individual level sliders. The simultaneous presence of
these layers — especially the sub — is what makes the Juno sound physically large.

Alloy Flux captures this without requiring the player to manually balance multiple
oscillator levels. A single **FATNESS** parameter controls how much of an octave-down
square sub oscillator is blended into each voice:

```txt
FATNESS = 0.0  —  pure SHAPE morph, no sub
FATNESS = 0.4  —  default: sub audible, adds body without dominating (Juno-ish)
FATNESS = 1.0  —  sub at 50% of main level, very fat, reduce VOL to taste
```

### Sub Oscillator Characteristics

- **Pitch**: always one octave below the voice’s ROOT frequency (`freq × 0.5`)
- **Waveform**: fixed square (SHAPE = 0.75 in the 5-table spectrum) — never morphs
- **Level**: 0 to 50% of main oscillator amplitude (FATNESS = 0.0 to 1.0)
- **Per voice**: each voice (v1 L, v2 R) has its own independent sub oscillator
- **SHAPE independence**: SHAPE knob changes the main voice character, sub is always square

### FATNESS Hardware Interaction — Button Shift

The panel has one SHAPE knob but two related parameters: SHAPE (main morph) and FATNESS
(sub level). The single button acts as a **shift key**:

```txt
SHAPE knob alone         —  adjusts main oscillator morph (sine → hollow)
[hold BUTTON] + SHAPE    —  adjusts FATNESS (sub octave level 0 → full)

LED feedback while in shift mode: dim white fill on the shape LED
```

This is the same interaction pattern used on many modern Eurorack modules (e.g. Make
Noise Maths alt-function via button hold). No extra panel hardware required.

> Implementation note: the button-shift detection belongs in the UI state machine
> (Milestone 30). For current development, `fat <0–1>` is the serial command.

### Mixing and Clip Safety

With sub at full level (FATNESS=1.0) and VOL=1.0, the combined signal can reach 150%
of the ±32512 range. The firmware soft-clips to ±32512 before the volume stage, so
behavior is graceful (saturation, not digital wraparound). At the default FATNESS=0.4
and VOL=0.8 the total is safely within range at all times.

---

## FM Philosophy

FM in Alloy Flux is intentionally restrained. The goal is warmth, harmonic animation, and movement — not metallic chaos or abrasive digital textures.

**FM characteristics:**

- linear FM preferred — cleaner sidebands, more musical at moderate depths
- soft-clipped internally — depth is bounded to musical ranges
- musically scaled depth control — full CW is not "maximum FM index"
- normalized RELATION → ROOT modulation in PAIR and CASCADE modes
- external FM IN overrides internal normalization

**FM IN jack (GP27 direct ADC):**

- audio-rate capable — read in `updateAudio()` not `updateControl()`
- bipolar input (±5V Eurorack) — scaled and clamped before ADC
- FM knob acts as depth/attenuverter when FM IN is patched

---

## Drift and Motion System

One of the most important synthesis layers. Movement should exist at multiple levels simultaneously:

- oscillator phase drift — subtle independent phase wander per voice
- detune drift — microdetune amounts shift slowly over time
- stereo motion — voice positions animate slowly in the stereo field
- chorus modulation variance — LFO rates are not perfectly stable
- voice timing offsets — subtle timing differences between voices
- envelope variance — slight variation in response curve per note

The MOTION knob controls the overall depth of all drift and animation. At zero, the module is stable and static. As MOTION increases, the module becomes more alive. At maximum, the movement is substantial but remains musical.

**MOTION CV** allows external control of animation depth — an envelope into MOTION gives notes that bloom and settle organically.

### Implementation — Frequency Drift (Milestone 11)

`DriftEngine<N_VOICES>` is a template class that runs independently per voice at
control rate (128 Hz). Each voice has its own LCG pseudo-random state so drift
patterns are always independent.

**Per-voice state:**

```txt
_freqOffset  float   current Hz offset applied to voice's nominal pitch
_freqTarget  float   Hz target the voice is slowly gliding toward
_timer       uint8_t ticks until a new random target is selected
_seed        uint32_t LCG state (Numerical Recipes multiplier)
```

**Each control tick:**

1. `_freqOffset` is one-pole smoothed toward `_freqTarget` (τ ≈ 0.31 s @ 128 Hz)
2. `_timer` counts down; when it reaches 0:
   - a new wait of 32–127 ticks (0.25–1.0 s) is chosen randomly
   - a new `_freqTarget` in `±MOTION · kMaxDriftHz` is chosen randomly

**Parameters:**

| Constant      | Value        | Meaning                                |
| ------------- | ------------ | -------------------------------------- |
| `kMaxDriftHz` | 2.5 Hz       | Maximum wander per voice at MOTION=1.0 |
| `kDriftSpeed` | 0.025        | One-pole LP coefficient (τ ≈ 0.31 s)   |
| Timer range   | 32–127 ticks | Time between new targets (0.25–1.0 s)  |

**Integration in `updateControl()`:**

```cpp
// 1. Apply drift offsets to nominal pitch before setFreq()
float freq1 = max(gBaseFreq - gDetune*0.5f + gDrift.offset(0), 20.0f);
float freq2 = max(gBaseFreq + gDetune*0.5f + gDrift.offset(1), 20.0f);

// 2. Advance engine (picks new targets, updates offsets)
gDrift.update(sMotion);
```

Sub oscillators always track `freq × 0.5`, so drift is consistent across the full voice.

**Future drift layers** (added incrementally as complexity grows):

- Stereo position drift — slow L/R pan variation per voice (multi-voice modes)
- Chorus modulation variance — LFO rate jitter in the chorus engine (Milestone 12)
- Detune drift — the DETUNE amount itself wanders slightly at high MOTION

---

## Chorus Philosophy

The chorus in Alloy Flux is part of the synthesis engine — not a post-effect applied at the end of the signal chain.

The chorus system contributes to:

- stereo image construction
- dimensionality and depth
- motion and animation
- ensemble realism
- harmonic softness

**Implementation approach:**

- multi-phase delay modulation — multiple delay taps at offset phases
- BBD-inspired modulation curves — non-linear LFO shapes for vintage character
- stereo phase offsets between L and R delay lines
- randomized modulation variance — no two cycles are identical
- MOTION knob depth feeds directly into chorus modulation depth

In STRING mode the chorus is the dominant synthesis element. In PAIR mode it provides subtle width. In CLOUD it contributes to ensemble density.

### Implementation — ChorusEngine (Milestone 12)

`ChorusEngine<SAMPLE_RATE>` template class in `include/ChorusEngine.h`, running in **Core 0 `updateAudio()` ISR** (~50 cycles, no trig in hot path).

| Parameter      | Value                        | Notes                                    |
| -------------- | ---------------------------- | ---------------------------------------- |
| Center delay   | 7 ms = 229 samples           | BBD-range, audible chorus character      |
| LFO mod depth  | ±3 ms = ±98 samples          | max read delay ~327 samples              |
| Buffer size    | 1024 samples = 31 ms         | 8 KB BSS (two int32 buffers)             |
| LFO rate L     | 0.513 Hz                     | slightly asymmetric for organic spread   |
| LFO rate R     | 0.618 Hz                     | golden-ratio spacing above L             |
| LFO phase init | L=0°, R=90°                  | maximum L/R independence                 |
| LFO algorithm  | quadrature phasor recurrence | 4 multiplies/sample, no sinf/cosf in ISR |
| Mix law        | `wet = depth × 0.6`          | depth=1.0 → 40% dry + 60% wet            |
| Interpolation  | linear (fractional delay)    | warble-free at all modulation depths     |

**Chorus modes (`ChorusMode` enum, `chorus` serial command):**

| Mode   | LFO L           | LFO R           | Character                                                        |
| ------ | --------------- | --------------- | ---------------------------------------------------------------- |
| `off`  | —               | —               | Dry pass-through; phasors keep running for glitch-free re-enable |
| `I`    | slow (0.513 Hz) | slow (0.513 Hz) | Subtle width, Juno type I character                              |
| `II`   | fast (0.618 Hz) | fast (0.618 Hz) | Deeper warble, Juno type II character                            |
| `I+II` | slow (0.513 Hz) | fast (0.618 Hz) | Maximum stereo spread, default                                   |

**Depth control** — `gChorusDepth` (volatile float), written atomically by `updateControl()` at 128 Hz, read directly in ISR. No mutex or barrier needed — 32-bit aligned float reads are atomic on Cortex-M33.

**ISR safety** — `init()` called in Core 0 `setup()` before `startMozzi()`, so delay buffers are zeroed and phasors seeded before the first ISR fires. Overrun counter is zeroed at end of `setup()` to exclude Mozzi’s startup DMA/PIO initialisation artifacts.

**Signal chain:**

```txt
Osc → Drift → soft-clip → VCA (CURVE) → [Chorus, Core 0 ISR] → [SPACE] → Output
```

VCA before chorus is intentional (Juno-60 topology): envelope close lets chorus delay lines drain naturally — shimmer tail rather than abrupt cut.

---

## SPACE Engine — Stereo Width

SPACE maps a single `space` parameter (0–1) to a mid-side stereo width processor. At `space 1` (default) the signal passes through unchanged; reducing it narrows the field toward mono centre at `space 0`.

With AlloyFlux's stereo topology — voice 1 → L, voice 2 → R, independent chorus LFOs per channel — SPACE controls how far apart those two voices sit in the mix. This is particularly useful when the output feeds a mono chain, or when you want the module to sit more centrally in a dense patch.

| `space` | L output               | R output               | Character                     |
| ------- | ---------------------- | ---------------------- | ----------------------------- |
| 0.0     | (L+R)/2 (mono)         | (L+R)/2 (mono)         | Voices fully summed to centre |
| 0.5     | mid + side×0.5         | mid − side×0.5         | Narrowed stereo field         |
| 1.0     | L unchanged (identity) | R unchanged (identity) | Full stereo (default)         |

### Implementation — SpaceEngine (Milestone 13)

`SpaceEngine` stateless utility class in `include/SpaceEngine.h`, called after chorus in Core 0 `updateAudio()` ISR. The bypass guard (`sSpace < 0.995f`) skips the 6-op path entirely at the default setting.

| Parameter   | Value                                            | Notes                                                                |
| ----------- | ------------------------------------------------ | -------------------------------------------------------------------- |
| Algorithm   | mid-side                                         | `mid=(L+R)>>1`, `side=(L−R)>>1`, `outL=mid+side×w`                   |
| Width range | 0.0–2.0                                          | 0=mono, 1=identity (default), 2=hyper-wide; output clamped to ±32512 |
| ISR cost    | ~6 integer ops (skipped at default w=1.0)        |                                                                      |
| State       | none — stateless static method, no init required |                                                                      |

**Signal chain with SPACE:**

```txt
Osc → Drift → soft-clip → VCA (CURVE) → Chorus → [SPACE, Core 0 ISR] → Output
```

**Motion and SPACE** — `gChorusDepth = sMotion` already handles M14: the MOTION knob (or `motion` command) simultaneously drives drift amplitude and chorus depth. SPACE is independent and complements MOTION — wide + full motion = expansive; narrow + motion = animated but centred.

---

## CURVE Engine — Envelope and Amplitude

CURVE implements a single-knob AR envelope generator and digital VCA. It provides basic articulation without requiring an external envelope and VCA — a single V/OCT + GATE patch produces a complete, playable voice.

### Gate Sources

The envelope is triggered identically by any of these sources — all are equivalent:

- **GATE jack** (GP12) — direct digital input
- **MIDI Note On / Note Off** — from TRS or USB MIDI
- **I2C command** — from Teletype or other I2C controllers

Gate high → attack phase begins. Gate low → release phase begins. A single `gGateHigh` volatile flag written by Core 0 and read by the audio path unifies all sources.

### Envelope Shape

A single AR (attack + release) envelope. CURVE morphs both attack and release time simultaneously:

```txt
fully CCW — pluck                      fully CW — swell

  ▲                                      ▲
  │█                                     │         ████████
  │ █                                    │        █        █
  │  ██                                  │       █          █
  │    █████                             │      █            ████
  └──────────────►                       └────────────────────────►
   fast A, fast R                         slow A, sustain, slow R
   gate length irrelevant                 gate = sustain duration
```

| CURVE position | Attack  | Release | Sustain behaviour                    |
| -------------- | ------- | ------- | ------------------------------------ |
| 0.0 (pluck)    | ~1 ms   | ~80 ms  | Decays regardless of gate length     |
| 0.25           | ~10 ms  | ~150 ms | Brief sustain, gate starts to matter |
| 0.5 (natural)  | ~50 ms  | ~300 ms | Sustains while gate is held          |
| 0.75           | ~200 ms | ~500 ms | Sustains while gate is held          |
| 1.0 (swell)    | ~800 ms | ~1 s    | Sustains while gate is held          |

At the pluck extreme the amplitude decays even if the gate remains high — the envelope ignores gate length below approximately CURVE = 0.2. Above that the gate duration controls the sustain level, transitioning naturally to a full sustain+release model at CW.

### VCA Placement — Before Chorus

The digital VCA sits **before** the chorus in the signal chain. This is intentional and Juno-inspired: when the VCA closes, the chorus delay lines continue to drain, producing a natural shimmer tail — exactly the character that makes the Juno-60/106 chorus sound warm rather than abrupt.

```txt
Oscillators → Drift/Motion → [VCA — CURVE envelope] → Chorus → SPACE → Output
```

### Implementation Notes

- Envelope runs at audio rate — computed each sample in `updateAudio()`
- AR times mapped from CURVE knob value using exponential scaling (perceptually linear)
- `gGateHigh` written atomically by Core 0 from all gate sources; read-only in audio path
- Envelope value (0.0–1.0) multiplies the mixed oscillator signal before chorus input
- CURVE also governs modulation response timing: faster CURVE values make drift and chorus react more instantly to new notes

### Implementation — CurveEngine (Milestone 15)

`CurveEngine<SAMPLE_RATE>` template class in `include/CurveEngine.h`:

| Parameter          | Value                              | Notes                                   |
| ------------------ | ---------------------------------- | --------------------------------------- |
| Attack range       | 1 ms – 800 ms                      | `0.001 + curve² × 0.799` s              |
| Release range      | 80 ms – 1 000 ms                   | `0.080 + curve² × 0.920` s              |
| Pluck threshold    | curve ≤ 0.2                        | auto-release at peak, gate hold ignored |
| Coefficient method | `expf()` in `setCurve()` at 128 Hz | never called in audio ISR               |
| `next()`           | one-pole multiply/add              | no expf, branch-minimal, safe in ISR    |

**Gate patched behaviour** — controlled by `gGatePatched` (volatile bool, Core 0):

- `false` (default): envelope fixed at 1.0 — module sounds continuously (drone/pad)
- `true`: AR envelope active, triggered by rising/falling edges

**Serial commands:**

```txt
curve <0–1>          — set envelope shape (0=pluck, 0.5=natural, 1=swell)
curvetime <0.25–4>   — time scale: 0.25=4× faster  1.0=default  4.0=4× slower
gate 1               — gate high (attack); sets gGatePatched=true
gate 0               — gate low (release); sets gGatePatched=true
gate free            — bypass envelope (drone mode)
trig [ms]            — one-shot gate pulse (default 100 ms)
```

**`curvetime` scaling** — range 0.25–4.0, log-symmetric around 1.0 (the identity point). For the M30 hardware knob the conversion will be:

```c
gCurveTime = powf(4.0f, (knob - 0.5f) * 2.0f);
// knob=0.0 → 0.25×  knob=0.5 → 1.0×  knob=1.0 → 4.0×
```

This gives equal perceptual resolution at all speeds — the same physical travel doubles or halves the time regardless of position.

**Signal chain position:** oscillators → drift → soft-clip → **[VCA — CURVE envelope]** → chorus (M12) → output

Future gate sources: GP12 jack (M19), MIDI Note On/Off (M26/M28), I2C (Teletype).

---

## Front Panel Controls

### ROOT

Primary pitch and transposition control. Sets the pitch center of the ROOT oscillator and the global harmonic reference for all modes.

Associated jack: **V/OCT**

### RELATION *(signature control — largest knob)*

The defining control of Alloy Flux. Its meaning changes per mode but always governs the harmonic relationship between ROOT and everything else.

| Mode    | RELATION controls                             |
| ------- | --------------------------------------------- |
| PAIR    | interval / detune / stereo divergence         |
| CLOUD   | ensemble spread and voice density             |
| CHORD   | chord shape — sweeps through the chord table  |
| CASCADE | FM interaction depth and modulation character |
| STRING  | ensemble width and microdetune distribution   |

Associated jack: **REL CV**

REL CV with RELATION knob: when REL CV is patched, RELATION knob becomes an attenuverter for that CV (−1× to +1×, centre = no effect).

### SHAPE

Continuous waveform morphing from sine → triangle → saw → pulse → hollow pulse.

Associated jack: **SHAPE CV**

When SHAPE CV is patched, SHAPE knob becomes attenuverter for that CV.

**Button shift:** hold the panel button while turning SHAPE to adjust **FATNESS**
(sub octave square level). LED dims white during shift mode.

### MOTION

Controls the depth of all internal animation — drift, chorus modulation, stereo movement, phase instability, voice offset variance. One of the defining controls of Alloy Flux.

At zero: the module is stable and precise.
At maximum: the module breathes and drifts — string machine territory.

Associated jack: **MOTION CV**

When MOTION CV is patched, MOTION knob becomes attenuverter for that CV.

**Button shift:** hold the panel button while turning MOTION to adjust **DRIFTSPEED**
(drift glide rate — how quickly each voice steps toward a new random target).

### FM

Controls oscillator interaction depth. In most modes this sets the amount of RELATION → ROOT frequency modulation. Soft-clipped and musically scaled.

When **FM IN** is patched, this knob becomes the attenuverter for the external FM source.

### CURVE

Envelope and response shaping. Controls the transient character of each note.

- fully CCW: pluck — fast attack, fast decay (percussive)
- centre: natural — moderate attack, medium decay
- fully CW: swell — slow attack, sustained (pad-like)

Also affects modulation response timing and internal envelope curves. Gives Alloy Flux basic articulation without an external envelope + VCA.

**Button shift:** hold the panel button while turning CURVE to adjust **CURVETIME**
(overall envelope time scale — compresses or stretches both A and R uniformly).

### SPACE

Stereo and dimensional control. Sets the width, spread, and positional placement of voices in the stereo field.

- CCW: narrow — voices close together in the centre
- CW: wide — voices spread to full stereo field, phase offsets applied

In STRING mode SPACE has the strongest effect — it governs the perceived width of the entire ensemble.

Associated jack: **SPACE CV**

When SPACE CV is patched, SPACE knob becomes attenuverter for that CV.

**Button shift:** hold the panel button while turning SPACE to adjust **VOL**
(master output volume). LED dims white during shift mode.

### Shift Function Summary

All shift functions use the same gesture: **hold the panel button** while turning the knob. LED indicator dims white to signal shift mode is active. Releasing the button exits shift mode.

| Knob  | Primary function             | Shift function (hold button)    |
| ----- | ---------------------------- | ------------------------------- |
| SHAPE | Waveform morph (sine→hollow) | FATNESS — sub oscillator level  |
| MOTN  | Drift + chorus depth         | DRIFTSPEED — drift glide rate   |
| CURVE | Envelope shape (pluck→swell) | CURVETIME — envelope time scale |
| SPACE | Stereo width                 | VOL — master output volume      |

ROOT, RELATION, and FM knobs have no shift function — they occupy the full knob travel for precision.

---

## Panel Layout — 14HP

```txt
┌──────────────────────┐  14HP (70.96mm)
│                      │
│  ●  ROOT    ●  REL   │  2× WS2812B LEDs (top L and R)
│                      │
│  ◎  ROOT    ◎  RELN  │  2× large knobs — ROOT and RELATION
│                      │  RELATION is the largest knob on panel
│  ◉  SHAPE   ◉  MOTN  │  2× medium knobs
│                      │
│  ◉  FM      ◉  CURV  │  2× medium knobs
│                      │
│  ◉  SPACE             │  1× medium knob (centred)
│                      │
│     ●  [MODE]        │  1× WS2812B LED + 1× button
│                      │
├──────────────────────┤
│ VOCT GATE MIDI RELCV │  ← input row 1 (4 jacks)
│ SHPCV MTNCV FMIN SPCCV│  ← input row 2 (4 jacks)
│      L OUT   R OUT   │  ← audio outputs (2 jacks)
└──────────────────────┘
```

**HP:** 14HP (70.96mm panel width)
**Jacks:** 10× Thonkiconn PJ398SM (switched, vertical mount)
**Knobs:** 7× Alpha 9mm (ROOT and RELATION largest)
**Buttons:** 1× tactile panel mount
**LEDs:** 3× WS2812B addressable RGB chained on one data line (GP7)

---

## Jack Assignment

### Input Row 1 — Primary Inputs

| Jack | Label  | Function                                          | Pin/Path    |
| ---- | ------ | ------------------------------------------------- | ----------- |
| 1    | V/OCT  | Pitch — 1V/oct, 0–6V range                        | GP26 direct |
| 2    | GATE   | Note trigger / envelope / articulation            | GP12 direct |
| 3    | MIDI   | TRS MIDI in — Type A/B dual circuit               | GP9 UART1   |
| 4    | REL CV | RELATION modulation — interval/detune/chord morph | Mux CH8     |

### Input Row 2 — Modulation Inputs

| Jack | Label  | Function                                   | Pin/Path    |
| ---- | ------ | ------------------------------------------ | ----------- |
| 5    | SHP CV | SHAPE CV — waveform morph modulation       | Mux CH9     |
| 6    | MTN CV | MOTION CV — animation depth modulation     | Mux CH10    |
| 7    | FM IN  | FM input — audio-rate capable, bipolar ±5V | GP27 direct |
| 8    | SPC CV | SPACE CV — stereo width modulation         | Mux CH11    |

### Output Row

| Jack | Label | Function                                       |
| ---- | ----- | ---------------------------------------------- |
| 9    | L OUT | Left audio — passive mono sum when R unplugged |
| 10   | R OUT | Right audio — stereo                           |

**Passive mono sum:** 10kΩ resistor from each output rail meets at the L jack NC (normally-closed) switching contact. When R OUT is unpatched, both channels sum passively to L OUT. Requires no firmware involvement.

---

## ADC Philosophy & CV Input Conditioning

### ADC Strategy

The RP2350 internal ADC is acceptable for pitch with careful implementation:

- pitch range limited to 0–6V (6 octave range)
- heavy oversampling — 16–64× per reading
- moving average filter over multiple samples
- note hysteresis — frequency only updates when change exceeds threshold
- precision op-amp scaling stage before ADC
- RC filter before ADC pin (10kΩ + 100nF = 1.6kHz cutoff, removes RF noise)
- software calibration table (V/Oct calibration routine)
- All CV should be accounted as floats from 0.0 to 1.0 in firmware after scaling and calibration for better resolution and consistency.

Priority: **musical stability over raw response speed.** The pitch must not jitter — musical accuracy matters more than fast response.

### V/OCT Input Conditioning

```txt
Eurorack CV ──→ MCP6002 precision scaling ──→ RC filter ──→ GP26 (ADC0)
               (rail-to-rail, 3.3V supply)    (10kΩ + 100nF)
               output range: 0–3.0V           BAT48 clamps on ADC pin
```

### Modulation CV Inputs (through mux)

```txt
Eurorack CV ──→ 100kΩ + 100kΩ divider ──→ BAT48 clamps ──→ 74HC4067 input
               (halves ±5V to ±2.5V,      (clamp 0–3.3V)
                offset for ADC range)
```

### FM IN Conditioning

```txt
FM jack ──→ 100kΩ + 100kΩ divider ──→ BAT48 clamps ──→ GP27 (ADC1)
```

FM IN is read in `updateAudio()` at audio rate. Must not go through the mux.

### Gate Input

```txt
Eurorack gate (0–5V) ──→ 10kΩ series ──→ BAT48 clamp ──→ GP12
```

### V/Oct Calibration Routine

Triggered by: power on while holding button. User patches two reference voltages (1 and 3V) with user clicking for each CV and voltage, module measures CV at each, builds a correction table. Confirmed by LED white flash. No trimmer pots required.

---

## Analog Multiplexer

**IC:** 74HC4067 (16:1 analog mux)

- 3.3V compatible — GP3–GP6 drive select lines directly
- Analog-transparent and bidirectional
- Switching + settling time < 1ms — fine for control-rate reads
- All 16 channels utilized

### Mux Channel Map

| Ch   | Signal         | Type    | Notes                                              |
| ---- | -------------- | ------- | -------------------------------------------------- |
| CH0  | ROOT knob      | Pot     | Coarse pitch offset                                |
| CH1  | RELATION knob  | Pot     | Signature control — interval/detune/chord/FM depth |
| CH2  | SHAPE knob     | Pot     | Waveform morph position                            |
| CH3  | MOTION knob    | Pot     | Animation depth                                    |
| CH4  | FM knob        | Pot     | FM depth / attenuverter when FM IN patched         |
| CH5  | CURVE knob     | Pot     | Envelope / articulation shaping                    |
| CH6  | SPACE knob     | Pot     | Stereo width / placement                           |
| CH7  | Spare          | —       | Future knob or CV                                  |
| CH8  | REL CV jack    | Slow CV | RELATION modulation input                          |
| CH9  | SHAPE CV jack  | Slow CV | SHAPE modulation input                             |
| CH10 | MOTION CV jack | Slow CV | MOTION modulation input                            |
| CH11 | SPACE CV jack  | Slow CV | SPACE modulation input                             |
| CH12 | REL jack SW    | Digital | Thonkiconn NC — cable detect for REL CV            |
| CH13 | SHAPE jack SW  | Digital | Thonkiconn NC — cable detect for SHAPE CV          |
| CH14 | MOTION jack SW | Digital | Thonkiconn NC — cable detect for MOTION CV         |
| CH15 | SPACE jack SW  | Digital | Thonkiconn NC — cable detect for SPACE CV          |

**Select lines:** GP3 (S0), GP4 (S1), GP5 (S2), GP6 (S3)
**Signal pin:** GP28 (ADC2)

### Attenuverter Logic via Jack Switch Detection

Thonkiconn switching contacts are read through mux CH12–CH15. When a cable is detected:

```txt
No cable (jack switch open):
    Knob → sets parameter value directly

Cable present (jack switch closed):
    Knob → attenuverter for that CV
    parameter = lerp(knob_pos, -1.0, +1.0) × CV_voltage
    knob centre = CV has zero effect
    knob CW = full positive CV depth
    knob CCW = full negative CV depth (inverted)
```

| Jack     | Knob     | No cable              | Cable present              |
| -------- | -------- | --------------------- | -------------------------- |
| REL CV   | RELATION | Interval/detune set   | REL CV depth + polarity    |
| SHAPE CV | SHAPE    | Fixed waveform shape  | Shape CV depth + polarity  |
| MTN CV   | MOTION   | Fixed animation depth | Motion CV depth + polarity |
| SPC CV   | SPACE    | Fixed stereo width    | Space CV depth + polarity  |

FM IN / FM knob always functions as attenuverter — FM amount is always relative to input signal (or internal normalization when unpatched).

---

## Audio Output Stage

### PCM5102A

Outputs approximately ±1V (2Vpp line level). Eurorack standard is ±5V (10Vpp). Gain required: ~5×.

### Op-Amp Gain Stage

```txt
PCM5102 OUT ──→ 10µF film cap (AC coupling) ──→ TL072 gain ──→ Eurorack jack
```

Inverting configuration: Rin = 10kΩ, Rf = 47kΩ → gain ≈ −4.7×

One TL072 (dual) handles both L and R channels. Powered from ±12V Eurorack rails.

### Passive Mono Sum

```txt
Left signal  ──10kΩ──┐
                      ├──→ L jack NC contact (normalled when R unplugged)
Right signal ──10kΩ──┘

L jack NO → Left channel (stereo when cable inserted)
R jack NO → Right channel
```

---

## MIDI Implementation

### Connections

| Type     | Hardware                       | Notes                              |
| -------- | ------------------------------ | ---------------------------------- |
| TRS MIDI | UART1 RX GP9, TRS jack         | Dual Type A/B via Kay LPZW circuit |
| USB MIDI | Native USB — no extra hardware | Shows as USB MIDI device on host   |

Both active simultaneously. Last-received source wins.

### Supported Messages

| Message         | Action                                                             |
| --------------- | ------------------------------------------------------------------ |
| Note On         | Set ROOT pitch + trigger GATE (monophonic, last-note priority)     |
| Note Off        | Release articulation                                               |
| Pitch Bend      | ±2 semitones (configurable via calibration routine)                |
| CC 1 Mod Wheel  | `motion` — drift + chorus depth (0–1)                            |
| CC 7 Volume     | `vol` — master output level (0–1)                                 |
| CC 64 Sustain   | `gate` — hold (≥64=on, <64=off)                                    |
| CC 71 Timbre    | `curve` — envelope shape (0–1)                                    |
| CC 72 Release   | `curvetime` — envelope time scale (0.25–4)                        |
| CC 73 Attack    | `dspeed` — drift glide speed (0.001–0.1)                         |
| CC 74 Brightness| `shape` — waveform morph (0–1)                                    |
| CC 91 Reverb    | `space` — stereo width (0–2)                                      |
| CC 92 Tremolo   | `detune` — symmetric fine spread (0–200 Hz)                        |
| CC 93 Chorus    | `fat` — sub oscillator level (0–1)                                |
| CC 94 Celeste   | `rel` — RELATION semitones above ROOT (0–24)                      |
| CC 123          | All Notes Off / panic                                              |
| Clock 0xF8      | MOTION sync to MIDI clock                                          |
| Program Change  | Voice mode select (1=PAIR, 2=CLOUD, 3=CHORD, 4=CASCADE, 5=STRING) |

All CC assignments are defined in `include/param_map.h` / `src/param_map.cpp` — a single shared table iterated by all transports. Adding a new parameter requires one row in that file only.

### MIDI Channel

Default: omni (responds to all channels). Configure via serial: `midichan 3` or `midichan omni`. Channel filter applies to Note On/Off, CC, and Program Change. Persisted to flash with `config save`.

---

## User Interface — Screenless

**Philosophy:** All feedback via 3 WS2812B RGB LEDs + 1 button. No menus. No reading required during performance. Eyes stay on the patch, not a display.

Hidden functions (behind long-hold) cover only: calibration, MIDI channel configuration, advanced settings. Core synthesis is always directly accessible.

The crucial UX distinction: mode changes are deliberate and infrequent. You choose a mode, you play, you hear the difference immediately. You don't need to remember what color means what — you just made that choice. This is why fixed-function jacks beat assignable jacks, and why 1 button with a clear mode cycle beats a menu.

### LED Language

**Consistent colour meaning across all contexts:**

| Colour       | Meaning                                                |
| ------------ | ------------------------------------------------------ |
| Warm red     | ROOT voice activity / left channel                     |
| Cool blue    | RELATION voice activity / right channel                |
| Green        | Motion / drift / animation activity                    |
| Purple       | Chorus active / STRING mode                            |
| Cyan         | CLOUD mode                                             |
| Amber/orange | CHORD mode / harmonic activity                         |
| White flash  | Confirmation — mode change, calibration complete, save |
| Off          | Idle / feature inactive                                |

**Brightness meaning:**

| Brightness     | Meaning                           |
| -------------- | --------------------------------- |
| Full           | Maximum value / strongly active   |
| Medium         | Moderate activity                 |
| Dim pulse      | Idle / minimal value              |
| Rhythmic pulse | Tracks internal motion / LFO rate |
| Off            | Inactive / disabled               |

### LED Behavior Per Mode

| Mode    | LED 1 (ROOT)                       | LED 2 (RELATION)                    | LED 3 (MODE/center)            |
| ------- | ---------------------------------- | ----------------------------------- | ------------------------------ |
| PAIR    | Warm red — root envelope level     | Cool blue — relation envelope level | Dim white pulse — motion depth |
| CLOUD   | Cyan — shifts with stereo position | Cyan — shifts opposite to LED 1     | Cyan pulse — ensemble motion   |
| CHORD   | Amber — root note activity         | Amber shift — chord interval spread | Amber — chord shape position   |
| CASCADE | Red/blue shift — FM interaction    | Red/blue — modulation activity      | Green pulse — FM depth         |
| STRING  | Purple — slow drift and width      | Purple — offset drift from LED 1    | Purple slow breathe — ensemble |

**Mode change:** Button tap → all three LEDs flash white briefly → settle into new mode colour. Immediate, unambiguous confirmation.

**Calibration (long hold):** LEDs cycle through white → flash confirmation pattern → return to normal.

### Button Interaction Map

| Action                | Result                                                           |
| --------------------- | ---------------------------------------------------------------- |
| Single tap            | Cycle voice mode: PAIR → CLOUD → CHORD → CASCADE → STRING → PAIR |
| Double tap            | Toggle MOTION sync to MIDI clock                                 |
| Long hold (3s)        | Enter V/Oct calibration routine                                  |
| Long hold + ROOT knob | MIDI channel select (LED shows channel 1–16 as brightness)       |

That is the complete button interaction surface. Nothing else is hidden. No color memorization required for performance — mode is chosen deliberately, confirmed by LED, heard immediately.

---

## Internal Modulation Philosophy

Alloy Flux should internally generate subtle movement even without patching. At zero MOTION, the module is stable. As MOTION increases, internal sources awaken:

- slow drift generators — independent per voice, not synchronized
- randomized LFO variance — no two chorus cycles are identical
- phase offsets between voices — constructive/destructive interference shifts slowly
- chorus modulation — always active, depth controlled by MOTION
- stereo movement — voice positions animate slowly in the field

This means a single V/OCT cable and gate produces a living, breathing sound — not a static tone.

---

## Default Behavior

The module should sound compelling with nothing more than one V/OCT cable and one gate. This is a critical design principle, not an optional feature.

With minimal patching, a user should immediately hear:

- stereo movement — voices spread naturally across the field
- harmonic richness — RELATION adds depth even at default position
- ensemble behavior — MOTION at default gives gentle animation
- subtle drift — the module breathes slightly even on held notes

---

## Firmware Architecture

### Dual Core DSP Split

The RP2350 dual core is the key architectural advantage. Core 0 handles all deterministic, low-latency control work. Core 1 handles all audio DSP continuously.

```txt
Core 0 — deterministic control:
├── ADC scanning via 74HC4067 (all knobs + slow CVs + jack switches)
├── Pitch CV reading (GP26 — oversampled, averaged, hysteresis)
├── Gate input reading (GP12)
├── MIDI parsing — UART1 + USB MIDI
├── Attenuverter logic (jack switch state → knob mode)
├── Parameter smoothing (one-pole LPF on all params)
├── Modulation routing matrix
├── LED update (WS2812B via PIO 1)
└── Button debounce and mode logic

Core 1 — audio DSP (runs continuously):
├── ROOT oscillator engine (polyBLEP / wavetable)
├── RELATION engine (interval, detune, spread — mode dependent)
├── SHAPE morph engine (continuous waveform interpolation)
├── Drift engine (phase, detune, stereo position per voice)
├── FM engine (linear, soft-clipped, depth-limited)
├── Chorus engine (multi-tap BBD-inspired, stereo, variance)
├── SPACE / stereo spatializer
└── Voice allocator (CLOUD / CHORD multi-voice)

Shared state (volatile struct, mutex-protected):
├── Core 0 writes: all parameter values
└── Core 1 reads: parameters each audio cycle
```

### DSP Components

| Module            | Core | Description                                           |
| ----------------- | ---- | ----------------------------------------------------- |
| Oscillator engine | 1    | ROOT voice — polyBLEP anti-aliased, SHAPE morphing    |
| Relation engine   | 1    | RELATION voice — interval/detune/ratio per mode       |
| Drift engine      | 1    | Per-voice phase drift, detune wander, timing variance |
| CURVE / VCA       | 1    | AR envelope, audio-rate VCA, pluck↔swell morph        |
| Chorus engine     | 1    | Multi-tap delay, BBD curves, stereo phase offsets     |
| SPACE spatializer | 1    | Stereo width, voice placement, phase offset           |
| Voice allocator   | 1    | Multi-voice management for CLOUD and CHORD modes      |
| Modulation matrix | 0    | Routes CV inputs to DSP parameters                    |
| MIDI parser       | 0    | Note, CC, clock, program change handling              |
| ADC scanner       | 0    | Mux scan, oversampling, smoothing                     |
| UI manager        | 0    | Button, LED, mode state machine                       |

### Mozzi Configuration

Mozzi is used for the audio framework on both MCUs.

**Pico 2 / RP2350 (production):**

```cpp
#include "MozziConfigValues.h"
#define MOZZI_AUDIO_MODE     MOZZI_OUTPUT_I2S_DAC
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#define MOZZI_AUDIO_BITS     16
#define MOZZI_AUDIO_RATE     32768
#define MOZZI_CONTROL_RATE   128          // higher rate with RP2350 headroom
#define MOZZI_I2S_PIN_BCK    1            // GP1
#define MOZZI_I2S_PIN_WS     2            // GP2
#define MOZZI_I2S_PIN_DATA   0            // GP0
#include "Mozzi.h"
```

### Golden Rules

```cpp
// ✅ updateAudio() — audio ISR, fires on Core 0 via Mozzi timer
// Fast math only. Reads shared params written by Core 0 updateControl().
// Heavy DSP (chorus) can be offloaded to Core 1 via shared buffer.
AudioOutput updateAudio() {
    // FM IN read here — audio rate, direct ADC
    int16_t fm    = mozziAnalogRead(27); // GP27 direct
    int16_t mixed = processVoices(fm);   // oscillators + envelope VCA
    // chorus result read from Core 1 shared buffer (one-cycle latency, inaudible)
    int16_t left  = applySpace(chorusOut_L);
    int16_t right = applySpace(chorusOut_R);
    chorusIn_L = mixed; chorusIn_R = mixed; // feed Core 1 chorus
    return StereoOutput::from16Bit(left, right);
}

// ✅ updateControl() — control rate, also Core 0 via Mozzi
void updateControl() {
    scanMux();          // 74HC4067 all channels
    updateJackStates(); // attenuverter logic
    updateGateState();  // GP12 + MIDI + I2C → gGateHigh
    parseMIDI();        // non-blocking
    updateLEDs();       // WS2812B via PIO
    routeModulation();  // CV → DSP param mapping
}

// ✅ Core 1 — heavy DSP offload running continuously in loop1()
// Reads chorusIn shared buffer, writes chorusOut. One audio-cycle latency.
void loop1() { processChorus(); }

// ❌ Never in updateAudio():
// Serial, Wire, SPI, delay, analogRead, anything that blocks
```

> **Note on Mozzi + dual core:** Mozzi's `updateAudio()` and `updateControl()` both run on Core 0 via a hardware timer ISR. Core 1 (`setup1()/loop1()`) is entirely independent and is used for offloading heavy DSP like the chorus engine. The "Core 1 = audio DSP" description in the architecture overview refers to this offload pattern — not to Mozzi moving its interrupt to Core 1.

### Parameter Smoothing

All control-rate parameters smoothed with a one-pole lowpass to eliminate zipper noise:

```cpp
// In updateControl() / Core 0
smoothedRelation += (rawRelation - smoothedRelation) * 0.08f;
smoothedShape    += (rawShape    - smoothedShape)    * 0.1f;
smoothedMotion   += (rawMotion   - smoothedMotion)   * 0.05f;
smoothedSpace    += (rawSpace    - smoothedSpace)    * 0.08f;
smoothedFM       += (rawFM       - smoothedFM)       * 0.06f;
smoothedCurve    += (rawCurve    - smoothedCurve)    * 0.05f;
```

---

## CPU Profiling & Headroom

Understanding CPU load is essential. Unlike RAM and Flash — reported at compile time — CPU usage is dynamic. More voices, more DSP, more modes = more work per audio interrupt = potential glitches.

### The Core Problem

At 32768Hz, the audio interrupt fires every **~30.5µs**. `updateAudio()` must complete within that window every time. At 150MHz that is approximately **4575 CPU cycles per sample budget.**

```txt
Healthy:  ▄▄▄_______▄▄▄_______  short pulses, long gaps
Tight:    ▄▄▄▄▄▄____▄▄▄▄▄▄____  longer pulses, shrinking gaps
Overload: ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄  no gaps = missed deadlines = clicks
```

### Method 1 — Timing Pin Toggle

```cpp
#define PROFILE_PIN 14  // GP14 — spare pin

AudioOutput updateAudio() {
    gpio_put(PROFILE_PIN, 1);
    // ... DSP ...
    gpio_put(PROFILE_PIN, 0);
    return StereoOutput::from16Bit(left, right);
}
```

View on oscilloscope or logic analyzer. Pulse width = time used. Gap = headroom.

### Method 2 — Hardware Cycle Counter (Recommended)

```cpp
volatile uint32_t audioElapsedUs = 0;

AudioOutput updateAudio() {
    uint32_t t0 = time_us_32();
    // ... DSP ...
    audioElapsedUs = time_us_32() - t0;
    return StereoOutput::from16Bit(left, right);
}

void updateControl() {
    static uint32_t lastReport = 0;
    if (millis() - lastReport >= 1000) {
        float headroom = ((30.5f - audioElapsedUs) / 30.5f) * 100.0f;
        Serial.print("Audio ISR: "); Serial.print(audioElapsedUs);
        Serial.print(" µs / 30.5 µs  Headroom: "); Serial.print(headroom, 1);
        Serial.println("%");
        lastReport = millis();
    }
}
```

Gate behind `#define CPU_PROFILE` — compiles out of release firmware.

### Method 3 — Idle Core Load Meter

```cpp
volatile uint32_t idleCount = 0;
void setup1() { }
void loop1()  { idleCount++; } // Core 1 increments as fast as possible

// Core 0 — in updateControl()
// Lower idleCount delta = more CPU used by Core 0
// Establish baseline at silence, compare as DSP load grows
```

### Method 4 — Audio Glitch / Overrun Counter

```cpp
volatile uint32_t audioOverruns = 0;
const uint32_t AUDIO_PERIOD_US = 31;

AudioOutput updateAudio() {
    uint32_t now = time_us_32();
    if (lastAudioTime > 0 && (now - lastAudioTime) > AUDIO_PERIOD_US + 5)
        audioOverruns++;
    lastAudioTime = now;
    // ... DSP ...
}
// Any non-zero overruns = audible problem — reduce load
```

### Voice Budget Estimates — RP2350

Approximate costs at 32768Hz, 150MHz. Always verify with Method 2.

| Configuration        | Est. µs | Headroom | Status |
| -------------------- | ------- | -------- | ------ |
| PAIR, no chorus      | ~6 µs   | ~80%     | ✅     |
| PAIR + chorus        | ~13 µs  | ~57%     | ✅     |
| CLOUD (4 voices)     | ~14 µs  | ~54%     | ✅     |
| CLOUD + chorus       | ~21 µs  | ~31%     | ✅     |
| CHORD (4 voices)     | ~14 µs  | ~54%     | ✅     |
| STRING (all engines) | ~24 µs  | ~21%     | ✅ ⚠️  |
| 8 voices + chorus C0 | ~34 µs  | −11%     | ❌     |
| 8 voices + chorus C1 | ~21 µs  | ~31%     | ✅     |

### Dual Core Offload Strategy

When Core 0 gets tight, move chorus to Core 1 (or vice versa depending on split). Cores communicate via shared volatile struct + RP2350 hardware mutex:

```cpp
mutex_t dspMutex;
volatile int16_t chorusIn_L, chorusIn_R;
volatile int16_t chorusOut_L, chorusOut_R;

// Core 1 — chorus processor
void loop1() {
    mutex_enter_blocking(&dspMutex);
    int16_t inL = chorusIn_L; int16_t inR = chorusIn_R;
    mutex_exit(&dspMutex);

    int16_t oL = processChorus_L(inL);
    int16_t oR = processChorus_R(inR);

    mutex_enter_blocking(&dspMutex);
    chorusOut_L = oL; chorusOut_R = oR;
    mutex_exit(&dspMutex);
}
```

One audio-cycle chorus latency (~30µs) — completely inaudible. Effectively doubles available DSP budget.

---

## Component BOM

| Component           | Part              | Qty                  | Purpose                                      |
| ------------------- | ----------------- | -------------------- | -------------------------------------------- |
| Raspberry Pi Pico 2 | RP2350            | 1                    | MCU — primary target                         |
| PCM5102A board      | —                 | 1                    | I2S stereo DAC                               |
| 74HC4067            | DIP-24 or SOIC    | 1                    | 16:1 analog mux                              |
| TL072 or TL074      | DIP/SOIC          | 2× TL072 or 1× TL074 | Op-amps — audio out + scaling                |
| MCP6002             | SOT-23 or DIP-8   | 1                    | Rail-to-rail op-amp for precision CV scaling |
| 6N138               | DIP-8             | 1                    | MIDI input optocoupler                       |
| MCP1700-3302        | SOT-89            | 1                    | 3.3V LDO regulator                           |
| BAT48 Schottky      | DO-35             | 10–12                | CV clamp diodes                              |
| 1N5817 or SS14      | —                 | 2                    | Reverse polarity protection                  |
| Ferrite bead        | BLM21PG221        | 3                    | Rail noise filtering                         |
| WS2812B LED         | 5mm or SMD        | 3                    | RGB status LEDs                              |
| Thonkiconn PJ398SM  | —                 | 10                   | Switched Eurorack jacks                      |
| Alpha 9mm pot       | RD901F            | 7                    | Panel knobs (2 large for ROOT/RELATION)      |
| Tactile button      | 6×6mm panel mount | 1                    | Single button                                |
| Eurorack header     | 16-pin shrouded   | 1                    | Power connector                              |
| Film cap            | 10µF              | 2                    | Audio AC coupling (L + R output)             |
| Electrolytic cap    | 100µF             | 3                    | Rail bypass                                  |
| Ceramic cap         | 100nF             | 10+                  | IC decoupling                                |
| Ceramic cap         | 10µF              | 1                    | LFO RC filter (if LFO CV out added)          |
| Resistors 1%        | 10kΩ, 47kΩ, 100kΩ | ~30                  | Gain, dividers, pull-ups, mono sum           |

---

## PlatformIO Setup

### Pico 2 / RP2350 Production Target

```ini
[env:pico2]
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board = rpipico2
framework = arduino
lib_deps =
    sensorium/Mozzi
    FortySevenEffects/MIDI Library
    adafruit/Adafruit_TinyUSB
    adafruit/Adafruit NeoPixel
```

---

## Prototyping Plan

### Phase 1 — Minimal Protoboard

Wire only what is needed to validate the audio chain.

```txt
RPi Pico 2 (GP0–GP2 for I2S)
├── 26 → PCM5102 BCK
├── 27 → PCM5102 LRCLK
├── 28 → PCM5102 DIN
├── 3.3V → PCM5102 VCC
├── GND → PCM5102 GND
└── USB → PC (power + serial console)

PCM5102 OUT → headphones or powered monitor
```

### Serial Console Control

Parsed in `updateControl()` — non-blocking. Gated behind `#define SERIAL_CONTROL` — remove for final firmware.

```txt
pitch 440       → ROOT frequency (Hz)
relation 0.3    → RELATION position (0.0–1.0)
shape 0.5       → SHAPE morph (0=sine, 1=hollow pulse)
motion 0.4      → MOTION depth (0.0–1.0)
fm 0.2          → FM depth (0.0–1.0)
curve 0.5       → CURVE position (0=pluck, 1=swell)
space 0.7       → SPACE width (0.0–1.0)
mode pair       → voice mode (pair/cloud/chord/cascade/string)
vol 0.8         → output volume
cpu             → print CPU headroom report (Method 2)
```


### Teletype I2C Integration

The module exposes an I2C slave interface on GP14 (SDA) and GP15 (SCL) for integration with Teletype. This allows Teletype scripts to control the module's parameters and presets directly, without needing MIDI.

It will require adding the custom command set to the Teletype firmware and implementing the corresponding I2C handlers in the module firmware. Commands will mirror the serial console commands for consistency.

### Web USB Configurator (future expansion)

A Web USB or WebMIDI/SysEx browser interface for advanced configuration and preset management. Planned features:

- Chord shape table editing (custom intervals per slot)
- LFO custom wavetable upload
- Envelope configuration (attack/release curves)
- Control how Motion and Space knobs control parameters and it's curves (e.g. assign Motion to only drift, or only chorus or mix, or both with different curves)
- Preset backup and restore via USB
- MIDI CC mapping (assign CCs to parameters)
- MIDI channel selection (fixed or omni)
- Pitch bend range (cents per semitone)
- Max detune range
- Waveform crossfade curve behaviour
- LFO sync division options
- LED brightness and colour theme
- Firmware version and build info display

---

## Development Milestones

- [x] 1. **Sine wave out via PCM5102** — implemented via serial; board=adafruit_itsybitsy_m0
- [x] 2. **Saw + shape oscillator** — saw done; continuous shape morph to be added
- [x] 3. **Serial pitch control** — `pitch 440` works; gated by `#define SERIAL_CONTROL`
- [x] 4. **Two detuned voices, stereo mix** — PAIR mode foundation
- [x] 5. **Output volume control** — `vol` via serial
- [x] 6. **Parameter smoothing** — one-pole LPF on all params in `updateControl()`
- [x] 7. **Migrate to Pico 2 / RP2350** — update platformio.ini, I2S defines, TinyUSB; verify audio chain
- [x] 8. **Implement the Performance Metrics** — CPU profiling via Method 2, audio glitch counter, and idle load meter
- [x] 9. **Dual core split** — Core 0 = control, Core 1 = DSP; shared param struct + mutex
- [x] 10. **SHAPE morph engine** — continuous wavetable crossfade: sine → triangle → saw → pulse → hollow pulse; all tables band-limited at startup; `ShapeOsc` replaces `Osc16` pair
- [x] 11. **Drift engine** — per-voice LCG random-walk frequency drift; `DriftEngine<N>` template; MOTION scales amplitude 0–±2.5 Hz; one-pole glide between targets
- [x] 12. **Chorus engine** — BBD-inspired stereo chorus on Core 0 ISR; phasor LFO (no trig in hot path); dual LFOs 0.513 Hz / 0.618 Hz, 90° offset; depth driven by MOTION; `ChorusMode` OFF/I/II/I+II; `chorus` serial command; overrun counter cleared after Mozzi init
- [x] 13. **SPACE spatializer** — stereo width and placement per voice
- [x] 14. **MOTION control** — governs drift + chorus depth simultaneously
- [x] 15. **CURVE engine** — AR envelope (audio-rate) + digital VCA; `CurveEngine<SAMPLE_RATE>` template; CURVE morphs attack (1ms–800ms) + release (80ms–1s); pluck mode auto-releases at peak (curve≤0.2); `gGatePatched=false` = drone bypass; serial `gate 1/0/free` + `curve <0–1>`
- [ ] 16. **Mux wiring** — 74HC4067 connected, all 7 knobs + 4 slow CVs readable
- [ ] 17. **Jack switch detection** — mux CH12–CH15, attenuverter mode switching
- [ ] 18. **V/OCT input** — precision scaling, oversampling, hysteresis, GP26
- [ ] 19. **Gate input** — GP12, CURVE-shaped articulation trigger
- [ ] 20. **V/Oct calibration** — two-point routine via button hold on power-up
- [x] 21. **RELATION engine** — `gRelation` (semitones 0–24) maps voice 2 via `powf(2, rel/12)` in `updateControl()`; `gDetune` retained as Hz fine-spread; `VoiceMode` enum (PAIR/CLOUD/CHORD/CASCADE/STRING) with `mode` serial command; only PAIR active; framework ready for M22–M25
- [ ] 22. **CLOUD mode** — multi-voice ensemble, animated stereo positioning
- [x] 23. **CHORD mode** — 4-voice interval table (11 shapes: Unison→Octaves); `voices[4]`/`subVoices[4]` arrays; `sActiveVoices` (2 for PAIR, 4 for CHORD); RELATION (0–24 st) sweeps + interpolates between chord shapes; hard-L/soft-L/soft-R/hard-R stereo pan (normalized ×256 fixed-point); `DriftEngine<4>`; cached 4× `powf` per shape/base-freq change; PAIR mode backward-compatible; `chord <name|0-10>` serial command as convenience shim over `rel`
- [ ] 24. **CASCADE mode** — restrained FM interaction, soft-clipped, bounded
- [ ] 25. **STRING mode** — microdetune, animated chorus, ensemble drift, full width
- [ ] 26. **Post Effects Section** — global chorus, stereo line delay (limited dut to amount of RAM), multimode filter, reverb (plate/spring - Schroeder or Dattorro networks), Karplus-Strong Resonator
- [ ] 27. **Hardware MIDI in** — UART1 RX GP9, TRS dual A/B circuit
- [x] 28. **Central param/CC dispatch table** — `include/param_map.h` + `src/param_map.cpp`; `CCParam` struct with `{cc, valMin, valMax, *target, name}`; `paramMap_dispatchCC()` shared by all transports; 10 parameters mapped (CC 1/7/71/72/73/74/91/92/93/94); `onControlChange` in USB MIDI reduced to 3 lines + specials (CC 64 sustain, CC 123 panic)
- [x] 29. **USB MIDI + MIDI channel config** — `Adafruit_USBD_MIDI` + `MIDI Library` via `-DUSE_TINYUSB`; composite CDC+MIDI device (serial console + MIDI coexist on same USB); Note On/Off → `gBaseFreq`/`gGateHigh` (monophonic, last-note priority); full CC map via `paramMap_dispatchCC`; Program Change 1–5 → VoiceMode; `usbMidi_init()` before `Serial.begin()` with `TinyUSBDevice.mounted()` wait; Web MIDI compatible (Chrome/Edge via `navigator.requestMIDIAccess`); `gMidiChannel` (0=omni, 1–16) set via `midichan` serial command; channel filter in all MIDI callbacks
- [x] 29b. **Flash config persistence** — `include/config_store.h` / `src/config_store.cpp`; Earle Philhower EEPROM emulation (wear-levelled circular buffer); `AlloyConfig` struct covers all 15 synthesis + MIDI parameters; `configStore_load()` in `setup()` auto-restores on boot; `config save|load|reset` serial commands; three-layer flash protection: dirty check (memcmp), 10 s rate limit, magic+version invalidation on struct change; 4-slot layout for future preset expansion (`kMaxPresets=4`)
- [ ] 30. **WS2812B LEDs** — PIO 1 on GP7, full LED language per mode
- [ ] 31. **Button UI** — single button, mode cycle, double-tap, long-hold
- [ ] 32. **PCB design** — KiCad, 14HP panel, Thonkiconn jacks, Pico 2 footprint
- [ ] 33. **Panel design** — Design final graphics and layout
- [ ] 34. **Expose I2C bus for Teletype** — I2C pins available on GP14 (SDA) and GP15 (SCL) for Teletype integration (like Mannequins Just Friends)
- [ ] 35. **Implement Teletype-support in it's firmware** — Inspired by Just Friends, add custom command set for controlling Alloy Flux parameters and presets via I2C from Teletype scripts
- [ ] 36. **Implement Web Configurator and Editor** — browser-based UI for configuration, calibration, preset management. Also can change parameters in real-time via Web MIDI API for performance control and visualization of internal state (e.g. chord shape, LFO waveforms, etc.)
- [ ] 37. **Create a VCV Rack port** — optional software emulation for VCV Rack, using the same codebase where possible
- [ ] 38. **Expand voice count and polyphony** - Enable multiple voices so polyphony is possible in all modes, not just CLOUD and CHORD. Evaluate CPU load and optimize as needed.


## Project Refinement

### Software

- [ ] Understand if the multiple voices should be mixed to the stereo output and if these voices should be used by the chord engine or the MIDI/I2C input
- [x] Define the command list which will span Serial control, MIDI CCs, Web USB/MIDI configurator and I2C — aim for consistent parameter names across all interfaces; **done: `param_map.h` single CC table shared by all transports**

### Hardware

- [ ] Evaluate adding CV inputs for all/most parameters
- [ ] Evaluate if will use the audio jack detection. Seems too complex to implement reliably with the mux and may not add much value. Could be reserved for future expansion if needed.
- [ ] Evaluate adding an expansion module (2hp) for future features with additional inputs and outputs

---

## Future Expansion

Possible future firmware additions:

- alternate chord tables (user-defined via Web USB)
- scale quantization for CHORD mode
- adaptive harmony — chord voicings follow scale context
- alternate oscillator models (FM operator, additive)
- harmonic quantization engine
- I2C voice networking — chain multiple Alloy Flux modules
- external sync behavior (clock in, reset)
- internal modulation matrix expansion
- MPE-inspired per-note expression via MIDI

---

## Full Feature Summary

```txt
FORMAT          14HP Eurorack
MCU             Raspberry Pi Pico 2 — RP2350, dual Cortex-M33 @ 150MHz, 4MB flash
DAC             PCM5102A — I2S, 16-bit, 112dB SNR, stereo
MUX             74HC4067 — 16:1 analog, 7 knobs + 4 CVs + 4 jack switches + 1 spare

VOICE MODES     PAIR     — ROOT + RELATION dual voice, interval + detune
                CLOUD    — multi-voice ensemble, animated stereo density
                CHORD    — harmonic interval stack, REL CV morphs chord shape
                CASCADE  — restrained oscillator FM interaction
                STRING   — microdetune ensemble, Juno chorus character

OSCILLATORS     ROOT + RELATION — relationship-based, not independent
SHAPE           Continuous morph: sine → triangle → saw → pulse → hollow pulse
FM              Linear, soft-clipped, musically bounded, audio-rate external input
DRIFT           Per-voice phase, detune, stereo position — animated by MOTION
CHORUS          BBD-inspired, multi-tap, stereo, randomized variance, part of synthesis
CURVE           Pluck ↔ swell articulation — basic VCA-like behavior built in
SPACE           Stereo width, voice placement, phase offsets

KNOBS (7)       ROOT, RELATION*, SHAPE, MOTION, FM, CURVE, SPACE
                * RELATION is the signature control — largest knob on panel
JACKS (10)      V/OCT, GATE, MIDI TRS, REL CV, SHAPE CV, MOTION CV, FM IN, SPACE CV,
                L OUT (mono norm.), R OUT
BUTTON (1)      Mode cycle, MIDI clock toggle, calibration routine
LEDs (3)        WS2812B RGB — mode colour, voice activity, motion depth

ATTENUVERTERS   REL CV, SHAPE CV, MOTION CV, SPACE CV — knob becomes attenuverter
                when cable inserted (detected via Thonkiconn switch + mux)

MIDI            TRS Type A/B dual + USB MIDI device — simultaneous
                Note, pitch bend, CC (RELATION/SHAPE/MOTION/SPACE/FM), clock, panic

OUTPUT          Stereo L/R — passive mono sum on L when R unplugged (10kΩ)
POWER           +12V via LDO → 3.3V for Pico 2 / PCM5102 / mux
                ±12V direct to TL072 op-amps

FRAMEWORK       Arduino + Mozzi 2.x via PlatformIO
ARCHITECTURE    Dual core: Core 0 = control / UI / ADC / MIDI
                           Core 1 = audio DSP / oscillators / chorus / drift
```

---

Voltage Foundry Modular — Alloy Flux
2026
