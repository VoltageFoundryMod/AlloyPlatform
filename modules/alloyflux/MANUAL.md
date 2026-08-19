# Alloy Flux — User Manual

## Dual Relation Oscillator · Stereo Voice · Eurorack

---

## What Is Alloy Flux?

Alloy Flux is a stereo oscillator that sounds rich and full with minimal patching. Plug in the outputs to your mixer and you immediately hear stereo movement, harmonic warmth, and gentle drift. Plug in a V/OCT cable and a gate and control the pitch and articulation — everything needed to play a complete, musical voice.

The module is built around one idea: **two voices in a relationship.** Rather than exposing two independent oscillators, Alloy Flux gives you a **ROOT** oscillator and a **RELATION** — the second voice is always defined relative to the first. That relationship changes meaning depending on which voice **Mode** is active, from simple interval tuning to full chorus ensemble, harmonic chord stacking, FM synthesis, and true six-voice polyphony.

Want to integrate the module to your DAW or MIDI controller? The built-in USB MIDI and TRS MIDI inputs mirror all parameters to MIDI CCs in real time, and support full patch dump/restore over SysEx. No drivers or native app required — connect with the Web Configurator for an intuitive visual editor, preset management, and serial console access.

---

## Specs at a Glance

|             |                                                                             |
| ----------- | --------------------------------------------------------------------------- |
| Format      | Eurorack                                                                    |
| Width       | 14HP                                                                        |
| Power       | ~100mA +12V, ~5mA −12V                                                      |
| Voice Modes | PAIR / CLOUD / CHORD / CASCADE / STRING / POLY                              |
| Knobs       | 9 — ROOT, COLOR, RELATION / SHAPE, CURVE, MOTION / DELAY, SPACE, REVERB     |
| Jacks       | 10 — V/OCT, GATE, MIDI, CV 1, CV 2 / FM IN, CV 3, CV 4, L OUT, R OUT        |
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

### Knobs

Nine knobs in three rows. The panel numbers them POT 1–9, left to right and top
to bottom:

```txt
  ROOT      COLOR     RELATION      ← pitch and harmonic relationship
  SHAPE     CURVE     MOTION        ← timbre, envelope, animation
  DELAY     SPACE     REVERB        ← space and effects
```

RELATION is physically the largest knob — it is the module's signature control.

### Jacks

Ten jacks in two rows of five:

```txt
  V/OCT   GATE   MIDI    CV 1    CV 2
  FM IN   CV 3   CV 4    L OUT   R OUT
```

The four modulation inputs are labelled **CV 1–CV 4** on the panel rather than
being named after their destination. What each one modulates is fixed:

| Label | Type   | Function                                        |
| ----- | ------ | ----------------------------------------------- |
| V/OCT | Input  | Pitch — 1V/oct                                  |
| GATE  | Input  | Note trigger / envelope                         |
| MIDI  | Input  | TRS MIDI (Type A/B accepted)                    |
| CV 1  | Input  | Modulates RELATION                              |
| CV 2  | Input  | Modulates SHAPE                                 |
| FM IN | Input  | FM modulation — audio rate capable              |
| CV 3  | Input  | Modulates MOTION                                |
| CV 4  | Input  | Modulates SPACE                                 |
| L OUT | Output | Left / mono (passive mono sum when R unplugged) |
| R OUT | Output | Right stereo                                    |

### Buttons

| Button       | Action               | Result                                                                  |
| ------------ | -------------------- | ----------------------------------------------------------------------- |
| MODE         | Tap                  | Cycle voice mode: PAIR → CLOUD → CHORD → CASCADE → STRING → POLY → PAIR |
| SHIFT        | Hold + knob          | Access secondary function for that knob                                 |
| SHIFT + MODE | Hold SHIFT, tap MODE | Toggle drone mode (sustained without gate)                              |
| MODE         | Hold during power-on | Enter V/Oct two-point calibration                                       |

---

## Voice Modes

Tap **MODE** to cycle through six voice characters. The MODE LED — lower left, beside the MODE button — shows the current mode colour.

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
- CV 1 (RELATION) from an LFO slowly pulses the ensemble width for breathing pads

