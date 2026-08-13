# Alloy Flux — User Manual

## Dual Relation Oscillator · Stereo Voice · Eurorack

---

## What Is Alloy Flux?

Alloy Flux is a stereo oscillator that sounds rich and full with minimal patching. Plug in the outputs to your mixer and you immediately hear stereo movement, harmonic warmth, and gentle drift. Plug in a V/OCT cable and a gate and control the pitch and articulation — everything needed to play a complete, musical voice.

The module is built around one idea: **two voices in a relationship.** Rather than exposing two independent oscillators, Alloy Flux gives you a **ROOT** oscillator and a **RELATION** — the second voice is always defined relative to the first. That relationship changes meaning depending on which voice **Mode** is active, from simple interval tuning to full chorus ensemble, harmonic chord stacking, FM synthesis, and true four-voice polyphony.

Want to integrate the module to your DAW or MIDI controller? The built-in USB MIDI and TRS MIDI inputs mirror all parameters to MIDI CCs in real time, and support full patch dump/restore over SysEx. No drivers or native app required — connect with the Web Configurator for an intuitive visual editor, preset management, and serial console access.

---

## Specs at a Glance

|             |                                                                             |
| ----------- | --------------------------------------------------------------------------- |
| Format      | Eurorack                                                                    |
| Width       | 14HP                                                                        |
| Power       | ~100mA +12V, ~5mA −12V                                                      |
| Voice Modes | PAIR / CLOUD / CHORD / CASCADE / STRING / POLY                              |
| Knobs       | 9 — ROOT, RELATION, SHAPE, MOTION, COLOR, CURVE, SPACE, DELAY, REVERB       |
| Jacks       | 10 — V/OCT, GATE, MIDI, REL CV, SHP CV, MTN CV, FM IN, SPC CV, L OUT, R OUT |
| Buttons     | 2 — MODE + SHIFT                                                            |
| LEDs        | 7× RGB (voice activity, motion layer, mode, shift/drone, heartbeat)         |
| Audio       | Stereo 16-bit, 48000 Hz, PCM5102A I2S DAC                                   |
| MIDI        | USB MIDI + TRS MIDI (simultaneous)                                          |

---

## Quick Start

1. Patch a V/OCT source into **V/OCT**
2. Patch a gate source into **GATE**
3. Connect **L OUT** and **R OUT** to your mixer
4. Press a note — you will hear a stereo dual-oscillator with subtle drift

If no gate is sent, the module defaults to drone mode — the voices run continuously without needing the gate trigger. Once a gate is received, it returns to normal gated operation and can be returned to drone mode by holding SHIFT and tapping MODE.

From there:

- Turn **RELATION** to set the harmonic interval between the two voices
- Turn **MOTION** to bring the sound to life with drift and chorus movement
- Turn **SHAPE** to morph the waveform from warm sine to bright saw
- Turn **CURVE** to shape the envelope from pluck to slow swell
- Tap **MODE** to switch to a different voice character

---

## Panel Overview

### Jacks

| Label  | Type   | Function                                        |
| ------ | ------ | ----------------------------------------------- |
| V/OCT  | Input  | Pitch — 1V/oct                                  |
| GATE   | Input  | Note trigger / envelope                         |
| MIDI   | Input  | TRS MIDI (Type A/B accepted)                    |
| REL CV | Input  | Modulates RELATION                              |
| SHP CV | Input  | Modulates SHAPE                                 |
| MTN CV | Input  | Modulates MOTION                                |
| FM IN  | Input  | FM modulation — audio rate capable              |
| SPC CV | Input  | Modulates SPACE                                 |
| L OUT  | Output | Left / mono (passive mono sum when R unplugged) |
| R OUT  | Output | Right stereo                                    |

### Buttons

| Button       | Action               | Result                                                                  |
| ------------ | -------------------- | ----------------------------------------------------------------------- |
| MODE         | Tap                  | Cycle voice mode: PAIR → CLOUD → CHORD → CASCADE → STRING → POLY → PAIR |
| SHIFT        | Hold + knob          | Access secondary function for that knob                                 |
| SHIFT + MODE | Hold SHIFT, tap MODE | Toggle drone mode (sustained without gate)                              |
| MODE         | Hold during power-on | Enter V/Oct two-point calibration                                       |

