# Alloy Coil — vendored engine

The feedback-resonator engine from
[Synthux Academy's Audrey II](https://github.com/Synthux-Academy/Audrey-II),
vendored as the Alloy platform's second module.

**The module is called Alloy Coil, not Audrey II.** The port was made with the
original author's blessing, and carries its own name at their request so that
support questions and bug reports reach whoever owns the code in front of the
user. "Audrey II" below always means *upstream*.

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

Two files here are **not** upstream's but exist to restore upstream behaviour
the port had dropped — see "Voicing parity" below:

| File | Notes |
| ---- | ----- |
| `OutputStage.h` | the peak limiter between the engine and the DAC |
| `ControlSmoother.h` | upstream's per-parameter glide, without its registry |

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

  ⚠ Leaving the *containers* behind was right; leaving the **smoothing** behind
  with them was not, and it was not noticed until the M63i parity audit. Those
  two files carried a per-parameter glide time that is part of how the
  instrument responds, and without them every parameter but two reached the
  engine as a step. `ControlSmoother.h` puts the coefficients back — upstream's
  times, verbatim — without the registry around them. See "Voicing parity"
  below.
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

## Voicing parity (M63i)

M63e–g got the engine running on the part. A later audit compared it against
upstream line by line, and found the DSP core exact but three departures at the
edges that were never decisions — they were things that fell out during porting.
All three are now closed.

The **engine itself was already right**: `KarplusString`, `BiquadFilters` and
`DSPUtils` differ from upstream only in their `#include` lines, and
`Engine::Process()` is identical through the whole loop. What had drifted was
everything around it.

### 1. The output limiter

Upstream's audio callback ends with `daisysp::Limiter::ProcessBlock(out, size,
0.7f)` on both channels. The port replaced it with a bare `SoftClip`, on the
reasoning that "upstream has the same headroom problem; on a Daisy the codec
clips it just as hard" — which is not true, because the limiter is between.

`Limiter` applies 0.7 pre-gain **and** 0.7 post-gain, 0.49 static, with
peak-tracked reduction on top that only engages once `|x · 0.7|` exceeds 1. That
0.49 is what keeps ordinary levels inside `SoftLimit`'s linear region:

| engine sample | `Limiter(0.7)` | bare `SoftClip` | Δ |
| ------------- | -------------- | --------------- | - |
| 0.20 | 0.0977 | 0.1977 | +6.1 dB |
| 1.00 | 0.4577 | 0.7778 | +4.6 dB |
| 1.34 | 0.5833 | 0.8940 | +3.7 dB |

So it was not only ~6 dB hot. At full scale `SoftLimit` was shaping *every* loud
sample — 3.4 dB of compression between 0.20 and 1.34, against upstream's 1.0 dB
— which on an instrument that is already a distorting feedback box read as grit
Audrey II does not have. Now in `OutputStage.h`, shared by the firmware, the
VCV module and the host harness. `-DCOIL_OUTPUT_LIMITER=0` restores the
SoftClip for comparison.

### 2. Control smoothing

Upstream runs every parameter through a one-pole with a per-parameter t60, at
its 12 kHz block rate. Dropping `ParameterRegistry`/`SmoothedValue` dropped
those coefficients, and only two parameters — feedback delay and echo time —
smooth inside the engine. The other ten stepped: pitch, which is a
Karplus-Strong delay length; both filter cutoffs, which re-solve five biquad
coefficients on every write; and every gain in the module.

`ControlSmoother.h` restores upstream's times verbatim. The subtlety is *where*
it runs: a one-pole cannot glide faster than its update rate, and this module's
control tick is 128 Hz, where a 7.8 ms period is longer than the 7.2 ms tau a
50 ms glide asks for — `onepole_coef()` clamps to 1.0 and the smoother
degenerates into the step it exists to remove. Nine of the twelve parameters use
that 50 ms default. So the goals are still read at 128 Hz, which is an I/O rate,
and the interpolation runs once per 32 frames (1500 Hz) in the audio path.

That move has a second benefit: every write to engine state now happens on the
audio core. `SetFeedbackLPFCutoff()` was being called from the control core
while `Process()` read the coefficients it was writing.

#### What it cost, and what that forced

Raising the push rate from 128 Hz to 1500 Hz is 12× the setter calls, and four
of the twelve setters end in a transcendental — `mtof`→`powf` for pitch,
`pow10f`→`expf` for feedback gain, and `tanf` for each filter cutoff. **On the
RP2350 those cost roughly 90 µs each**, which is about an order of magnitude
worse than a host benchmark suggests: x86 has them in hardware, the M33 does
not. The first hardware build was unplayable:

| | block time (budget 666 µs) | headroom | |
| - | - | - | - |
| all twelve setters, every step | **942 µs** | −41 % | DMA underruns, no usable audio |
| deadband, settled patch | **578 µs** | +13 % | clean |

Two mechanisms make it affordable, both in `ControlSmoother`:

- **A per-parameter deadband.** A one-pole approaches its goal asymptotically
  and never arrives, so without one every setter fires forever on a patch
  nobody is touching. With it, a settled patch pushes **0 of 12** and a single
  knob sweep pushes **1 of 12**, going quiet ~72 ms after the knob stops. Every
  threshold is well under its parameter's JND.
- **One costly setter per step, round-robin.** 13 % of headroom is ~88 µs,
  which fits one 90 µs call and not four. With several knobs moving together
  each costly parameter still reaches the engine at 375 Hz — three times the
  old control tick — and the worst-case block is bounded.

`Snap()` bypasses both, because a preset recall has to land completely.

The serial console reports `set/step` in the `cpu` line for exactly this: it
should read 0 on a settled patch. A steady non-zero reading means a deadband is
too tight and the smoother is burning transcendentals re-sending values the
engine already has.

### 3. Two knob curves

`fbbody` and `echotime` had become `log` where upstream uses `Mapping::EXP`
(`min + t² · range`, which is the manifest's `skew: 2.0`). Both endpoints agreed
so nothing was unreachable, but the middle of the travel — which is where this
instrument lives — landed somewhere else: body's midpoint at 10 ms/100 Hz
against upstream's 25.8 ms/38.8 Hz, and echo time's at 0.45 s against 1.29 s.
Restored in `params.json`, with the reasoning and a one-word revert recorded
there.

**Not** changed, because `params.json` shows each was considered and voiced
deliberately: the feedback gain floor at −30 dB rather than −60 (a loop gain of
0.032 decays instantly, so nothing audible is out of reach), echo feedback
capped at 1.2 rather than 1.5, reverb decay's `skew: 0.5` rather than upstream's
`ftension(−3)`, and the 4 s echo ceiling.

#### The curve change had two hand-written mirrors, and both were stale

`params.json` was the source of truth, but only two of its four consumers were
generated. The audit that found the curves also had to find every number
somebody had worked out by hand from the *old* ones:

- **`vcv/AlloyCoil.cpp` knob defaults.** `configParam()` takes a knob
  *position*, so each default was the inverse of that knob's curve. Echo time's
  `0.5261f` was the inverse of the log curve, and against square-law it put the
  module's boot echo at **1.14 s instead of 0.5 s** — in the build that is
  supposed to be the hardware's A/B reference. Feedback HPF was separately 1.5 %
  off (253.7 Hz for a 250 Hz default), predating M63i. Both are gone: the
  defaults now come from `ParamDescriptor::toPos()` on the manifest row.
- **The Alloy Controller's slider travel.** `ParamSlider.svelte` drove the range
  input in normalized travel for `scale: "log"` but not for `skew`, so six of
  Alloy Coil's knobs were linear-in-value on screen and curved everywhere else.
  The value on the wire was always right — `floatToCC()` applies the skew — but
  the *feel* was not: echo time's 0.05–0.5 s occupied 11 % of the slider against
  34 % of the knob.

The `default` column had two more mirrors that the curve audit did not cover:
the initialisers on the parameter globals — typed out once per binary in
`src/main.cpp`, `vcv/AlloyCoil.cpp` and `test/params_check.cpp` — and the
factory-reset table in `src/config_store.cpp`. Four copies of twelve numbers,
and editing `params.json` moved none of them, so a changed default reached the
CC range and the web slider while every boot value stayed behind. All four are
generated now: `"globals_output": true` emits
`include/param_globals.generated.h`, which *defines* the globals initialised to
`default` (include it in one TU per binary), and `applyParamDefaults()` in the
manifest applies the same column at runtime for factory reset. Changing a
default is a one-line `params.json` edit plus `make params`.

**`make coil-params`** now walks every knob across its travel and asserts
`IOBridge.h` lands on exactly what `ParamDescriptor::fromPos()` gives for the
same position, then checks each default round-trips. It is the guard that would
have caught the echo-time default, and it is worth running on any params.json
edit. All twelve currently agree to 0.0e+00.

## Status: it fits

At the default build — echo 4 s, decimated ÷4, `int16` storage — the engine is
**443.1 KiB**, against a measured budget of roughly **466 KiB**. See "Making it
fit" below for how that was arrived at and what the levers are.

```text
  build: echo 4 s, decimation /4, int16 storage, shaping off
  EchoDelay<4s>      x2 =   192384 B  (  187.9 KiB)
  daisysp::ReverbSc     =    99384 B  (   97.1 KiB)
  DelayLine<f,12000> x2 =    96048 B  (   93.8 KiB)
  KarplusString      x2 =    65744 B  (   64.2 KiB)
  sizeof(Engine)        =   453752 B  (  443.1 KiB)
  10 s @ 48000 Hz: peak 0.5652  DC L -0.000020  R -0.000017  non-finite 0
  clipped at the DAC: 0 of 960000 samples (0.00%)
