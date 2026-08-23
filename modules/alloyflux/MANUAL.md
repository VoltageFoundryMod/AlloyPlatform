# Alloy Flux — User Manual

## Dual Relation Oscillator · Stereo Voice · Eurorack

---

## What Is Alloy Flux?

Alloy Flux is a stereo oscillator that sounds rich and full with minimal patching. Plug in the outputs to your mixer and you immediately hear stereo movement, harmonic warmth, and gentle drift. Plug in a V/OCT cable and a gate and control the pitch and articulation — everything needed to play a complete, musical voice.

The module is built around one idea: **two voices in a relationship.** Rather than exposing two independent oscillators, Alloy Flux gives you a **ROOT** oscillator and a **RELATION** — the second voice is always defined relative to the first. That relationship changes meaning depending on which voice **Mode** is active, from simple interval tuning to full chorus ensemble, harmonic chord stacking, FM synthesis, and true six-voice polyphony.

Want to integrate the module to your DAW or MIDI controller? The built-in USB MIDI and TRS MIDI inputs mirror all parameters to MIDI CCs in real time, and support full patch dump/restore over SysEx. No drivers or native app required — connect with the Alloy Controller for an intuitive visual editor, preset management, and serial console access.

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

### CLOUD — Supersaw

**Sound:** the big one. A seven-oscillator stack in the JP-8000 tradition — huge, bright, and wide, from a barely-thickened single voice to a wall.

Seven oscillators tuned around the root, spread by an uneven detune pattern and balanced centre-against-sides. Two knobs run it, and they do genuinely different things: **RELATION detunes, COLOR balances.** Neither is a substitute for the other.

RELATION's response is deliberately non-linear. The first third of its travel barely moves — that is where the subtle chorusing lives and it needs the resolution — then it opens out steeply to a spread of about 1.8 semitones at full CW.

COLOR sets how loud the six outer oscillators are against the centre one. Fully CCW you hear essentially one clean voice; fully CW the outer six dominate and the stack is at its widest and most restless. It changes the character without moving a single frequency, so you can set the detune you want and then decide how much of it you want to hear.

Every note attack **randomises the seven phases**, which is why no two stabs sound quite alike — a large part of what makes this sound recognisable.

| Control  | Effect in CLOUD                                                                       |
| -------- | ------------------------------------------------------------------------------------- |
| RELATION | **DETUNE** — spread of the seven oscillators, 0 = unison to ±1.8 semitones at full CW |
| COLOR    | **MIX** — level of the six outer oscillators against the centre one                   |
| SHAPE    | Applies to all seven. Saw is the classic, but the whole morph works — see below       |
| MOTION   | Slow drift on top. Deliberately restrained here; the detune supplies the width        |
| SPACE    | Width of the stereo image — CCW collapses to mono, CW throws the stack wider          |
| FATNESS  | One sub oscillator on the centre voice                                                |

**SHAPE is not locked to saw.** The detune pattern and the mix balance are about how the stack is _tuned_, not what it is made of, so the whole SHAPE morph works: a super-sine is a gorgeous shimmering pad, a super-pulse is enormous and hollow, and sweeping SHAPE across the stack while it plays is a sound the original never made.

A high-pass filter tracks the played note, which is what keeps seven detuned oscillators from turning to mud in the bass.

**Tips:**

- The classic: SHAPE at saw, RELATION about a third up, COLOR high, into the filter
- COLOR is the expressive one — try it on CV or an envelope, it opens the stack up without any pitch movement
- Keep MOTION low. The detune is already doing that job, and drift on top smears the beating that makes a supersaw legible
- For pads, slow CURVE swell + reverb; for stabs, fast CURVE and let the phase randomisation give each hit its own character
- Want movement instead of width? That is STRING's job now

---

### CHORD — Harmonic Stack

**Sound:** four-voice harmonic content from a single note. RELATION sweeps through 11 chord shapes — from unison to full octave stacks.

| Control  | Effect in CHORD                                                     |
| -------- | ------------------------------------------------------------------- |
| RELATION | Selects and morphs between chord shapes (see table below)           |
| COLOR    | **VOICING** — how the chord is spaced: close, open, spread, or wide |
| MOTION   | Drift animates each voice of the chord independently                |
| SPACE    | Distributes chord voices across the stereo field                    |

**COLOR sets the voicing.** RELATION says which chord; COLOR says how it is
spaced, lifting the upper voices away from the root in octaves. Four positions
across the knob:

