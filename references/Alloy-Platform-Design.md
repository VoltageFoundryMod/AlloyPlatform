# Alloy — Platform Design

**Purpose:** turn AlloyFlux from a single module into a firmware platform that hosts
multiple synthesis engines, with Alloy Coil as the second module and the proof case.

**Status:** design agreed, not started
**Supersedes:** `audrey-alloyflux-design.md` (its Phase 0 assumptions were largely wrong —
see §1). Its DSP analysis in §6 remains valid _at 48 kHz_ and is carried forward here.
**Author:** Carlos — Voltage Foundry Modular
**Date:** 2026-08-13

---

## 0. The three questions, answered

| Question                                        | Answer                                                                         |
| ----------------------------------------------- | ------------------------------------------------------------------------------ |
| Is it worth moving the stack to something else? | **No.** ~90% stays. One library swap: Mozzi out, own I2S driver in.            |
| Can Alloy become a generic platform?            | **Yes.** `IHardwareIO` is already a real HAL. Four mechanical blockers remain. |
| Can Alloy Coil port cleanly, without artifacts?     | **Yes — at 48 kHz.** ~10 lines of platform coupling in the whole engine.       |

---

## 1. Phase 0 recon — verified against source

The prior design document was written without access to either tree. Every assumption has
now been checked. Four were wrong, and two of those (A5, A7) changed the plan materially.

| #      | Assumed                         | Verified reality                                                                                                                  |
| ------ | ------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| **A1** | HAL boundary exists             | ✅ `IHardwareIO` — `common/include/io/HardwareIO.h`, 6 virtuals, genuinely clean                                                  |
| **A2** | VCV wraps the engine abstractly | ⚠️ Partly. VCV shares `common/` **by source**; `VCVRackIO` implements the HAL. But `AlloyFlux.cpp` (86 KB) is not engine-agnostic |
| **A3** | Configurator emits a mapping    | ❌ **No.** `web-configurator/src/lib/paramMap.ts` states it "mirrors src/param_map.cpp" — a hand-maintained duplicate             |
| **A4** | arduino-pico                    | ✅ …via **PlatformIO** (`maxgerhardt/platform-raspberrypi`, `board_build.core = earlephilhower`) — which the prior doc rejected   |
| **A5** | Raw PIO + DMA audio             | ❌ **Mozzi**, `MOZZI_OUTPUT_I2S_DAC` (`firmware/src/main.cpp:15`)                                                                 |
| **A6** | Unknown control count           | ✅ 9 pots + 6 shift-secondaries (15 `PotId`), 7 CV, 2 buttons, 7 RGB LEDs                                                         |
| **A7** | 48 kHz float                    | ❌ **32768 Hz**, signal path `int32_t ±32512` fixed-point                                                                         |

**Consequence of A7:** the prior doc's entire §6 memory table, its float/Q15 analysis, and
its central claim — _"nothing that was voiced by ear needs revoicing"_ — all assumed a rate
AlloyFlux does not run. Resolved by giving each module its own rate (§3).

**Consequence of A5:** the prior doc's §2 recommendation (migrate to pico-sdk + CMake)
was argued from "three build systems duplicate the engine build." That is already solved —
`common/` is one source of truth. Migration would cost proven TinyUSB and EEPROM code for
no gain. Rejected; see §2.

**Audrey-II recon:** engine is ~33 KB of source with **~10 lines of platform coupling**:
`SDRAM::allocate<T>()` ×3, `#ifdef __arm__ / <dev/sdram.h>`, a dead `<arm_math.h>` include,
and `<daisysp.h>`. `Engine::Process(float in, float &outL, float &outR)` is per-sample and
hardware-free. DaisySP surface is 9 files. Submodules were empty on first inspection —
now populated.

---

## 2. Decision 1 — the stack stays, minus Mozzi

### Keep

PlatformIO · arduino-pico (earlephilhower) · Adafruit TinyUSB (composite CDC + MIDI) ·
EEPROM flash emulation with automatic Core 1 pause · `common/` source sharing · root
Makefile · VCV plugin build.

