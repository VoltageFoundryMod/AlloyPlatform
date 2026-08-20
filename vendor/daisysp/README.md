# DaisySP — vendored subset

Fifteen files taken from [DaisySP](https://github.com/electro-smith/DaisySP),
the only ones the Audrey II engine reaches for. The library builds for RP2350
unmodified — a scan of the whole of upstream `Source/` finds **no hardware
includes at all**, so nothing here needed porting, only selecting.

| Upstream | Commit |
| -------- | ------ |
| `electro-smith/DaisySP` | `7f67768eb22103e5f7743e709e090ac435a11fae` (`v0.0.1-24-g7f67768`) |

## Layout

Upstream keeps these under `Source/Utility/`, `Source/Effects/` and so on, and
its build puts every one of those directories on the include path — which is why
`overdrive.cpp` can say `#include "dsp.h"`. The subset is **flattened** here so
those includes still resolve against a single `-Ivendor/daisysp`, with no
`daisysp.h` umbrella header and no unused module pulled in behind it.

The flattening is also an admission: this is a *patched* copy, not a mirror.
`reverbsc.h` declares `float aux_[98936]` — 386 KiB as a single member, which is
fine on a Daisy with SDRAM and impossible on a 520 KB RP2350 — so it has to be
resized locally. Diffing against upstream by path was never going to be the way
this gets maintained; the commit above plus the patch list below is.

## Files

| File | Used by |
| ---- | ------- |
| `dsp.h` | everything — `fclamp`, `fmax`, `fmin`, `fmap`, `fonepole`, `mtof`, `pow10f`, `fastlog10f`, `SoftClip`, `Mapping` |
| `delayline.h` | feedback delay lines, echo, Karplus-Strong string |
| `dcblock.h/.cpp` | Karplus-Strong string |
| `whitenoise.h` | engine excitation |
| `tone.h/.cpp` | Karplus-Strong damping filter |
| `crossfade.h/.cpp` | Karplus-Strong |
| `overdrive.h/.cpp` | resonator feedback loop |
| `reverbsc.h/.cpp` | reverb |
| `limiter.h/.cpp` | the module's output edge — see `OutputStage.h` |

`custom_dsp.h` is referenced by `dsp.h` but sits behind `#ifdef DSY_CUSTOM_DSP`,
which is not defined here, so it is not vendored.

## Local patches

Record every one here as it lands, so re-vendoring from a newer upstream is a
matter of re-applying a known list rather than a diff hunt.

### 1. `reverbsc` — byte/float units bug in `Init()` (M63f)

Upstream accumulates `DelayLineBytesAlloc()`, a **byte** count, and uses it to
offset `aux_`, which is `float*`:

```c
delay_lines_[i].buf = (aux_) + n_bytes;      // strides by n_bytes FLOATS
n_bytes += DelayLineBytesAlloc(sr, 1, i);    // returns samples * sizeof(float)
```

Pointer arithmetic scales by `sizeof(float)`, so every delay line was placed
four times further along the pool than it needed to be, and
`DSY_REVERBSC_MAX_SIZE` had to be four times the real requirement to
compensate. Nothing ever read the wasted space, so **output is unaffected** —
it simply cost 4× the RAM.

Fixed to accumulate samples. The eight lines need 24 726 samples at 48 kHz
(2543 + 2842 + 3325 + 3605 + 3977 + 4202 + 2251 + 1981, from
`DelayLineMaxSamples`), against the 98 936 the byte/float mix-up demanded.

**386.9 KiB → 97.1 KiB, with bit-identical output** — confirmed by the host
harness reporting the same peak (1.4019) and DC (+0.000108 / −0.000090) before
and after.

The bound check also moved *before* the write rather than after it, so an
overflow now returns 1 instead of scribbling past the end of `aux_` on the last
line.

`DelayLineBytesAlloc()` was removed — it had no other caller and was the source
of the confusion.

### 2. `reverbsc` — pool sized by sample rate, and a safe failure (M63i)

Patch 1 replaced 98 936 with a hard-coded 24 800, which is right for a module
pinned to 48 kHz and wrong for one that is not. The requirement scales with the
rate and overflows above ~48.1 kHz — and the VCV plugin re-inits the reverb at
whatever rate Rack is set to. At 88.2 kHz or above only four of the eight lines
fit; `Init()` returned 1 and the remaining four kept **indeterminate `buf`
pointers**, which `Process()` then wrote through. Nothing checks `Init()`'s
return value, in this tree or upstream.

Two changes:

- `DSY_REVERBSC_MAX_SIZE` is now derived from `DSY_REVERBSC_MAX_SRATE`
  (default 48000) as a closed-form upper bound on the eight lines. The firmware
  keeps 24 740 floats / 96.6 KiB; `vcv-plugin/Makefile` sets 192000, giving
  98 540 floats / 384.9 KiB, which covers every rate Rack offers.
- Overflow now nulls the un-assigned lines and clears `init_done_`, so
  `Process()` becomes the guarded no-op it already had a check for rather than
  undefined behaviour. `Process()` also writes silence to `out1`/`out2` on that
  path — upstream returns without touching them, and every caller here declares
  them as plain locals, so the caller was reading stack garbage into a mix.

Verified across 44.1/48/88.2/96/176.4/192 kHz: with the firmware's sizing the
first two initialise and produce the same tail as before while the rest fall
back to silence; with the plugin's, all six initialise and ring.

### 3. `limiter` — added, not patched (M63i)

`limiter.h/.cpp` are vendored verbatim. They are not new upstream code, they
are code the port had been missing: Audrey II's audio callback runs both output
channels through `Limiter::ProcessBlock(..., 0.7f)`, and the port had replaced
that with a bare `SoftClip`. See `modules/alloycoil/include/OutputStage.h` for
what the difference sounded like.

## Licensing

**Two licences apply here, not one.**

| Files | Licence | Text |
| ----- | ------- | ---- |
| everything except `reverbsc.*` | MIT (Electrosmith Corp., 2020) | `LICENSE` |
| `reverbsc.h`, `reverbsc.cpp` | **LGPL-2.1** (Electrosmith / Csound lineage) | `LICENSE.LGPL-2.1` |

This was recorded incorrectly at first, so it is worth stating plainly:
`reverbsc` is **not** part of MIT DaisySP. Electrosmith keeps it in a separate
repository, [`electro-smith/DaisySP-LGPL`](https://github.com/electro-smith/DaisySP-LGPL),
alongside `moogladder`, `bitcrush` and `fold` — and the split exists precisely
so that the LGPL-derived code is not covered by DaisySP's MIT licence. The main
`DaisySP` repo carries `DaisySP-LGPL` as a submodule, which is why a casual look
at one commit makes the whole thing appear MIT.

Its attribution block at the top of `reverbsc.h` is kept exactly as upstream
ships it, and must not be dropped:

> Reverb SC: ported from csound/soundpipe. Original author(s): Sean Costello,
> Istvan Varga (1999, 2005). Ported to soundpipe by Paul Batchelor. Ported by
> Stephen Hensley.

### Why the combination is still fine

LGPL-2.1 §3 grants the option of relicensing a copy under "the ordinary GNU
General Public License, version 2 or any later version", and this project
exercises that option: `reverbsc` is used here **as GPL-3.0-or-later**, under
the terms of the repository's root `LICENSE`. That is a one-way door for this
copy only — upstream stays LGPL for everyone else.

Taking that route also means the LGPL's relinking obligation (§6: end users must
be able to swap in a modified LGPL component) does not attach to the combined
work, which matters because the firmware is a statically linked binary with no
dynamic loading. It would have been satisfiable anyway — the whole project is
open source and buildable from this tree — but it is better not to depend on
that.

One further lineage, inside `dsp.h`: `SoftLimit()` is noted as "ported extracted
from pichenettes/stmlib", which is MIT (Émilie Gillet). Absorbed the same way.
