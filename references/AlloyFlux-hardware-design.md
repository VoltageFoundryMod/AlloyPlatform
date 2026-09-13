# Alloy Flux — Hardware Design

The board: what the module is made of and why. Pin assignments, analog front
end, power, panel and BOM.

This is a **design document**, not a user manual and not a description of the
shipping firmware. Where it describes an input stage or a jack that milestones
16–19 have not yet wired, it is stating intent.

| Looking for                                     | Go to                                                            |
| ----------------------------------------------- | ---------------------------------------------------------------- |
| How to play the module, panel controls, LEDs    | [MANUAL.md](../modules/alloyflux/MANUAL.md)                      |
| The DSP engines and their algorithms            | [AlloyFlux-dsp-design.md](./AlloyFlux-dsp-design.md)             |
| Firmware architecture, cores, the platform seam | [README.md](../README.md) and [AGENTS.md](../AGENTS.md)          |
| MIDI, SysEx and the CC map                      | [AlloyFlux-MIDI-reference.md](./AlloyFlux-MIDI-reference.md)     |
| Serial console commands                         | [AlloyFlux-serial-reference.md](./AlloyFlux-serial-reference.md) |
| Teletype / i2c                                  | [I2C/II_Protocol_Spec.md](./I2C/II_Protocol_Spec.md)             |

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

| Parameter   | Value                                                                                                      |
| ----------- | ---------------------------------------------------------------------------------------------------------- |
| Format      | Eurorack                                                                                                   |
| Width       | 14HP                                                                                                       |
| MCU         | Raspberry Pi Pico 2 — RP2350 (dual Cortex-M33 @ 150MHz)                                                    |
| DAC         | PCM5102A (I2S, 16-bit, 112dB SNR)                                                                          |
| Output      | Stereo L/R — passive mono normalled on Left when R unplugged                                               |
| Voice modes | PAIR / CLOUD / CHORD / CASCADE / STRING / PLASMA / POLY                                                    |
| Framework   | Arduino + the Alloy Platform's own I2S driver; custom DSP engines (ShapeOsc, ChorusEngine, CurveEngine, …) |
| Build tool  | PlatformIO                                                                                                 |
| Knobs       | 9 (ROOT, RELATION, SHAPE, MOTION, FM, CURVE, SPACE, DELAY, REVERB)                                         |
| Jacks       | 10 (V/OCT, GATE, MIDI, RELATION CV, COLOR CV, FM IN, SHAPE CV, MOTION CV, L OUT, R OUT)                    |
| Buttons     | 2 (MODE + SHIFT)                                                                                           |
| LEDs        | 7× APA102/SK9822 Dotstar RGB                                                                               |
| Power draw  | ~100mA +12V, ~5mA −12V (estimate)                                                                          |

---

## Hardware Stack

### RP2350 Peripheral Usage

| Peripheral | Usage                                                      |
| ---------- | ---------------------------------------------------------- |
| PIO 0      | I2S audio output to PCM5102A (BCK=16, LCK=17, DATA=18)     |
| GPIO       | Dotstar LED bitbang SPI (GP6=data, GP7=clk)                |
| PIO 2      | Spare — future use                                         |
| ADC GP26   | V/OCT pitch CV — direct, fast reads                        |
| ADC GP27   | FM IN — direct, audio-rate reads in `renderAudio()`        |
| ADC GP28   | 74HC4067 mux signal — all knobs + slow CVs + jack switches |
| UART1 RX   | Hardware MIDI in on GP9                                    |
| USB        | USB MIDI device — native, no pins consumed                 |
| Core 0     | UI, ADC scanning, MIDI, modulation routing                 |
| Core 1     | Audio DSP — oscillators, chorus, drift, spatializer        |
| DMA        | I2S block transfer; paces `AudioDriver::pump()` on Core 1  |

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
| GP6  | 9        | Dotstar LED data    | Out    | LED chain (all 7 LEDs)                                               |
| GP7  | 10       | Dotstar LED clk     | Out    | LED chain (all 7 LEDs)                                               |
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
| GP27 | 32       | ADC1 — FM IN        | In     | Direct ADC, audio-rate reads in `renderAudio()`                      |
| GP28 | 34       | ADC2 — Mux signal   | In     | 74HC4067 SIG — all knobs + slow CVs + jack switches                  |
| GP25 | internal | Onboard LED         | Out    | Debug only — **does not exist on a Pico 2W** (see below)             |
| —    | 36       | 3.3V out            | Pwr    | Powers PCM5102, 74HC4067                                             |
| —    | 39       | VSYS                | Pwr    | System power — 5V from AP63205WU buck converter                      |
| —    | 40       | VBUS                | Pwr    | USB 5V                                                               |