None of this is what costs you duplication, and all of it is hardware-validated.

### Remove — Mozzi

Not because it is bad, but for two specific reasons:

1. **Per-sample only.** A platform wants block processing — to amortise control work, to let
   modules choose a block size, and because DaisySP has block APIs (Alloy Coil's own main uses
   `limiter[0].ProcessBlock(OUT_L, size, 0.7f)`, which has no home under Mozzi).
2. **Sample rate was a project-wide `#define`.** `MOZZI_AUDIO_RATE` / `MOZZI_CONTROL_RATE`
   were macros in `main.cpp` consumed by Mozzi's own config machinery, which makes a
   per-module rate awkward — you cannot have two modules at two rates behind one set of
   global defines. _(Correction: an earlier draft of this document claimed the macros had
   leaked into `common/include/params.h` and `SynthEngine.h`. They appear there only in
   comments — verified by grep. `SynthEngine` already takes both rates as `init()`
   arguments, so the shared headers were clean all along.)_

The actual API surface being removed is **six touch points**, all in `main.cpp`:
`#include <Mozzi.h>`, `MOZZI_AUDIO_RATE`/`MOZZI_CONTROL_RATE`, `startMozzi()`,
`AudioOutput updateAudio()` + `StereoOutput::from16Bit()`, `updateControl()`, `audioHook()`.

**AlloyFlux uses none of Mozzi's DSP** — `ShapeOsc`, `AREnvelope`, `DattorroReverb`,
`SVFFilter`, `OTALadder`, `ChorusEngine`, `DelayEngine` are all ours. Mozzi is functioning
purely as an output driver and a 128 Hz tick source.

### Add — DaisySP

Pure DSP, **zero hardware includes** (verified by grep across all of `DaisySP/Source` for
`stm32|daisy|dev/|per/|arm_math` — no hits). No migration; it coexists with anything.
Vendor only what Alloy Coil needs:

| File                                | Location in DaisySP |
| ----------------------------------- | ------------------- |
| `delayline.h`, `dcblock.*`, `dsp.h` | `Source/Utility/`   |
| `whitenoise.h`                      | `Source/Noise/`     |
| `tone.*`                            | `Source/Filters/`   |
| `crossfade.*`, `limiter.*`          | `Source/Dynamics/`  |
| `overdrive.*`, `reverbsc.*`         | `Source/Effects/`   |

Note `reverbsc` is in the main DaisySP tree in the current checkout, not a separate
DaisySP-LGPL. It is Csound-derived — check its header before shipping. GPL-3.0 downstream
absorbs either MIT or LGPL, so this is paperwork, not a blocker.

### Rejected

| Option                             | Why not                                                                                                                                                                  |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **pico-sdk + CMake**               | Prior doc's premise (duplicate engine builds) is already solved by `common/`. Costs TinyUSB + EEPROM. No gain.                                                           |
| **PlatformIO → something else**    | Already gives per-env flags and `build_src_filter` — exactly the multi-module mechanism needed.                                                                          |
| **Zephyr**                         | Vastly overkill. I²S, DMA, an ADC — not an RTOS.                                                                                                                         |
| **Rust (embassy / rp-hal)**        | Engine and all of DaisySP are C++.                                                                                                                                       |
| **Teensy Audio port (pico-audio)** | `int16_t` 128-sample blocks + a graph model that competes with our own. Lateral move from Mozzi, not an upgrade.                                                         |
| **rheslip/DaisySP_Teensy**         | DSP source is unmodified upstream — nothing to gain. Wrapper is Teensy-Audio-specific, and supports only one DaisySP object instance; Alloy Coil is stereo pairs throughout. |

`pico-audio` is still valuable **as reference** — see §4.

---

## 3. Decision 2 — platform / module split

### What belongs where

**Platform** (engine-agnostic, shared):
`IHardwareIO` + `HardwarePicoIO` + `VCVRackIO` · `ParameterRegistry` · config store ·
MIDI/SysEx + serial transports · `LedEngine` · `PotTakeover` · Alloy Controller ·
VCV scaffolding · audio driver · build system.