---

## Voice Modes

Tap **MODE** to cycle through six voice characters. The centre LED (L2) shows the current mode colour.

---

### PAIR — Dual Oscillator _(default)_

**Sound:** clean, direct, fundamental. Two oscillators in a defined relationship — warm, full, immediate.

The foundation mode. Root and Relation play together with configurable interval and FM interaction. Best starting point for most patches. In this mode, note input (eg. from MIDI) is monophonic and the next received note steals the voice.

| Control  | Effect in PAIR                                                                           |
| -------- | ---------------------------------------------------------------------------------------- |
| RELATION | Interval between root and second voice in semitones (0 = unison, 7 = fifth, 12 = octave) |
| COLOR    | FM depth — adds harmonic complexity and warmth (low = subtle, high = metallic)           |
| MOTION   | Subtle drift between the two voices; gentle stereo movement                              |
| SPACE    | Stereo spread — how far apart the two voices sit                                         |

**Tips:**

- RELATION at 7 semitones (a fifth) is the classic starting point
- Bring MOTION up slightly for a more organic, breathing tone
- COLOR at low values adds harmonic warming without going metallic

---

### CLOUD — Ensemble

**Sound:** thick, lush, detuned. Multiple virtual voices distributed across the stereo field with animated positioning — supersaw-like density without harshness.

| Control  | Effect in CLOUD                                                                          |
| -------- | ---------------------------------------------------------------------------------------- |
| RELATION | Ensemble spread and density — CCW = tight unison, CW = wide shimmer (±25¢ across voices) |
| MOTION   | Brings the ensemble to life — voices drift and breathe at higher MOTION values           |
| COLOR    | Fine Hz detune spread — adds beating and shimmer on top of the RELATION cent spread      |
| SPACE    | Width of the stereo ensemble image                                                       |

**Tips:**

- Slow CURVE (swell) + high MOTION = classic lush pad territory
- CLOUD works well with reverb — the natural drift and reverb movement complement each other
- REL CV from an LFO slowly pulses the ensemble width for breathing pads

---

### CHORD — Harmonic Stack

**Sound:** four-voice harmonic content from a single note. RELATION sweeps through 11 chord shapes — from unison to full octave stacks.

| Control  | Effect in CHORD                                                                |
| -------- | ------------------------------------------------------------------------------ |
| RELATION | Selects and morphs between chord shapes (see table below)                      |
| COLOR    | Fine Hz detune spread across all chord voices — adds ensemble ensemble beating |
| MOTION   | Drift animates each voice of the chord independently                           |
| SPACE    | Distributes chord voices across the stereo field                               |

**Chord table:**

| RELATION position | Chord      | Intervals     |
| ----------------- | ---------- | ------------- |
| 0                 | Unison     | 0, 0, 0, 0    |
| 1                 | Power      | 0, 7, 12, 19  |
| 2                 | Minor      | 0, 3, 7, 12   |
| 3                 | Major      | 0, 4, 7, 12   |
| 4                 | Sus2       | 0, 2, 7, 12   |
| 5                 | Sus4       | 0, 5, 7, 12   |
| 6                 | Major 7    | 0, 4, 7, 11   |
| 7                 | Minor 7    | 0, 3, 7, 10   |
| 8                 | Dominant 7 | 0, 4, 7, 10   |
| 9                 | Diminished | 0, 3, 6, 9    |
| 10                | Octaves    | 0, 12, 24, 36 |

**Tips:**

- REL CV from a sample-and-hold creates instant random chord changes
- Morphing RELATION slowly during a long swell envelope = chord evolution
- L1/L5 show amber in CHORD mode

---

### CASCADE — FM Synthesis

**Sound:** bell-like to metallic-warm, harmonically complex. RELATION selects the FM ratio between the modulator and carrier oscillators. COLOR controls the FM depth independently.

FM here is intentionally restrained: depth is soft-clipped, ratios are musical, and the result stays harmonic across the full RELATION sweep.