### Pico 2 vs Pico 2W (M78)

The board accepts either, and the firmware is built per board (`make firmware` /
`make firmware WIRELESS=1`). **The pin map above needs no change**, because the
2W's CYW43439 takes GP23 (WL_ON), GP24 (WL_DATA), GP25 (WL_CS) and GP29
(WL_CLK) — four pins AlloyFlux does not use.

Two things do change, neither of them on a signal net:

- **GP25 stops being the onboard LED.** On a 2W that LED sits on the radio
  chip. It was debug-only, so nothing is lost, but code that assumes a GPIO
  there is wrong on half the boards.
- **ADC3 / VSYS sense on GP29 is gone.** Also unused.

⚠️ **Antenna keepout.** The 2W's antenna is at the USB end of the board, and on
this module it sits behind an aluminium panel, inside a metal rack, beside the
AP63205WU switcher. Keep the ground pour clear beneath it and expect
attenuation relative to a bare board on a desk.

⚠️ **Power.** BLE advertising draws ~10–20 mA with ~40 mA transmit peaks on top
of the ~100 mA +12 V figure in the table above. The buck has the average
headroom easily; what it needs is bulk capacitance near VSYS for the peaks.

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
SPACE engine (stereo width)     ◄── knob only (no CV jack)
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

## Panel Layout — 14HP

**HP:** 14HP (70.96mm panel width)
**Jacks:** 10× Thonkiconn PJ398SM (switched, vertical mount)
**Knobs:** 9× Alpha 9mm (ROOT and RELATION largest)
**Buttons:** 2× tactile panel mount
**LEDs:** 7× APA102/SK9822 Dotstar RGB (GP6=data, GP7=clk, bitbang SPI in `updateControl()`)

---

## Jack Assignment

Ten jacks in two rows of five. **The authority for this section is
`hardware/MainPCB/` — `MainPCB.kicad_pcb` for where a jack sits and
`Inputs.kicad_sch` for what it connects to.** The `Ref` column is the board
designator and the `Pin/Path` column is read off U9's pads; both were verified
against those files rather than transcribed, because this section has drifted
from the board twice before.

Panel positions below are millimetres from the panel origin, which is
`(pcb_x − 190.370, pcb_y − 39.234)` — the same offset `platform/vcv/PanelLayout.h`
records. Row 1 is y 93.7, row 2 is y 107.7.

### Input Row 1 — Primary Inputs

| Pos | Ref  | Label    | x (mm) | Function                                     | Pin/Path    |
| --- | ---- | -------- | ------ | -------------------------------------------- | ----------- |
| 1   | J3   | V/OCT    | 9.443  | Pitch — 1V/oct, 0–6V range                   | GP26 direct |
| 2   | J4   | GATE     | 22.217 | Note trigger / envelope / articulation       | Mux CH0     |
| 3   | J2   | MIDI     | 35.390 | TRS MIDI in — Type A/B dual circuit          | GP9 UART1   |
| 4   | J21  | RELATION | 48.663 | RELATION modulation — interval / chord morph | Mux CH1     |
| 5   | J6   | COLOR    | 61.437 | COLOR modulation — timbre / FM depth         | Mux CH2     |

### Input Row 2

| Pos | Ref  | Label    | x (mm) | Function                                       | Pin/Path    |
| --- | ---- | -------- | ------ | ---------------------------------------------- | ----------- |
| 1   | J9   | FM IN    | 9.343  | Pitch FM — 0.2 oct/V, bipolar ±8 V             | GP27 direct |
| 2   | J7   | SHAPE    | 22.117 | SHAPE modulation — waveform morph              | Mux CH3     |
| 3   | J8   | MOTION   | 35.340 | MOTION modulation — animation depth            | Mux CH4     |
| 4   | J10  | L OUT    | 48.563 | Left audio — passive mono sum when R unplugged |             |
| 5   | J11  | R OUT    | 61.337 | Right audio — stereo                           |             |