**Module** (per-engine):
the engine · its parameter manifest · **its sample rate and block size** · panel/LED
semantics · memory footprint.

### Engine selection is compile-time

Memory forces it: AlloyFlux is ~290 KB of 520 KB today, Alloy Coil lands ~414 KB. **They cannot
be co-resident.** One firmware image per module — which is also what lets each module pick
its own rate.

So do **not** use a vtable. Use a build-flag typedef:

```cpp
// platform/engine_select.h
#if defined(ALLOY_ENGINE_COIL)
  #include "modules/alloycoil/CoilEngine.h"
  using ActiveEngine = coil::Engine;      // 48000 Hz
#else
  #include "modules/alloyflux/SynthEngine.h"
  using ActiveEngine = SynthEngine;         // 32768 Hz (see §7)
#endif
```

Zero dispatch cost, full inlining, contract enforced by `static_assert` rather than a base
class.

### The four blockers

1. **Semantic pot IDs.** `PotId::ROOT, RELATION, SHAPE, MOTION…` is AlloyFlux vocabulary
   baked into the shared HAL. → positional `POT_1..POT_9` + per-module semantic aliases.
2. **`SynthParams` + `fillSynthParams()` + `gSynthEngine` singleton.** A fixed 40-field
   struct and a global reached into directly by `commands.cpp`, `usb_midi.cpp`,
   `config_store.cpp`. → module-owned params, no singleton.
3. **Parameter-surface duplication.** See §5. The largest ongoing cost.
4. **`AlloyConfig` is flat with one global `kConfigVersion`.** → `{magic, engineId,
engineVersion, blob[]}` so an Alloy Coil preset does not invalidate an AlloyFlux one.

### The float / int32 seam

AlloyFlux is `int32 ±32512` per-sample; Alloy Coil is float. Make the **platform boundary
float**, and keep AlloyFlux's internals fixed-point with conversion only at its own edge.
One multiply per sample is nothing; the point is to contain the change to a wrapper rather
than touching a signal path that was voiced by ear.

Keep AlloyFlux's rate-templating (`ShapeOsc<32768u>` etc.) **inside its module**. It must
not leak into platform code.

---

## 4. The audio driver

Replaces Mozzi. Built on earlephilhower's `I2S` class — which is what Mozzi's RP2040
backend wraps anyway (`i2s.setBCLK` / `setBuffers` / `begin(rate)` / `write16`).

Reference implementation: `pico-audio/src/AudioOutputI2S.h` (~40 lines). Take the pattern,
not the library. Specifically:

- **`i2s.onTransmit(callback)`** — DMA/interrupt-driven on buffer completion. Block-based
  and no core spinning. Preferred over a blocking tight loop.
- **`setBitsPerSample(32)`, samples written as `int32 = sample << 16`** — 16-bit data
  left-aligned in 32-bit frames.
- **Batch `i2s.write(buf, size)`** rather than per-sample writes — materially faster.
- **`setBuffers(6, blockSamples * 2 * sizeof(int32_t) / sizeof(uint32_t))`** as a starting
  point.
- **`setFrequency(rate)`** — arbitrary, no power-of-two constraint. This is what makes
  per-module sample rate free.

⚠️ **PCM5102A zero-sample gotcha.** From pico-audio, tested on the PCM5100A: _"When sending
0, some DAC will power off causing a hearable jump from silence to floor noise and cracks;
sending 1 is a workaround."_ We ship a PCM5102A. Worth checking whether this already affects
AlloyFlux at digital silence (all knobs down, no gate) — it may be masked today by whatever
Mozzi happens to emit.

**Core split.** ✅ Done in M63b2. The whole audio loop moved to Core 1, leaving Core 0
entirely for controls/USB/MIDI/LED, and the inter-core reverb transport was _removed_
rather than reimplemented. One correction to the plan as written: the driver must also be
**started** on Core 1, not merely pumped there — the I2S library enables `DMA_IRQ_0` on the
calling core, so `begin()` on Core 0 would leave the DMA handler and the writer on opposite
sides of the split.