| COLOR   | Voicing | What moves                          |
| ------- | ------- | ----------------------------------- |
| 0–15%   | Close   | as written in the table above       |
| 15–50%  | Open    | top two voices up an octave         |
| 50–85%  | Spread  | middle voices up, top up two        |
| 85–100% | Wide    | the chord opens across the register |

It steps rather than sweeps, and moves in octaves rather than semitones, so the
chord is in tune at every knob position — a continuous sweep would glide the
upper voices through every microtone on the way and spend most of its travel
out of tune.

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
| COLOR    | **TIMBRE SPREAD** — fans the four voices apart across the SHAPE morph      |
| MOTION   | Deepens chorus movement; a baseline chorus floor remains active regardless |
| SPACE    | Width of the ensemble image — very wide at full CW                         |

**COLOR spreads timbre, not pitch.** Each of the four voices sits at a slightly
different point on the SHAPE morph — up to a quarter of the way apart at full
CW — so the section thickens without getting any wider. Set SHAPE to saw and
turn COLOR up and the four voices run from triangle to pulse, which is much
closer to how a real string section fails to agree with itself than four copies
of one waveform at slightly different pitches ever was.

It is genuinely a third axis: RELATION owns pitch width, MOTION owns movement,
COLOR owns timbre.

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

| Control  | Effect in POLY                                                          |
| -------- | ----------------------------------------------------------------------- |
| RELATION | Detune spread across the voice slots — 0 = dead in tune, full CW = ±15¢ |
| COLOR    | **TIMBRE SPREAD** — fans the slots apart across the SHAPE morph         |
| MOTION   | Per-voice drift, scaled by how many voices are held — see below         |
| CURVE    | Envelope shape applies per-voice — each note has its own AR             |
| SPACE    | Width of the voice spread — CCW collapses to mono, CW throws it wider   |
| SHAPE    | Waveform morph — the centre of the spread COLOR fans out from           |

RELATION spreads the slots flat-to-sharp in the same order the stereo positions
run left-to-right, so the detune reads as width rather than as mistuning. It
works in cents rather than Hz, so a chord stays equally detuned whether you play
it low or high on the keyboard.

**COLOR spreads timbre**, exactly as in STRING: each slot sits at a slightly
different point on the SHAPE morph, so a held chord has six voices that differ
in character rather than six copies of one waveform. This is the right axis for
it in POLY specifically — the slots hold different notes, so the fixed Hz offset
COLOR used to apply was a shimmer up top and a sour interval down low.

**MOTION scales with the chord.** Drift on a single held note is just tuning
instability; it only reads as life when there is something for it to beat
against. So a lone note stays steady enough to play a line on, and the drift
opens up as more voices are held, reaching full depth on a six-note chord.

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

FATNESS is **level-matched across the modes.** They run different numbers of
voices — two in PAIR, four in the ensemble modes, six in POLY, and CLOUD's
supersaw takes a single sub on its centre voice — so the same knob position
would otherwise mean two sub oscillators in one mode and six in another, and
the modes with the most voices would go muddy first. Each mode scales its sub
so the knob means the same amount of weight wherever you are.

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

The second mode control, and RELATION's counterpart: where RELATION sets how the
voices are **tuned**, COLOR sets what they are **made of**. What that means
changes per mode, but it is never a second detune — that is RELATION's job, and
one spread control is enough.

| Mode         | COLOR does                                                                           |
| ------------ | ------------------------------------------------------------------------------------ |
| PAIR         | **FM depth** — the RELATION voice phase-modulates ROOT. Warmth to bell to metal      |
| CLOUD        | **MIX** — the six outer supersaw oscillators against the centre one                  |
| CHORD        | **VOICING** — close, open, spread or wide. RELATION picks the chord, COLOR spaces it |
| CASCADE      | **FM depth** — as PAIR, but the modulator is never heard directly                    |
| STRING, POLY | **TIMBRE SPREAD** — fans the voices apart across the SHAPE morph                     |

At COLOR = 0 every mode sits at its neutral position: no FM, centre voice only,
close voicing, every voice on the same waveform.

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

**Enable/disable:** Use CC 65 (≥64 = on, <64 = off) or the **Alloy Controller** (Animation → Glide toggle).

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
2000 ms. Set it with **CC 85**, the **Alloy Controller** (Envelope → Gate
Length), or the serial console. There is no panel control — all nine knobs and
all six SHIFT-secondaries are already assigned.

In the monophonic modes there is only one voice to hold, so the setting has
nothing to stack and the gate behaves as it always did.

---

### Scale Quantizer & Transpose

