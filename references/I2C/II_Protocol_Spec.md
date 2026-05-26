v1.1.0-beta.1: i2c protocol polish (s16V, ET-table pitch, mode mirror)
Aligns the Four Seas i2c follower with the Monome ecosystem conventions
used by W/synth and Just Friends.

Continuous params (positions, spreads, mod depths) now follow s16V:
V 5 = full positive, V -5 = full negative, 0 = no offset. Pitch ops
(FS.TUNE, FS.O.NT) use the standard ET-table scaling so N 12 = V 1
= +1 octave exactly. Mode params (sync/mod/lfo/interp/spread.type)
are now consume-on-apply, freeing the front-panel button after an
i2c write; mode and bank queries return current effective state via
a per-tick live mirror in I2CParamState. 16-bit unset queries return
0 rather than the INT16_MIN sentinel.

Adds I2CToSemitones helper alongside the existing I2CTo* family in
ui.h. Updates II_PROTOCOL.md (s16V table, decimal column for raw ops,
IIA op name fix, query semantics split). Rewrites all five
teletype_scenes/ demos and the scenes README to use V/N idioms. Bumps
CHANGELOG and release notes from the stale v0.9.0-beta.1 draft to
v1.1.0-beta.1 dated 2026-05-20.

-------------------------

# AlloyFlux ii Protocol

I2C address: **0x59** (decimal 89)

All values are sent as 16-bit MSB-first per Teletype convention. Continuous parameters follow the **s16V** convention used by W/synth and Just Friends: signed int16 where `V 1 = 1638`, `V 5 = 8192`, `V 10 = 16384`. `±V 5` maps to a parameter's full bipolar range; values past that saturate.

## Behavior

Continuous parameters (positions, spreads, tuning, mod depths) are **additive** — the I2C value is summed with the knob position and CV input, then clamped to the parameter's natural range. A value of `0` has no effect, letting hardware controls pass through unchanged.

Bank is **additive** — the I2C value is added to the pot/CV bank selection and clamped to valid range.

Mode/toggle parameters (sync, mod, LFO, interpolate, spread type) are **direct-set** — they override the button state when set. These commands are accepted over raw I2C but do not have dedicated Teletype ops.

Use `AF.REL` to release all I2C overrides; hardware controls pass through unmodified. Internal state resets to a sentinel (`-32768` / `-1`), not literal zero — reads of any field after `AF.REL` return the sentinel.

## Teletype Ops

| Hex  | Op       | Range           | Effect at full scale (`V 5` / `V -5`)          |
| ---- | -------- | --------------- | ---------------------------------------------- |
| 0x00 | FS.X     | s16V            | X position offset ±7.0                         |
| 0x01 | FS.Y     | s16V            | Y position offset ±7.0                         |
| 0x02 | FS.Z     | s16V            | Z position offset ±7.0                         |
| 0x03 | FS.X.SPR | s16V            | X spread offset ±1.0                           |
| 0x04 | FS.Y.SPR | s16V            | Y spread offset ±1.0                           |
| 0x05 | FS.Z.SPR | s16V            | Z spread offset ±1.0                           |
| 0x14 | FS.F.SPR | s16V            | Frequency spread offset ±1.0                   |
| 0x09 | FS.MD1   | s16V            | Mod depth 1 offset ±1.0 (clamps to 0..1 final) |
| 0x0A | FS.MD2   | s16V            | Mod depth 2 offset ±1.0 (clamps to 0..1 final) |
| 0x06 | FS.TUNE  | ET (s16V pitch) | Pitch offset; `N 12` = `V 1` = +1 octave       |
| 0x0B | FS.BANK  | signed int8     | Bank offset (additive, clamped 0–11)           |

### Examples

```txt
FS.X V 5         ; +7.0 X offset (full positive)
FS.X V -5        ; -7.0 X offset (full negative)
FS.X 0           ; no offset — hardware controls pass through
FS.X.SPR V 2     ; +0.4 spread offset
FS.MD1 V -5      ; cancel knob's mod depth
FS.TUNE N 12     ; +1 octave (= V 1 = 1638 raw)
FS.TUNE N 7      ; +7 semitones (perfect 5th up)
FS.REL           ; release all overrides
```

## Raw I2C Only (no dedicated Teletype ops)