**Control tick.** ✅ Done in M63b2, as the counter rather than a `repeating_timer`:
`AudioDriver` publishes `controlTicks()` and Core 0 watches it for change, which keeps the
control rate derived from the audio clock while running the work off the audio core.

---

## 5. The parameter manifest — the biggest win

### The problem, measured

One parameter today touches **ten sites**:

`common/include/params.h` (extern) → `firmware/src/main.cpp` (definition + default) →
`firmware/src/param_map.cpp` (CC row) → `firmware/src/commands.cpp` (serial command) →
`firmware/include/config_store.h` (field) → **`kConfigVersion` bump** →
`firmware/src/config_store.cpp` (pack/apply) → `firmware/src/usb_midi.cpp` (SysEx) →
`common/include/SynthEngine.h` (`SynthParams` field) → `common/include/io/IOBridge.h` (fill)
→ `vcv-plugin/src/AlloyFlux.cpp` (`configParam` + widget + `assignPot`) →
`web-configurator/src/lib/paramMap.ts` (row).

The codebase concedes this: there are **three** "Adding a new parameter" checklists
(`param_map.h:18`, `config_store.h:27`, `paramMap.ts:13`) and they do not agree with each
other. `DELAY_MAX_MS=500` is hand-mirrored across `platformio.ini` and `vcv-plugin/Makefile`
with a comment explaining what breaks when they drift.

A second module multiplies all of this by two.

### The solution already exists — in Alloy Coil

`Audrey-II/Source/ParameterRegistry.h` + `FeedbackSynthControls.cpp:97-141` **is** the
declarative manifest:

```cpp
params_.Register(Parameter::FeedbackLPFCutoff, 18000.0f, 100.0f, 18000.0f,
    std::bind(&Engine::SetFeedbackLPFCutoff, &engine, _1), 0.05f, daisysp::Mapping::LOG);
//               id                      default   min      max
//                                setter                  smoothing   curve
```

Eleven parameters, eleven rows, nothing else in the codebase knows a parameter exists.

**This inverts the prior doc's §4.** It assumed the Alloy Controller would supply the
manifest and Alloy Coil would conform. It is the other way round: promote `ParameterRegistry`
into the platform, express AlloyFlux's parameters in it, then **generate** the CC table,
config struct, VCV `configParam` calls and `paramMap.ts` from it.

Two fixes while promoting it: replace `std::function`/`std::bind` with function pointers or
CRTP, and replace `unique_ptr` + `SDRAM::allocate` with static storage. Registration is
init-time and dispatch is control-rate, so this is a no-heap-on-embedded concern, not a
hot-path one.

**This step pays for itself on AlloyFlux alone, before Alloy Coil exists.**

---

## 6. Alloy Coil port

### 6.1 Run it at 48 kHz — this is the whole "no artifacts" answer

At 32768 Hz, three things break:

1. **Hard breakage at boot.** `FeedbackLPFCutoff` is registered with range 100–18000 Hz and
   a **default of 18000** (`FeedbackSynthControls.cpp:113`). Nyquist at 32768 is 16384.
   `BiquadCascade::SetCutoff` clamps to `sample_rate_ * 0.5f` — exactly Nyquist — where the
   bilinear prewarp `tan(π·fc/fs)` diverges. Degenerate at startup, and the top fifth of
   that knob's travel dies.
2. **Two waveshapers inside feedback loops.** `Overdrive` (drive 0.4) in the resonator loop,
   `SoftClip` in the echo loop. Static waveshapers alias at any rate — that aliasing is part
   of how the instrument was voiced. Drop the rate 32% and fold-back lands in a much more
   audible region, and because both sit _inside_ feedback paths the error regenerates each
   pass rather than decaying.
