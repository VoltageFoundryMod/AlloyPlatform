# DaisySP — vendored subset

Thirteen files taken from [DaisySP](https://github.com/electro-smith/DaisySP),
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

`custom_dsp.h` is referenced by `dsp.h` but sits behind `#ifdef DSY_CUSTOM_DSP`,
which is not defined here, so it is not vendored.

## Local patches

None yet. Record every one here as it lands, so re-vendoring from a newer
upstream is a matter of re-applying a known list rather than a diff hunt.

## Licensing

DaisySP is **MIT** (Electrosmith Corp., 2020) — see `LICENSE`, preserved
verbatim.

`reverbsc` carries a longer lineage, and its attribution block at the top of
`reverbsc.h` is kept exactly as upstream ships it:

> Reverb SC: ported from csound/soundpipe. Original author(s): Sean Costello,
> Istvan Varga (1999, 2005). Ported to soundpipe by Paul Batchelor. Ported by
> Stephen Hensley.

Csound is LGPL-2.1 and soundpipe is MIT; Electrosmith distributes the result
under DaisySP's MIT licence. This project is GPL-3.0, which absorbs MIT and
LGPL-2.1 alike, so the combination is fine on any reading of that chain — but
the attribution block must not be dropped, whichever reading applies.
