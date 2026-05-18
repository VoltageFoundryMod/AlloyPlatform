# Alloy Flux — User Manual

> **Firmware status: M1–M15 + M21 + M23 + M26a + M26b + M26c + M28 + M29 + M29b + M31(partial) + M40(partial) + M41(partial) + M5x** (PAIR + CHORD modes, serial console, RELATION interval engine, chord shape command, drift, chorus, stereo width, envelope/VCA, multimode filter + OTA ladder filter, Dattorro plate reverb with 4-LFO modulation + freeze mode, reverb modspeed/moddepth, stereo ping-pong delay, central param/CC table, USB MIDI + Web MIDI, MIDI channel config, flash config persistence, mode button cycling, drone-return via button combo or CC 119, SHIFT trig-on-release, runtime-selectable filter/envelope algorithms, ADSR envelope + loop mode, oscillator phase reset on retrigger)
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
      - [`note <name>` — Set pitch by note name](#note-name--set-pitch-by-note-name)
      - [`detune <hz>` — Stereo spread](#detune-hz--stereo-spread)
    - [RELATION — Voice Interval](#relation--voice-interval)
      - [`rel <0–24>` — Voice 2 interval](#rel-024--voice-2-interval)
      - [`mode <pair|cloud|chord|cascade|string>` — Voice mode](#mode-paircloudchordcascadestring--voice-mode)
      - [`chord <name|0–10>` — Chord shape selector](#chord-name010--chord-shape-selector)
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
  - [MIDI Control](#midi-control)
    - [`midichan <1–16|omni>` — MIDI receive channel](#midichan-116omni--midi-receive-channel)
    - [USB MIDI CC Map](#usb-midi-cc-map)
  - [Config Persistence](#config-persistence)
    - [`config save` / `config load` / `config reset`](#config-save--config-load--config-reset)
    - [Volume](#volume)
      - [`vol <0–1>` — Master volume](#vol-01--master-volume)
  - [Post-Effects](#post-effects)
    - [`filter <mode> [cutoff] [res]` — Multimode filter](#filter-mode-cutoff-res--multimode-filter)
    - [`fxorder <filter|delay> <pre|post>` — Effect chain order](#fxorder-filterdelay-prepost--effect-chain-order)
    - [`reverb <mix> [size] [damping]` — Plate reverb](#reverb-mix-size-damping--plate-reverb)
      - [Reverb sub-commands](#reverb-sub-commands)
    - [`delay <mix> [time_ms] [feedback]` — Ping-pong delay](#delay-mix-time_ms-feedback--ping-pong-delay)
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
    - [Lush plate reverb pad](#lush-plate-reverb-pad)
    - [Ping-pong echo lead](#ping-pong-echo-lead)
    - [Dark filtered drone](#dark-filtered-drone)
    - [Cathedral (reverb + delay)](#cathedral-reverb--delay)
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

Sets the root pitch of both voices. Both voices share this pitch; RELATION spreads them apart.

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

#### `note <name>` — Set pitch by note name

Alternative to `pitch` — set root frequency by scientific pitch notation (letter A–G, optional `#`/`b` accidental, octave number). A4 = 440 Hz reference.

```txt
note C4           # middle C — 261.63 Hz
note A4           # concert A — 440 Hz
note A3           # A3 — 220 Hz
note C#4          # C sharp 4 — 277.18 Hz
note Bb3          # B flat 3 — 233.08 Hz
```

---

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

### RELATION — Voice Interval

RELATION controls the harmonic relationship between the two voices. In PAIR mode it sets the pitch of voice 2 as a continuous interval above ROOT, from unison to two octaves.

#### `rel <0–24>` — Voice 2 interval

Range: 0.0–24.0 (semitones; fractional values allowed for micro-intervals)

Sets voice 2 pitch as semitones above ROOT: `ratio = 2^(rel / 12)`. Works on top of any `detune` fine-spread.

| Value | Interval       | Example at A4 (440 Hz)            |
| ----- | -------------- | --------------------------------- |
| 0     | Unison         | 440 Hz (both voices, chorus body) |
| 3     | Minor third    | 523 Hz                            |
| 4     | Major third    | 554 Hz                            |
| 5     | Perfect fourth | 587 Hz                            |
| 7     | Perfect fifth  | 659 Hz                            |
| 10    | Major seventh  | 739 Hz                            |
| 12    | Octave         | 880 Hz                            |
| 24    | Two octaves    | 1760 Hz                           |

```txt
rel 0             # unison — chorus and drift colour the stereo field
rel 7             # perfect fifth — open, stable harmony
rel 12            # octave — doubling, thickens fundamental
rel 4             # major third — bright, tense harmony
rel 3             # minor third — melancholic
rel 7.5           # micro-interval between fifth and tritone
```

> Combine `rel` with `detune` for intervals with micro-tuned width: `rel 7` + `detune 4` = a slightly detuned fifth, classic thick synth sound.

#### `mode <pair|cloud|chord|cascade|string>` — Voice mode

Default: `pair`

Selects the synthesis personality.

| Mode      | Description                                         | Status       |
| --------- | --------------------------------------------------- | ------------ |
| `pair`    | ROOT + RELATION dual voice — interval + fine detune | Active (M21) |
| `cloud`   | Multi-voice detuned ensemble                        | Future (M22) |
| `chord`   | 4-voice chord stack — RELATION sweeps chord shapes  | Active (M23) |
| `cascade` | Restrained FM oscillator interaction                | Future (M24) |
| `string`  | Vintage string machine ensemble                     | Future (M25) |

**CHORD mode** — RELATION knob (or CC 94) sweeps through 11 chord shapes, interpolating smoothly between them. Voices are spread hard-L → hard-R across the stereo field.

| RELATION | Chord      | Intervals (semitones) |
| -------- | ---------- | --------------------- |
| 0.0      | Unison     | 0, 0, 0, 0            |
| 0.1      | Power      | 0, 7, 12, 19          |
| 0.2      | Minor      | 0, 3, 7, 12           |
| 0.3      | Major      | 0, 4, 7, 12           |
| 0.4      | Sus2       | 0, 2, 7, 12           |
| 0.5      | Sus4       | 0, 5, 7, 12           |
| 0.6      | Major 7    | 0, 4, 7, 11           |
| 0.7      | Minor 7    | 0, 3, 7, 10           |
| 0.8      | Dominant 7 | 0, 4, 7, 10           |
| 0.9      | Diminished | 0, 3, 6, 9            |
| 1.0      | Octaves    | 0, 12, 24, 36         |

#### `chord <name|0–10>` — Chord shape selector

Convenience shim over `rel` for CHORD mode. Selects a shape by name or index without calculating semitone values manually.

```txt
chord major      # Major triad + octave — rel set to 7.2
chord minor      # Minor triad + octave — rel set to 4.8
chord dom7       # Dominant 7th — rel set to 19.2
chord 0          # Unison (all voices at root)
chord 10         # Octaves (0, 12, 24, 36 st)
```

Accepts all 11 names: `unison` `power` `minor` `major` `sus2` `sus4` `maj7` `min7` `dom7` `dim` `octaves`, or index 0–10.
Works in any mode — `rel` is always updated, so you can preview chord shapes while in PAIR mode too.

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

The CURVE system gives the module basic articulation without an external envelope/VCA. Two envelope algorithms are available at runtime:

**AR mode (default):** single-knob attack+release. CURVE and CURVETIME control the shape and speed.

**ADSR mode:** full four-stage envelope with independent Attack, Decay, Sustain, and Release times. Optional loop mode turns the envelope into a free-running LFO.

Switch envelope type without stopping audio:

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

### ADSR Envelope (M5x)

When `env type adsr` is active the single-knob AR is replaced with a full four-stage envelope.

#### `env type <ar|adsr>` — Envelope algorithm

| Type           | Description                                                          |
| -------------- | -------------------------------------------------------------------- |
| `ar` (default) | Single-knob AR — CURVE + CURVETIME control both attack and release   |
| `adsr`         | Full ADSR with independent attack, decay, sustain level, and release |

```txt
env type ar           # back to single-knob AR
env type adsr         # switch to full ADSR
```

#### `adsr <A> <D> <S> <R> [loop|noloop]` — ADSR parameters

All times in seconds. Sustain is a level (0.0–1.0).

| Parameter | Range      | Default |
| --------- | ---------- | ------- |
| Attack    | 0.001–10 s | 0.05 s  |
| Decay     | 0.001–10 s | 0.10 s  |
| Sustain   | 0.0–1.0    | 0.80    |
| Release   | 0.001–10 s | 0.30 s  |

```txt
adsr 0.01 0.2 0.7 0.4         # fast attack, medium decay, sustain 0.7, 0.4s release
adsr 0.5 0.3 0.6 1.0          # slow attack pad
adsr 0.001 0.1 0.0 0.05       # percussive pluck (zero sustain)
adsr 0.05 0.1 0.8 0.3 loop    # looping ADSR — cycles continuously as LFO
adsr 0.1 0.2 0.5 0.8 noloop   # remove loop flag
```

#### `env loop <on|off>` — ADSR loop mode

When loop is on, after the release reaches zero the envelope automatically restarts from attack. This turns the ADSR into a free-running, cycling amplitude LFO. The cycle rate is determined by the total of all four stage times.

```txt
env loop on           # enable loop
env loop off          # disable loop
```

> **Tip:** `adsr 0.1 0.05 0.0 0.1 loop` with `gate free` and a slow pad preset creates a tremolo effect driven by the ADSR loop. Adjust the times to change the tremolo rate.

> **Oscillator phase reset:** every time a gate rises (note on), all oscillator phase accumulators are reset to zero. This prevents the metallic/PWM-like artifact that occurs when retriggering during a release cycle — the attack always starts from a clean waveform zero-crossing.

---

## MIDI Control

Alloy Flux appears as a standard USB MIDI device — no drivers needed. Connect via USB and use any DAW, MIDI controller, or browser-based tool (Chrome/Edge support Web MIDI via `navigator.requestMIDIAccess`).

**Note On / Note Off** set the root pitch and trigger the gate (monophonic, last-note priority). Sending Note On also arms the envelope if it was in drone mode. To return to drone mode from MIDI, send **CC 119** (any value).

### `midichan <1–16|omni>` — MIDI receive channel

Default: `omni` (responds to all channels)

```txt
midichan 1        # listen only on channel 1 (most DAW default)
midichan 10       # channel 10 (drums convention — not recommended)
midichan omni     # back to all channels (default)
```

Combine with DAW multi-instrument routing to run multiple AlloyFlux modules on separate channels.

### USB MIDI CC Map

All continuous parameters are reachable via MIDI CC. Assignments follow GM/MMA conventions where a standard meaning exists.

| CC  | GM/MMA name         | AlloyFlux parameter | Range                          |
| --- | ------------------- | ------------------- | ------------------------------ |
| 1   | Modulation Wheel    | `motion`            | 0–1                            |
| 7   | Channel Volume      | `vol`               | 0–1                            |
| 64  | Sustain Pedal       | `gate` (hold)       | ≥64=on, <64=off                |
| 71  | Resonance / Timbre  | `curve`             | 0–1                            |
| 72  | Release Time        | `curvetime`         | 0.25–4                         |
| 73  | Attack Time         | `dspeed`            | 0.001–0.1                      |
| 74  | Brightness          | `shape`             | 0–1                            |
| 91  | Reverb Send Depth   | `space`             | 0–2                            |
| 92  | Tremolo Send Depth  | `detune`            | 0–200 Hz                       |
| 93  | Chorus Send Depth   | `fat`               | 0–1                            |
| 94  | Celeste / Variation | `rel`               | 0–24 semitones                 |
| 112 | (unassigned)        | `revModSpeed`       | 0.1–4.0 (LFO rate multiplier)  |
| 113 | (unassigned)        | `revModDepth`       | 0.0–1.0 (LFO depth multiplier) |
| 114 | (unassigned)        | reverb freeze       | ≥64=freeze on, <64=freeze off  |
| 119 | (unassigned)        | drone return        | — (any value)                  |
| 123 | All Notes Off       | panic               | —                              |

Program Change messages 1–5 select voice mode (1=PAIR, 2=CLOUD, 3=CHORD, 4=CASCADE, 5=STRING).

The CC table is defined in a single file (`src/param_map.cpp`) shared by all transports — future hardware TRS MIDI and I2C will use the same mapping automatically.

---

## Config Persistence

### `config save` / `config load` / `config reset`

Saves and restores all parameters to flash using the RP2350 EEPROM emulation library (wear-levelled, safe for thousands of cycles).

```txt
config save       # write current parameters to flash
config load       # restore last saved parameters
config reset      # wipe stored config (defaults used on next boot)
```

**Flash protection:** saves are rate-limited to one every 10 seconds. If the parameters haven’t changed since the last save, no write occurs (dirty check). The response tells you which case applied:

```txt
> config save
config saved

> config save
config unchanged — no write needed

> config save
config save throttled — wait 10s between saves
```

On boot, parameters are loaded automatically if a valid saved config exists. If the firmware version changes, the stored config is silently discarded and compile-time defaults are used.

> **Wear estimate:** at the 10 s rate limit, flash rated at 100,000 erase cycles ≈ 31 years of continuous saving. The wear-levelling circular buffer multiplies this further.

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

## Post-Effects

The post-effects chain sits between Chorus and the SPACE engine. All three effects are optional and fully bypassed (zero CPU) when disabled.

```txt
Signal chain (default): VCA → Filter → Chorus → Delay → Reverb → SPACE → Output
```

Effect positions are reorderable via `fxorder`.

### `filter <mode> [cutoff] [res]` — Multimode filter

Stereo filter — two runtime-selectable algorithms (see `filter type`).

| Mode    | Character                          |
| ------- | ---------------------------------- |
| `off`   | Bypass — zero CPU (default)        |
| `lp`    | Low-pass — warm, rolls off highs   |
| `hp`    | High-pass — removes low end        |
| `bp`    | Band-pass — midrange resonant peak |
| `notch` | Notch — scoops a frequency         |

| Parameter | Range       | Default |
| --------- | ----------- | ------- |
| cutoff    | 20–16000 Hz | 8000 Hz |
| resonance | 0.0–1.0     | 0.0     |

```txt
filter lp 2000 0.6    # low-pass at 2 kHz, resonance 0.6
filter hp 400         # high-pass at 400 Hz
filter bp 1200 0.8    # band-pass at 1.2 kHz, high resonance
filter off            # bypass
```

### `filter type <svf|ladder>` — Filter algorithm

Switches the filter engine at runtime. Both are always compiled into flash — switching is silent and glitch-free.

| Type            | Algorithm                  | Modes                | Character                                          |
| --------------- | -------------------------- | -------------------- | -------------------------------------------------- |
| `svf` (default) | Cytomic TVA state-variable | LP / HP / BP / NOTCH | Clean, precise, all-mode                           |
| `ladder`        | ZDF 4-pole Moog-style OTA  | LP4 only             | Warm, saturating; self-oscillates at resonance 1.0 |

```txt
filter type svf       # clean Cytomic SVF (default)
filter type ladder    # OTA 4-pole ladder — lp mode forced, tanh saturation
```

> **Tip:** with `ladder` and `resonance 0.9–0.95`, turning up resonance on low cutoffs gives a deep, warm Moog-style squeal. At `resonance 1.0` the ladder self-oscillates — use as a sine wave source.

### `fxorder <filter|delay> <pre|post>` — Effect chain order

Controls where the filter and delay sit relative to chorus and reverb.

| Command               | Chain result                                 |
| --------------------- | -------------------------------------------- |
| `fxorder filter pre`  | Filter → Chorus (default — shapes raw voice) |
| `fxorder filter post` | Chorus → Filter (sculpts the chorused mix)   |
| `fxorder delay pre`   | Delay → Reverb (default — reverb'd echoes)   |
| `fxorder delay post`  | Reverb → Delay (echoes of the reverb tail)   |

### `reverb <mix> [size] [damping]` — Plate reverb

Dattorro 1997 plate algorithm running entirely on Core 1 — zero load on the audio ISR. Four independent LFOs (0.10 / 0.12 / 0.15 / 0.18 Hz) modulate the tank allpass filters for smooth, diffuse reverberation.

| Parameter | Range   | Default | Notes                              |
| --------- | ------- | ------- | ---------------------------------- |
| mix       | 0.0–1.0 | 0.35    | Wet level added on top of dry      |
| size      | 0.0–1.0 | 0.5     | Tank decay — higher = longer tail  |
| damping   | 0.0–1.0 | 0.5     | High-frequency rolloff in the tail |

```txt
reverb 0.3 0.7 0.4    # subtle plate — large, slightly bright
reverb 0.5 0.9 0.7    # large hall — long dark tail
reverb 0.2 0.5 0.3    # small room — short, clear
reverb on             # re-enable with current settings
reverb off            # disable (Core 1 sleeps — zero CPU)
reverb freeze on      # hold reverb tail — decay → 1.0, input gated
reverb freeze off     # return to normal decay
reverb modspeed 2.0   # LFO rate multiplier 0.1–4.0 (default 1.0)
reverb moddepth 0.5   # LFO depth multiplier 0.0–1.0 (default 1.0)
```

#### Reverb sub-commands

| Sub-command           | Range   | Description                                                       |
| --------------------- | ------- | ----------------------------------------------------------------- |
| `reverb freeze on`    | —       | Freeze reverb tail — decay set to 1.0, input gated; tail sustains |
| `reverb freeze off`   | —       | Unfreeze — return to configured decay and re-open input           |
| `reverb modspeed <v>` | 0.1–4.0 | LFO rate multiplier; 1.0 = default (0.10–0.18 Hz range)           |
| `reverb moddepth <v>` | 0.0–1.0 | LFO depth multiplier; 0.0 = static (no modulation)                |

Also reachable via MIDI CC: CC 112 = modspeed, CC 113 = moddepth, CC 114 = freeze (≥64 on).

### `delay <mix> [time_ms] [feedback]` — Ping-pong delay

Stereo ping-pong delay — cross-channel feedback routes echoes L→R→L alternating. Maximum time: 300 ms.

| Parameter | Range     | Default | Notes                                   |
| --------- | --------- | ------- | --------------------------------------- |
| mix       | 0.0–1.0   | 0.0     | Wet level; 0 = bypass (zero CPU)        |
| time_ms   | 10–300 ms | 100 ms  | Fractional sample accuracy              |
| feedback  | 0.0–0.95  | 0.5     | Echo decay; >0.8 gives long fading tail |

```txt
delay 0.4 150 0.6     # ping-pong at 150 ms, 60% feedback
delay 0.5 80 0.4      # tight short bounces
delay 0.3 300 0.8     # long slow echo
delay on              # re-enable with current settings
delay off             # bypass
```

> **Tip:** `fxorder delay post` with long reverb creates echoes of the reverb tail — cathedral-like decay. Default `fxorder delay pre` creates reverb'd echoes — classic studio plate+delay sound.

---

## Knob Shift Functions

The panel has two buttons: **MODE** (GP10) and **SHIFT** (GP11). Hold SHIFT while turning a knob to access a secondary parameter — four knobs have shift functions, giving eight parameters from seven knobs.

| Knob  | Primary parameter               | Shift parameter (hold SHIFT)      |
| ----- | ------------------------------- | --------------------------------- |
| SHAPE | `shape` — waveform morph        | `fat` — sub oscillator level      |
| MOTN  | `motion` — drift + chorus depth | `dspeed` — drift glide rate       |
| CURVE | `curve` — envelope shape        | `curvetime` — envelope time scale |
| SPACE | `space` — stereo width          | `vol` — master output volume      |

The LED indicator dims white while shift mode is active. ROOT, RELATION, and FM knobs have no shift function.

**Drone shortcut:** holding MODE + SHIFT simultaneously returns to drone mode — envelope is disarmed and the module sounds continuously regardless of the last gate source.

> **Current firmware (M31 partial / M40 partial / M41 partial):** mode cycling and drone combo are active — MODE steps through PAIR → CHORD, MODE+SHIFT returns to drone. **SHIFT** also fires a 100 ms gate trigger on release (trig-on-release) — press and release SHIFT alone to trigger a one-shot note from the panel; if MODE+SHIFT drone combo was used, the trig is suppressed automatically (`sShiftConsumed`). Reverb now includes 4-LFO modulation, freeze, and modspeed/moddepth controls (CC 112/113/114). Shift functions (hold SHIFT + knob) are not yet implemented; secondary parameters are still accessed via serial commands.

---

## Gate and Envelope Modes

### Drone mode (default)

No gate patching required. The module sounds continuously. Useful for:

- Testing timbre without an envelope
- Pads and drones that sustain indefinitely
- Tuning and sound design exploration

Return to drone mode at any time via:

- **Serial:** `gate free`
- **Buttons:** hold MODE + SHIFT simultaneously
- **MIDI:** send CC 119 (any value)

**SHIFT button — one-shot trigger:** pressing and releasing SHIFT alone fires a 100 ms gate pulse (equivalent to `trig 100`). Use this to trigger the envelope from the panel without a MIDI keyboard or serial command. If the MODE+SHIFT drone combo was used during the same press, the trig is suppressed — no accidental retriggering.

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

`status` prints all current parameters across two lines — voice parameters on line 1, post-effects chain on line 2:

```txt
pitch=220.00 mode=PAIR rel=0.000 detune=4.00 shape=0.250 fat=0.400 motion=0.400 dspeed=0.0400 curve=0.500 ctime=1.00 gate=free chorus=I+II vol=0.800 space=1.000 midichan=omni
filter=off fxorder=filter:pre,delay:pre reverb=off delay=off
```

With effects active:

```txt
filter=lp cut=2000 res=0.60 fxorder=filter:post,delay:pre reverb=on mix=0.40 size=0.70 damp=0.50 delay=on mix=0.50 time=150ms fb=0.60
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

### Lush plate reverb pad

```txt
pitch 220
shape 0.25
fat 0.3
motion 0.4
curve 0.75
curvetime 1.5
reverb 0.4 0.85 0.5
gate free
```

### Ping-pong echo lead

```txt
note A4
shape 0.5
fat 0.0
motion 0.15
curve 0.3
curvetime 0.7
delay 0.45 160 0.65
chorus off
gate 1
gate 0
```

### Dark filtered drone

```txt
pitch 110
shape 0.5
fat 0.5
motion 0.5
filter lp 800 0.5
curve 0.8
curvetime 2.0
gate free
```

### Cathedral (reverb + delay)

```txt
pitch 261.63
shape 0.2
fat 0.2
motion 0.3
reverb 0.5 0.95 0.7
delay 0.3 300 0.75
fxorder delay post
curve 1.0
curvetime 2.0
gate 1
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

| Command                | Range                             | Description                                                                        |
| ---------------------- | --------------------------------- | ---------------------------------------------------------------------------------- |
| `pitch <hz>`           | 20–8000                           | Base frequency                                                                     |
| `note <name>`          | —                                 | Set pitch by note name (C4, A#3, etc.)                                             |
| `mode <name>`          | pair/chord/…                      | Voice mode (pair: M21, chord: M23)                                                 |
| `rel <0–24>`           | 0–24 st                           | RELATION: voice 2 interval (0=unison, 7=fifth, 12=octave)                          |
| `detune <hz>`          | 0–200                             | Symmetric fine spread between voices                                               |
| `shape <0–1>`          | 0–1                               | Waveform: 0=sine 0.25=tri 0.5=saw 0.75=pulse 1=hollow                              |
| `fat <0–1>`            | 0–1                               | Sub oscillator level (0=off, 1=50% of main)                                        |
| `motion <0–1>`         | 0–1                               | Frequency drift + chorus depth (0=dry/static, 1=full)                              |
| `dspeed <n>`           | 0.001–0.1                         | Drift glide speed (τ coefficient)                                                  |
| `chorus <mode>`        | off/I/II/I+II                     | Chorus mode (default: I+II)                                                        |
| `space <0–2>`          | 0–2                               | Stereo width (0=mono, 1=full stereo, 2=hyper-wide, default: 1)                     |
| `curve <0–1>`          | 0–1                               | Envelope shape (0=pluck, 1=swell)                                                  |
| `curvetime <n>`        | 0.25–4                            | Envelope time scale (1=default)                                                    |
| `gate <1\|0\|free>`    | —                                 | Gate high / low / bypass (drone)                                                   |
| `trig [ms]`            | —                                 | One-shot gate pulse (default 100 ms)                                               |
| `vol <0–1>`            | 0–1                               | Master volume                                                                      |
| `filter <mode> …`      | off/lp/hp/bp/notch                | Multimode filter: mode [cutoff Hz] [resonance 0–1]                                 |
| `filter type <t>`      | svf / ladder                      | Filter algorithm: `svf` (clean, all modes) or `ladder` (LP4, saturating, self-osc) |
| `fxorder <fx> <pos>`   | filter/delay × pre/post           | Effect chain position                                                              |
| `reverb <mix> …`       | 0–1, 0–1, 0–1                     | Plate reverb: mix size damping; `reverb on/off`                                    |
| `reverb freeze on/off` | —                                 | Hold reverb tail (decay→1.0, input gated) / release                                |
| `reverb modspeed <v>`  | 0.1–4.0                           | LFO rate multiplier (default 1.0)                                                  |
| `reverb moddepth <v>`  | 0.0–1.0                           | LFO depth multiplier (default 1.0; 0=static)                                       |
| `delay <mix> …`        | 0–1, 10–300, 0–0.95               | Ping-pong delay: mix time_ms feedback; `delay on/off`                              |
| `env type <t>`         | ar / adsr                         | Envelope algorithm: `ar` (single-knob) or `adsr` (full ADSR)                       |
| `adsr <A> <D> <S> <R>` | 0.001–10, 0.001–10, 0–1, 0.001–10 | ADSR times (s) + sustain level; append `loop` for loop mode                        |
| `env loop <on\|off>`   | —                                 | Toggle ADSR loop mode (envelope as cycling LFO)                                    |
| `midichan <n\|omni>`   | 1–16, omni                        | MIDI receive channel (default: omni)                                               |
| `config <cmd>`         | save/load/reset                   | Persist / restore / wipe all parameters to flash                                   |
| `status`               | —                                 | Print all current parameters                                                       |
| `cpu`                  | —                                 | Audio ISR timing and headroom                                                      |
| `perf on\|off`         | —                                 | Auto CPU reporting every 5 s                                                       |
| `help`                 | —                                 | List all commands                                                                  |

---

## Default Values

| Parameter     | Default                   | Notes                                       |
| ------------- | ------------------------- | ------------------------------------------- |
| `pitch`       | 440 Hz                    | A4                                          |
| `mode`        | pair                      |                                             |
| `rel`         | 0.0                       | Unison (voice 2 at ROOT)                    |
| `detune`      | 0 Hz                      | No fine spread                              |
| `shape`       | 0.0                       | Sine                                        |
| `fat`         | 0.4                       | Sub slightly audible                        |
| `motion`      | 0.0                       | Static                                      |
| `dspeed`      | 0.04                      | ~0.20 s drift glide                         |
| `chorus`      | I+II                      | Juno I+II stereo spread                     |
| `space`       | 1.0                       | Full stereo (identity, range 0–2)           |
| `curve`       | 0.5                       | Natural AR                                  |
| `curvetime`   | 1.0                       | Normal speed                                |
| `gate`        | free                      | Drone, envelope bypassed                    |
| `vol`         | 0.8                       |                                             |
| `filter`      | off                       | Filter bypassed (SVF algorithm)             |
| `filter type` | svf                       | Cytomic SVF                                 |
| `env type`    | ar                        | Single-knob AR envelope                     |
| `adsr`        | 0.05 / 0.10 / 0.80 / 0.30 | Attack / Decay / Sustain / Release defaults |
| `env loop`    | off                       |                                             |
| `reverb`      | off                       | Reverb disabled                             |
| `delay`       | off                       | Delay bypassed (mix=0)                      |
| `midichan`    | omni                      | All MIDI channels                           |

> All parameters marked above are automatically restored on boot if `config save` has been used.