3. **Guaranteed revoicing.** `SetBrightness(0.98)` / `SetDamping(0.4)` map onto rate-dependent
   one-pole coefficients — different rate, different string timbre. `verb_->SetLpFreq(12000)`
   moves from 0.5× to 0.73× Nyquist, leaving the reverb tail noticeably less damped.

At 48 kHz none of this happens and the original voicing holds unchanged. Cost: one constant
in a separate build env. **Free.** (Oversampling the two nonlinearities at 32768 would cost
more CPU than simply running the engine at 48 kHz — strictly the worse trade.)

Budget at 48 kHz on a 150 MHz M33: 3,125 cycles/sample against an estimated 800–1,500.
`ReverbSc` is the expensive block and the one to measure first.

### 6.2 Memory

Verified: `reverbsc.h:5` declares `#define DSY_REVERBSC_MAX_SIZE 98936` and `float
aux_[DSY_REVERBSC_MAX_SIZE]` — a **float count**, so 386 KiB as a single member. Resizing it
is not optional.

| Buffer             | Upstream (Daisy + SDRAM)    | Ported                       | Size        |
| ------------------ | --------------------------- | ---------------------------- | ----------- |
| Echo ×2 (5.0 s)    | 240,000 float ea (1.83 MiB) | 60,000 × `int16` ea @ 12 kHz | **234 KiB** |
| `ReverbSc::aux_`   | 98,936 float (386 KiB)      | 25,600 float                 | **100 KiB** |
| `fb_delayline` ×2  | 12,000 float ea (94 KiB)    | 6,144 float ea               | **48 KiB**  |
| `KarplusString` ×2 | 8,192 float ea (64 KiB)     | 4,096 float ea               | **32 KiB**  |
| **Engine total**   | ~2.36 MiB                   |                              | **414 KiB** |

Plus platform infrastructure (TinyUSB, MIDI, config, LED, serial) — estimate ~50 KB.
Against 520 KB that leaves roughly 56 KB. **Tight but workable.** Lever if it bites: 4.0 s
max echo instead of 5.0 s, ≈ 47 KB back, without touching the decimation ratio.

### 6.3 The two genuinely new DSP risks

Both are testable in VCV before any hardware exists. This is the prior doc's best idea and
it stands.

- **Anti-alias filter on the echo send.** The only new DSP in the port and the one place
  aliasing can enter an otherwise clean chain — the send tap is post-reverb and
  full-bandwidth. Reuse `BiquadCascade`, ~5 kHz 2-pole. Verify with a swept sine and a
  spectrum analyser.
- **Q15 echo storage.** Echo feedback is deliberately unbounded (max 1.5), so quantization
  noise regenerates across passes. Clamp hard to ±0.999 before quantizing (`SoftClip` can
  exceed unity and wrapping sounds like a gunshot); convert to float _then_ interpolate;
  optionally add first-order error-feedback noise shaping. Listen critically at max feedback.

Why decimation is safe: `EchoDelay::Process` already runs a `BPF12` at 800 Hz Q=1 _inside_
its feedback loop. At 12 kHz (÷4 of 48 kHz) the 6 kHz Nyquist sits where content is already
−17.5 dB down. Expose ÷3 / ÷4 and float@48k / Q15@12k as compile-time switches and A/B them
live.

### 6.4 Port mechanics

Strip the ten coupling lines; `SDRAM::allocate<T>()` → static storage; delete
`Source/memory/`, `FeedbackSynth_main.cpp`, the Daisy `Makefile` and `.vscode`. Rewrite
`FeedbackSynthControls` against the platform's `ParameterRegistry` + positional `PotId`.
Preserve upstream MIT headers and `CREDITS.md`; GPL-3.0 at project root; credit Synthux
prominently.

---

## 7. Open decision — does AlloyFlux move to 48 kHz too?

Dropping Mozzi makes sample rate a per-module property. It does **not** automatically move
AlloyFlux. Genuinely open, and to be decided by measurement:

