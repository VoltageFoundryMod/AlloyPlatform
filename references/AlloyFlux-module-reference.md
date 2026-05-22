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
    - [POLY](#poly)
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
    - [Runtime Envelope Selection (M5x)](#runtime-envelope-selection-m5x)
  - [Post-Effects Section (M26)](#post-effects-section-m26)
    - [Signal Chain](#signal-chain-1)
    - [FilterEngine (M26a)](#filterengine-m26a)
    - [Filter Algorithm Selection (M5x)](#filter-algorithm-selection-m5x)
    - [FxChain Ordering (M26a)](#fxchain-ordering-m26a)
    - [ReverbEngine (M26b)](#reverbengine-m26b)
    - [DelayEngine (M26c)](#delayengine-m26c)
    - [ISR Budget (M26)](#isr-budget-m26)
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
    - [Input Row 2](#input-row-2)
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
    - [Normalization Probe — CV Jack Detection](#normalization-probe--cv-jack-detection)
      - [Circuit Topology](#circuit-topology)
      - [Overvoltage Protection](#overvoltage-protection)
      - [Detection Algorithm (firmware)](#detection-algorithm-firmware)
      - [Probed Jacks](#probed-jacks)
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
      - [Hardware Layout](#hardware-layout)
      - [LED Role Assignment](#led-role-assignment)
      - [Colour Language](#colour-language)
      - [Brightness Language](#brightness-language)
      - [D12 + D16 — Voice Activity (always)](#d12--d16--voice-activity-always)
      - [D13 — Mode Indicator (near MODE\_SW)](#d13--mode-indicator-near-mode_sw)
      - [D15 — Shift / Drone State (near SHIFT\_SW)](#d15--shift--drone-state-near-shift_sw)
      - [D14 — Centre Heartbeat / Global](#d14--centre-heartbeat--global)
      - [Mode Change Animation](#mode-change-animation)
  - [Drone Mode Entry / Exit](#drone-mode-entry--exit)
    - [SHIFT Button Interaction (D15 context)](#shift-button-interaction-d15-context)
    - [Calibration Routine Visual](#calibration-routine-visual)
    - [Button Interaction Map](#button-interaction-map)
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
    - [Dual Core Offload Strategy](#dual-core-offload-strategy)
  - [Component BOM](#component-bom)
  - [PlatformIO Setup](#platformio-setup)
    - [Pico 2 / RP2350 Production Target](#pico-2--rp2350-production-target)
  - [Prototyping Plan](#prototyping-plan)
    - [Phase 1 — Minimal Protoboard](#phase-1--minimal-protoboard)
    - [Serial Console Control](#serial-console-control)
    - [Teletype I2C Integration](#teletype-i2c-integration)
    - [Web USB Configurator (future expansion)](#web-usb-configurator-future-expansion)
    - [VCV Rack Module Port](#vcv-rack-module-port)
  - [Development Milestones](#development-milestones)
  - [Future Expansion](#future-expansion)

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

The goal is a Eurorack instrument that sounds rich and dimensional even when minimally patched — compelling with nothing more than one V/OCT cable and one gate. With that minimal setup a user should immediately hear stereo movement, harmonic richness from RELATION, gentle animation from MOTION, and subtle drift on held notes. This is a critical design principle, not an optional feature.

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

| Parameter   | Value                                                                               |
| ----------- | ----------------------------------------------------------------------------------- |
| Format      | Eurorack                                                                            |
| Width       | 14HP                                                                                |
| MCU         | Raspberry Pi Pico 2 — RP2350 (dual Cortex-M33 @ 150MHz)                             |
| DAC         | PCM5102A (I2S, 16-bit, 112dB SNR)                                                   |
| Output      | Stereo L/R — passive mono normalled on Left when R unplugged                        |
| Voice modes | PAIR / CLOUD / CHORD / CASCADE / STRING / POLY                                      |
| Framework   | Arduino + Mozzi 2.x, custom DSP engines (ShapeOsc, ChorusEngine, CurveEngine, etc.) |
| Build tool  | PlatformIO                                                                          |
| Knobs       | 7 (ROOT, RELATION, SHAPE, MOTION, FM, CURVE, SPACE)                                 |
| Jacks       | 10 (V/OCT, GATE, MIDI, REL CV, SHAPE CV, MOTION CV, FM IN, SPACE CV, L OUT, R OUT)  |
| Buttons     | 2 (MODE + SHIFT)                                                                    |
| LEDs        | 5× APA102/SK9822 Dotstar RGB                                                        |
| Power draw  | ~100mA +12V, ~5mA −12V (estimate)                                                   |

---

## Hardware Stack

### RP2350 Peripheral Usage

| Peripheral | Usage                                                      |
| ---------- | ---------------------------------------------------------- |
| PIO 0      | I2S audio output to PCM5102A (BCK=16, LCK=17, DATA=18)     |
| GPIO       | Dotstar LED bitbang SPI (GP7=data, GP8=clk)                |
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

| GPIO | Pico Pin | Function            | In/Out | Notes                                                                |
| ---- | -------- | ------------------- | ------ | -------------------------------------------------------------------- |
| GP0  | 1        | Spare               | —      | Future expansion                                                     |
| GP1  | 2        | Spare               | —      | Future expansion                                                     |
| GP2  | 4        | Mux S0              | Out    | 74HC4067 select bit 0                                                |
| GP3  | 5        | Mux S1              | Out    | 74HC4067 select bit 1                                                |
| GP4  | 6        | Mux S2              | Out    | 74HC4067 select bit 2                                                |
| GP5  | 7        | Mux S3              | Out    | 74HC4067 select bit 3                                                |
| GP6  | 9        | Dotstar LED data    | Out    | LED chain (all 5 LEDs)                                               |
| GP7  | 10       | Dotstar LED clk     | Out    | LED chain (all 5 LEDs)                                               |
| GP8  | 11       | UART1 TX            | Out    | Hardware MIDI Out (TRS jack)                                         |
| GP9  | 12       | UART1 RX            | In     | Hardware MIDI in (TRS jack)                                          |
| GP10 | 14       | MODE button         | In     | Internal pull-up — cycles voice modes                                |
| GP11 | 15       | SHIFT button        | In     | Internal pull-up — secondary pot functions; MODE+SHIFT combo → drone |
| GP12 | 16       | MODE Button LED     | Out    |                                                                      |
| GP13 | 17       | SHIFT Button LED    | Out    |                                                                      |
| GP14 | 19       | I2C External        | Out    | SDA 1 for I2C external comm                                          |
| GP15 | 20       | I2C External        | Out    | SCL 1 for I2C external comm                                          |
| GP16 | 21       | I2S BCK             | Out    | PCM5102 bit clock — PIO 0                                            |
| GP17 | 22       | I2S LRCLK           | Out    | PCM5102 LR clock — PIO 0                                             |
| GP18 | 24       | I2S DATA (SD)       | Out    | PCM5102 serial data — PIO 0                                          |
| GP19 | 25       | Spare               | —      | Future? PWM → RC filter → op-amp if LFO CV output added later        |
| GP20 | 26       | Spare               | —      | Future expansion                                                     |
| GP21 | 27       | Spare               | —      | Future expansion                                                     |
| GP22 | 29       | Normalization Probe | In     | Normalization Probe as in MI Modules                                 |
| GP26 | 31       | ADC0 — V/OCT pitch  | In     | Direct ADC, fast reads, 1V/oct tracking                              |
| GP27 | 32       | ADC1 — FM IN        | In     | Direct ADC, audio-rate reads in updateAudio()                        |
| GP28 | 34       | ADC2 — Mux signal   | In     | 74HC4067 SIG — all knobs + slow CVs + jack switches                  |
| GP25 | internal | Onboard LED         | Out    | Debug only                                                           |
| —    | 36       | 3.3V out            | Pwr    | Powers PCM5102, 74HC4067                                             |
| —    | 39       | VSYS                | Pwr    | System power — 5V from AP63205WU buck converter                      |
| —    | 40       | VBUS                | Pwr    | USB 5V                                                               |

> Spare GPIO (GP0–GP2, GP19–GP21) are reserved for future expansion. See [Future Expansion](#future-expansion).

---

## Power Architecture

```txt
Eurorack +12V ──→ Buck Converter (AP63205WU) ──→ 5V ──→ Pico 2 VSYS
                  Buck Converter ──→ LDO (MCP1700x-3302) ──→ 3.3V ──→ PCM5102A + 74HC4067
Eurorack +12V / −12V ───────────────────────────→ TL072 op-amps
Eurorack +12V ──→ ferrite bead + 100µF ─────────→ clean analog rail for -10V Ref
```

- Pico 2 runs from 5V via VSYS
- PCM5102A and 74HC4067 both on the same 3.3V LDO rail
- Op-amps on ±12V directly for full Eurorack output swing
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
Chorus L        Chorus R         ◄── [FILTER: post-chorus if fxorder filter post]
(BBD-inspired)  (different phase/rate)
   │               │
   └───────┬───────┘
           │
   ┌───────▼───────────────┐
   │  [FILTER — pre-chorus] │  ← default position (lp/hp/bp/notch/off)
   │  Cytomic SVF, stereo  │
   └───────┬───────────────┘
           │  ← filter post-chorus position if fxorder=post
   ┌───────▼───────────────┐
   │  [DELAY — pre-reverb] │  ← default (ping-pong, 10–200ms)
   └───────┬───────────────┘
           │
   ┌───────▼───────────────┐
   │      REVERB            │  ← additive mix-in from Core 1
   │   (Core 1 offload)    │    (1-frame latency, inaudible)
   └───────┬───────────────┘
           │  ← delay post-reverb position if fxorder=post
SPACE engine (stereo width)     ◄── SPACE CV (mux)
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

Mode is selected by tapping MODE. D13 confirms the current mode colour. Six modes cycle in sequence: PAIR → CLOUD → CHORD → CASCADE → STRING → POLY → PAIR.

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

Oscillator interaction mode. Musical and restrained FM. RELATION becomes the FM operator modulating ROOT, producing harmonically rich timbres through controlled frequency modulation.

- RELATION oscillator runs at a ratio relationship to ROOT (not just detuned)
- RELATION modulates ROOT's frequency linearly — linear FM preferred for musical sidebands
- FM depth is soft-clipped and bounded — prevents the harsh, abrasive character of unconstrained FM
- RELATION knob controls the interaction depth and harmonic ratio character
- at low RELATION values: subtle harmonic warming and beating
- at mid RELATION values: recognisable FM timbres — bell-like, metallic-warm
- at high RELATION values: dramatic spectral content, still bounded by soft-clip
- MOTION applies drift to both carrier and modulator independently — the FM relationship itself wanders slightly, producing organic harmonic animation
- FM depth from the external FM IN jack stacks additively with the internal RELATION modulation

**Implementation notes:**

- FM ratio derived from RELATION position: simple integer and half-integer ratios (1:1, 1:2, 2:3, 3:2, etc.) at detent-like positions produce the most musical results
- soft-clip function applied to FM index before phase accumulator update — `tanh(index × 0.7)` keeps output bounded
- depth range deliberately scaled: knob at maximum = FM index ~3.0 (perceptually loud but not chaotic)
- ratio constraints should snap toward simple ratios as RELATION approaches certain positions (optional future hysteresis)
- both ROOT and RELATION voices still pass through chorus and SPACE independently — FM interaction happens pre-chorus

> **Completion criteria:** depth limiting, ratio snap behaviour, and soft clipping must all be validated before CASCADE is considered production-ready. The defining test: sweep RELATION from 0 to 1 while a held note plays — the timbre should move through recognisable harmonic territories without ever becoming unpleasantly harsh.

### STRING

Vintage ensemble inspired mode. The most atmospheric mode.

- distributed microdetune across all internal voices
- animated chorus interaction — movement never fully stops
- voice drift and slow phase instability
- stereo width enhancement — SPACE has the strongest effect here
- slow ensemble movement inspired by vintage string machines
- Juno chorus character embedded into the synthesis layer

### POLY

True 4-voice polyphonic mode. Each incoming MIDI note or I2C voice command is allocated to its own independent voice slot, allowing up to four simultaneous pitches — each with its own CURVE envelope, drift, and stereo position.

- 4 independent voice slots, each a full `ShapeOsc` instance
- round-robin voice allocation — oldest note stolen on 5th note
- each voice has its own independent AR envelope triggered per note
- voices distributed across the stereo field: slot 0 → hard L, slot 1 → soft L, slot 2 → soft R, slot 3 → hard R
- RELATION knob sets a global detune spread applied symmetrically across all active voices
- SHAPE, MOTION, and CHORUS settings are shared across all voices — timbre is consistent
- V/OCT + GATE input plays monophonically into slot 0; MIDI/I2C fill slots 1–3
- works with CHORD mode chord table: each POLY voice can itself fan into chord intervals (future)

**Voice allocation algorithm:**

```txt
Note On received:
    1. If a free slot exists → assign note to lowest free slot
    2. If all 4 slots occupied → steal oldest active slot (lowest note age)
    3. Trigger CURVE attack on assigned slot
    4. D12/D16 brightness reflects number of active voices

Note Off received:
    1. Find slot matching note
    2. Trigger CURVE release on that slot only
    3. Other voices unaffected
```

**LED behaviour in POLY mode:**

D13 shows a distinct yellow-green colour (lime) to distinguish POLY from CHORD (amber). D12 brightness tracks the number of active voices (dim = 1, full = 4). D16 reflects the chord spread width.

---

## Voice and Polyphony Architecture

Oscillators are anti-aliased wavetable-based (`ShapeOsc`), preserving low-end warmth and supporting smooth waveform transitions without zipper noise. Tuning is stable — no drift from temperature or load. Band-limited additive synthesis with Lanczos sigma smoothing generates the five wavetables at startup.

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

**POLY** (up to 4 voices, Milestone 38):

```txt
slot[0]  MIDI/I2C note 1  →  hard L    independent AR envelope
slot[1]  MIDI/I2C note 2  →  soft L    independent AR envelope
slot[2]  MIDI/I2C note 3  →  soft R    independent AR envelope
slot[3]  MIDI/I2C note 4  →  hard R    independent AR envelope

Round-robin allocation, oldest-note steal on overflow.
V/OCT + GATE always plays into slot 0.
```

### Polyphony from MIDI / I2C

A voice allocator maps incoming note events to voice slots. Behaviour is mode-dependent:

```txt
MIDI Note On  (note=60)  ─┬───────────────────────────────────────┐
I2C command   (note=67)  ─┤  voice allocator → assign to slot     │
V/OCT + GATE             ─┼───────────────────────────────────────┤
MIDI Note Off (note=60)  ─┘  → release that slot's CURVE envelope

PAIR mode:  one logical note fans to [ROOT] and [ROOT + RELATION interval]
CHORD mode: one logical note fans to 4 calculated interval slots
CLOUD mode: one logical note fans to N micro-detuned, drifting slots
POLY mode:  each note gets its own independent slot (up to 4 simultaneous)
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

- **Pitch**: one octave below (default, `freq × 0.5`) or two octaves below (`freq × 0.25`) — set with `suboct`
- **Waveform**: fixed square (SHAPE = 0.75 in the 5-table spectrum) — never morphs
- **Level**: 0 to 50% of main oscillator amplitude (FATNESS = 0.0 to 1.0)
- **Per voice**: each voice (v1 L, v2 R) has its own independent sub oscillator
- **SHAPE independence**: SHAPE knob changes the main voice character, sub is always square

**Serial commands:**

```txt
fat <0–1>      — sub oscillator level: 0=off  1=full (50% of main)
suboct <1|2>   — sub octave: 1=one octave below (default)  2=two octaves below
```

**Tonal character by octave setting:**

| `suboct`      | Multiplier | Character                                                         |
| ------------- | ---------- | ----------------------------------------------------------------- |
| `1` (default) | ×0.5       | Classic Juno sub — warm, adds weight without crowding the bass    |
| `2`           | ×0.25      | Deep sub — anchors the sound two octaves down, darker and heavier |

### FATNESS Hardware Interaction — Button Shift

The panel has one SHAPE knob but two related parameters: SHAPE (main morph) and FATNESS
(sub level). The single button acts as a **shift key**:

```txt
SHAPE knob alone         —  adjusts main oscillator morph (sine → hollow)
[hold BUTTON] + SHAPE    —  adjusts FATNESS (sub octave level 0 → full)

LED feedback while in shift mode: dim white fill on the shape LED
```

This is the same interaction pattern used on many modern Eurorack modules. No extra panel hardware required.

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

This layered internal animation means a single V/OCT cable and gate already produces a living, breathing sound — not a static tone. The module is internally alive without requiring extensive patching.

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

- `false` (default on boot): envelope fixed at 1.0 — module sounds continuously (drone/pad)
- `true`: AR envelope active, triggered by rising/falling edges of `gGateHigh`
- Auto-armed to `true` by: any MIDI Note On, `gate 0`/`gate 1` serial command, CC 64 sustain, hardware gate jack (M19)
- Reset to `false` (drone) by: `gate free` serial command, CC 119 from MIDI, MODE+SHIFT buttons held simultaneously

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

**Signal chain position:** oscillators → drift → soft-clip → **[VCA — CURVE envelope]** → chorus (M12) → M26 effects → output

Future gate sources: GP12 jack (M19), I2C (Teletype).

### Runtime Envelope Selection (M5x)

The envelope engine is runtime-selectable without recompilation. Both concrete instances (`AREnvelope`, `ADSREnvelope`) are always compiled into flash; a `gCurveEng` pointer selects the active one. Switching calls `reset()` then re-points the pointer — silent, zero-glitch.

| Type           | Class                       | Params                                                      | Loop                   |
| -------------- | --------------------------- | ----------------------------------------------------------- | ---------------------- |
| `ar` (default) | `AREnvelope<SAMPLE_RATE>`   | `curve`, `curvetime`                                        | —                      |
| `adsr`         | `ADSREnvelope<SAMPLE_RATE>` | `gAdsrAttack`, `gAdsrDecay`, `gAdsrSustain`, `gAdsrRelease` | optional (`gAdsrLoop`) |

`ADSREnvelope::setADSR()` calls `expf()` × 3 at control rate; `next()` is multiply-only, safe in ISR.

Loop mode (`adsr … loop`) auto-restarts attack after release hits 0 — turns the envelope into a free-running LFO at rates determined by the combined ADSR times.

**Phase reset on retrigger (M5x):** on every rising gate edge, all oscillator phase accumulators are reset to zero. This ensures the attack always starts at the waveform zero-crossing, eliminating the metallic click artifact that occurs when retriggering during a release cycle at a random phase point.

**Serial commands:**

```txt
env type ar           — switch to single-knob AR (default)
env type adsr         — switch to full ADSR
adsr <A> <D> <S> <R> [loop|noloop]  — set ADSR times (seconds) and sustain (0–1)
env loop on|off       — toggle loop mode (envelope as cycling LFO)
```

---

## Post-Effects Section (M26)

The post-effects section sits between the Chorus and the Space engine. It adds a multimode filter, reverb, and delay — all optional and zero-cost when bypassed.

### Signal Chain

```txt
Osc → Drift → soft-clip → VCA (CURVE) → [Filter pre] → Chorus → [Filter post]
    → [Delay pre] → [Reverb additive] → [Delay post] → SPACE → Output
```

Effect positions are configured via `fxorder` — two boolean flags give four orderings:

| `fxorder filter` | `fxorder delay` | Chain                            |
| ---------------- | --------------- | -------------------------------- |
| `pre` (default)  | `pre` (default) | Filter → Chorus → Delay → Reverb |
| `post`           | `pre`           | Chorus → Filter → Delay → Reverb |
| `pre`            | `post`          | Filter → Chorus → Reverb → Delay |
| `post`           | `post`          | Chorus → Filter → Reverb → Delay |

All four orderings are musically distinct and all determined at control rate — the ISR executes two branch predictions at audio rate (zero overhead).

### FilterEngine (M26a)

`FilterEngine` — pure-virtual abstract base class. Both concrete implementations are always compiled into flash; the `gFilterInst` pointer selects the active algorithm at runtime. Switching calls `reset()` then re-points the pointer — silent and glitch-free.

| Implementation | File                      | Mode(s)                    | Character                                    |
| -------------- | ------------------------- | -------------------------- | -------------------------------------------- |
| `SVFFilter`    | `include/dsp/SVFFilter.h` | LP / HP / BP / NOTCH / OFF | Clean, precise, all-mode                     |
| `OTALadder`    | `include/dsp/OTALadder.h` | LP4 only                   | Warm, saturating, self-oscillates at res=1.0 |

**SVFFilter** — Cytomic TVA-SVF (trapezoidal state-variable filter). Coefficients (`g`, `k`, `a1`, `a2`, `a3`) recomputed in `updateControl()` at 128 Hz via `tanf()`. Audio path is pure float multiply-add — no trig in ISR. Both channels share coefficients but have independent integrator state.

| Parameter | Range              | Notes                                         |
| --------- | ------------------ | --------------------------------------------- |
| Mode      | OFF/LP/HP/BP/NOTCH | OFF = hard bypass, zero CPU                   |
| Cutoff    | 20–16000 Hz        | `tanf()` at 128 Hz only — never in ISR        |
| Resonance | 0.0–1.0            | 0=flat, 1=near self-oscillation; soft-clipped |

**OTALadder** — ZDF 4-pole Moog-style ladder filter with `tanh` saturation. LP4 mode only. Self-oscillates at resonance=1.0. Cutoff clamped to 8 kHz (sr/4 Nyquist guard). Fast piecewise `tanh` approximation — no `libm` in audio path.

| Parameter | Range      | Notes                                      |
| --------- | ---------- | ------------------------------------------ |
| Mode      | LP4 only   | All other modes fall back to LP            |
| Cutoff    | 20–8000 Hz | Hard-clamped at sr/4 to prevent aliasing   |
| Resonance | 0.0–1.0    | 0=flat, 1.0=self-oscillation (sine output) |

```txt
filter lp 2000 0.6    → SVF low-pass at 2 kHz, resonance 0.6
filter hp 400         → SVF high-pass at 400 Hz
filter off            → hard bypass
filter type svf       → switch to Cytomic SVF (default, all modes)
filter type ladder    → switch to OTA 4-pole ladder (LP4, saturating)
```

### Filter Algorithm Selection (M5x)

Runtime filter switching via `filter type <svf|ladder>`. The type change is detected in `updateControl()` on the next 128 Hz tick — `reset()` is called then `gFilterInst` is re-pointed. No ISR disruption.

### FxChain Ordering (M26a)

Two boolean flags; written by `updateControl()` or serial command, read atomically by the ISR (byte-aligned on Cortex-M33).

```txt
fxorder filter pre    → filter before chorus (default — shapes raw voice)
fxorder filter post   → filter after chorus (sculpts the chorused mix)
fxorder delay pre     → delay feeds into reverb (spacious, default)
fxorder delay post    → reverb feeds into delay (echoed reverb tail)
```

### ReverbEngine (M26b)

Plate reverb running on **Core 1** — completely offloaded from the audio ISR.

**Inter-core protocol (artifact-free by design):**

- `gRevIn_L/R` — Core 0 ISR writes once per sample, Core 1 reads
- `gRevOut_L/R` — Core 1 writes after processing, Core 0 ISR reads and mixes in
- All four are `volatile int32_t`, 32-bit aligned → atomic on Cortex-M33, no mutex needed
- Core 0 adds wet return additively: `out += (gRevOut * wetScale) >> 8`
- The return is at most 1 audio frame (≤30µs) old — completely inaudible on a 0.5–3s reverb tail

This avoids the artifact that forced chorus back to Core 0: chorus is in-line (blocking = dropped sample); reverb is additive (stale-by-one-frame = acoustically transparent).

**Algorithm interface is abstract** (`ReverbEngine` pure-virtual base class) — swap implementations without touching the ISR or Core 1 loop:

| Implementation       | Status            | Notes                                                                             |
| -------------------- | ----------------- | --------------------------------------------------------------------------------- |
| `NullReverb`         | Stub (bypassed)   | Outputs zeros, zero CPU                                                           |
| `DattorroReverb`     | **Active (M26b)** | Lush plate, float delay lines, WFE/SEV inter-core sync, FTZ enabled on both cores |
| Spring / Hall / Room | Future            | Same interface                                                                    |

```txt
reverb 0.4 0.7 0.5   → mix=0.4, size=0.7, damping=0.5
reverb off           → disable (gRevEnabled=false, Core 1 outputs zeros)
```

### DelayEngine (M26c)

Stereo ping-pong delay with compile-time configurable maximum (`DELAY_MAX_MS`, default 300ms).

| Max delay | RAM cost | Notes                               |
| --------- | -------- | ----------------------------------- |
| 200 ms    | ~26 KB   |                                     |
| 300 ms    | ~39 KB   | **Default** — set in platformio.ini |
| 500 ms    | ~65 KB   | All safe within RP2350's 520KB SRAM |

**Cross-channel feedback** creates the ping-pong effect — echoes alternate L/R/L/R:

```txt
L delay line ← inL + feedback × delayedR
R delay line ← inR + feedback × delayedL
```

Linear interpolation on fractional delay samples eliminates zipper artefacts when time changes. `process()` is `always_inline` — fully absorbed into `updateAudio()` in SRAM.

| Parameter | Range       | Notes                                    |
| --------- | ----------- | ---------------------------------------- |
| mix       | 0.0–1.0     | 0 = hard bypass (zero CPU, early return) |
| time_ms   | 10–300 ms   | Fractional sample accuracy               |
| feedback  | 0.0–0.95    | Clamped to prevent runaway accumulation  |
| dry gain  | 1 − mix×0.5 | Slight dry reduction at high mix         |

```txt
delay 0.5 150 0.6    → mix=0.5, time=150ms, feedback=0.6 (ping-pong bounce)
delay on             → re-enable with current mix/time/feedback
delay off            → hard bypass (zero CPU)
```

### ISR Budget (M26)

| Effect                      | Core | Cost      | Notes                                                           |
| --------------------------- | ---- | --------- | --------------------------------------------------------------- |
| Filter (OFF)                | 0    | ~0 µs     | Hard bypass — single branch                                     |
| Filter SVF (LP/HP/BP/NOTCH) | 0    | ~2–3 µs   | 10× float mul/add per channel                                   |
| Filter OTALadder (LP4)      | 0    | ~4–6 µs   | 4-stage ZDF + tanh per channel; piecewise tanh (no libm)        |
| Delay (active)              | 0    | ~1–2 µs   | 8× float ops + 2 buffer reads/writes per channel; bypass = 0 µs |
| Reverb mix-in               | 0    | ~0.5 µs   | 1 multiply + 1 add per channel (additive)                       |
| Reverb DSP                  | 1    | offloaded | Core 1 free-runs; never touches ISR budget                      |
| FxOrder flags               | 0    | ~0 µs     | 2 branch predictions, static config                             |

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

The panel has two buttons: **MODE** (GP10) and **SHIFT** (GP11). Hold SHIFT while turning a knob to access the secondary parameter. The LED dims white while shift mode is active. Releasing SHIFT exits shift mode.

| Knob  | Primary function             | Shift function (hold SHIFT)     |
| ----- | ---------------------------- | ------------------------------- |
| SHAPE | Waveform morph (sine→hollow) | FATNESS — sub oscillator level  |
| MOTN  | Drift + chorus depth         | DRIFTSPEED — drift glide rate   |
| CURVE | Envelope shape (pluck→swell) | CURVETIME — envelope time scale |
| SPACE | Stereo width                 | VOL — master output volume      |

ROOT, RELATION, and FM knobs have no shift function — they occupy the full knob travel for precision.

---

## Panel Layout — 14HP


**HP:** 14HP (70.96mm panel width)
**Jacks:** 10× Thonkiconn PJ398SM (switched, vertical mount)
**Knobs:** 7× Alpha 9mm (ROOT and RELATION largest)
**Buttons:** 2× tactile panel mount
**LEDs:** 5× APA102/SK9822 Dotstar RGB (GP7=data, GP8=clk, bitbang SPI in `updateControl()`)

---

## Jack Assignment

### Input Row 1 — Primary Inputs

| Jack | Label  | Function                                          | Pin/Path    |
| ---- | ------ | ------------------------------------------------- | ----------- |
| 1    | V/OCT  | Pitch — 1V/oct, 0–6V range                        | GP26 direct |
| 2    | GATE   | Note trigger / envelope / articulation            | GP12 direct |
| 3    | MIDI   | TRS MIDI in — Type A/B dual circuit               | GP9 UART1   |
| 4    | REL CV | RELATION modulation — interval/detune/chord morph | Mux CH8     |
| 5    | SHP CV | SHAPE CV — waveform morph modulation              | Mux CH9     |

### Input Row 2

| Jack | Label  | Function                                       | Pin/Path    |
| ---- | ------ | ---------------------------------------------- | ----------- |
| 6    | MTN CV | MOTION CV — animation depth modulation         | Mux CH10    |
| 7    | FM IN  | FM input — audio-rate capable, bipolar ±5V     | GP27 direct |
| 8    | SPC CV | SPACE CV — stereo width modulation             | Mux CH11    |
| 9    | L OUT  | Left audio — passive mono sum when R unplugged |             |
| 10   | R OUT  | Right audio — stereo                           |             |

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

Triggered by holding MODE while powering on. The module enters calibration mode before audio starts. User patches two reference voltages in sequence — 1V then 3V — confirming each with a MODE tap. The module measures the ADC reading at each voltage, builds a two-point linear correction table (offset + gain), and saves it to flash. Confirmed by LED white flash sequence. No trimmer pots required.

```txt
Entry: hold MODE during power-on
    D15 begins slow white pulse      (calibration mode active)
    D14 white steady                 (awaiting 1V input)

Step 1 — patch 1V, tap MODE:
    D14 brief white flash            (1V point accepted)
    D12 full brightness              (point 1 of 2 confirmed)

Step 2 — patch 3V, tap MODE:
    D14 brief white flash            (3V point accepted)
    D16 full brightness              (point 2 of 2 confirmed)

Complete:
    All LEDs → white ripple centre-out
    D15 → three quick flashes → off  (saved to flash)
    Module boots normally
```

---

## Analog Multiplexer

**IC:** 74HC4067 (16:1 analog mux)

- 3.3V compatible — GP3–GP6 drive select lines directly
- Analog-transparent and bidirectional
- Switching + settling time < 1ms — fine for control-rate reads
- All 16 channels utilized

### Mux Channel Map

| Ch   | Pin | Signal         | Type    | Notes                                              |
| ---- | --- | -------------- | ------- | -------------------------------------------------- |
| CH0  | 9   | Gate jack      | Slow CV | Gate Input                                         |
| CH1  | 8   | REL CV jack    | Slow CV | RELATION modulation input                          |
| CH2  | 7   | SHAPE CV jack  | Slow CV | SHAPE modulation input                             |
| CH3  | 6   | MOTION CV jack | Slow CV | MOTION modulation input                            |
| CH4  | 5   | SPACE CV jack  | Slow CV | SPACE modulation input                             |
| CH5  | 4   | ROOT knob      | Pot     | Coarse pitch offset                                |
| CH6  | 3   | RELATION knob  | Pot     | Signature control — interval/detune/chord/FM depth |
| CH7  | 2   | SHAPE knob     | Pot     | Waveform morph position                            |
| CH8  | 23  | MOTION knob    | Pot     | Animation depth                                    |
| CH9  | 22  | SPACE knob     | Pot     | Stereo width / placement                           |
| CH10 | 21  | FM knob        | Pot     | FM depth / attenuverter when FM IN patched         |
| CH11 | 20  | CURVE knob     | Pot     | Envelope / articulation shaping                    |
| CH12 |     | Spare          | —       | Future knob or CV                                  |
| CH13 |     | Spare          | Digital | Future knob or CV                                  |
| CH14 |     | Spare          | Digital | Future knob or CV                                  |
| CH15 |     | Spare          | Digital | Future knob or CV                                  |

**Select lines:** GP3 (S0), GP4 (S1), GP5 (S2), GP6 (S3)
**Signal pin:** GP28 (ADC2)

### Attenuverter Logic via Jack Switch Detection

Cable presence on CV input jacks is detected via the Normalization Probe (GP22) — see [Normalization Probe](#normalization-probe--cv-jack-detection) below. When a cable is detected on a jack, the associated knob switches from direct parameter control to attenuverter mode:

```txt
No cable detected (probe signal tracks — jack empty):
    Knob → sets parameter value directly

Cable detected (probe signal stationary — cable present):
    Knob → attenuverter for that CV
    parameter = lerp(knob_pos, -1.0, +1.0) × CV_voltage
    knob centre = CV has zero effect
    knob CW     = full positive CV depth
    knob CCW    = full negative CV depth (inverted)
```

| Jack     | Knob     | No cable              | Cable present              |
| -------- | -------- | --------------------- | -------------------------- |
| REL CV   | RELATION | Interval/detune set   | REL CV depth + polarity    |
| SHAPE CV | SHAPE    | Fixed waveform shape  | Shape CV depth + polarity  |
| MTN CV   | MOTION   | Fixed animation depth | Motion CV depth + polarity |
| SPC CV   | SPACE    | Fixed stereo width    | Space CV depth + polarity  |

FM IN / FM knob always functions as attenuverter — FM amount is always relative to the external signal (or internal normalization when unpatched), regardless of cable state.

### Normalization Probe — CV Jack Detection

AlloyFlux uses the Mutable Instruments shared-bus normalization probe technique to detect cable presence on CV input jacks using a single GPIO pin (GP22). This allows the firmware to distinguish between an unpatched input (apply internal default) and a patched input (process external CV), with no dedicated detection pin per jack.

#### Circuit Topology

```txt
          [ GP22 — PROBE_IN ]

                    |
           +--------+--------+  (Shared Bus Node)
           |                 |
        [ 10kΩ ]          [ 10kΩ ]

           |                 |
    [ Jack J7 Pin 3 ]  [ Jack J8 Pin 3 ]   ← Normalization switch contacts
```

- **GP22** drives a square-wave test signal onto the shared bus through individual 10 kΩ isolation resistors.
- The jacks use Thonkiconn-style switching contacts (Pin 3). When **no cable is inserted**, Pin 3 is internally shorted to Pin 1 (tip), superimposing the probe signal onto the op-amp input path.
- When a **cable is inserted**, the spring-switch opens, fully isolating Pin 3 from Pin 1. Only the external CV voltage reaches the op-amp.
- The **V/Oct input is intentionally excluded** from the probe bus to prevent any charge-injection from the digital pin affecting sub-millivolt analog precision.

#### Overvoltage Protection

A BAT54S dual Schottky clamp diode (D18) is placed immediately at GP22 to clamp any incoming transients to the safe range ($0\text{ V} - 0.3\text{ V}$ to $3.3\text{ V} + 0.3\text{ V}$). This protects the MCU against accidental Eurorack-level voltages on the switch contact when partially inserting a patch cable.

#### Detection Algorithm (firmware)

The probe cycle runs inside `updateControl()` (128 Hz), spending only ~few microseconds:

1. **Drive HIGH** — set GP22 HIGH ($3.3\text{ V}$), settle, read ADC → $V_{\text{high}}$
2. **Drive LOW** — set GP22 LOW ($0\text{ V}$), settle, read ADC → $V_{\text{low}}$
3. **Compute delta** — $\Delta V = |V_{\text{high}} - V_{\text{low}}|$
4. **Decide:**

| $\Delta V$                    | Interpretation                        | Firmware action                                 |
| ----------------------------- | ------------------------------------- | ----------------------------------------------- |
| $\Delta V > \text{threshold}$ | ADC tracks the probe — **jack empty** | Apply internal default / software normalization |
| $\Delta V \approx 0$          | ADC stationary — **cable plugged in** | Process external CV directly; ignore probe      |

#### Probed Jacks

Currently J7 and J8 are on the shared probe bus (exact jack assignments finalized at PCB layout). The V/Oct input is excluded. Each probed jack independently reports cable presence via its own ADC channel through the mux.

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

| Message          | Action                                                                                                |
| ---------------- | ----------------------------------------------------------------------------------------------------- |
| Note On          | Set ROOT pitch + trigger GATE (monophonic, last-note priority)                                        |
| Note Off         | Release articulation                                                                                  |
| Pitch Bend       | ±2 semitones (configurable via calibration routine)                                                   |
| CC 1 Mod Wheel   | `motion` — drift + chorus depth (0–1)                                                                 |
| CC 7 Volume      | `vol` — master output level (0–1)                                                                     |
| CC 64 Sustain    | `gate` — hold (≤64=on, <64=off); arms `gGatePatched=true`                                             |
| CC 71 Timbre     | `curve` — envelope shape (0–1)                                                                        |
| CC 72 Release    | `curvetime` — envelope time scale (0.25–4)                                                            |
| CC 73 Attack     | `dspeed` — drift glide speed (0.001–0.1)                                                              |
| CC 74 Brightness | `shape` — waveform morph (0–1)                                                                        |
| CC 91 Reverb     | `space` — stereo width (0–2)                                                                          |
| CC 92 Tremolo    | `detune` — symmetric fine spread (0–200 Hz)                                                           |
| CC 93 Chorus     | `fat` — sub oscillator level (0–1)                                                                    |
| CC 94 Celeste    | `rel` — RELATION semitones above ROOT (0–24)                                                          |
| CC 112           | `revmodspeed` — reverb LFO rate multiplier (0.1–4.0) (M40)                                            |
| CC 113           | `revmoddepth` — reverb LFO depth multiplier (0.0–1.0) (M40)                                           |
| CC 114           | Reverb freeze — ≥64 = freeze on, <64 = freeze off (M41)                                               |
| CC 119           | Drone return — clears gate arm, module returns to continuous drone                                    |
| CC 123           | All Notes Off / panic                                                                                 |
| Clock 0xF8       | MOTION sync to MIDI clock                                                                             |
| CC 115           | Voice mode — 6 bands: 0–20=PAIR, 21–41=CLOUD, 42–62=CHORD, 63–83=CASCADE, 84–104=STRING, 105–127=POLY |
| Program Change   | Voice mode select (1=PAIR, 2=CLOUD, 3=CHORD, 4=CASCADE, 5=STRING, 6=POLY)                             |

All CC assignments are defined in `include/param_map.h` / `src/param_map.cpp` — a single shared table iterated by all transports. Adding a new parameter requires one row in that file only.

### MIDI Channel

Default: omni (responds to all channels). Configure via serial: `midichan 3` or `midichan omni`. Channel filter applies to Note On/Off, CC, and Program Change. Persisted to flash with `config save`.

---

## User Interface — Screenless

**Philosophy:** All feedback via 5 APA102/SK9822 Dotstar RGB LEDs + 2 buttons. No menus. No reading required during performance. Eyes stay on the patch, not a display.

Hidden functions (behind long-hold) cover only: calibration, MIDI channel configuration, advanced settings. Core synthesis is always directly accessible.

The crucial UX distinction: mode changes are deliberate and infrequent. You choose a mode, you play, you hear the difference immediately. You don't need to remember what color means what — you just made that choice. This is why fixed-function jacks beat assignable jacks, and why a clear mode cycle with LED confirmation beats a menu.

### LED Language

---

#### Hardware Layout

```txt
D12  ·  ·  ·  D16       ← top: voice activity (left / right)
D13  ·  ·  ·  D15       ← mid: mode indicator / shift state
     ·  D14  ·           ← centre: heartbeat / motion / global

SW2 = MODE_SW   (left button  — near D13)
SW3 = SHIFT_SW  (right button — near D15)
```

---

#### LED Role Assignment

| LED | Position  | Primary Role            | Secondary Role              |
| --- | --------- | ----------------------- | --------------------------- |
| D12 | Top left  | ROOT voice activity     | Left channel stereo energy  |
| D16 | Top right | RELATION voice activity | Right channel stereo energy |
| D13 | Mid left  | Current voice mode      | MODE_SW feedback            |
| D15 | Mid right | Shift state / drone     | SHIFT_SW feedback           |
| D14 | Centre    | Motion / heartbeat      | Global event confirmation   |

---

#### Colour Language

| Colour            | Meaning                                       |
| ----------------- | --------------------------------------------- |
| Warm red          | ROOT voice / left channel activity            |
| Cool blue         | RELATION voice / right channel activity       |
| Green             | Motion / drift / animation active             |
| Purple            | STRING mode / chorus dominant                 |
| Cyan              | CLOUD mode / ensemble                         |
| Amber             | CHORD mode / harmonic stack                   |
| Magenta           | CASCADE mode / FM interaction                 |
| Lime/yellow-green | POLY mode / independent voice allocation      |
| Soft white        | PAIR mode (default) / neutral state           |
| White flash       | Confirmation — mode change, save, calibration |
| Off               | Inactive / feature not engaged                |

---

#### Brightness Language

| Brightness     | Meaning                                 |
| -------------- | --------------------------------------- |
| Full           | Maximum value / peak activity           |
| Medium steady  | Active, moderate value                  |
| Slow breathe   | Drone mode / idle but running           |
| Rhythmic pulse | Tracks MOTION rate / LFO / gate rhythm  |
| Single flash   | Event confirmation (tap, save, note on) |
| Off            | Inactive / silent / disabled            |

---

#### D12 + D16 — Voice Activity (always)

D12 and D16 always reflect voice activity — they breathe with the audio envelope in all modes. Bright on attack, fading on release. Readable without mode awareness.

| Mode    | D12 (ROOT / Left)                          | D16 (RELATION / Right)                      |
| ------- | ------------------------------------------ | ------------------------------------------- |
| PAIR    | Warm red — envelope level, gate response   | Cool blue — relation envelope, detune depth |
| CLOUD   | Cyan — shifts with left voice position     | Cyan — shifts opposite, stereo spread       |
| CHORD   | Amber — root note envelope                 | Amber dimmer — chord interval spread amount |
| CASCADE | Magenta — FM carrier activity              | Magenta brighter — modulator depth          |
| STRING  | Purple — slow drift, breathes              | Purple — offset phase from D12              |
| POLY    | Warm red — brightness = active voice count | Cool blue — chord spread / detune width     |

---

#### D13 — Mode Indicator (near MODE_SW)

One LED, one job. Always shows the current voice mode as a steady colour. The only LED the user needs to learn once.

| State         | Colour            | Pattern                             |
| ------------- | ----------------- | ----------------------------------- |
| PAIR          | Soft white        | Steady dim                          |
| CLOUD         | Cyan              | Steady                              |
| CHORD         | Amber             | Steady                              |
| CASCADE       | Magenta           | Steady                              |
| STRING        | Purple            | Steady                              |
| POLY          | Lime/yellow-green | Steady                              |
| Mode changing | White flash       | Brief flash → settles to new colour |

---

#### D15 — Shift / Drone State (near SHIFT_SW)

Dark in normal operation — lights up only when something state-level is active.

| State              | Colour     | Pattern                       |
| ------------------ | ---------- | ----------------------------- |
| Normal gated mode  | Off        | Dark — nothing special        |
| SHIFT held         | White      | Steady while held             |
| Drone mode active  | Warm white | Slow breathe — always visible |
| Drone + SHIFT held | White      | Brighter steady while held    |
| Calibration active | White      | Slow pulse during routine     |
| Calibration done   | White      | Three quick flashes → off     |

---

#### D14 — Centre Heartbeat / Global

The module's pulse. Shows overall animation and event state. Even without understanding any other LED, D14 tells you if the module is doing something.

| State                 | Colour      | Pattern                             |
| --------------------- | ----------- | ----------------------------------- |
| MOTION = 0, silent    | Off         | Dark — module is static             |
| MOTION > 0            | Green       | Pulses at internal drift rate       |
| Gate active           | White       | Bright on attack, fades with CURVE  |
| MIDI note active      | White       | Same as gate                        |
| Drone, no MOTION      | Green dim   | Very slow breathe — alive but still |
| Drone + MOTION        | Green       | Rhythmic pulse — shows drift rate   |
| CASCADE / FM active   | Magenta dim | Pulses with FM depth                |
| STRING / chorus heavy | Purple dim  | Slow movement matching chorus rate  |

---

#### Mode Change Animation

Triggered by MODE_SW tap.

```txt
1. D14 white flash       ← centre ignites first
2. D12, D13, D16, D15   ← ripple outward, all flash white
3. Settle               ← D13 → new mode colour
                           D12/D16 → new mode voice colours
                           D14 → resumes heartbeat role
Total duration: ~300ms
```

---

## Drone Mode Entry / Exit

**Entering drone** (hold SHIFT then tap MODE, or automatically on power-on before any gate is received):

```txt
D15 → fades up to warm white slow breathe   (drone is on)
D14 → fades to dim green slow breathe       (module is running)
D12/D16 → hold steady voice colours         (no gate pulsing)
```

**Exiting drone** (first gate received automatically, or hold SHIFT then tap MODE to toggle back to gated):

```txt
D15 → fades down to off                     (gated mode active)
D12/D16 → begin responding to gate/envelope (pulsing resumes)
D14 → pulses with gate and MOTION           (heartbeat resumes)
```

---

### SHIFT Button Interaction (D15 context)

| Action                    | Result                                                              |
| ------------------------- | ------------------------------------------------------------------- |
| SHIFT hold + MODE tap     | Toggle drone / gated mode — D15 breathes warm white in drone        |
| SHIFT held                | D15 white steady — secondary layer active                           |
| SHIFT held + turn knob    | Access secondary parameter (FATNESS / DRIFTSPEED / CURVETIME / VOL) |
| Hold MODE during power-on | Enter V/OCT calibration — D15 pulses white during routine           |

---

### Calibration Routine Visual

```txt
Entry: hold MODE during power-on
    D15 begins slow white pulse          (calibration mode active)
    D14 white steady                     (awaiting 1V input)

Step 1 — patch 1V, tap MODE:
    D14 brief white flash                (1V point accepted)
    D12 full brightness                  (point 1 of 2 confirmed)

Step 2 — patch 3V, tap MODE:
    D14 brief white flash                (3V point accepted)
    D16 full brightness                  (point 2 of 2 confirmed)

Calibration complete:
    All LEDs → white ripple centre-out   (same as mode change)
    D15 → three quick flashes → off      (confirms saved to flash)
    Module boots normally into PAIR mode
```

### Button Interaction Map

| Action                    | Result                                                                                    |
| ------------------------- | ----------------------------------------------------------------------------------------- |
| MODE tap (SHIFT not held) | Cycle voice mode: PAIR → CLOUD → CHORD → CASCADE → STRING → POLY → PAIR                   |
| SHIFT hold + turn knob    | Access secondary pot parameter (FATNESS / DRIFTSPEED / CURVETIME / VOL)                   |
| SHIFT hold + MODE tap     | Return to drone mode — hold SHIFT then tap MODE; clears gate arm, D15 breathes warm white |
| Hold MODE during power-on | Enter V/OCT calibration routine (2-point: 1V then 3V)                                     |

MODE may also serve as a third shift layer in future firmware — SHIFT+MODE for secondary parameters, a potential future third combination for deeper configuration without adding buttons.

That is the complete button interaction surface. Nothing else is hidden. No colour memorization required during performance — mode is chosen deliberately, confirmed by LED, heard immediately.

---

## Firmware Architecture

### Dual Core DSP Split

The RP2350 dual core is the key architectural advantage. Core 0 handles all deterministic, low-latency control work. Core 1 handles all audio DSP continuously.

```txt
Core 0 — deterministic control:
├── ADC scanning via 74HC4067 (all knobs + slow CVs)
├── Pitch CV reading (GP26 — oversampled, averaged, hysteresis)
├── Gate input reading (GP12)
├── Normalization probe scanning (GP22 — detects cable presence per jack)
├── MIDI parsing — UART1 + USB MIDI
├── Attenuverter logic (probe state → knob mode switch)
├── Parameter smoothing (one-pole LPF on all params)
├── Modulation routing matrix
├── LED update (APA102/SK9822 Dotstar, bitbang SPI on GP7/GP8)
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
    updateLEDs();       // APA102/SK9822 Dotstar bitbang SPI (GP7/GP8)
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

| Component           | Part              | Qty                  | Purpose                                               |
| ------------------- | ----------------- | -------------------- | ----------------------------------------------------- |
| Raspberry Pi Pico 2 | RP2350            | 1                    | MCU — primary target                                  |
| PCM5102A board      | —                 | 1                    | I2S stereo DAC                                        |
| 74HC4067            | DIP-24 or SOIC    | 1                    | 16:1 analog mux                                       |
| TL072 or TL074      | DIP/SOIC          | 2× TL072 or 1× TL074 | Op-amps — audio out + scaling                         |
| MCP6002             | SOT-23 or DIP-8   | 1                    | Rail-to-rail op-amp for precision CV scaling          |
| HCPL-0631           | SOIC8             | 1                    | MIDI input optocoupler                                |
| MCP1700-3302        | SOT-89            | 1                    | 3.3V LDO regulator                                    |
| BAT48 Schottky      | DO-35             | 10–12                | CV clamp diodes                                       |
| 1N5817 or SS14      | —                 | 2                    | Reverse polarity protection                           |
| Ferrite bead        | BLM21PG221        | 3                    | Rail noise filtering                                  |
| APA102/SK9822       | 5mm or SMD        | 5                    | Dotstar RGB status LEDs (clocked SPI, interrupt-safe) |
| Thonkiconn PJ398SM  | —                 | 10                   | Switched Eurorack jacks                               |
| Alpha 9mm pot       | RD901F            | 7                    | Panel knobs (2 large for ROOT/RELATION)               |
| Tactile button      | 6×6mm panel mount | 1                    | Single button                                         |
| Eurorack header     | 16-pin shrouded   | 1                    | Power connector                                       |
| Film cap            | 10µF              | 2                    | Audio AC coupling (L + R output)                      |
| Electrolytic cap    | 100µF             | 3                    | Rail bypass                                           |
| Ceramic cap         | 100nF             | 10+                  | IC decoupling                                         |
| Ceramic cap         | 10µF              | 1                    | LFO RC filter (if LFO CV out added)                   |
| Resistors 1%        | 10kΩ, 47kΩ, 100kΩ | ~30                  | Gain, dividers, pull-ups, mono sum                    |

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
pitch 440            → ROOT frequency (Hz)
note A4              → ROOT frequency by note name (A4=440 Hz, C4=261.63 Hz, etc.)
relation 0.3         → RELATION position (0.0–1.0)
shape 0.5            → SHAPE morph (0=sine, 1=hollow pulse)
motion 0.4           → MOTION depth (0.0–1.0)
fm 0.2               → FM depth (0.0–1.0)
curve 0.5            → CURVE position (0=pluck, 1=swell)
space 0.7            → SPACE width (0.0–1.0)
mode pair            → voice mode (pair/cloud/chord/cascade/string/poly)
vol 0.8              → output volume
filter lp 2000 0.6   → SVF filter: lp/hp/bp/notch/off, cutoff Hz, resonance 0–1 (M26a)
fxorder filter post  → effect chain ordering: filter/delay × pre/post (M26a)
reverb 0.4 0.7 0.5   → reverb: mix, size, damping (M26b; Core 1 offload)
reverb on            → re-enable with current mix/size/damping
reverb off           → disable (Core 1 stays in WFE — zero bus traffic)
reverb freeze on/off → freeze reverb tail (decay→1.0, input gated) (M41)
reverb modspeed <v>  → LFO rate multiplier 0.1–4.0 (M40)
reverb moddepth <v>  → LFO depth multiplier 0.0–1.0 (M40)
delay 0.5 150 0.6    → ping-pong delay: mix, time_ms, feedback (M26c)
delay on             → re-enable with current mix/time/feedback
delay off            → hard bypass (zero CPU)
status               → print all parameters (two lines: voice + fx chain)
cpu                  → print CPU headroom report (Method 2)
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

### VCV Rack Module Port

The intent is to have a software module in VCV Rack that shares the same DSP codebase and parameter structure as the hardware module. This allows users to prototype patches in software, or use the module's unique features in a DAW environment.

The VCV Module will implement a shim layer that translates VCV's control inputs (knobs, CV jacks, MIDI) into the same parameter set used by the hardware firmware. The DSP code will be shared as a library between the two.

This allows having in the module codebase, all required features for hardware integration like reading CV, reading potentiometers, MIDI input, handling LEDs and colors, etc. but in the VCV Rack environment, those will be fed by the VCV interface instead of the physical hardware inputs. The audio output will go to VCV's audio engine instead of the PCM5102. This also allows testing and iterating on features that require complex interaction (like the modulation matrix, or the SHAPE morphing) in a more visual and flexible environment before finalizing them in hardware.

The ideal workflow is to develop and test new features in the VCV Rack module first, where iteration is faster, and then port them to the hardware firmware with confidence that they work as intended.

The implementation should structure the codebase to maximize shared code between the hardware and VCV versions, with clear abstraction layers for hardware-specific functionality (e.g. ADC reading, MIDI parsing) that can be stubbed or replaced in the VCV environment.

Eg. The potentiometers should all be mapped to floats between 0.0 and 1.0 in the shared code and the inputs from both hardware and VCV should adhere to this. CV should work the same way — normalized to 0.0–1.0 range, with the same scaling and response curves applied in the shared code. MIDI input should also be abstracted to a common event structure that both hardware and VCV can generate.

Also the audio output layer should be abstracted so that the same DSP code can output to either the PCM5102 or VCV's audio engine without modification.

Preset saving and loading can also be shared, with the hardware version saving to flash and the VCV version saving to disk, but both using the same data format using VCV Rack preset conventions.

The VCV Rack plugin guide is at <https://vcvrack.com/manual/PluginGuide> and the doxygen api docs is at <https://vcvrack.com/docs-v2/>.

**Architecture decisions:**

- **Sample rate:** Firmware stays at 32768 Hz (power-of-2, PIO-friendly, clean 256 samples/control tick). All DSP engine constructors accept sample rate as a runtime argument rather than a compile-time template parameter — hardware passes `MOZZI_AUDIO_RATE`, VCV passes `args.sampleRate`.
- **`SynthEngine`:** All voice/effects DSP logic is extracted from `main.cpp` into a platform-independent class. Both `main.cpp` (hardware) and `AlloyFlux.cpp` (VCV) call into it. Features added to `SynthEngine` arrive on both platforms automatically.
- **`IHardwareIO`:** A thin abstract interface (`readPot`, `readCV`, `readButton`, `writeLight`) decouples hardware I/O from DSP logic. `HardwarePicoIO` implements mux ADC reads, hysteresis, ButtonEngine debounce, APA102 SPI. `VCVRackIO` reads `params[]`/`inputs[]` and writes `lights[]`. LED patterns, CV conditioning, and button debounce are implemented once in the hardware shim.

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
- [x] 10. **SHAPE morph engine** — continuous wavetable crossfade: sine → triangle → saw → pulse → hollow pulse; all tables band-limited at startup via additive synthesis + Lanczos sigma smoothing; `ShapeOsc` replaces `Osc16` pair; wavetables stored as `int16_t` (±32767, −96dBFS noise floor) — upgraded from `int8_t` (±127, −48dBFS)
- [x] 11. **Drift engine** — per-voice LCG random-walk frequency drift; `DriftEngine<N>` template; MOTION scales amplitude 0–±2.5 Hz; one-pole glide between targets
- [x] 12. **Chorus engine** — BBD-inspired stereo chorus on Core 0 ISR; phasor LFO (no trig in hot path); dual LFOs 0.513 Hz / 0.618 Hz, 90° offset; depth driven by MOTION; `ChorusMode` OFF/I/II/I+II; `chorus` serial command; overrun counter cleared after Mozzi init
- [x] 13. **SPACE spatializer** — stereo width and placement per voice
- [x] 14. **MOTION control** — governs drift + chorus depth simultaneously
- [x] 15. **CURVE engine** — AR envelope (audio-rate) + digital VCA; `CurveEngine<SAMPLE_RATE>` template; CURVE morphs attack (1ms–800ms) + release (80ms–1s); pluck mode auto-releases at peak (curve≤0.2); `gGatePatched=false` = drone bypass; serial `gate 1/0/free` + `curve <0–1>`
- [ ] 16. **Mux wiring** — 74HC4067 connected, all 7 knobs + 4 slow CVs readable
- [ ] 17. **Jack switch detection**
- [ ] 17b. **Normalization Probe** — GP22 shared-bus square-wave excitation; toggle high/low in `updateControl()`, read ADC delta per probed jack; unpatched → internal normalization/default; patched → pass external CV; BAT54S clamp on GP22 for overvoltage protection; V/Oct excluded from probe bus; J7 + J8 on bus (exact assignments at PCB layout)
- [ ] 18. **V/OCT input** — precision scaling, oversampling, hysteresis, GP26
- [ ] 19. **Gate input** — GP12, CURVE-shaped articulation trigger
- [ ] 20. **V/Oct calibration** — two-point routine via button hold on power-up
- [x] 21. **RELATION engine** — `gRelation` (semitones 0–24) maps voice 2 via `powf(2, rel/12)` in `updateControl()`; `gDetune` retained as Hz fine-spread; `VoiceMode` enum (PAIR/CLOUD/CHORD/CASCADE/STRING) with `mode` serial command; only PAIR active; framework ready for M22–M25
- [x] 22. **CLOUD mode** — multi-voice ensemble, animated stereo positioning
- [x] 23. **CHORD mode** — 4-voice interval table (11 shapes: Unison→Octaves); `voices[4]`/`subVoices[4]` arrays; `sActiveVoices` (2 for PAIR, 4 for CHORD); RELATION (0–24 st) sweeps + interpolates between chord shapes; hard-L/soft-L/soft-R/hard-R stereo pan (normalized ×256 fixed-point); `DriftEngine<4>`; cached 4× `powf` per shape/base-freq change; PAIR mode backward-compatible; `chord <name|0-10>` serial command as convenience shim over `rel`
- [x] 24. **CASCADE mode** — 2-voice FM synthesis: voice 1 (modulator) phase-modulates voice 0 (carrier); RELATION (0–24 st) selects harmonic ratio zone (1:1→4:3→3:2→2:1→5:2→3:1) and raw FM index 0–3.0; `sFmDepth = tanhf(index × 0.7) × kFmMaxScale` soft-clips depth and pre-scales to Q16 phase units; `ShapeOsc::nextPM(phaseOffset)` reads wavetable at offset phase in audio ISR without disturbing accumulator; both voices still pass through chorus and SPACE; idle modulator sub-voice still advanced each sample; MOTION drifts carrier and modulator independently; CC 115 band 63–83
- [x] 25. **STRING mode** — 4-voice vintage string ensemble; ±15¢ max spread (RELATION 0–24 st → 0–30¢ total); 3× drift multiplier (vs 1.5× CLOUD) for constant organic movement; `gChorusDepth = max(sMotion, 0.3f)` — chorus never fully stops; same hard-L/soft-L/soft-R/hard-R stereo geometry as CLOUD/CHORD; sub-octave voices with FATNESS identical to all other modes; CC 115 band 84–104
- [x] 26a. **Post Effects Section — Filter + Chain + Core 1 infra** — Cytomic TVA-SVF stereo filter (LP/HP/BP/NOTCH/OFF); `FilterEngine` with trig-free audio-rate path; `FxOrder` 2-flag reorderable chain (4 orderings: filter pre/post-chorus × delay pre/post-reverb); `ReverbEngine` abstract base + `NullReverb` stub running on Core 1 via volatile int32 inter-core slots (no mutex, additive 1-frame latency, artifact-free); `DelayEngine` static 26KB buffers + pass-through stub; `filter`, `fxorder`, `reverb`, `delay` serial commands; `-DDELAY_MAX_MS=200` compile flag; RAM 92KB (17.7%), Flash 117KB (2.8%)
- [x] 26b. **Dattorro plate reverb (M26b rev)** — `DattorroReverb` on Core 1; Dattorro 1997 plate topology; float delay lines; correct cross-coupling; **4 LFOs at 0.10/0.12/0.15/0.18 Hz** (Plateau/Valley inspired, 10× slower than original — eliminates metallic wobble); **all 4 tank APFs now modulated** (APF6+APF8 newly so); **OnePoleHP tank filter** (~30 Hz, arrests bass accumulation); **OnePoleHP output DC block** (~10 Hz, prevents tail DC offset at high decay); **7th output tap** per channel completing Dattorro Table 1; **freeze mode** (decay→1.0, input gated); **modSpeed/modDepth parameters** (M40 partial); FTZ on both cores; WFE/SEV inter-core sync; `reverb freeze/modspeed/moddepth` serial sub-commands; CC 112 revModSpeed, CC 113 revModDepth, CC 114 freeze; RAM 236KB (45.1%)
- [x] 26c. **Delay ring buffer** — stereo ping-pong delay; cross-channel feedback (L←fbR, R←fbL) creates L/R alternating bounce; linear fractional interpolation for accurate sub-sample delay time; `always_inline process()`; `delay on/off/mix/time/feedback` serial commands; `status` line 2 shows all fx state; RAM 236KB (45.1%)
- [ ] 26d. **Post Effects** — global chorus, Karplus-Strong Resonator (original M26 remainder)
- [ ] 27. **Hardware MIDI in** — UART1 RX GP9, TRS dual A/B circuit
- [x] 28. **Central param/CC dispatch table** — `include/param_map.h` + `src/param_map.cpp`; `CCParam` struct with `{cc, valMin, valMax, *target, name}`; `paramMap_dispatchCC()` shared by all transports; 10 parameters mapped (CC 1/7/71/72/73/74/91/92/93/94); `onControlChange` in USB MIDI reduced to 3 lines + specials (CC 64 sustain, CC 123 panic)
- [x] 29. **USB MIDI + MIDI channel config** — `Adafruit_USBD_MIDI` + `MIDI Library` via `-DUSE_TINYUSB`; composite CDC+MIDI device (serial console + MIDI coexist on same USB); Note On/Off → `gBaseFreq`/`gGateHigh` (monophonic, last-note priority); full CC map via `paramMap_dispatchCC`; Program Change 1–5 → VoiceMode; `usbMidi_init()` before `Serial.begin()` with `TinyUSBDevice.mounted()` wait; Web MIDI compatible (Chrome/Edge via `navigator.requestMIDIAccess`); `gMidiChannel` (0=omni, 1–16) set via `midichan` serial command; channel filter in all MIDI callbacks
- [x] 29b. **Flash config persistence** — `include/config_store.h` / `src/config_store.cpp`; Earle Philhower EEPROM emulation (wear-levelled circular buffer); `AlloyConfig` struct covers all synthesis + effects parameters (see M42/M42a); `configStore_load()` in `setup()` auto-restores on boot; `config save|load|reset [slot|all]` serial commands; three-layer flash protection: dirty check (memcmp), 10 s rate limit (live slot only), magic+version invalidation on struct change; 10-slot layout (`kMaxPresets=10`): slot 0 = auto-save live state, slots 1–9 = user presets; `config reset all` = factory reset
- [x] 29c. **MIDI SysEx config backup/restore** — dump/load `AlloyConfig` struct as SysEx message; allows users to manage presets via external MIDI controllers or DAWs that support SysEx, without needing the Web Configurator
- [x] 29d. **MIDI channel configurator** — allow users to set MIDI channel (0=omni, 1–16) via Web Configurator; store in flash; filter incoming MIDI messages accordingly
- [ ] 30. **APA102/SK9822 Dotstar LEDs** — bitbang SPI on GP7/GP8, full LED language per mode
- [x] 31. **Button UI (partial)** — `ButtonEngine` class: active-low INPUT_PULLUP, 4-tick debounce (~31 ms), `pressed()`/`released()`/`held()`/`isDown()` events; **GP10 MODE** cycles PAIR→CHORD; **GP11 SHIFT** — trig fires on **release** (not press) so holding SHIFT for combos doesn’t accidentally trigger; `sShiftConsumed` file-scope flag suppresses trig-on-release whenever SHIFT is consumed by any combo or future SHIFT+knob handler; **SHIFT held + MODE tap** → drone mode (`gGatePatched=false`, `sShiftConsumed=true`); SHIFT+knob secondary pot functions pending (M31 remainder); MODE reserved as potential third shift layer for future use
- [ ] 32. **PCB design** — KiCad, 14HP panel, Thonkiconn jacks, Pico 2 footprint
- [ ] 33. **Panel design** — Design final graphics and layout
- [ ] 34. **Expose I2C bus for Teletype** — I2C pins available on GP14 (SDA) and GP15 (SCL) for Teletype integration (like Mannequins Just Friends). Implement I2C slave handlers in firmware to receive parameter change commands from Teletype scripts, allowing deep integration without MIDI. Document command set and usage for Teletype users.
- [ ] 35. **Implement Teletype-support in it's firmware** — Inspired by Just Friends, add custom command set for controlling Alloy Flux parameters and presets via I2C from Teletype scripts
- [x] 36a. **Web Configurator MIDI CC control** — allow real-time parameter changes via Web MIDI API; visualize internal state (e.g. chord shape, LFO waveforms) in the UI for performance feedback
- [x] 36b. **Web Configurator preset management** — create, save, load presets from the browser; store in flash via SysEx or direct USB commands; backup/restore presets to/from files on the user's computer
- [ ] 36c. **Web Configurator parameter editing** — advanced configuration options like chord shape table editing, LFO wavetable upload, envelope curve configuration, MIDI CC mapping, etc.
- [ ] 37. **VCV Rack port** — software module sharing the same DSP codebase as the hardware firmware. [Ref](#vcv-rack-module-port)
  - [ ] 37a. **Plugin scaffold** — `vcv-plugin/` directory at repo root; `plugin.json` manifest (`slug: AlloyFlux`, `brand: Voltage Foundry Modular`); `Makefile` linking against Rack SDK 2.6.6 with `-I../include`; `plugin.hpp` / `plugin.cpp`; empty plugin loads in VCV Rack without errors.
  - [ ] 37b. **`SynthEngine` extraction** — all DSP logic moved from `main.cpp` into a platform-independent `include/SynthEngine.h` / `src/SynthEngine.cpp`; sample rate converted from compile-time template parameter to runtime constructor argument on all engines (`ShapeOsc`, `AREnvelope`, `ADSREnvelope`, `DriftEngine`, `ChorusEngine`) — hardware passes `MOZZI_AUDIO_RATE` (32768), VCV passes `args.sampleRate`; wavetable generation logic extracted to shared header; hardware `main.cpp` reduced to a thin shim calling `SynthEngine`; firmware build verified clean after refactor.
  - [ ] 37c. **VCV minimal audio** — `AlloyFlux.cpp` VCV `Module` struct; instantiates `SynthEngine`; ROOT knob wired to `setBaseFreq()`; stereo ±5V audio output; audible sine tone confirms DSP path through VCV audio engine.
  - [ ] 37d. **`IHardwareIO` abstraction** — `include/io/HardwareIO.h` defines `IHardwareIO` interface: `readPot(id)→0–1`, `readCV(id)→0–1`, `readButton(id)→bool`, `writeLight(id, r, g, b)`; `VCVRackIO` implementation reads `params[]`/`inputs[]`, writes `lights[]`; `HardwarePicoIO` implementation wraps existing mux ADC reads, ButtonEngine debounce, APA102 SPI — all hardware-specific logic lives here once; `main.cpp` and `AlloyFlux.cpp` both program against `IHardwareIO`.
  - [ ] 37e. **All knobs + CV jacks** — `configParam()` for all 7 knobs (ROOT, RELATION, SHAPE, MOTION, FM, CURVE, SPACE) with correct ranges; `configInput()` for all CV jacks (V/OCT 1V/oct, GATE, REL CV, SHP CV, MTN CV, SPC CV, FM IN, Assignable CV); `configOutput()` for L/R; scaling in `VCVRackIO::readCV()` matches hardware ADC conditioning path.
  - [ ] 37f. **Voice modes** — MODE button param and context-menu switch expose all 6 modes (PAIR/CLOUD/CHORD/CASCADE/STRING/POLY); `configSwitch()` with named labels; all mode logic already contained in `SynthEngine` after 37b.
  - [ ] 37g. **CurveEngine + gate** — GATE input drives envelope; CURVE knob; drone mode when GATE unpatched; AR/ADSR type selectable via right-click context menu (mirrors M43).
  - [ ] 37h. **Effects chain** — Chorus, Space, Filter (SVF/OTA), DattorroReverb, DelayEngine all run inline in `process()` (no Core 1 split needed in VCV); reverb/delay mix/time/feedback exposed as VCV-side params not on hardware panel.
  - [ ] 37i. **MIDI input** — `rack::midi::InputQueue` in module; NoteOn/Off → pitch/gate; CC dispatch via shared `paramMap_dispatchCC()` — same CC numbers as hardware.
  - [ ] 37j. **Scale quantizer** — SCALE and TRANSPOSE params wired through `quantizeNote()` on all note paths (same as `usb_midi.cpp`).
  - [ ] 37k. **Panel SVG + LEDs** — 14HP panel SVG matching hardware layout; 5× RGB LEDs mapped to voice activity / mode state / shift state per LED language spec (M30); light behaviour implemented once in `SynthEngine` or `IHardwareIO`, visible on both platforms.
  - [ ] 37l. **JSON state serialization** — `dataToJson()` / `dataFromJson()` saves voice mode, envelope type, filter type, FxOrder, MIDI channel, scale, transpose; VCV patch recall restores full synth state.
- [x] 38. **POLY mode — 4-voice true polyphony** — implement independent voice allocator for POLY mode; each MIDI note gets its own `ShapeOsc` + independent `CurveEngine` instance; round-robin allocation with oldest-note steal; voices distributed L→R across stereo field; V/OCT+GATE always slot 0; Program Change 6 → POLY mode; `mode poly` serial command; LED: D13 lime/yellow-green, D12 brightness tracks active voice count; evaluate CPU load on RP2350 with 4 independent envelopes + chorus
- [x] 39a. **Implement load/save presets via MIDI Sysex** — allows users to store and recall presets from web configurator
- [x] 39b. **Document MIDI implementation and SysEx format** — provide clear documentation on the MIDI CC mappings, SysEx message structure for presets, and how to integrate with external controllers or software
- [x] 40. **Expose reverb modulation parameters via MIDI CC** — `gRevModSpeed` (CC 112, 0.1–4.0) and `gRevModDepth` (CC 113, 0.0–1.0) added to central param map; `reverb modspeed/moddepth` serial sub-commands; `setModulation()` virtual method on `ReverbEngine`; change-detected in `updateControl()` at 128 Hz (partial — Web Configurator UI pending)
- [x] 41. **Reverb freeze mode** — CC 114 (≥64=on, <64=off) and `reverb freeze on/off` serial command; `freeze()` virtual method on `ReverbEngine`; `DattorroReverb`: decay→1.0 + new input gated when frozen; tail holds indefinitely at full level; re-introducing input mixes in cleanly on next onset (partial — SHIFT+knob macro gesture pending)
- [x] 42. **Improve flash persistence data** — all synthesis + effects parameters now persisted; `AlloyConfig` extended with filter (cutoff/res/mode/type), envelope (AR vs ADSR, full ADSR params + loop), reverb (enabled/mix/size/damping/modSpeed/modDepth/frozen), delay (time/feedback/mix), and FxOrder (filterPostChorus/delayPostReverb); `kConfigVersion` bumped 2→3 (old configs invalidated, safe defaults applied); RAM 236KB (45.2%), Flash 3.0%
- [x] 42a. **Factory reset + preset management** — `kMaxPresets` bumped 4→10 (slot 0 = auto-save live state, slots 1–9 = user presets); `configStore_save(slot)`, `configStore_load(slot)`, `configStore_reset(slot|255)` API updated with slot parameter (default 0); rate-limit bypassed for explicit preset slots 1–9, enforced only on auto-save slot 0; `reset all` (slot 255) wipes every slot; `cmd_config` updated: `config save [1-9]`, `config load [1-9]`, `config reset [1-9|all]`; inline usage help printed on bad args; RAM 236KB (45.2%), Flash 3.7%
- [x] 43. **Runtime-selectable DSP algorithms (M5x)** — abstract `FilterEngine` base + `SVFFilter` (moved to own `include/dsp/SVFFilter.h`) + `OTALadder` (ZDF 4-pole Moog-style, tanh-saturating, self-oscillating at res=1.0, LP4 only); abstract `EnvelopeEngine` base + `AREnvelope` (former `CurveEngine` alias retained) + `ADSREnvelope` (full ADSR + loop mode — turns envelope into cycling LFO); pointer-based runtime switching for both engines, change-detected in `updateControl()`; **phase reset on retrigger** — all oscillator phase accumulators reset to zero on gate rising edge, eliminating metallic/PWM artifact when retriggering during release; `filter type svf|ladder`, `env type ar|adsr`, `adsr <A> <D> <S> <R> [loop]`, `env loop on|off` serial commands; RAM 236KB (45.1%), Flash 3.0%
- [ ] 44. **Implement internal modulator LFO** — single or multi-waveform LFO (sine/triangle/saw/ramp/square); assignable to parameters like filter cutoff, reverb size, delay time; rate and depth controls; potential for tempo sync via MIDI clock; evaluate CPU load and sonic impact; consider adding as a modulation source in the Web Configurator with visual feedback
- [ ] 45. **Implement internal modulator matrix** — flexible routing of modulation sources (LFOs, envelopes, MIDI CCs) to any parameter; matrix stored in flash; real-time control via Web Configurator; evaluate CPU load and optimize as needed
- [ ] 46. **Implement Wavefolder** — non-linear waveshaping for added harmonic complexity; simple tanh or more complex multi-stage folding; parameterized by `foldAmount`; evaluate CPU load and sonic impact. Potentially add as a post-effect in the chain for more character.
- [ ] 47. **Improve new envelope** — Add new envelope that could be routed. Add exponential curve in addition to current linear.
- [ ] 48. **Create controller VST3 plugin** — optional software plugin for DAWs, using the same codebase where possible; MIDI control surface that sends commands to the hardware module; visual feedback of parameters and states; potential for preset management and integration with DAW automation
- [x] 49. **Scale quantization + transposition engine** — **Implemented.** 15-scale bitmask quantizer (`include/scale_quantizer.h`): CHROMATIC (bypass), MAJOR, NATURAL_MINOR, HARMONIC_MINOR, MELODIC_MINOR, PENTATONIC_MAJ, PENTATONIC_MIN, BLUES, DORIAN, PHRYGIAN, LYDIAN, MIXOLYDIAN, LOCRIAN, WHOLE_TONE, DIMINISHED. `quantizeNote(note, scale, transpose)` inline function searches outward ±1–6 semitones, ties go up. Applied to all MIDI NoteOn paths (mono + POLY) in `usb_midi.cpp`; serial `pitch` command bypassed. CC 103 = scale select (0–14 direct enum index); CC 104 = transpose (0–48 encodes −24…+24 st offset). `scale <name>` and `transpose <semitones>` serial commands. Config fields `quantizeScale` / `transpose` persisted in flash (kConfigVersion 5). Web Configurator: Voice → Scale (select) + Transpose (slider). *Inspired by Seashell's “customisable scale transposition engine”*
- [ ] 50. **MIDI learn mode** — gesture-based dynamic CC-to-parameter binding; hold a dedicated combo (e.g. SHIFT+MODE long-press), wiggle any hardware knob or CV source, then send any MIDI CC — the module binds that CC to that parameter; learned mappings stored in flash alongside preset slot; `learn` and `learn clear [param]` serial commands; Web Configurator shows current mapping with per-param override / clear; supersedes static M29d; *inspired by Seashell’s “MIDI learn functionality”*
- [ ] 51. **Web Configurator UX redesign (Seashell-inspired)** — streamlined single-screen layout inspired by Seashell’s compact controller software: fewer visual layers, larger touch targets, collapsible category strips, real-time oscilloscope/waveform preview pane driven by an audio snapshot CC stream; optional PWA install for standalone desktop/mobile use (replaces browser-tab workflow); evaluate Electron wrapper for OS-level MIDI device enumeration without Web MIDI permission prompts; *inspired by Seashell’s dedicated controller app (macOS/Windows/Linux builds)*
- [ ] 52. **Expand modulation matrix (Seashell-style macro control)** — build on M45 to add hardware macro knob: one knob simultaneously drives multiple mod-matrix destinations with per-destination depth and polarity; useful for performance (one twist = filter + reverb + drift together); store macro assignments in preset; Web Configurator drag-assign UI; *inspired by Seashell’s “4×4 modulation matrix mixer with hardware macro control”*
- [ ] 53. **Handle Clock** — MIDI clock sync for LFOs, envelopes, and delay time; `clock` serial command to set tempo; MIDI Clock Start/Stop/Continue handling; evaluate CPU load and timing accuracy; consider adding tap tempo via button for non-MIDI use. Forward MIDI to MIDI Out for external clock sync.


---

## Future Expansion

Possible future firmware additions:

- alternate chord tables (user-defined via Web USB)
- adaptive harmony — chord voicings follow scale context
- POLY + CHORD combined — each POLY voice fans into chord intervals
- alternate oscillator models (FM operator, additive)
- harmonic quantization engine
- I2C voice networking — chain multiple Alloy Flux modules
- external sync behavior (clock in, reset)
- MPE-inspired per-note expression via MIDI
- expansion module (2HP) using spare GPIO (GP0–GP2, GP19–GP21) for additional CV inputs and outputs

> Scale quantization (M49), MIDI learn (M50), Web Configurator redesign (M51), and modulation matrix macro control (M52) have been promoted from this list to formal milestones.

---

Voltage Foundry Modular — Alloy Flux
2026