---

### CHORD — Harmonic Stack

**Sound:** four-voice harmonic content from a single note. RELATION sweeps through 11 chord shapes — from unison to full octave stacks.

| Control  | Effect in CHORD                                                                |
| -------- | ------------------------------------------------------------------------------ |
| RELATION | Selects and morphs between chord shapes (see table below)                      |
| COLOR    | Fine Hz detune spread across all chord voices — adds ensemble beating |
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

- CV 1 (RELATION) from a sample-and-hold creates instant random chord changes
- Morphing RELATION slowly during a long swell envelope = chord evolution
- The voice LEDs show amber in CHORD mode

---

### CASCADE — FM Synthesis

**Sound:** bell-like to metallic-warm, harmonically complex, and wide. RELATION selects the FM ratio between the modulator and carrier oscillators. COLOR controls the FM depth independently.

FM here is intentionally restrained: depth is soft-clipped, ratios are musical, and the result stays harmonic across the full RELATION sweep.

CASCADE runs **two** FM voices, not one — a second carrier and modulator pair, detuned a hair against the first and placed on the opposite side of the stereo field. The two drift apart slowly, which is what gives the mode its width and a gentle chorus of its own even with MOTION at zero.

| Control  | Effect in CASCADE                                                                     |
| -------- | ------------------------------------------------------------------------------------- |
| RELATION | Sweeps FM ratio zones (1:1 → 4:3 → 3:2 → 2:1 → 5:2 → 3:1)                             |
| COLOR    | FM depth — 0 = dry carrier, full CW = rich harmonics (soft-clipped)                   |
| MOTION   | Drifts carrier and modulator independently — the harmonic relationship itself wanders |
| SPACE    | Width of the two FM voices — CCW collapses to mono, CW pushes them apart              |

**Tips:**

- Keep MOTION above 20% to avoid a static FM character — the gentle drift keeps it musical
- Low COLOR values = subtle harmonic warming; high values = pronounced bell or metal tones
- Sweeping RELATION slowly while playing = evolving FM timbre in real time
- The voice LEDs show magenta in CASCADE mode

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
- The voice LEDs show purple in STRING mode

---

### POLY — True Polyphony

**Sound:** six-voice polyphonic — each MIDI note is independent, with its own envelope, drift and velocity.

Notes are allocated round-robin across six slots; a seventh note steals the oldest. Each slot sits at its own fixed position in the stereo field, running left to right across the six, so a held chord opens out across the image instead of stacking in the centre. Note velocity sets each voice's level.

Every slot is equally loud wherever it sits, so the same note played twice does not jump in volume as round-robin moves it along the field.

**POLY plays from CV as well as MIDI.** Every gate on the **GATE** jack claims the next voice and plays whatever pitch is on **V/OCT** at that moment, so a run of gates stacks up voices that ring together rather than one voice retriggering. Feed it a sequencer and the chord builds itself.

When the gate falls the voice is released but keeps sounding through its tail, and its slot is only reclaimed once it has faded — so a new note takes a genuinely free voice first, an already-fading one next, and only steals a voice that is still held when all six are busy. MIDI notes draw from the same six voices, so CV and MIDI can be played together.

| Control  | Effect in POLY                                                        |
| -------- | --------------------------------------------------------------------- |
| RELATION | Detune spread across the voice slots — 0 = dead in tune, full CW = ±15¢ |
| COLOR    | Fine Hz detune spread per voice slot — up to ±25 Hz at full CW         |
| MOTION   | Per-voice drift — each voice drifts independently                     |
| CURVE    | Envelope shape applies per-voice — each note has its own AR           |
| SPACE    | Width of the voice spread — CCW collapses to mono, CW throws it wider  |
| SHAPE    | Waveform morph, applied to every voice                                |

RELATION spreads the slots flat-to-sharp in the same order the stereo positions
run left-to-right, so the detune reads as width rather than as mistuning. It
works in cents rather than Hz, so a chord stays equally detuned whether you play
it low or high on the keyboard — where COLOR, being a fixed Hz offset, beats at
the same rate everywhere and gets proportionally stronger as you play lower.
Together they are the same pairing CLOUD and STRING use.