```

That peak was **1.3360** before M63i, and the harness was reporting the raw
engine rather than the module's output. It is the limiter, not a quieter engine:
the same build with `-DCOIL_OUTPUT_LIMITER=0` peaks at 0.8802.

## Making it fit

### The budget

AlloyFlux's firmware links at 356 664 B of RAM, of which `sizeof(SynthEngine)`
is 325 792 B — so everything else the platform needs (USB, serial console,
LEDs, config, I²S DMA buffers, parameter tables, globals) is **30 872 B**.
Alloy Coil's image needs the same infrastructure. Allowing ~16 KB for stacks and
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

### Reduction 1 — `reverbsc`, 386.9 → 97.1 KiB, free

This turned out not to be a trade-off at all but an upstream **units bug**:
`Init()` accumulated a byte count and used it to offset a `float*`, striding 4×
too far and forcing `DSY_REVERBSC_MAX_SIZE` to be 4× the real requirement.
Output is bit-identical after the fix — the harness reports the same peak and
DC to every digit it prints. Details in
[`vendor/daisysp/README.md`](../../vendor/daisysp/README.md).

The pool is now sized from `DSY_REVERBSC_MAX_SRATE` rather than hard-coded,
because the fixed 24 800 floats was a 48 kHz figure and the VCV plugin re-inits
at whatever rate Rack is set to. Above ~48.1 kHz only four of the eight delay
lines fitted and the other four were left holding indeterminate pointers. The
firmware is unaffected — it is pinned to 48 kHz — but the plugin now builds with
a 192 kHz pool, and overflow fails to silence instead of to undefined behaviour.

### Reduction 2 — echo decimation and `int16`, 1875 → 188 KiB

The real trade-off. `EchoDelay` now runs its loop at `fs/N` (default ÷4, so
12 kHz) and stores samples as Q15. Three compile-time switches, so the
trade-offs can be compared by ear rather than argued about:

| Switch | Default | Effect |
| ------ | ------- | ------ |
| `COIL_ECHO_DECIMATION` | 4 | echo loop rate = `fs / N` |
| `COIL_ECHO_Q15` | 1 | `int16` storage; 0 = float |
| `COIL_ECHO_NOISE_SHAPE` | 0 | first-order error feedback on the quantiser |
| `COIL_ECHO_ANTIALIAS` | 1 | the filter ahead of the decimator — measurement only |
| `COIL_ECHO_MAX_S` | 4 | maximum echo time, seconds |

`-DCOIL_ECHO_DECIMATION=1 -DCOIL_ECHO_Q15=0` restores upstream behaviour
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

| `COIL_ECHO_MAX_S` | `sizeof(Engine)` | Margin vs ~466 KiB |
| ------------------- | ---------------- | ------------------ |
| 5 | 490.2 KiB | **−24 KiB — does not fit** |
| **4 (default)** | **443.3 KiB** | **+23 KiB** |
| 3 | 396.4 KiB | +70 KiB |
| 2 | 349.6 KiB | +116 KiB |

4 s is the default because it keeps most of upstream's range while leaving real
margin. Drop to 3 s if the firmware build comes in tighter than the estimate.

### What the host harness does and does not prove

`make coil-host` runs 10 s at unity feedback gain with echo feedback at 1.05
and checks for NaN, silence and DC drift. All four switch combinations pass,
with peaks within 0.6 % of each other — so decimation and quantisation are not
changing gross behaviour or destabilising the loop.

It does **not** discriminate noise shaping on from off: the two produce
identical peak and DC figures, because shaping moves quantisation noise in
frequency without changing the signal's peak or mean, and the difference sits
around −90 dBFS — below what a 4-decimal peak and a 6-decimal DC mean can
resolve. The same goes for the anti-alias filter.

That is what **`make coil-ab`** is for, and it has now been run — see
`modules/alloycoil/test/echo_ab.cpp` and milestone 63f-ab. Both risks came back
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
| `daisysp::ReverbSc` | 386.9 KiB | 97.1 KiB |
| `DelayLine<float,12000>` ×2 | 93.8 KiB | 93.8 KiB |
| `KarplusString` ×2 | 64.2 KiB | 64.2 KiB |
| **`sizeof(Engine)`** | **2420.2 KiB** | **443.1 KiB** |

The feedback delay lines and the Karplus-Strong strings are untouched: they sit
inside the resonator loop and are the core of the sound, so they stay float and
full-rate.

## Still to do

Integration, the VCV A/B and the budget check are all done — the Alloy Coil image
links at 477 328 B (91.0 % of the part) with `COIL_ECHO_MAX_S=4`, so the
inferred 466 KiB budget held and `-DCOIL_ECHO_MAX_S=3` stays in reserve rather
than being needed. What remains is hardware, not DSP:

- **No `IHardwareIO` implementation.** Alloy Coil's firmware drives its parameters
  from MIDI, SysEx and the serial console only; `io/PanelMap.h` and
  `io/IOBridge.h` are live in the VCV build and ready for the knobs, CV, buttons
  and LEDs whenever the multiplexed ADC driver lands.
- **Audio-rate exciter on hardware.** `gExciterIn` is written at the 128 Hz
  control tick, so the jack is a control voltage there and a true audio input in
  Rack. FM IN is on a dedicated direct ADC pin for exactly this, so it is a
  firmware job rather than a board change.
- **Long-run stability on hardware.** The host harness covers 10 s; the
  milestone asks for 10 minutes at maximum feedback on a real board.

Two notes kept from the platform design document, which has since been retired:

- **If a PSRAM revision ever happens** (APS6404 on QSPI CS1), the echo can go
  back to float at 48 kHz with a much longer maximum. The decimation factor and
  the storage type are compile-time constants precisely so that stays a one-line
  change — see [Reduction 2](#reduction-2--echo-decimation-and-int16-1875--188-kib).
- **The panel deserves an homage, not a copy.** Upstream Audrey II has a
  distinctive ring of circles around the feedback knob. Worth reinterpreting in
  the copper-on-dark metallurgical language rather than reproducing — and worth
  crediting Synthux prominently on the panel itself, given the module ships
  their DSP.