The scale quantizer snaps incoming MIDI notes to a chosen musical scale before they are played. Notes that fall outside the scale are shifted to the nearest in-scale semitone (ties go up).

**Scale select:** CC 103 (0 = Off / chromatic, 1–14 = scale index). Available scales: Major, Minor (Natural), Harmonic Minor, Melodic Minor, Pentatonic Major, Pentatonic Minor, Blues, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Whole Tone, Diminished. Select with the **Alloy Controller** (Voice → Scale) or send CC 103 directly.

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
| REVERB | Reverb wet send                | **REVERBSIZE** — virtual plate size |

ROOT, RELATION, and COLOR have no shift function — full knob travel is needed for precision.

---

## Knob Takeover

Alloy Flux can be driven from the panel and from the Alloy Controller (or a DAW, or a MIDI controller) at the same time. Whatever changes a parameter, the panel LEDs and the Alloy Controller both follow it — the module reports its own state, so a knob you turn shows up on screen, and a slider you move on screen takes effect immediately.

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

When changing mode the panel plays two things in a row. First the CENTRE LED
ignites white and the flash ripples outward through the others — that says
_something changed_. Then the panel **counts the mode out**: the LEDs light one
after another around the panel, and the number left burning is the mode's
position in the cycle.

```txt
      LED1  ·  ·  ·  LED7          the count runs LED1 → LED2 → LED3 → …
     LED2  ·  ·  ·  ·  LED6        anticlockwise from the upper left,
        LED3  ·   LED5             the same order the panel numbers them
            ·  LED4  ·
```

| Mode    | LEDs lit | Colour              |
| ------- | -------- | ------------------- |
| PAIR    | 1        | Soft white          |
| CLOUD   | 2        | Cyan                |
| CHORD   | 3        | Amber               |
| CASCADE | 4        | Magenta             |
| STRING  | 5        | Purple              |
| POLY    | 6        | Lime / yellow-green |

Each LED stays lit once it fires, so by the end of the sweep you can simply
count them; the newest one carries a white leading edge so the advance is easy
to follow, and any LED past the count is held dark so there is nothing to
misread. The whole thing takes about 0.8 s in PAIR and 1.2 s in POLY, then the
panel fades back to its normal display with MODE settled on the new colour.

You get the reading two ways, which is the point: colour if you already know it,
a count if you don't.

### VOICE L + VOICE R — Voice Activity

The top pair. They breathe with the audio envelope in every mode — bright on
attack, fading on release — so they stay readable without knowing which mode is
active.

| Mode    | VOICE L (left)                | VOICE R (right)                    |
| ------- | ----------------------------- | ---------------------------------- |
| PAIR    | Warm red — envelope level     | Cool blue — relation depth         |
| CLOUD   | Cyan — centre voice           | Cyan/blue — detune spread          |
| CHORD   | Amber — root envelope         | Amber dimmer — interval spread     |
| CASCADE | Magenta — carrier activity    | Magenta brighter — modulator depth |
| STRING  | Purple — slow drift           | Purple — offset phase              |
| POLY    | Warm red — active voice count | Cool blue — detune spread          |

### MOD L + MOD R — Motion Layer

The upper-middle pair. They take the colour of the current mode and show how far
the sound is being animated: **MOD L follows MOTION, MOD R follows COLOR.**

That pairs with the row above it — VOICE R shows what RELATION is doing, MOD R
shows what COLOR is doing — so the two knobs that define a mode each have a
light. What COLOR means still changes per mode (FM depth in PAIR and CASCADE,
MIX in CLOUD, voicing in CHORD, timbre spread in STRING and POLY), but the LED
reads the same way everywhere: brighter means more of it.

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

**Default channel:** omni (responds to all channels). To configure a specific channel, use the Alloy Controller or a serial terminal.

---

### Note Messages

| Message            | Action                                                                    |
| ------------------ | ------------------------------------------------------------------------- |
| Note On            | Set pitch + trigger envelope                                              |
| Note Off           | Release envelope                                                          |
| Pitch Bend         | ±2 semitones                                                              |
| Program Change 1–6 | Switch voice mode (1=PAIR, 2=CLOUD, 3=CHORD, 4=CASCADE, 5=STRING, 6=POLY) |

The velocity on Note On messages is used to set the output volume of that note, from 0 (off) to 1 (full volume). This can be disabled so all notes play at the global volume level regardless of how hard they are struck. Use the **Alloy Controller** (Envelope → Velocity Response), the serial command `veloc off`, or **CC 102 < 64** to disable.

---

### MIDI CC Map