**Tips:**

- Pair with a MIDI keyboard for immediate polyphonic play
- Sequence the GATE and V/OCT jacks and let CURVE's tail do the work — an arpeggio becomes a chord
- Playing from short triggers rather than gates? Set **Gate Length** (CC 85) and each one holds for a fixed time, so the chord builds even at slow CURVE settings
- Short gates with a long CURVE tail stack the most voices; long gates hold fewer
- CURVE swell + slow MOTION = lush evolving pads from held chords
- RELATION and COLOR are the ensemble controls here — a little of either goes a long way across six voices
- RELATION at zero is exactly in tune; a third of the way up is vintage-polysynth looseness
- Play a wide chord and turn SPACE up: the voices fan out across the field
- The MODE LED shows lime/yellow-green in POLY; the left voice LED brightness tracks the active voice count

---

## Controls Reference

### ROOT

Primary pitch. Sets the pitch centre for the entire module. Apply V/OCT for melodic tracking.
_Associated CV: V/OCT jack_

---

### RELATION _(signature control — largest knob)_

The defining control of Alloy Flux. Its behaviour changes per mode — always governs the relationship between ROOT and everything else. Controls interval, ensemble spread, chord shape, FM ratio, or polyphonic detune depending on the active mode.

_Associated CV: CV 1 jack_
When CV 1 is patched, RELATION knob becomes an **attenuverter** for that CV (centre = no effect, CW = full depth, CCW = inverted depth).

---

### SHAPE

Continuous waveform morph across five timbres:

```txt
Fully CCW ────────────────────────────────────── Fully CW
  Sine    Triangle     Saw     Pulse    Hollow Pulse
```

_Associated CV: CV 2 jack_
When CV 2 is patched, SHAPE becomes an attenuverter for that CV.

**SHIFT function:** Hold SHIFT + turn SHAPE → adjusts **FATNESS** (sub oscillator level — adds a square wave one or two octaves (set by configurator) below each voice for Juno-style body).

---

### MOTION

Controls all internal animation depth simultaneously — drift, chorus modulation, stereo movement, voice timing offsets, and phase instability.

- At zero: module is stable and static
- At full: module breathes and moves — string machine territory

_Associated CV: CV 3 jack_
When CV 3 is patched, MOTION becomes an attenuverter for that CV.

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

SPACE has its strongest effect in STRING mode, but every voice mode gives it something to work on — each one places its voices across the field, so SPACE can collapse any of them to mono or throw them wider.

_Associated CV: CV 4 jack_
When CV 4 is patched, SPACE becomes an attenuverter for that CV.

**SHIFT function:** Hold SHIFT + turn SPACE → adjusts **VOL** (master output level).

---

### Glide (Portamento)

Glide causes pitch changes to slide smoothly from the previous note to the new one rather than jumping instantly — a classic lead synth and bass effect.

**Enable/disable:** Use CC 65 (≥64 = on, <64 = off) or the **Web Configurator** (Animation → Glide toggle).

**Glide time:** Use CC 5 to set the slide duration from 0 (instant, effectively off) to 2 seconds. The glide uses a one-pole exponential smoother so short slides are snappy and long slides trail off naturally.

Glide applies to all monophonic voice modes (PAIR, CLOUD, CHORD, CASCADE, STRING). In POLY mode each voice has its own independent pitch and glide has no effect.

---

### Gate Length

Normally a note lasts as long as the **GATE** input is high — the gate rises, the
note starts; the gate falls, the note releases. **Gate Length** changes that: set
it above zero and the gate is treated as a *trigger*, with every note lasting the
same fixed time no matter how narrow or wide the gate is.

Its reason for existing is POLY. With CURVE below about 0.2 the envelope already
ignores the gate — the note is a fixed-length blip — so a run of triggers already
piles up into a chord. Turn CURVE up and each voice is held for as long as its
gate is high instead, and a short trigger gives you almost nothing. Gate Length
restores the stacking at *any* CURVE setting: feed the module a trigger sequence
and a slow swell, and an arpeggio assembles itself into a sustained chord.