These modes/toggles must be set with generic ops, e.g. `IIA 88` then `IIS1 12 1` for sync 1 = SOFT. (`IIA` sets the i2c address; `IIS1 cmd value` sends `[cmd, value_MSB, value_LSB]`.)

| Hex  | Dec | Field          | Value Range              | Description                     |
| ---- | --- | -------------- | ------------------------ | ------------------------------- |
| 0x0C | 12  | FS.SYNC.1      | 0=HARD 1=SOFT 2=FLIP     | Sync mode osc 1 (direct-set)    |
| 0x0D | 13  | FS.SYNC.2      | 0=HARD 1=SOFT 2=FLIP     | Sync mode osc 2 (direct-set)    |
| 0x0E | 14  | FS.MOD.1       | 0=PM 1=WS 2=XOR          | Mod mode osc 1 (direct-set)     |
| 0x0F | 15  | FS.MOD.2       | 0=PM 1=WS 2=XOR          | Mod mode osc 2 (direct-set)     |
| 0x10 | 16  | FS.LFO.1       | 0=OFF 1=ON 2=SLOW 3=GLAC | LFO mode osc 1 (direct-set)     |
| 0x11 | 17  | FS.LFO.2       | 0=OFF 1=ON 2=SLOW 3=GLAC | LFO mode osc 2 (direct-set)     |
| 0x12 | 18  | FS.INTERP      | 0/1                      | Wave interpolation (direct-set) |
| 0x13 | 19  | FS.SPREAD.TYPE | 0-7                      | Freq spread type (direct-set)   |

These are 8-bit fields on the wire: only the LSB of the 16-bit value is read, so `IIS1 12 1` and `IIS1 12 257` both set SOFT. Setting any of these to `-1` releases the override (returns to hardware/button control).

## Per-Oscillator Commands

| Hex  | Op      | Args       | Description                               |
| ---- | ------- | ---------- | ----------------------------------------- |
| 0x20 | FS.O.NT | osc, value | Per-osc pitch offset; `N 12` = +1 octave  |
| 0x21 | FS.O.X  | osc, value | Per-osc X offset (8192=ctr, -7.0 to +7.0) |
| 0x22 | FS.O.Y  | osc, value | Per-osc Y offset                          |
| 0x23 | FS.O.Z  | osc, value | Per-osc Z offset                          |

Oscillator index: 0-3. All per-osc values are additive with the global value.

## Control

| Hex  | Op     | Description                   |
| ---- | ------ | ----------------------------- |
| 0x30 | FS.REL | Clear all I2C offsets to zero |

## Queries

All `FS.*` ops support get-by-omitting-the-set-arg: e.g. `FS.X` returns the current X offset, `FS.O.NT 2` returns OSC 2's current note offset.

Query semantics differ by parameter type:

- **Continuous params** (positions, spreads, tune, mod depths) return the **current i2c override** in s16V units, not the effective parameter. Unset reads back as `0` rather than the internal `-32768` sentinel — there's no observable difference between "never set" and "set to 0" since both produce no offset.
- **Mode/toggle params** (sync, mod, lfo, interp, spread_type) and **bank** return the **current effective value**, regardless of whether i2c, the front-panel button, or the bank pot last touched it. A query right after `FS.SYNC.1 1` returns `1` (SOFT); pressing the sync button to advance to FLIP then querying returns `2`. There is no "passthrough" sentinel for these — the live state is always available.

For raw access, any command ORed with 0x80 (add 128) returns the current 16-bit value.

Example: `IIQ 128` returns current X position (0x00 | 0x80 = 128).

## Wire Format

- Write: `[cmd, value_MSB, value_LSB]` (3 bytes)
- Write per-osc: `[cmd, osc_MSB, osc_LSB, value_MSB, value_LSB]` (5 bytes)
- Query: write `[cmd | 0x80]` (1 byte), then read 2 bytes (MSB-first)
- Query per-osc: write `[cmd | 0x80, osc_MSB, osc_LSB]` (3 bytes), then read 2 bytes (MSB-first)

## Source

Command enum and param state struct: `firmware/src/drivers/ii_follower.h`
ISR and command parsing: `firmware/src/drivers/ii_follower.cc`
Parameter application to synth engine: `firmware/src/ui.cc` (search for `i2c_state_`)
Teletype op definitions (out-of-tree fork): `monome/teletype:src/ops/fourseas.c`