| Control  | Effect in CASCADE                                                                     |
| -------- | ------------------------------------------------------------------------------------- |
| RELATION | Sweeps FM ratio zones (1:1 → 4:3 → 3:2 → 2:1 → 5:2 → 3:1)                             |
| COLOR    | FM depth — 0 = dry carrier, full CW = rich harmonics (soft-clipped)                   |
| MOTION   | Drifts carrier and modulator independently — the harmonic relationship itself wanders |

**Tips:**

- Keep MOTION above 20% to avoid a static FM character — the gentle drift keeps it musical
- Low COLOR values = subtle harmonic warming; high values = pronounced bell or metal tones
- Sweeping RELATION slowly while playing = evolving FM timbre in real time
- L1/L5 show magenta in CASCADE mode

---

### STRING — Vintage Ensemble

**Sound:** warm, constantly moving, atmospheric. Inspired by classic string machine ensemble sections — the chorus movement never fully stops, even at zero MOTION.

This is the most atmospheric mode. SPACE has its strongest effect here.

| Control  | Effect in STRING                                                           |
| -------- | -------------------------------------------------------------------------- |
| RELATION | Microdetune spread across all four voices — CCW = tight, CW = wide shimmer |
| COLOR    | Fine Hz detune spread — adds a second layer of beating on top of RELATION  |
| MOTION   | Deepens chorus movement; a baseline chorus floor remains active regardless |
| SPACE    | Width of the ensemble image — very wide at full CW                         |

**Tips:**

- SPACE CW + slow CURVE swell = massive string section entrance
- MOTION at 30–50% is the sweet spot for vintage string character
- Add a touch of reverb for classic string-machine depth
- L1/L5 show purple in STRING mode

---

### POLY — True Polyphony

**Sound:** four-voice polyphonic — each MIDI note is independent with its own envelope, drift, and stereo position.

Voices are distributed left to right across the stereo field: slot 0 → hard left, slot 1 → soft left, slot 2 → soft right, slot 3 → hard right. Round-robin allocation, oldest-note stolen on a fifth note.

| Control  | Effect in POLY                                               |
| -------- | ------------------------------------------------------------ |
| RELATION | Global detune spread across all active voices                |
| COLOR    | Fine Hz detune spread per voice slot (adds ensemble beating) |
| MOTION   | Per-voice drift — each voice drifts independently            |
| CURVE    | Envelope shape applies per-voice — each note has its own AR  |
| SPACE    | Stereo spread of the voice distribution                      |

**Tips:**

- Pair with a MIDI keyboard for immediate polyphonic play
- CURVE swell + slow MOTION = lush evolving pads from held chords
- V/OCT + GATE input always plays into voice slot 0; MIDI fills slots 1–3
- L2 shows lime/yellow-green in POLY mode; L1 brightness tracks active voice count

---

## Controls Reference

### ROOT

Primary pitch. Sets the pitch centre for the entire module. Apply V/OCT for melodic tracking.
_Associated CV: V/OCT jack_

---

### RELATION _(signature control — largest knob)_

The defining control of Alloy Flux. Its behaviour changes per mode — always governs the relationship between ROOT and everything else. Controls interval, spread, chord shape, FM depth, or polyphonic detune depending on the active mode.

_Associated CV: REL CV jack_
When REL CV is patched, RELATION knob becomes an **attenuverter** for that CV (centre = no effect, CW = full depth, CCW = inverted depth).

---

### SHAPE

Continuous waveform morph across five timbres:

```txt
Fully CCW ────────────────────────────────────── Fully CW
  Sine    Triangle     Saw     Pulse    Hollow Pulse
```

_Associated CV: SHP CV jack_
When SHP CV is patched, SHAPE becomes an attenuverter for that CV.

**SHIFT function:** Hold SHIFT + turn SHAPE → adjusts **FATNESS** (sub oscillator level — adds a square wave one or two octaves (set by configurator) below each voice for Juno-style body).

---

### MOTION

Controls all internal animation depth simultaneously — drift, chorus modulation, stereo movement, voice timing offsets, and phase instability.

- At zero: module is stable and static
- At full: module breathes and moves — string machine territory

_Associated CV: MTN CV jack_
When MTN CV is patched, MOTION becomes an attenuverter for that CV.

**SHIFT function:** Hold SHIFT + turn MOTION → adjusts **DRIFTSPEED** (how quickly each voice steps toward a new random pitch target).

---

