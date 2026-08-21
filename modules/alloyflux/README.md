# Alloy Flux — Dual Relation Oscillator

The Alloy platform's first module, and the one the platform was factored out of.
A stereo synth voice for Eurorack: two oscillators in a defined relationship,
six voice modes, an envelope and VCA, filter, chorus, delay and reverb — playable
from one V/OCT cable and one gate.

**[→ User manual](MANUAL.md)** — panel, voice modes, MIDI CC map, calibration.

| | |
| --- | --- |
| Format | Eurorack, 14 HP |
| Engine | 6 voices, `int32 ±32512` signal path, 48 kHz |
| Firmware | `make firmware MODULE=alloyflux` → `.pio/build/alloyflux/firmware.uf2` |
| VCV | slug `AlloyFlux`, inside the one `AlloyPlatform` plugin |
| Web | `make web MODULE=alloyflux` |
| SysEx ID | `0x41 0x46` (`'A' 'F'`) |
| Engine ID | `0x4146`, config version 8 |

## What lives here

```txt
params.json                     the parameter manifest — one row per parameter
include/param_manifest.generated.h   generated from it; never hand-edit
include/SynthEngine.h           the engine: voices, modes, effects chain
include/dsp/                    ShapeOsc, DattorroReverb, SVFFilter, OTALadder,
                                ChorusEngine, DelayEngine, DriftEngine, …
include/io/PanelMap.h           slot -> name: Pot::ROOT, Cv::VOCT, Led::MODE
include/io/LedEngine.h          the LED language, shared by firmware and VCV
include/io/IOBridge.h           fillSynthParams() — hardware reads -> SynthParams
include/io/HardwarePicoIO.h     IHardwareIO for the RP2350 board
include/alloy_config.h          the preset blob this module writes to flash
src/main.cpp                    platform shim: core split, renderAudio, updateControl
src/module_hooks.cpp            ModuleHooks: notes, action CCs, identity, presets
vcv/AlloyFlux.cpp               the Rack module
```

Everything else — USB MIDI, the SysEx patch protocol, preset slots, the serial
console, the I2S driver, pot takeover — is [`platform/`](../../platform/) and is
shared with [Alloy Coil](../alloycoil/).

## Changing a parameter

Edit [`params.json`](params.json) and run `make params`. That one row supplies the
CC number, range, curve, default, label, category, unit and option bands to both
`param_manifest.generated.h` and the Alloy Controller's parameter map. Commit the
regenerated files; `make params-check` is the CI gate.

Only the config pack/apply in `src/config_store.cpp` and the VCV param list in
`vcv/AlloyFlux.cpp` are still hand-maintained. Bump `kEngineVersion` in
[`include/alloy_config.h`](include/alloy_config.h) whenever the blob's fields
change — old flash data is discarded on mismatch rather than misread.

## Engine notes

**Voice counts differ per mode.** PAIR runs 2 voices, CHORD, CLOUD, STRING and
CASCADE run 4, POLY runs 6. `_activeVoices` in `SynthEngine::control()` is what
the non-FM audio loop iterates; the FM modes are rendered by a separate branch
driven by `_fmPairs`.

**POLY notes come from MIDI, the serial `trig` command and the GATE jack**, all
through `polyNoteOn()` and all drawing on one pool of six slots — no source owns
a fixed subset. `PolySlot::midiNote` doubles as the occupancy state, with
sentinels above the MIDI range so a real note number can never collide:
`kPolySlotFree`, `kPolySlotCvHeld`, `kPolySlotReleasing` (see `params.h`).

Allocation prefers a free slot, then one that is merely ringing out, and steals a
held note only when all six are busy. The gate's falling edge releases its voice
but leaves the slot marked releasing, so the tail stays audible and the slot is
reclaimed by `updateControl()` once the envelope reaches silence. That is what
lets a run of short gates stack voices instead of retriggering one.