> **The designator sequence is not the panel order**, and two entries in
> particular have caught people out:
>
> - **The row-1 position-4 jack is J21, not J5. There is no J5 on this board.**
>   Both `io/PanelMap.h` and `platform/vcv/PanelLayout.h` called it J5 for a
>   while; nothing depended on the name, but it sent two readers to a footprint
>   that does not exist.
> - **J9 is FM IN and it sits at the LEFT end of row 2**, where the panel art
>   puts it. An earlier board revision had J7 there with J9 one position to its
>   right, which mattered because J9 is the only jack on a direct ADC pin — the
>   two are not interchangeable, since FM IN is ±8 V with a 100 pF cap while
>   the mux channels are ±5 V. **That is fixed: the board and the art now
>   agree.** The warning blocks that said otherwise have been removed from
>   `PanelMap.h` and `PanelLayout.h`.

> ⚠️ **The schematic net names are one panel revision behind.** `Inputs.kicad_sch`
> still calls the four modulation nets `REL_CV`, `SHAPE_CV`, `MOTION_CV` and
> `SPACE_CV`, from the layout in which SPACE had a jack. By position they now
> carry RELATION, **COLOR**, **SHAPE** and **MOTION** respectively — so
> `SHAPE_CV` is the COLOR jack, `MOTION_CV` is the SHAPE jack and `SPACE_CV` is
> the MOTION jack. Nothing is mis-wired and no firmware change is needed: the
> mux channel order is positional and `Cv::` maps CV_3..CV_6 onto CH1..CH4 in
> panel order regardless of what the nets are called. Rename them at the next
> schematic revision; until then, read this table and not the net label.

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