### COLOR

Tonal color and FM depth. Behaviour changes per mode:

- **PAIR / CASCADE:** FM depth — the RELATION voice modulates ROOT via phase modulation. Low values add harmonic warmth; full CW produces bell-like or metallic harmonics.
- **CLOUD / CHORD / STRING / POLY:** Fine Hz detune spread across all voices. Adds a second layer of beating and shimmer on top of the RELATION cent spread. At full CW, outer voices are up to ±25 Hz from nominal.

At COLOR = 0 the effect is completely absent in all modes.

---

### CURVE

Envelope and articulation shaping. Controls attack and release time together, and how much the note sustains while the gate is held:

- Fully CCW to ~0.2: pluck — fast attack, fast decay (percussive). The note is a fixed-length blip; holding the gate longer does not lengthen it.
- ~0.2 to ~0.4: the note decays partway and then holds at that level — the sustain amount rises smoothly across this stretch of the knob.
- Centre: natural attack, medium decay, holds while gate is high
- Fully CW: swell — slow attack, long sustain (pad-like)

The module includes an envelope and VCA — no external envelope needed for basic play.

**SHIFT function:** Hold SHIFT + turn CURVE → adjusts **CURVETIME** (overall envelope time scale — compresses or stretches both attack and release uniformly).

---

### SPACE

Stereo width and placement. Controls how far apart voices sit in the stereo field.

- Fully CCW: voices narrow toward mono centre
- Fully CW: voices spread to full stereo width with phase offsets

SPACE has its strongest effect in STRING mode.

_Associated CV: SPC CV jack_
When SPC CV is patched, SPACE becomes an attenuverter for that CV.

**SHIFT function:** Hold SHIFT + turn SPACE → adjusts **VOL** (master output level).

---

### Glide (Portamento)

Glide causes pitch changes to slide smoothly from the previous note to the new one rather than jumping instantly — a classic lead synth and bass effect.

**Enable/disable:** Use CC 65 (≥64 = on, <64 = off) or the **Web Configurator** (Animation → Glide toggle).

**Glide time:** Use CC 5 to set the slide duration from 0 (instant, effectively off) to 2 seconds. The glide uses a one-pole exponential smoother so short slides are snappy and long slides trail off naturally.

Glide applies to all monophonic voice modes (PAIR, CLOUD, CHORD, CASCADE, STRING). In POLY mode each voice has its own independent pitch and glide has no effect.

---

### Scale Quantizer & Transpose

The scale quantizer snaps incoming MIDI notes to a chosen musical scale before they are played. Notes that fall outside the scale are shifted to the nearest in-scale semitone (ties go up).

**Scale select:** CC 103 (0 = Off / chromatic, 1–14 = scale index). Available scales: Major, Minor (Natural), Harmonic Minor, Melodic Minor, Pentatonic Major, Pentatonic Minor, Blues, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Whole Tone, Diminished. Select with the **Web Configurator** (Voice → Scale) or send CC 103 directly.

**Transpose:** CC 104 shifts all incoming MIDI notes by −24 to +24 semitones. The CC value encodes the offset as `value − 24` (CC 24 = −0 st, CC 0 = −24 st, CC 48 = +24 st). Transpose is applied before scale quantization, so the root stays consistent when you move both together.

Quantization applies to all MIDI NoteOn events (monophonic and POLY modes). The `pitch` serial command for calibration bypasses quantization.

---

## Shift Functions Summary

Hold **SHIFT** and turn a knob to access its secondary parameter. The L4 LED (near SHIFT button) lights white while SHIFT is held.

| Knob   | Primary                        | SHIFT + Knob                        |
| ------ | ------------------------------ | ----------------------------------- |
| SHAPE  | Waveform morph (sine → hollow) | **FATNESS** — sub oscillator level  |
| MOTION | Drift + chorus depth           | **DRIFTSPEED** — drift glide rate   |
| CURVE  | Envelope shape (pluck → swell) | **CURVETIME** — envelope time scale |
| SPACE  | Stereo width                   | **VOL** — master output volume      |
| DELAY  | Delay wet mix                  | **DELAYTIME** — delay time (ms)     |
| REVERB | Reverb wet mix                 | **REVERBSIZE** — virtual plate size |