- **For:** the engine is already rate-parametric (`init(audioRate, controlRate)`,
  `setSampleRate()`) and runs at host rate in VCV daily — so hardware at 48 kHz would
  finally match the VCV build. It also fixes a latent issue: `gFilterCutoff` maxes at
  16000 Hz against a 16384 Hz Nyquist (97.6%), near-degenerate for the SVF and ladder.
- **Against:** +46% CPU per sample; delay buffer grows ~65 KB → ~96 KB at `DELAY_MAX_MS=500`.

⚠️ **Measure before committing.** The `CPU_PROFILE` budget is 30 µs against a 30.5 µs period
at 32768 (`main.cpp:687`). At 48 kHz the period drops to 20.8 µs. Read `gAudioElapsedUs` on
real hardware first — if AlloyFlux is near budget today, 48 kHz breaks it.

Default if unmeasured: **leave AlloyFlux at 32768.** Alloy Coil gets 48 kHz either way.

---

## 8. Plan

| Step  | Work                                                                                                                                 | Est.   |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------ | ------ |
| **1** | Replace Mozzi with own I2S driver — **on the current structure, nothing else changed.** Confirm 1 kHz sine on a scope.               | ½ day  |
| **2** | Restructure into `platform/` + `modules/`; purge `MOZZI_*` from shared headers; move audio loop to Core 1, drop `gRevInQueue`.       | 1 day  |
| **3** | Promote `ParameterRegistry` into the platform; port AlloyFlux's params; generate CC table, config struct, VCV params, `paramMap.ts`. | 2 days |
| **4** | Positional `PotId`/`CVId` + per-module aliases; engine-tagged config blobs.                                                          | 1 day  |
| **5** | Vendor DaisySP subset (9 files) + `libaudrey`; strip coupling; static-allocate; compile standalone on host.                          | ½ day  |
| **6** | Second build env at 48 kHz; VCV Alloy Coil module with ÷3/÷4 and float/Q15 switches; **voice it there**.                                 | 1 day  |
| **7** | Firmware bring-up; `arm-none-eabi-size` against the margin; cycle-count; long-run at max feedback.                                   | 1 day  |

**Step 1 must be done alone and first.** Bundling a directory restructure with an
audio-driver replacement means that if audio breaks you will not know which caused it.

**Steps 1–4 are worth doing even if Alloy Coil never ships** — they fix duplication being paid
for today. Step 3 has the largest payoff.

---

## 9. Risks

| Risk                                         | Severity | Mitigation                                                                      |
| -------------------------------------------- | -------- | ------------------------------------------------------------------------------- |
| I2S driver replacement breaks audio          | High     | Step 1 alone, on current structure, scope-verified before anything else moves   |
| Alloy Coil memory margin (~56 KB) too thin       | Medium   | 4.0 s echo instead of 5.0 s buys ~47 KB                                         |
| Q15 noise buildup at feedback > 1.0          | Medium   | Clamp + error-feedback noise shaping; A/B in VCV at max feedback                |
| AA filter placement wrong → audible aliasing | Medium   | Swept sine + spectrum analyser in VCV, before hardware                          |
| AlloyFlux at 48 kHz exceeds CPU budget       | Medium   | §7 — measure `gAudioElapsedUs` first; default is to stay at 32768               |
| PCM5102A zero-sample click                   | Low      | §4 — test at digital silence; workaround is to emit 1 instead of 0              |
| Codegen scope creep in step 3                | Low      | Generate the four highest-cost sites first; hand-maintain the rest until proven |

---

## 10. Notes for later

- If a PSRAM revision ever happens (APS6404 on QSPI CS1), Alloy Coil's echo can return to float
  @48 kHz with a much longer maximum. Keep decimation factor and storage type as
  compile-time constants so that is a one-line change.
- The platform/module boundary is reusable for any future third-party engine port. If this
  works, it is the beginning of a Voltage Foundry firmware platform rather than a one-off.
- Panel: upstream Alloy Coil has a distinctive ring-of-circles around the feedback knob. Worth
  an homage in the copper-on-dark metallurgical language rather than a copy — and worth
  crediting Synthux prominently given we would be shipping their DSP.