> ⚠️ **Two input topologies are described here and they disagree.** The block
> diagrams immediately below are the early passive-divider sketch; the component
> table under [Op-Amp Input Stage](#op-amp-input-stage--component-values) is the
> later MCP6004 design with a −10 V reference, which is what the
> [Power Architecture](#power-architecture) rail feeds. The op-amp stage is
> believed current — it inverts, which the divider does not, and the firmware
> has to know which. **Resolve before the PCB is cut.**

### V/OCT Input Conditioning

```txt
Eurorack CV ──→ MCP6004 precision scaling ──→ RC filter ──→ GP26 (ADC0)
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

FM IN is read in `renderAudio()` at audio rate. Must not go through the mux.

### Gate Input

```txt
Eurorack gate (0–5V) ──→ 10kΩ series ──→ BAT48 clamp ──→ U9 I0 (mux CH0) ──→ GP28
```

**Resolved: the gate does not need a GPIO of its own.** This stage was once
written against GP12 — which is the MODE button backlight, not a free pin — and
the jack table carried an "unassigned" placeholder for the same reason. The
board settled it differently: J4 goes through the same conditioning as the other
slow CVs and lands on **U9 pin 9 (I0), read as mux channel 0 through GP28**, so
no dedicated pin is spent. `Inputs.kicad_sch` names the net `GATEIN`.

Being on the mux means the gate is sampled at the control rate (128 Hz), not by
edge interrupt, so the shortest reliably-detected trigger is one control tick.
That is the design, not a limitation to route around: `readCV(Cv::GATE)`
compares against a threshold and every other consumer reads the debounced flag.

> ⚠️ **Firmware has not caught up.** `HardwarePicoIO::readCV()` still returns the
> MIDI/button gate rather than reading CH0, because the ADC mux driver does not
> exist yet (`POT_ADC_PRESENT == 0`) — the same seam that leaves every knob and
> every CV jack unread on hardware. See `readPotRaw()` in
> [`io/HardwarePicoIO.h`](../modules/alloyflux/include/io/HardwarePicoIO.h).

### Op-Amp Input Stage — Component Values

For the MCP6004 with a 3.3 V supply and a −10 V reference. Use 1 nF in the
feedback loop except on FM, where 100 pF keeps high-frequency noise out of the
audio range.

| Input voltage           | Input R | Reference R | Feedback R | Feedback C |
| ----------------------- | ------- | ----------- | ---------- | ---------- |
| Bipolar −5/+5 V         | 100k    | 200k        | 33k        | 1 nF       |
| Bipolar −8/+8 V, for FM | 100k    | 120k        | 20k        | 1 nF       |
| Unipolar 0/10 V         | 100k    | 43k         | 33k        | 1 nF       |
| V/Oct −3/+7 V           | 100k    | 140k        | 33k        | 1 nF       |
| Gate −0.8/8 V           | 100k    | 110k        | 33k        | 1 nF       |

Per-jack voltage ranges:

| CV input  | Voltage range |
| --------- | ------------- |
| V/Oct     | −8/+7 V       |
| GATE      | −0.8/+8 V     |
| REL CV    | −5/+5 V       |
| COLOR CV  | −5/+5 V       |
| SHAPE CV  | −5/+5 V       |
| MOTION CV | −5/+5 V       |
| FM IN     | −8/+8 V       |

The pots act as attenuators when a CV is patched, so the modulation inputs are
bipolar (±5 V) for a wider modulation range.

**All CV inputs are inverted by this op-amp stage** — the firmware has to undo
that when it scales the reading. Note this contradicts the "V/Oct 0–6 V,
unipolar" figure quoted in the block diagrams and the ADC strategy above; see
the warning at the top of this section.

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
- 15 of 16 channels used; I15 (pad 16) is unconnected on the board

### Mux Channel Map

| Ch   | Pin | Signal         | Type    | Notes                                              |
| ---- | --- | -------------- | ------- | -------------------------------------------------- |
| CH0  | 9   | Gate jack      | Slow CV | Gate Input                                         |
| CH1  | 8   | REL CV jack    | Slow CV | RELATION modulation input                          |
| CH2  | 7   | COLOR CV jack  | Slow CV | COLOR modulation input                             |
| CH3  | 6   | SHAPE CV jack  | Slow CV | SHAPE modulation input                             |
| CH4  | 5   | MOTION CV jack | Slow CV | MOTION modulation input                            |
| CH5  | 4   | ROOT knob      | Pot     | Coarse pitch offset                                |
| CH6  | 3   | RELATION knob  | Pot     | Signature control — interval/detune/chord/FM depth |
| CH7  | 2   | SHAPE knob     | Pot     | Waveform morph position                            |
| CH8  | 23  | MOTION knob    | Pot     | Animation depth                                    |
| CH9  | 22  | SPACE knob     | Pot     | Stereo width / placement                           |
| CH10 | 21  | COLOR knob     | Pot     | FM depth / timbre spread — top-right knob          |
| CH11 | 20  | CURVE knob     | Pot     | Envelope / articulation shaping                    |
| CH12 | 19  | Assignable CV  | CV      | J14 — an internal header, **not** a panel jack     |
| CH13 | 18  | DELAY knob     | Pot     | Delay control                                      |
| CH14 | 17  | REVERB knob    | Pot     | Reverb control                                     |
| CH15 | 16  | Spare          | —       | Unconnected on the board — future knob or CV       |

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
| RELATION | RELATION | Interval/detune set   | REL CV depth + polarity    |
| COLOR    | COLOR    | Fixed timbre spread   | Color CV depth + polarity  |
| SHAPE    | SHAPE    | Fixed waveform shape  | Shape CV depth + polarity  |
| MOTION   | MOTION   | Fixed animation depth | Motion CV depth + polarity |

SPACE has no jack — the panel has four generic modulation inputs and COLOR earns one of them ahead of stereo width. The knob, its CC and its preset field are unaffected.

FM IN is the fifth input and is not one of the four: it is the only jack on a direct ADC pin (GP27, off the analogue mux), conditioned for ±8 V with a 100 pF cap so it can eventually be sampled at audio rate. It modulates **pitch**, exponentially, at a fixed 0.2 oct/V — ±1 octave over the jack's ±5 V nominal swing — with no attenuverter, since neither knob in the top row is free to be one. See `kFmInOctPerVolt` in `SynthEngine.h`.

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

## User Interface — Screenless

**Philosophy:** All feedback via 7 APA102/SK9822 Dotstar RGB LEDs + 2 buttons. No menus. No reading required during performance. Eyes stay on the patch, not a display.

Hidden functions (behind long-hold) cover only: calibration, MIDI channel configuration, advanced settings. Core synthesis is always directly accessible.

The crucial UX distinction: mode changes are deliberate and infrequent. You choose a mode, you play, you hear the difference immediately. You don't need to remember what color means what — you just made that choice. This is why fixed-function jacks beat assignable jacks, and why a clear mode cycle with LED confirmation beats a menu.

### LED Language

---

#### Hardware Layout

```txt
 D12 ·  ·  · D22      ← top:        voice activity (left / right)
D13  ·  ·  ·  D21     ← mid-top:    voice motion / modulation / stereo position
  D14   ·   D16       ← mid-bottom: mode indicator / shift state
    ·  D15  ·         ← bottom:     heartbeat / motion / global

SW2 = MODE_SW   (left button  — near D14)
SW3 = SHIFT_SW  (right button — near D16)
```

---

#### LED Role Assignment

| LED | Position  | Primary Role            | Secondary Role                 |
| --- | --------- | ----------------------- | ------------------------------ |
| D12 | Top left  | ROOT voice activity     | Left channel stereo energy     |
| D22 | Top right | RELATION voice activity | Right channel stereo energy    |
| D13 | Mid top l | Motion and modulation   | Stereo position / chorus depth |
| D21 | Mid top r | Modulation depth        | Stereo position / chorus depth |
| D14 | Mid left  | Current voice mode      | MODE_SW feedback               |
| D16 | Mid right | Shift state / drone     | SHIFT_SW feedback              |
| D15 | Centre    | Motion / heartbeat      | Global event confirmation      |

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

#### D12 + D22 — Voice Activity (always)

D12 and D22 always reflect voice activity — they breathe with the audio envelope in all modes. Bright on attack, fading on release. Readable without mode awareness.

| Mode    | D12 (ROOT / Left)                          | D22 (RELATION / Right)                      |
| ------- | ------------------------------------------ | ------------------------------------------- |
| PAIR    | Warm red — envelope level, gate response   | Cool blue — relation envelope, detune depth |
| CLOUD   | Cyan — shifts with left voice position     | Cyan — shifts opposite, stereo spread       |
| CHORD   | Amber — root note envelope                 | Amber dimmer — chord interval spread amount |
| CASCADE | Magenta — FM carrier activity              | Magenta brighter — modulator depth          |
| STRING  | Purple — slow drift, breathes              | Purple — offset phase from D12              |
| POLY    | Warm red — brightness = active voice count | Cool blue — chord spread / detune width     |

---

#### D14 — Mode Indicator (near MODE_SW)

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

#### D16 — Shift / Drone State (near SHIFT_SW)

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

#### D15 — Centre Heartbeat / Global

The module's pulse. Shows overall animation and event state. Even without understanding any other LED, D15 tells you if the module is doing something.

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

#### D13 + D21 — Modulation & Stereo Position (always)

Together with the voice activity on D12/D22, these give a visual sense of how the sound is moving and modulating. They are the "motion" layer of the LED language. They should follow gradients with the voice activity to show the flow of sound.

| Mode    | D13 (mid-top left)     | D21 (mid-top right)                 |
| ------- | ---------------------- | ----------------------------------- |
| PAIR    | Green — MOTION depth   | Green dimmer — detune amount        |
| CLOUD   | Cyan — MOTION depth    | Cyan dimmer — stereo spread         |
| CHORD   | Amber — MOTION depth   | Amber dimmer — chord spread         |
| CASCADE | Magenta — MOTION depth | Magenta dimmer — FM depth           |
| STRING  | Purple — MOTION depth  | Purple dimmer — stereo offset       |
| POLY    | Green — MOTION depth   | Green dimmer — detune/spread amount |

---

#### Mode Change Animation

Triggered by MODE_SW tap. Two stages: the ripple says *something changed*, the
count that follows says *which mode*, without the user having to remember what a
colour means.

```txt
1. D15 white flash      ← centre ignites first
2. D13, D14, D16, D21   ← ripple outward, all flash white
                           (~300 ms so far)
3. Count                ← LED1..LEDn fill in along the silkscreen chain
                           D12 → D13 → D14 → D15 → D16 → D21 → D22,
                           n = the mode's position in the cycle:
                             PAIR 1  CLOUD 2  CHORD 3
                             CASCADE 4  STRING 5  PLASMA 6
                             POLY 7  → the count fills the panel
                           80 ms apart, each staying lit once it fires, in
                           the mode's own colour with a white leading edge
                           on the newest. Every LED past n is forced dark
                           so the count cannot be misread.
4. Hold                 ← all n lit together, 240 ms
5. Settle               ← 200 ms dissolve back to:
                           D14 → new mode colour
                           D12/D22 → new mode voice colours
                           D15 → resumes heartbeat role
Total duration: ~800 ms (PAIR) … ~1.3 s (POLY)
```

**Seven modes is the ceiling**, and it is this animation that sets it. POLY
lights all seven; an eighth mode would have nowhere to show itself.

The chain order, not the role order, is what the count walks — `LedId` pairs
left with right and would zig-zag across the panel. `kLedPanelOrder` in
`io/LedEngine.h` holds the mapping.

---

## Drone Mode Entry / Exit

**Entering drone** (hold SHIFT then tap MODE, or automatically on power-on before any gate is received):

```txt
D16 → fades up to warm white slow breathe    (drone is on)
D15 → fades to dim green slow breathe        (module is running)
D12 /D22 → hold steady voice colours         (no gate pulsing)
```

**Exiting drone** (first gate received automatically, or hold SHIFT then tap MODE to toggle back to gated):

```txt
D16 → fades down to off                     (gated mode active)
D12/D22 → begin responding to gate/envelope (pulsing resumes)
D15 → pulses with gate and MOTION           (heartbeat resumes)
```

---

### SHIFT Button Interaction (D16 context)

| Action                    | Result                                                              |
| ------------------------- | ------------------------------------------------------------------- |
| SHIFT hold + MODE tap     | Toggle drone / gated mode — D16 breathes warm white in drone        |
| SHIFT held                | D16 white steady — secondary layer active                           |
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
| MODE tap (SHIFT not held) | Cycle voice mode: PAIR → CLOUD → CHORD → CASCADE → STRING → PLASMA → POLY → PAIR          |
| SHIFT hold + turn knob    | Access secondary pot parameter (FATNESS / DRIFTSPEED / CURVETIME / VOL)                   |
| SHIFT hold + MODE tap     | Return to drone mode — hold SHIFT then tap MODE; clears gate arm, D15 breathes warm white |
| Hold MODE during power-on | Enter V/OCT calibration routine (2-point: 1V then 3V)                                     |

MODE may also serve as a third shift layer in future firmware — SHIFT+MODE for secondary parameters, a potential future third combination for deeper configuration without adding buttons.

That is the complete button interaction surface. Nothing else is hidden. No colour memorization required during performance — mode is chosen deliberately, confirmed by LED, heard immediately.

---

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
| APA102/SK9822       | 5mm or SMD        | 7                    | Dotstar RGB status LEDs (clocked SPI, interrupt-safe) |
| Thonkiconn PJ398SM  | —                 | 10                   | Switched Eurorack jacks                               |
| Alpha 9mm pot       | RD901F            | 9                    | Panel knobs (2 large for ROOT/RELATION)               |
| Tactile button      | 6×6mm panel mount | 1                    | Single button                                         |
| Eurorack header     | 16-pin shrouded   | 1                    | Power connector                                       |
| Film cap            | 10µF              | 2                    | Audio AC coupling (L + R output)                      |
| Electrolytic cap    | 100µF             | 3                    | Rail bypass                                           |
| Ceramic cap         | 100nF             | 10+                  | IC decoupling                                         |
| Ceramic cap         | 10µF              | 1                    | LFO RC filter (if LFO CV out added)                   |
| Resistors 1%        | 10kΩ, 47kΩ, 100kΩ | ~30                  | Gain, dividers, pull-ups, mono sum                    |

---