ROOT, RELATION, and FM have no shift function — full knob travel is needed for precision.

---

## Knob Takeover

Alloy Flux can be driven from the panel and from the Web Configurator (or a DAW, or a MIDI controller) at the same time. Whatever changes a parameter, the panel LEDs and the Web Configurator both follow it — the module reports its own state, so a knob you turn shows up on screen, and a slider you move on screen takes effect immediately.

Only the panel knobs can be out of step, because a physical knob cannot move itself. When the web sets SHAPE to 0.80 while the knob sits at 0.20, the knob is no longer telling the truth. Turning it hands control back, and how it does that is selectable:

- **Scale** _(default)_ — the value moves as soon as the knob does, in the same direction, scaled across the travel that remains, and lands exactly on the knob position at either end. No jump, and no dead knob.
- **Pickup** — the knob does nothing until it passes through the current value, then takes over. No jump, but the knob can feel dead for up to a full turn.
- **Jump** — the first movement takes over instantly. Simplest, and the most abrupt.

A knob has to move about 1.5 % of its travel to count as touched, so nothing is stolen by vibration or noise. The mode is saved with your settings — set it with `pot takeover scale|pickup|jump` on the serial console, and use `pot sync` to make every knob the truth immediately.

At power-on the module restores its last state rather than reading the knobs, so a patch survives a power cycle even though the knobs are wherever you left them. Each knob claims its parameter the first time you move it.

---

## LED Guide

### LED Positions

```txt
L1  ·  ·  ·  L5       ← voice activity (left / right)
L2  ·  ·  ·  L4       ← mode indicator / shift state
    · L3  ·           ← motion heartbeat / global
```

### L2 — Mode Indicator

Always shows the current voice mode as a steady colour.

| Mode    | Colour              |
| ------- | ------------------- |
| PAIR    | Soft white          |
| CLOUD   | Cyan                |
| CHORD   | Amber               |
| CASCADE | Magenta             |
| STRING  | Purple              |
| POLY    | Lime / yellow-green |

When changing mode: brief white flash → settles to new colour.

### L1 + L5 — Voice Activity

Breathe with the audio envelope in all modes. Bright on attack, fade on release.

| Mode    | L1 (left)                     | L5 (right)                         |
| ------- | ----------------------------- | ---------------------------------- |
| PAIR    | Warm red — envelope level     | Cool blue — relation depth         |
| CLOUD   | Cyan — left voice position    | Cyan — stereo spread               |
| CHORD   | Amber — root envelope         | Amber dimmer — interval spread     |
| CASCADE | Magenta — carrier activity    | Magenta brighter — modulator depth |
| STRING  | Purple — slow drift           | Purple — offset phase              |
| POLY    | Warm red — active voice count | Cool blue — color spread           |

### L4 — Shift / Drone State

Dark in normal operation. Lights when state changes:

| State              | Colour     | Pattern           |
| ------------------ | ---------- | ----------------- |
| Normal             | Off        | Dark              |
| SHIFT held         | White      | Steady while held |
| Drone mode active  | Warm white | Slow breathe      |
| Calibration active | White      | Slow pulse        |

### L3 — Motion Heartbeat

Shows module activity and animation state.

| State                 | Colour      | Pattern                            |
| --------------------- | ----------- | ---------------------------------- |
| MOTION = 0, silent    | Off         | Dark                               |
| MOTION > 0            | Green       | Pulses at drift rate               |
| Gate active           | White       | Bright on attack, fades with CURVE |
| Drone, no MOTION      | Green dim   | Very slow breathe                  |
| CASCADE active        | Magenta dim | Pulses with FM depth               |
| STRING / chorus heavy | Purple dim  | Slow movement                      |

---

## Drone Mode

Drone mode keeps voices running continuously without needing a sustained gate — useful for ambient playing and held textures. The module starts in drone mode until a gate is present.

**Enter drone:** Hold SHIFT then tap MODE. L4 fades up to warm white slow breathe.

**Exit drone:** First incoming gate automatically returns to gated mode, or hold SHIFT and tap MODE again to toggle back.

In drone mode, V/OCT and MIDI notes change pitch without retriggering the envelope.

---

## V/Oct Calibration