Map your MIDI controller to any of these parameters for expressive real-time control.

> This table is **generated** from [`params.json`](params.json) by `make params` — the same file that generates the firmware's CC table and the Alloy Controller's parameter map. It cannot disagree with the module. To change a range or a description, edit the JSON and regenerate.
>
> **Value range** is the parameter in its own units; **CC range** is what a controller sends.

<!-- BEGIN GENERATED: cc-map — `make params`, do not edit by hand -->

<!-- generated from modules/alloyflux/params.json -->

#### Voice

| CC     | Parameter          | Value range  | CC range                                                                                                                                                                                                                                                          | Description                                                                 |
| ------ | ------------------ | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| CC 103 | Input Quantization | —            | 0 = Chromatic (off) · 1 = Major · 2 = Natural Minor · 3 = Harmonic Minor · 4 = Melodic Minor · 5 = Pentatonic Maj · 6 = Pentatonic Min · 7 = Blues · 8 = Dorian · 9 = Phrygian · 10 = Lydian · 11 = Mixolydian · 12 = Locrian · 13 = Whole Tone · 14 = Diminished | Quantize incoming pitch to a scale                                          |
| CC 104 | Transpose          | −24 … +24 st | 0–48                                                                                                                                                                                                                                                              | Semitone offset applied after quantization; the CC value is the offset + 24 |
| CC 115 | Voice Mode         | —            | 0–20 = Pair · 21–41 = Cloud · 42–62 = Chord · 63–83 = Cascade · 84–104 = String · 105–127 = Poly                                                                                                                                                                  | Switch voice mode                                                           |

#### Oscillator

| CC    | Parameter  | Value range  | CC range                        | Description                                                                   |
| ----- | ---------- | ------------ | ------------------------------- | ----------------------------------------------------------------------------- |
| CC 16 | Root       | −48 … +48 st | 0–127                           | Root pitch relative to A4; stored internally as 27.5–7040 Hz                  |
| CC 78 | Shape      | 0–1          | 0–127                           | Waveform morph, sine → hollow pulse                                           |
| CC 84 | Fatness    | 0–1          | 0–127                           | Sub oscillator level                                                          |
| CC 90 | Sub Octave | —            | 0–63 = −1 Oct · 64–127 = −2 Oct | Sub oscillator octave below root                                              |
| CC 92 | Color      | 0–1          | 0–127                           | Tonal colour — FM depth in PAIR/CASCADE, fine Hz spread in the ensemble modes |
| CC 94 | Relation   | 0–24 st      | 0–127                           | Interval of the RELATION voice above ROOT                                     |

#### Animation

| CC    | Parameter   | Value range | CC range                 | Description                                 |
| ----- | ----------- | ----------- | ------------------------ | ------------------------------------------- |
| CC 1  | Motion      | 0–1         | 0–127                    | Drift and chorus depth together (mod wheel) |
| CC 5  | Glide Time  | 0–2 s       | 0–127                    | Portamento slide time                       |
| CC 65 | Glide       | —           | 0–63 = Off · 64–127 = On | Enable portamento between notes             |
| CC 89 | Drift Speed | 0.001–0.1   | 0–127                    | Rate at which drift glides between targets  |

#### Envelope

| CC     | Parameter              | Value range | CC range                                   | Description                                          |
| ------ | ---------------------- | ----------- | ------------------------------------------ | ---------------------------------------------------- |
| CC 71  | Curve                  | 0–1         | 0–127                                      | Envelope shape, pluck → swell (AR mode)              |
| CC 72  | Release                | 0.001–8 s   | 0–127                                      | ADSR release time                                    |
| CC 73  | Attack                 | 0.001–4 s   | 0–127                                      | ADSR attack time                                     |
| CC 81  | Type                   | —           | 0–63 = AR · 64–127 = ADSR                  | Envelope generator — two-stage AR or four-stage ADSR |
| CC 82  | Decay                  | 0.001–4 s   | 0–127                                      | ADSR decay time                                      |
| CC 83  | Sustain                | 0–1         | 0–127                                      | ADSR sustain level                                   |
| CC 85  | Gate Length            | 0–2000 ms   | 0–127                                      | GATE note length; 0 follows the incoming gate        |
| CC 88  | Time Scale             | 0.25–4×     | 0–127                                      | Scales the whole envelope's timing                   |
| CC 102 | MIDI Velocity Response | —           | 0–63 = Fixed · 64–127 = Velocity-sensitive | Whether MIDI velocity scales volume                  |

#### Filter