**Range:** 0 = follow the gate (the classic behaviour, and the default), up to
2000 ms. Set it with **CC 85**, the **Web Configurator** (Envelope → Gate
Length), or the serial console. There is no panel control — all nine knobs and
all six SHIFT-secondaries are already assigned.

In the monophonic modes there is only one voice to hold, so the setting has
nothing to stack and the gate behaves as it always did.

---

### Scale Quantizer & Transpose

The scale quantizer snaps incoming MIDI notes to a chosen musical scale before they are played. Notes that fall outside the scale are shifted to the nearest in-scale semitone (ties go up).

**Scale select:** CC 103 (0 = Off / chromatic, 1–14 = scale index). Available scales: Major, Minor (Natural), Harmonic Minor, Melodic Minor, Pentatonic Major, Pentatonic Minor, Blues, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Whole Tone, Diminished. Select with the **Web Configurator** (Voice → Scale) or send CC 103 directly.

**Transpose:** CC 104 shifts all incoming MIDI notes by −24 to +24 semitones. The CC value encodes the offset as `value − 24` (CC 24 = −0 st, CC 0 = −24 st, CC 48 = +24 st). Transpose is applied before scale quantization, so the root stays consistent when you move both together.

Quantization applies to all MIDI NoteOn events (monophonic and POLY modes). The `pitch` serial command for calibration bypasses quantization.

---

## Shift Functions Summary

Hold **SHIFT** and turn a knob to access its secondary parameter. The SHIFT LED — lower right, beside the SHIFT button — lights white while SHIFT is held.

| Knob   | Primary                        | SHIFT + Knob                        |
| ------ | ------------------------------ | ----------------------------------- |
| SHAPE  | Waveform morph (sine → hollow) | **FATNESS** — sub oscillator level  |
| MOTION | Drift + chorus depth           | **DRIFTSPEED** — drift glide rate   |
| CURVE  | Envelope shape (pluck → swell) | **CURVETIME** — envelope time scale |
| SPACE  | Stereo width                   | **VOL** — master output volume      |
| DELAY  | Delay wet mix                  | **DELAYTIME** — delay time (ms)     |
| REVERB | Reverb wet mix                 | **REVERBSIZE** — virtual plate size |

ROOT, RELATION, and COLOR have no shift function — full knob travel is needed for precision.

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

Seven RGB LEDs, in four tiers:

```txt
 VOICE L  ·  ·  ·  VOICE R     ← top:       voice activity (left / right)
MOD L  ·  ·  ·  ·  ·  MOD R    ← upper mid: motion and modulation depth
     MODE   ·   SHIFT          ← lower mid: mode colour / shift and drone
         ·  CENTRE  ·          ← bottom:    heartbeat and confirmations
```

MODE sits beside the MODE button and SHIFT beside the SHIFT button, so the LED
that tells you about a button is always the one next to it.

### MODE — Mode Indicator

Always shows the current voice mode as a steady colour.

| Mode    | Colour              |
| ------- | ------------------- |
| PAIR    | Soft white          |
| CLOUD   | Cyan                |
| CHORD   | Amber               |
| CASCADE | Magenta             |
| STRING  | Purple              |
| POLY    | Lime / yellow-green |

When changing mode: the CENTRE LED ignites white, the flash ripples outward
through the others, then MODE settles to the new colour. About 300 ms.

### VOICE L + VOICE R — Voice Activity

The top pair. They breathe with the audio envelope in every mode — bright on
attack, fading on release — so they stay readable without knowing which mode is
active.

| Mode    | VOICE L (left)                | VOICE R (right)                    |
| ------- | ----------------------------- | ---------------------------------- |
| PAIR    | Warm red — envelope level     | Cool blue — relation depth         |
| CLOUD   | Cyan — left voice position    | Cyan — stereo spread               |
| CHORD   | Amber — root envelope         | Amber dimmer — interval spread     |
| CASCADE | Magenta — carrier activity    | Magenta brighter — modulator depth |
| STRING  | Purple — slow drift           | Purple — offset phase              |
| POLY    | Warm red — active voice count | Cool blue — detune spread          |

