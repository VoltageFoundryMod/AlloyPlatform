# Audrey II — vendored engine

The feedback-resonator engine from
[Synthux Academy's Audrey II](https://github.com/Synthux-Academy/Audrey-II),
vendored as the Alloy platform's second module.

| Upstream | Commit |
| -------- | ------ |
| `Synthux-Academy/Audrey-II` | `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3` |

⚠️ The repository was `Synthux-Academy/audrey-ii-simple` when this was vendored
and has since been renamed to `Audrey-II`; the old URL now 404s. The commit hash
is the thing that actually pins what was taken.

**Original work by [Nick Donaldson](https://github.com/ndonald2) (concept and
firmware) and [Roey Tsemah](https://github.com/roeytsemah) (visual and hardware
design)** — see `CREDITS.md` and `LICENSE`, both preserved verbatim. Upstream is
MIT; this repository is GPL-3.0, which absorbs it.

## What was taken

| File | Notes |
| ---- | ----- |
| `FeedbackSynthEngine.{h,cpp}` | the engine |
| `KarplusString.{h,cpp}` | resonator — itself a modified DaisySP class |
| `BiquadFilters.{h,cpp}` | feedback-loop LPF/HPF |
| `EchoDelay.h` | tape-ish echo |
| `DSPUtils.h` | dB/linear, one-pole coefficients |

## What was left behind, and why

- **`FeedbackSynth_main.cpp`, `Makefile`, `.vscode/`** — Daisy Seed application
  scaffolding. The platform provides all of it.
- **`FeedbackSynthControls.{h,cpp}`** — includes `<daisy.h>` / `<daisy_seed.h>`
  and binds knobs to Daisy hardware. M63f replaces it with a `params.json`
  manifest, the same one AlloyFlux uses.
- **`ParameterRegistry.h` + `SmoothedValue.h`** — only `FeedbackSynthControls`
  used them. They were the inspiration for the declarative manifest built in
  M63c; keeping a second, unused registry built on `std::function` and
  `std::unordered_map` would be carrying exactly the weight this step exists to
  shed.
- **`Source/memory/sdram_alloc.{h,cpp}`** — a bump allocator over the Daisy's
  32 MiB external SDRAM. The RP2350 has neither SDRAM nor a heap worth using in
  an audio path.

## Coupling removed

Ten lines, as surveyed in M63e planning, and all of it mechanical:

- `SDRAM::allocate<T>()` ×3 and the `std::unique_ptr` members that held the
  results → concrete members. The footprint becomes a link-time fact instead of
  a run-time surprise, which on a 520 KB part is the difference between a build
  error and a boot failure.
- `#ifdef __arm__` → `#include <dev/sdram.h>` — gone with the allocator.
- `#include <arm_math.h>` — dead already; the only code that used it was
  commented out.
- `DSPUtils::tanf()`'s `__arm__` fork — both branches called the same function.
- `<daisysp.h>` umbrella → the specific vendored headers, so nothing drags in a
  DaisySP module the engine never touches.

## Status: it fits

At the default build — echo 4 s, decimated ÷4, `int16` storage — the engine is
**443.3 KiB**, against a measured budget of roughly **466 KiB**. See "Making it
fit" below for how that was arrived at and what the levers are.

```text
  build: echo 4 s, decimation /4, int16 storage, shaping on
  EchoDelay<4s>      x2 =   192384 B  (  187.9 KiB)
  daisysp::ReverbSc     =    99624 B  (   97.3 KiB)
  DelayLine<f,12000> x2 =    96048 B  (   93.8 KiB)
  KarplusString      x2 =    65744 B  (   64.2 KiB)
  sizeof(Engine)        =   453960 B  (  443.3 KiB)
  10 s @ 48000 Hz: peak 1.3360  DC L -0.000030  R +0.000047  non-finite 0
```

## Making it fit

### The budget

AlloyFlux's firmware links at 356 664 B of RAM, of which `sizeof(SynthEngine)`
is 325 792 B — so everything else the platform needs (USB, serial console,
LEDs, config, I²S DMA buffers, parameter tables, globals) is **30 872 B**.
Audrey's image needs the same infrastructure. Allowing ~16 KB for stacks and
headroom on a 524 288 B part leaves roughly **466 KiB for the engine**.

### Where it started

```text
  EchoDelay<5s>      x2 =  1920192 B  ( 1875.2 KiB)
  daisysp::ReverbSc     =   396168 B  (  386.9 KiB)
  DelayLine<f,12000> x2 =    96048 B  (   93.8 KiB)
  KarplusString      x2 =    65744 B  (   64.2 KiB)
  sizeof(Engine)        =  2478312 B  ( 2420.2 KiB)
```

2.36 MiB against a 520 KB part — 4.8× the whole chip. Not a defect: upstream
runs on a Daisy Seed, which pushes all three big members into 64 MiB of
external SDRAM, so nothing upstream ever had to be small.

### Reduction 1 — `reverbsc`, 386.9 → 97.3 KiB, free

This turned out not to be a trade-off at all but an upstream **units bug**:
`Init()` accumulated a byte count and used it to offset a `float*`, striding 4×
too far and forcing `DSY_REVERBSC_MAX_SIZE` to be 4× the real requirement.
Output is bit-identical after the fix — the harness reports the same peak and
DC to every digit it prints. Details in
[`vendor/daisysp/README.md`](../../vendor/daisysp/README.md).

### Reduction 2 — echo decimation and `int16`, 1875 → 188 KiB

The real trade-off. `EchoDelay` now runs its loop at `fs/N` (default ÷4, so
12 kHz) and stores samples as Q15. Three compile-time switches, so the
trade-offs can be compared by ear rather than argued about:

| Switch | Default | Effect |
| ------ | ------- | ------ |
| `AUDREY_ECHO_DECIMATION` | 4 | echo loop rate = `fs / N` |
| `AUDREY_ECHO_Q15` | 1 | `int16` storage; 0 = float |
| `AUDREY_ECHO_NOISE_SHAPE` | 0 | first-order error feedback on the quantiser |
| `AUDREY_ECHO_ANTIALIAS` | 1 | the filter ahead of the decimator — measurement only |
| `AUDREY_ECHO_MAX_S` | 4 | maximum echo time, seconds |

`-DAUDREY_ECHO_DECIMATION=1 -DAUDREY_ECHO_Q15=0` restores upstream behaviour
exactly.

Both reductions carry real DSP risk, and the mitigations are the substance:

- **Aliasing.** The send is tapped *post-reverb* and is full-bandwidth — the
  feedback loop's own LPF sits at 18 kHz — so decimating it raw would fold
  6–18 kHz down into the audible band. Some of that lands *low* (11 kHz folds
  to 1 kHz) where the echo's 800 Hz bandpass cannot help. A 24 dB/oct LPF at
  0.25 × the decimated rate now sits ahead of the decimator, and the return is
  linearly interpolated back up.
- **Quantisation noise.** Feedback is deliberately allowed past unity here, so
  anything the quantiser adds is recirculated and amplified rather than
  decaying. The value is clamped before quantising (`SoftClip` bounds the loop
  output, but `out * feedback + in` is a sum of two unbounded terms); samples
  are converted to float *before* interpolation; and the quantiser carries its
  error forward.

### The remaining lever

Echo maximum time, at ~11.7 KiB per second of stereo echo:

| `AUDREY_ECHO_MAX_S` | `sizeof(Engine)` | Margin vs ~466 KiB |
| ------------------- | ---------------- | ------------------ |
| 5 | 490.2 KiB | **−24 KiB — does not fit** |
| **4 (default)** | **443.3 KiB** | **+23 KiB** |
| 3 | 396.4 KiB | +70 KiB |
| 2 | 349.6 KiB | +116 KiB |

4 s is the default because it keeps most of upstream's range while leaving real
margin. Drop to 3 s if the firmware build comes in tighter than the estimate.

### What the host harness does and does not prove

`make audrey-host` runs 10 s at unity feedback gain with echo feedback at 1.05
and checks for NaN, silence and DC drift. All four switch combinations pass,
with peaks within 0.6 % of each other — so decimation and quantisation are not
changing gross behaviour or destabilising the loop.

It does **not** discriminate noise shaping on from off: the two produce
identical peak and DC figures, because shaping moves quantisation noise in
frequency without changing the signal's peak or mean, and the difference sits
around −90 dBFS — below what a 4-decimal peak and a 6-decimal DC mean can
resolve. The same goes for the anti-alias filter.

That is what **`make audrey-ab`** is for, and it has now been run — see
`modules/audrey/test/echo_ab.cpp` and milestone 63f-ab. Both risks came back
clean: the anti-alias filter buys 19–47 dB of fold-down rejection and is
load-bearing (without it, 11 kHz folds to 1 kHz at −3 dB), and `int16` storage
costs 0.0–0.1 dB of SNR because the floor is set by `SoftClip`'s
intermodulation rather than by the quantiser. Two defaults changed as a result:
noise shaping off, and the anti-alias cutoff from 0.35 to 0.25 × the decimated
rate.

## Original footprint, for reference

| Member | Vendored (M63e) | Now |
| ------ | --------------- | --- |
| `EchoDelay` ×2 | 1875.2 KiB | 187.9 KiB |
| `daisysp::ReverbSc` | 386.9 KiB | 97.3 KiB |
| `DelayLine<float,12000>` ×2 | 93.8 KiB | 93.8 KiB |
| `KarplusString` ×2 | 64.2 KiB | 64.2 KiB |
| **`sizeof(Engine)`** | **2420.2 KiB** | **443.3 KiB** |

The feedback delay lines and the Karplus-Strong strings are untouched: they sit
inside the resonator loop and are the core of the sound, so they stay float and
full-rate.

## Still to do

Integration, the VCV A/B and the budget check are all done — the Audrey image
links at 477 396 B (91.1 % of the part) with `AUDREY_ECHO_MAX_S=4`, so the
inferred 466 KiB budget held and `-DAUDREY_ECHO_MAX_S=3` stays in reserve rather
than being needed. What remains is hardware, not DSP:

- **No `IHardwareIO` implementation.** Audrey's firmware drives its parameters
  from MIDI, SysEx and the serial console only; `io/PanelMap.h` and
  `io/IOBridge.h` are live in the VCV build and ready for the knobs, CV, buttons
  and LEDs whenever the multiplexed ADC driver lands.
- **Audio-rate exciter on hardware.** `gExciterIn` is written at the 128 Hz
  control tick, so the jack is a control voltage there and a true audio input in
  Rack. FM IN is on a dedicated direct ADC pin for exactly this, so it is a
  firmware job rather than a board change.
- **Long-run stability on hardware.** The host harness covers 10 s; the
  milestone asks for 10 minutes at maximum feedback on a real board.
