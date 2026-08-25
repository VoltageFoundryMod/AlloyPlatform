# Alloy Flux — DSP Design

How the voice is built: the engines, their algorithms and the constants they
were tuned to. The companion to
[AlloyFlux-hardware-design.md](./AlloyFlux-hardware-design.md), which covers
the board.

For what the controls _do_ from a player's seat, see
[MANUAL.md](../modules/alloyflux/MANUAL.md) — this document is the engineering
behind it. For the platform the engines run on (cores, audio driver, parameter
manifest), see [README.md](../README.md) and [AGENTS.md](../AGENTS.md).

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

**CLOUD** (shared oscillator pool, up to 4 held notes — Milestone 22, made
polyphonic in M78):

```txt
no notes held    →  one 7-saw stack on ROOT, no envelope (the drone)
note held        →  one stack per note, own AR envelope, own stereo arc
                    saws per note = pool / notes, rounded down to odd
                    pool 12: 1 note → 7   2 → 5   3 → 3   4 → 3
one sub          →  tuned to the lowest note held, for the whole mode
```

Width is derived, never chosen, and a single held note therefore keeps the full
seven-saw stack — the drone and one note are the same sound. Oscillators fade in
and out of a stack over ~190 ms and are only re-tasked once silent; the drone
crossfades on its own per-sample gain over ~40 ms because it is the whole output
when it moves. Pool size and max notes are runtime (`cloud pool`, `cloud notes`)
so the CPU ceiling can be measured on hardware rather than derived.

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

- **GATE jack** (Mux Ch.0) — From ADC Mux
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

Times below are computed from the formulas in the implementation table further
down (`0.001 + curve² × 0.799` s and `0.080 + curve² × 1.920` s), at CURVETIME ×1.

| CURVE position | Attack  | Release | Sustain level | Sustain behaviour                     |
| -------------- | ------- | ------- | ------------- | ------------------------------------- |
| 0.0 (pluck)    | ~1 ms   | ~80 ms  | 0.00          | Decays regardless of gate length      |
| 0.20           | ~33 ms  | ~157 ms | 0.00          | Last fully percussive position        |
| 0.25           | ~51 ms  | ~200 ms | 0.25          | Decays to a quarter level, then holds |
| 0.30           | ~73 ms  | ~253 ms | 0.50          | Decays to half level, then holds      |
| 0.40           | ~129 ms | ~387 ms | 1.00          | First fully sustaining position       |
| 0.5 (natural)  | ~201 ms | ~560 ms | 1.00          | Sustains while gate is held           |
| 0.75           | ~450 ms | ~1.16 s | 1.00          | Sustains while gate is held           |
| 1.0 (swell)    | ~800 ms | ~2 s    | 1.00          | Sustains while gate is held           |

At the pluck extreme the amplitude decays even if the gate remains high — the envelope ignores gate length at and below CURVE = 0.20. From there the **sustain level** morphs continuously up to full across CURVE 0.20 → 0.40, so the note decays partway and then holds; at and above 0.40 it is a full sustain+release model. The morph is continuous in both level and time, so there is no audible step anywhere on the knob.

> Prior to this the transition was a hard branch at `curve > 0.2`: sustain was either 0.0 or 1.0 with nothing in between, so a single ADC count near the threshold flipped a note between a fixed-length blip and an indefinite hold. The endpoints of the knob are unchanged.

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

| Parameter          | Value                              | Notes                                                    |
| ------------------ | ---------------------------------- | -------------------------------------------------------- |
| Attack range       | 1 ms – 800 ms                      | `0.001 + curve² × 0.799` s                               |
| Release range      | 80 ms – 2 000 ms                   | `0.080 + curve² × 1.920` s                               |
| Sustain morph      | `(curve − 0.20) / 0.20`, clamped   | 0.0 at curve ≤ 0.20, 1.0 at curve ≥ 0.40                 |
| Pluck region       | curve ≤ 0.20 (sustain 0.0)         | decays to silence at the release rate, gate hold ignored |
| Decay rate         | same coefficient as release        | one-pole toward the sustain level                        |
| Coefficient method | `expf()` in `setCurve()` at 128 Hz | never called in audio ISR                                |
| `next()`           | one-pole multiply/add              | no expf, branch-minimal, safe in ISR                     |

State machine: `IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE`. At sustain 0.0
the DECAY step reduces algebraically to `env *= relDecay` — the identical pluck
release the engine performed before the morph existed.

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

| Max delay | RAM cost | Notes                                                      |
| --------- | -------- | ---------------------------------------------------------- |
| 200 ms    | ~26 KB   |                                                            |
| 300 ms    | ~39 KB   | `DelayEngine.h` fallback default                           |
| 500 ms    | ~65 KB   | **In use** — set in platformio.ini and vcv-plugin/Makefile |

**Cross-channel feedback** creates the ping-pong effect — echoes alternate L/R/L/R:

```txt
L delay line ← inL + feedback × delayedR
R delay line ← inR + feedback × delayedL
```

Linear interpolation on fractional delay samples eliminates zipper artefacts when time changes. `process()` is `always_inline` — fully absorbed into `updateAudio()` in SRAM.

| Parameter | Range       | Notes                                       |
| --------- | ----------- | ------------------------------------------- |
| mix       | 0.0–1.0     | 0 = hard bypass (zero CPU, early return)    |
| time_ms   | 10–500 ms   | Fractional sample accuracy (`DELAY_MAX_MS`) |
| feedback  | 0.0–0.95    | Clamped to prevent runaway accumulation     |
| dry gain  | 1 − mix×0.5 | Slight dry reduction at high mix            |

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
