# Audrey II — vendored engine

The feedback-resonator engine from
[Synthux Academy's Audrey II](https://github.com/Synthux-Academy/audrey-ii-simple),
vendored as the Alloy platform's second module.

| Upstream | Commit |
| -------- | ------ |
| `Synthux-Academy/audrey-ii-simple` | `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3` |

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

## Status: does not fit yet

`make audrey-host` compiles the engine against nothing but the vendored DaisySP
subset and the standard library, runs it for 10 s at unity feedback gain with
echo feedback above 1.0, and prints the static footprint. It is stable — no
NaNs, no DC drift, self-sustaining — and it is **far too big**:

```
  EchoDelay<5s>      x2 =  1920192 B  ( 1875.2 KiB)
  daisysp::ReverbSc     =   396168 B  (  386.9 KiB)
  DelayLine<f,12000> x2 =    96048 B  (   93.8 KiB)
  KarplusString      x2 =    65744 B  (   64.2 KiB)
  sizeof(Engine)        =  2478312 B  ( 2420.2 KiB)
```

2.36 MiB against a 520 KB chip — **4.8× the whole part**, before any platform
overhead. That is not a surprise and not a defect: upstream runs on a Daisy
Seed, which pushes exactly these three members into 64 MiB of external SDRAM, so
nothing upstream ever had to be small.

Shrinking it is M63f's job, and the numbers above say where the work is:

| Member | Now | Plan | After |
| ------ | --- | ---- | ----- |
| Echo ×2 | 1875 KiB | decimate ÷4 to 12 kHz, store `int16` | ~234 KiB |
| `ReverbSc` | 387 KiB | resize `aux_[98936]` → 25 600 floats | ~100 KiB |
| Feedback delay ×2 | 94 KiB | keep | 94 KiB |
| `KarplusString` ×2 | 64 KiB | keep | 64 KiB |

≈ 492 KiB, which is still over budget once the platform's own ~60 KB is counted
— so M63f will need one more lever, and the cheapest is the echo's maximum time:
4.0 s instead of 5.0 s takes another ~47 KiB off, and 3.0 s takes ~94 KiB.

Note also that the ÷4 decimation and the `int16` storage are the two changes
that carry real DSP risk (anti-alias filtering on the echo send, and Q15
quantization noise regenerating at feedback > 1.0). Both are meant to be
A/B-tested in VCV before the firmware is built, which is why M63f says to expose
them as compile-time switches.