`gGateLength` (CC 85) switches the gate between the two behaviours: 0 follows
the gate, above 0 treats it as a trigger and releases the voice after that many
milliseconds. It needs *six* deadlines rather than the single `sTrigReleaseAt`
the serial `trig` command uses, because the entire point is that timed notes
overlap. Measured effect, six triggers 200 ms apart with a 5 ms pulse: peak
simultaneous voices goes 3 → 5 at CURVE 0.50 and 4 → 5 at CURVE 0.80, and is
unchanged at CURVE 0.10 — where the envelope already ignores the gate, so there
is nothing for the setting to do.

The CV block sits *after* `SynthEngine::control()` in `updateControl()`, because
control() frees every slot on the tick it sees a mode change — a voice claimed
before it would be erased on the way into POLY. Ordering it after costs nothing:
`polyRetrigger()` sets frequency, resets phase and arms the envelope
immediately, which is the same path a MIDI note takes when it lands between two
control ticks.

**SPACE is a mid/side width stage on the summed bus**, not a per-voice panner.
It can only widen what the voice stage already placed off-centre, so every mode
has to produce side content of its own or SPACE does nothing there.

Two of them get that content in ways worth knowing about:

- **CASCADE renders two FM pairs.** Only the carrier of an FM pair is summed —
  the modulator is heard solely through the phase modulation it applies — so a
  single pair is one mono source that no pan weight can widen. Voices 2/3 are a
  second carrier/modulator pair at the same ratio, detuned ±1.4 cents against
  0/1 and panned opposite. The fixed detune is load-bearing: without it the
  pairs would be sample-identical at MOTION = 0 and collapse back to mono.
- **POLY's RELATION is a cent-based detune spread** across the six slots,
  ±15 cents at full CW — the same range STRING uses. Cents rather than Hz
  because poly notes span the keyboard: a fixed Hz offset would be an
  inaudible nudge in the top octave and a sour interval in the bottom one.
  COLOR keeps the absolute-Hz spread, so the two layer the way they do in the
  ensemble modes. The cache holds per-slot *ratios* rather than absolute
  frequencies as the other modes do, since every slot carries its own note —
  six `powf()` calls when the knob moves, one multiply per voice otherwise.
- **POLY pans its six slots on a constant-power law** (cos/sin across a 5°–85°
  arc, precomputed — no trig at runtime). Constant power rather than the
  constant-sum law the ensemble modes use, because allocation is round-robin:
  the same note lands in a different slot each time it is played, and a
  constant-sum law would make it audibly louder at the edges of the field than
  in the middle. Measured spread is within 0.02 dB across all six slots.

**The signal path is fixed-point.** `int32_t ±32512` throughout, converted to the
platform's float ±1.0 boundary only at the edge: `renderAudio()` on hardware,
`setVoltage()` in VCV. See `kSignalToFloat` / `kFloatToSignal` in `SynthEngine.h`.

**Rate templating stays inside the module.** `ShapeOsc<48000u>` and friends take
the rate as a template *default*; `setSampleRate()` is what actually sets it, and
`SynthEngine::init()` calls it on every engine. Firmware runs 48 kHz at the stock
150 MHz clock — the PIO divider lands on 48.828125 and needs no overclock. VCV
runs at the host rate.

## Documentation

- [`MANUAL.md`](MANUAL.md) — the user manual
- [`../../references/AlloyFlux-hardware-design.md`](../../references/AlloyFlux-hardware-design.md) — pin map, analog front end, power, panel, BOM
- [`../../references/AlloyFlux-dsp-design.md`](../../references/AlloyFlux-dsp-design.md) — the DSP engines and their algorithms
- [`../../references/AlloyFlux-MIDI-reference.md`](../../references/AlloyFlux-MIDI-reference.md) — SysEx protocol
- [`../../references/AlloyFlux-serial-reference.md`](../../references/AlloyFlux-serial-reference.md) — serial console commands
- [`../../references/AlloyFlux-VCVRack.md`](../../references/AlloyFlux-VCVRack.md) — the Rack build

GPL-3.0-or-later, as the rest of the firmware — see [`../../LICENSING.md`](../../LICENSING.md).