Alloy Flux uses a two-point software calibration for accurate pitch tracking.

**To calibrate:**

1. Hold **MODE** while powering the module on
2. L4 begins a slow white pulse — calibration mode is active
3. Patch a **1V** reference into V/OCT, tap MODE to confirm
4. Patch a **3V** reference into V/OCT, tap MODE to confirm
5. All LEDs flash white, L4 triple-flashes → saved to flash

Calibration survives power cycles. Repeat only if pitch tracking drifts.

---

## MIDI Implementation

Alloy Flux responds to USB MIDI and TRS MIDI simultaneously. Connect via USB to a computer or DAW — it appears as a standard USB MIDI device (no driver required). TRS MIDI supports both Type A and Type B automatically.

**Default channel:** omni (responds to all channels). To configure a specific channel, use the Web Configurator or a serial terminal.

---

### Note Messages

| Message            | Action                                                                    |
| ------------------ | ------------------------------------------------------------------------- |
| Note On            | Set pitch + trigger envelope                                              |
| Note Off           | Release envelope                                                          |
| Pitch Bend         | ±2 semitones                                                              |
| Program Change 1–6 | Switch voice mode (1=PAIR, 2=CLOUD, 3=CHORD, 4=CASCADE, 5=STRING, 6=POLY) |

The velocity on Note On messages is used to set the output volume of that note, from 0 (off) to 1 (full volume). This can be disabled so all notes play at the global volume level regardless of how hard they are struck. Use the **Web Configurator** (Envelope → Velocity Response), the serial command `veloc off`, or **CC 102 < 64** to disable.

---

### MIDI CC Map

Map your MIDI controller to any of these parameters for expressive real-time control.

#### Core Parameters

| CC    | Parameter | Range | Description                                                             |
| ----- | --------- | ----- | ----------------------------------------------------------------------- |
| CC 1  | Motion    | 0–127 | Drift + chorus depth (mod wheel)                                        |
| CC 7  | Volume    | 0–127 | Master output level                                                     |
| CC 8  | Space     | 0–127 | Stereo width (0–2× — 0=mono, 1=normal, 2=hyper wide)                    |
| CC 78 | Shape     | 0–127 | Waveform morph (sine → hollow pulse)                                    |
| CC 84 | Fatness   | 0–127 | Sub oscillator level (0–1)                                              |
| CC 92 | Color     | 0–127 | Tonal color: FM depth in PAIR/CASCADE; fine Hz spread in ensemble modes |
| CC 94 | Relation  | 0–127 | RELATION semitones above ROOT (0–24 st)                                 |

#### Envelope & Articulation

| CC     | Parameter            | Range                 | Description                                                                                                                                                                                 |
| ------ | -------------------- | --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CC 5   | Glide Time           | 0–127                 | Portamento slide time (0–2 s)                                                                                                                                                               |
| CC 64  | Sustain / Gate       | ≥64=on                | Hold voices sustained (drone toggle)                                                                                                                                                        |
| CC 65  | Glide On/Off         | ≥64=on / <64=off      | Enable or disable portamento glide                                                                                                                                                          |
| CC 71  | Curve                | 0–127                 | Envelope shape (pluck → swell)                                                                                                                                                              |
| CC 72  | ADSR Release         | 0–127                 | ADSR release time (0.001–4 s)                                                                                                                                                               |
| CC 73  | ADSR Attack          | 0–127                 | ADSR attack time (0.001–4 s)                                                                                                                                                                |
| CC 81  | Envelope Type        | 0–63=AR / 64–127=ADSR | Switch between AR and ADSR envelope                                                                                                                                                         |
| CC 82  | ADSR Decay           | 0–127                 | ADSR decay time (0.001–4 s)                                                                                                                                                                 |
| CC 83  | ADSR Sustain         | 0–127                 | ADSR sustain level (0–1)                                                                                                                                                                    |
| CC 88  | Curve Time           | 0–127                 | Envelope time scale (0.25× – 4×)                                                                                                                                                            |
| CC 89  | Drift Speed          | 0–127                 | Drift glide rate                                                                                                                                                                            |
| CC 102 | Velocity Sensitivity | ≥64=on / <64=off      | On = velocity scales volume (default), Off = fixed                                                                                                                                          |
| CC 103 | Scale Quantizer      | 0–14                  | 0=Off/chromatic, 1=Major, 2=Minor, 3=Harm. Minor, 4=Mel. Minor, 5=Penta Maj, 6=Penta Min, 7=Blues, 8=Dorian, 9=Phrygian, 10=Lydian, 11=Mixolydian, 12=Locrian, 13=Whole Tone, 14=Diminished |
| CC 104 | Transpose            | 0–48                  | Semitone offset: value−24 (0=−24 st, 24=0 st, 48=+24 st)                                                                                                                                    |
| CC 119 | Drone Return         | any                   | Clear gate arm, return to continuous drone                                                                                                                                                  |
| CC 123 | All Notes Off        | any                   | Panic — release all voices                                                                                                                                                                  |