### MOD L + MOD R — Motion Layer

The upper-middle pair. They take the colour of the current mode and show how far
the sound is being animated: MOD L follows MOTION depth, MOD R follows the mode's
secondary spread — detune in PAIR, stereo width in CLOUD and STRING, FM depth in
CASCADE, chord spread in CHORD.

With MOTION at zero and nothing modulating, both sit dark.

### SHIFT — Shift / Drone State

Dark in normal operation. Lights when state changes:

| State              | Colour     | Pattern           |
| ------------------ | ---------- | ----------------- |
| Normal             | Off        | Dark              |
| SHIFT held         | White      | Steady while held |
| Drone mode active  | Warm white | Slow breathe      |
| Drone + SHIFT held | White      | Brighter steady   |
| Calibration active | White      | Slow pulse        |

### CENTRE — Heartbeat

The bottom LED, and the module's pulse. Even if you learn no other LED, this one
tells you whether the module is doing something.

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

**Enter drone:** Hold SHIFT then tap MODE. The SHIFT LED fades up to a warm white slow breathe.

**Exit drone:** First incoming gate automatically returns to gated mode, or hold SHIFT and tap MODE again to toggle back.

In drone mode, V/OCT and MIDI notes change pitch without retriggering the envelope.

---

## V/Oct Calibration

Alloy Flux uses a two-point software calibration for accurate pitch tracking.

**To calibrate:**

1. Hold **MODE** while powering the module on
2. The CENTRE LED begins a slow white pulse — calibration mode is active
3. Patch a **1V** reference into V/OCT, tap MODE to confirm
4. Patch a **3V** reference into V/OCT, tap MODE to confirm
5. All LEDs ripple white, then CENTRE triple-flashes → saved to flash

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

> Ranges below are authoritative in [`params.json`](params.json), which generates both the firmware's CC table and the Web Configurator's parameter map. If a value here ever disagrees with the module, the JSON is right.

#### Core Parameters

| CC    | Parameter | Range | Description                                                             |
| ----- | --------- | ----- | ----------------------------------------------------------------------- |
| CC 16 | Root      | 0–127 | Root pitch, ±48 semitones around A4 (27.5 Hz – 7040 Hz, log)            |
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
| CC 64  | Sustain              | ≥64=on / <64=off      | Standard sustain pedal — arms the gate and holds it high. Not drone: use CC 119 to release the gate                                                                                          |
| CC 65  | Glide On/Off         | ≥64=on / <64=off      | Enable or disable portamento glide                                                                                                                                                          |
| CC 71  | Curve                | 0–127                 | Envelope shape (pluck → swell)                                                                                                                                                              |
| CC 72  | ADSR Release         | 0–127                 | ADSR release time (0.001–8 s)                                                                                                                                                               |
| CC 73  | ADSR Attack          | 0–127                 | ADSR attack time (0.001–4 s)                                                                                                                                                                |
| CC 81  | Envelope Type        | 0–63=AR / 64–127=ADSR | Switch between AR and ADSR envelope                                                                                                                                                         |
| CC 82  | ADSR Decay           | 0–127                 | ADSR decay time (0.001–4 s)                                                                                                                                                                 |
| CC 83  | ADSR Sustain         | 0–127                 | ADSR sustain level (0–1)                                                                                                                                                                    |
| CC 85  | Gate Length          | 0–127                 | GATE note length, 0–2000 ms; 0 = follow the gate                                                                                                                                             |
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

For MIDI SysEx implementation, check the MIDI & SysEx reference: [AlloyFlux-MIDI-reference.md](../../references/AlloyFlux-MIDI-reference.md).

---

_For serial console commands, firmware architecture, hardware details, and developer notes, see [AlloyFlux-module-reference.md](../../references/AlloyFlux-module-reference.md)._

---

Voltage Foundry Modular — Alloy Flux · 2026
