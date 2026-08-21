# Licensing

Not one licence but four, scoped by directory. The intent is the one Mutable
Instruments settled on: **everything needed to build, study, fork and improve
the module is open; the names and the artwork are not.** Fork the module; draw
your own front.

| What | Where | Licence |
| ---- | ----- | ------- |
| Firmware, VCV plugin, Alloy Controller, tooling | everywhere else | **GPL-3.0-or-later** — [`LICENSE`](LICENSE) |
| Hardware design | [`hardware/`](hardware/) | **CERN-OHL-S v2** — [`hardware/LICENSE`](hardware/LICENSE) |
| Panel artwork | [`panel-src/`](panel-src/), [`vcv-plugin/res/`](vcv-plugin/res/) | **CC BY-NC-ND 4.0** — [`panel-src/LICENSE`](panel-src/LICENSE) |
| Names, marks, logos | — | **not licensed.** See below. |

The three source licences are all copyleft: take the firmware and you owe your
changes back, take the board and you owe your changes back. What you cannot do
is ship something that looks like this module and carries its name.

Third-party code keeps its own licence and its own notices; the rest of this
file records what those are and why the combination works.

## What is in here, and under what terms

| Component | Origin | Licence | Notice |
| --------- | ------ | ------- | ------ |
| Alloy Platform, Alloy Flux, Alloy Coil, integration, Alloy Controller | this project | **GPL-3.0-or-later** | [`LICENSE`](LICENSE) |
| Schematics, PCB, fabrication files | this project | **CERN-OHL-S v2** | [`hardware/LICENSE`](hardware/LICENSE) |
| Panel drawings and logos | this project | **CC BY-NC-ND 4.0** | [`panel-src/LICENSE`](panel-src/LICENSE) |
| Audrey II engine (as Alloy Coil) | [Synthux Academy](https://github.com/Synthux-Academy/Audrey-II) | MIT | [`modules/alloycoil/LICENSE`](modules/alloycoil/LICENSE), [`CREDITS.md`](modules/alloycoil/CREDITS.md) |
| DaisySP subset (all but `reverbsc`) | [electro-smith/DaisySP](https://github.com/electro-smith/DaisySP) | MIT | [`vendor/daisysp/LICENSE`](vendor/daisysp/LICENSE) |
| `reverbsc.{h,cpp}` | [electro-smith/DaisySP-LGPL](https://github.com/electro-smith/DaisySP-LGPL) | **LGPL-2.1** | [`vendor/daisysp/LICENSE.LGPL-2.1`](vendor/daisysp/LICENSE.LGPL-2.1) |
| `SoftLimit()` in `dsp.h` | pichenettes/stmlib | MIT | attribution in the file |
| VCV Rack API (plugin build) | VCV | GPL-3.0-or-later, plus a plugin exception | Rack SDK |
| arduino-pico core, Pico SDK (firmware build) | earlephilhower / Raspberry Pi | LGPL-2.1 / BSD-3-Clause | not vendored; fetched by PlatformIO |

## Why it all combines

**MIT into GPLv3** — permitted, one-way. MIT code can be incorporated into a
GPLv3 work provided its copyright notice and permission notice travel with it.
Both `modules/alloycoil/LICENSE` and `vendor/daisysp/LICENSE` are preserved
verbatim for exactly this reason, and `CREDITS.md` names the original authors.
Audrey II remains MIT upstream; only *this distribution* of it is GPLv3.

**LGPL-2.1 into GPLv3** — permitted via LGPL-2.1 §3, which lets any recipient
relicense a copy under "the ordinary GNU General Public License, version 2 or
any later version". This project takes that option for `reverbsc`, so the copy
here is GPL-3.0-or-later. Upstream stays LGPL for everyone else.

⚠ This one is easy to get wrong, and was recorded incorrectly here at first:
**`reverbsc` is not part of MIT DaisySP.** Electrosmith keeps it in a separate
`DaisySP-LGPL` repository, together with `moogladder`, `bitcrush` and `fold`,
specifically so the Csound-derived code is *not* covered by DaisySP's MIT
licence. The main DaisySP repo carries that as a submodule, which is what makes
the whole library look MIT at a glance. See
[`vendor/daisysp/README.md`](vendor/daisysp/README.md).

Taking the §3 route also means the LGPL relinking obligation does not attach to
the combined work — which matters, because the firmware is a statically linked
binary with no dynamic loading. It would have been satisfiable regardless, since
the entire project is open source and buildable from this tree.

**Apache-2.0 into GPLv3** — permitted one-way (GPLv3 only, not GPLv2). Only
relevant to build tooling; nothing Apache-licensed is linked into a build
output.

**GPLv3 firmware linking LGPL-2.1 (arduino-pico) and BSD-3-Clause (Pico SDK)** —
both are GPLv3-compatible. Neither is vendored; PlatformIO fetches them.

## VCV Rack, specifically

Rack is GPL-3.0-or-later and additionally grants a **Non-Commercial Plugin
License Exception**, which lets a plugin distributed free of charge carry any
licence at all. This plugin does not rely on that exception: GPLv3 satisfies
Rack's terms directly, whether the plugin is free or sold through the VCV
Library.

⚠ **The Rack Component Library graphics are a separate question from the code.**
The widgets this plugin instantiates — `ScrewBlack`, `RoundBlackKnob`,
`Davies1900hBlackKnob`, `Trimpot`, `PJ301MPort`, `VCVButton`, `MediumLight` —
draw graphics that are © VCV under **CC BY-NC 4.0**. Non-commercial use is
allowed with credit. Commercial distribution is allowed *only* through the VCV
Library or under a commercial licence from VCV. Relicensing this project as
GPLv3 does not change that, because those assets are not ours to relicense.

If the plugin is ever to be sold outside the VCV Library, those widgets have to
be replaced with original artwork. Nothing else in the plugin is affected.

## Names, artwork and branding

Except where noted below, **all module names, panel artwork, silkscreen layout,
logos, schematics and PCB layouts in this repository are copyright © 2026 Carlos
Eduardo de Paula, trading as Voltage Foundry Modular.** That includes:

- the names **Alloy Platform**, **Alloy Flux** and **Alloy Coil**, and the
  Voltage Foundry Modular name and logo
- the panel artwork in `panel-src/` and `vcv-plugin/res/`, including the
  layout, typography and silkscreen of both modules
- the schematics, board layout and fabrication files under `hardware/`

### What is not

**The Audrey II engine is not ours.** Concept and firmware by Nick Donaldson,
original visual and hardware design by Roey Tsemah, at Synthux Academy. The
engine is MIT and used under that licence with attribution preserved.

**The name "Audrey II" is not ours either, and Alloy Coil does not carry it.**
The port was made with the original author's blessing, on the understanding
that it ship under its own name so that questions, bug reports and support land
with whoever actually owns the code in front of the user. Alloy Coil is that
name: ours, along with its panel artwork, which is the Alloy Platform layout
with new labels rather than a reproduction of the Synthux panel. Report Alloy
Coil issues here; report Audrey II issues upstream.

**Rack Component Library graphics are VCV's** — see the section above. © VCV,
CC BY-NC 4.0, and not ours to relicense or assert over.

**Third-party code** keeps its own copyright as listed in the table above.

### How the carve-out actually works

Asserting copyright is not the same as reserving rights — a notice records who
made something, a licence says what others may do with it. So the reservation is
made by **scope**, not by a warning:

- The root `LICENSE` grants GPL-3.0-or-later over the **source code**. It is not
  a grant over every file that happens to sit in the repository.
- `hardware/LICENSE`, `panel-src/LICENSE` and `vcv-plugin/res/LICENSE` each say,
  in their own directory, that the directory is outside that grant and state the
  licence that does apply.

This is not "adding restrictions to the GPL", which GPLv3 §7 would forbid. The
artwork was never part of the GPL'd work: it is a set of data files loaded at
runtime, authored by the same copyright holder, licensed separately. A licensor
who owns both may scope each grant as they choose.

**CC BY-NC-ND rather than all rights reserved** for the artwork, deliberately.
Reserving everything would mean a fork could not legally ship a working plugin
at all, which would make the GPL on the code close to meaningless in practice.
BY-NC-ND lets the panels travel with the project so anyone can build and
redistribute it, while forbidding modified panels and commercial use — which is
the part that stops a clone.

**Names are not covered by any of the above.** "Alloy Platform", "Alloy Flux"
and "Voltage Foundry Modular" are marks, and none of the three licences grants
any right to use them. CERN-OHL-S v2 is explicit about it — **§8.2**: "You shall
not use any of the name (including acronyms and abbreviations), image, or logo
by which the Licensor ... is known" — and CC BY-NC-ND §2(b) likewise conveys no
trademark rights. A fork must rename.

### What this does not do

It does not stop someone building this module for themselves, selling boards
they have fabricated from `hardware/` under CERN-OHL-S (they must publish their
changes and may not use the artwork or the name), or writing a compatible
firmware. That is the deal copyleft makes, and taking it is the point.

## Does GPLv3 actually protect the code here?

Partly, and it is worth being precise about where the limit is.

GPLv3 obliges anyone who **distributes** the firmware, the plugin or a derived
work to offer complete corresponding source under the same terms. For a
Eurorack module that is meaningful: shipping a binary on a module you sell
carries the obligation with it, so a competitor cannot take the firmware, modify
it and ship a closed product.

What it does **not** cover: the panel artwork, the PCB layout as a physical
object, and the module's name and branding. Copyleft applies to the source and
its derived works, not to a manufactured object built from published design
files. If protecting the hardware design specifically matters, that is a
separate licence choice for `hardware/` — CERN-OHL-S is the copyleft equivalent
for hardware and is the usual companion to GPLv3 firmware. Right now
`hardware/` inherits the root GPLv3, which is a defensible but unusual choice
for board files.

## Adding a file

New source files should carry an SPDX identifier:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
```

Vendored third-party files keep their own headers untouched, and any local patch
is recorded in that vendor directory's `README.md`.