| CC    | Parameter | Value range      | CC range                                                             | Description                                         |
| ----- | --------- | ---------------- | -------------------------------------------------------------------- | --------------------------------------------------- |
| CC 74 | Cutoff    | 20–16000 Hz, log | 0–127                                                                | Filter cutoff frequency                             |
| CC 75 | Resonance | 0–1              | 0–127                                                                | Filter resonance                                    |
| CC 76 | Mode      | —                | 0–25 = Off · 26–50 = LP · 51–76 = HP · 77–101 = BP · 102–127 = Notch | Filter response, or Off to bypass the filter        |
| CC 77 | Algorithm | —                | 0–63 = SVF · 64–127 = Ladder                                         | Filter algorithm — Cytomic SVF or OTA 4-pole ladder |

#### FX Chain

| CC    | Parameter       | Value range | CC range                                 | Description                               |
| ----- | --------------- | ----------- | ---------------------------------------- | ----------------------------------------- |
| CC 79 | Filter Position | —           | 0–63 = Pre-Chorus · 64–127 = Post-Chorus | Where the filter sits in the effect chain |
| CC 80 | Delay Position  | —           | 0–63 = Pre-Reverb · 64–127 = Post-Reverb | Where the delay sits in the effect chain  |

#### Output

| CC   | Parameter            | Value range | CC range | Description                                      |
| ---- | -------------------- | ----------- | -------- | ------------------------------------------------ |
| CC 7 | Volume               | 0–1         | 0–127    | Master output level                              |
| CC 8 | Space (Stereo Width) | 0–2         | 0–127    | Stereo width — 0 is mono, 1 normal, 2 hyper-wide |

#### Chorus

| CC    | Parameter | Value range | CC range                                            | Description                 |
| ----- | --------- | ----------- | --------------------------------------------------- | --------------------------- |
| CC 93 | Mode      | —           | 0–31 = Off · 32–63 = I · 64–95 = II · 96–127 = I+II | Juno-style chorus character |

#### Reverb

| CC     | Parameter | Value range | CC range | Description                   |
| ------ | --------- | ----------- | -------- | ----------------------------- |
| CC 91  | Wet       | 0–1         | 0–127    | Reverb wet send level         |
| CC 112 | Mod Speed | 0.1–4       | 0–127    | Reverb LFO rate multiplier    |
| CC 113 | Mod Depth | 0–1         | 0–127    | Reverb LFO depth              |
| CC 117 | Size      | 0–1         | 0–127    | Plate size / decay time       |
| CC 118 | Damping   | 0–1         | 0–127    | Reverb high-frequency damping |

#### Delay

| CC    | Parameter | Value range | CC range | Description           |
| ----- | --------- | ----------- | -------- | --------------------- |
| CC 86 | Time      | 10–500 ms   | 0–127    | Delay time            |
| CC 87 | Feedback  | 0–0.95      | 0–127    | Delay feedback amount |
| CC 95 | Mix       | 0–1         | 0–127    | Delay wet level       |

<!-- END GENERATED: cc-map -->

### MIDI Messages Outside the Manifest

These are actions rather than parameters, so they carry no value range and are not in `params.json`. They are handled in [`src/module_hooks.cpp`](src/module_hooks.cpp) — except the MIDI channel, which is the platform's.

| CC     | Message       | Range            | Description                                                                                        |
| ------ | ------------- | ---------------- | -------------------------------------------------------------------------------------------------- |
| CC 64  | Sustain       | ≥64=on / <64=off | Standard sustain pedal — arms the gate and holds it high. Not drone: use CC 119 to release the gate |
| CC 110 | MIDI Channel  | 0=omni, 1–16     | Set MIDI receive channel                                                                            |
| CC 114 | Reverb Freeze | ≥64=on / <64=off | Freeze the reverb tail indefinitely                                                                 |
| CC 119 | Drone Return  | any              | Clear gate arm, return to continuous drone                                                          |
| CC 123 | All Notes Off | any              | Panic — release all voices                                                                          |

For MIDI SysEx implementation, check the MIDI & SysEx reference: [AlloyFlux-MIDI-reference.md](../../references/AlloyFlux-MIDI-reference.md).

---

_For serial console commands see [AlloyFlux-serial-reference.md](../../references/AlloyFlux-serial-reference.md); for the board, [AlloyFlux-hardware-design.md](../../references/AlloyFlux-hardware-design.md); for the engines, [AlloyFlux-dsp-design.md](../../references/AlloyFlux-dsp-design.md)._

---

Voltage Foundry Modular — Alloy Flux · 2026