#### Filter

| CC    | Parameter        | Range                                                      | Description                               |
| ----- | ---------------- | ---------------------------------------------------------- | ----------------------------------------- |
| CC 74 | Filter Cutoff    | 0–127                                                      | Cutoff frequency (20–16000 Hz, log scale) |
| CC 75 | Filter Resonance | 0–127                                                      | Resonance (0–1)                           |
| CC 76 | Filter Mode      | 0–25=OFF / 26–50=LP / 51–76=HP / 77–101=BP / 102–127=NOTCH | Select filter type                        |
| CC 77 | Filter Algorithm | 0–63=SVF / 64–127=Ladder                                   | Cytomic SVF or OTA 4-pole ladder          |
| CC 79 | Filter Position  | 0–63=pre-chorus / 64–127=post-chorus                       | Effect chain placement                    |

#### Chorus

| CC    | Parameter   | Range                                       | Description           |
| ----- | ----------- | ------------------------------------------- | --------------------- |
| CC 90 | Sub Octave  | 0–63=1 oct / 64–127=2 oct                   | Sub oscillator octave |
| CC 93 | Chorus Mode | 0–31=OFF / 32–63=I / 64–95=II / 96–127=I+II | Chorus character      |

#### Reverb

| CC     | Parameter        | Range            | Description                         |
| ------ | ---------------- | ---------------- | ----------------------------------- |
| CC 91  | Reverb Mix       | 0–127            | Reverb wet level (0–1)              |
| CC 112 | Reverb Mod Speed | 0–127            | Reverb LFO rate multiplier (0.1–4×) |
| CC 113 | Reverb Mod Depth | 0–127            | Reverb LFO depth (0–1)              |
| CC 114 | Reverb Freeze    | ≥64=on / <64=off | Freeze reverb tail indefinitely     |
| CC 117 | Reverb Size      | 0–127            | Plate size / decay time (0–1)       |
| CC 118 | Reverb Damping   | 0–127            | High frequency damping (0–1)        |

#### Delay

| CC    | Parameter      | Range                                | Description              |
| ----- | -------------- | ------------------------------------ | ------------------------ |
| CC 80 | Delay Position | 0–63=pre-reverb / 64–127=post-reverb | Effect chain placement   |
| CC 86 | Delay Time     | 0–127                                | Delay time (10–500 ms)   |
| CC 87 | Delay Feedback | 0–127                                | Feedback amount (0–0.95) |
| CC 95 | Delay Mix      | 0–127                                | Delay wet level (0–1)    |

#### Voice Mode & Configuration

| CC     | Parameter    | Range                                                                                | Description              |
| ------ | ------------ | ------------------------------------------------------------------------------------ | ------------------------ |
| CC 115 | Voice Mode   | 0–20=PAIR / 21–41=CLOUD / 42–62=CHORD / 63–83=CASCADE / 84–104=STRING / 105–127=POLY | Switch voice mode        |
| CC 110 | MIDI Channel | 0–127 → 0=omni, 1–16                                                                 | Set MIDI receive channel |

For MIDI SysEx implementation, check the MIDI & SysEx reference: [AlloyFlux-MIDI-reference.md](references/AlloyFlux-MIDI-reference.md).

---

_For serial console commands, firmware architecture, hardware details, and developer notes, see [AlloyFlux-module-reference.md](references/AlloyFlux-module-reference.md)._

---

Voltage Foundry Modular — Alloy Flux · 2026
