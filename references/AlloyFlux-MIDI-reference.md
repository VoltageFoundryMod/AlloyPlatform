# MIDI & SysEx Reference

For MIDI CC implementation table, check the user manual: [Manual.md](../Manual.md#midi-cc-map).

## Patch Management & SysEx

Alloy Flux stores your entire parameter state in flash memory and can exchange it over USB MIDI as a standard SysEx message. This means you can back up patches, restore them, exchange them with other users, and automate preset management from a DAW — using any MIDI tool that handles SysEx files.

---

### What Is a Patch?

A patch is a snapshot of every parameter: voice mode, oscillator settings, envelope, filter, effects, scale, transpose, MIDI channel, and so on. It is encoded as a sequence of MIDI CC pairs (CC number + 7-bit value) wrapped in a SysEx message. All parameter values fit in 7 bits, so no data-splitting is required.

---

### SysEx Message Format

Every AlloyFlux SysEx message follows this structure:

```txt
F0  7D  41  46  <cmd>  [data bytes]  F7
│   │   │   │    │
│   │   │   │    └── Command byte (see table below)
│   │   └───┘──── Device signature: 'A' 'F' (0x41 0x46)
│   └── Non-commercial manufacturer ID (0x7D)
└── SysEx start
```

All bytes between `F0` and `F7` are 7-bit safe (0x00–0x7F). The device signature `41 46` allows DAW SysEx filters to target Alloy Flux messages specifically.

#### Command Bytes

| Cmd  | Direction     | Name          | Payload                                                       |
| ---- | ------------- | ------------- | ------------------------------------------------------------- |
| 0x01 | Host → Device | REQUEST\_DUMP | *(none)* — request a full patch dump                          |
| 0x02 | Device → Host | PATCH\_DUMP   | CC pairs: `[cc0, val0, cc1, val1, …]`                         |
| 0x03 | Host → Device | APPLY\_PATCH  | CC pairs: `[cc0, val0, cc1, val1, …]`                         |
| 0x04 | Host → Device | PRESET\_SAVE  | `[slot]` — save current state to slot 0–9                     |
| 0x05 | Host → Device | PRESET\_LOAD  | `[slot]` — load slot and respond with PATCH\_DUMP             |
| 0x06 | Host → Device | PRESET\_RESET | `[slot]` — reset slot to factory defaults; `0x7F` = all slots |
| 0x07 | Host → Device | SET\_MIDI\_CH | `[ch]` — set receive channel (0 = omni, 1–16 = specific)      |

**How REQUEST\_DUMP works:** Send a REQUEST\_DUMP (cmd 0x01, no payload). The device immediately responds with a PATCH\_DUMP (cmd 0x02) containing all current parameter values as CC pairs. The Web Configurator uses this automatically on connect.

**How APPLY\_PATCH works:** Send a APPLY\_PATCH (cmd 0x03) with an arbitrary subset of CC pairs. The device applies each one immediately — you do not need to send the full set. Useful for partial updates or live automation.

---

### .syx File Format

Alloy Flux patch files use the standard raw SysEx format (`.syx`):

```txt
F0  7D  41  46  02  [cc0 val0  cc1 val1  …]  F7
```

The file contains exactly one SysEx message. A typical full patch is about 84 bytes. These files are compatible with any software that reads standard `.syx` files.

---

### Exporting a Patch

**From the Web Configurator:**

1. Connect via USB MIDI or Serial.
2. Open the **Presets** panel on the right.
3. Click **Export** next to any preset slot (or the live state) to download a `.syx` file.

**From a DAW (e.g. Ableton Live, Logic, Bitwig):**
Send a REQUEST\_DUMP SysEx to Alloy Flux. The device responds immediately. Capture the response with your DAW's SysEx recorder or a dedicated tool such as **Sysex Librarian** (macOS), **MIDI-OX** (Windows), or **Midi Quest**.

Example SysEx string to send: `F0 7D 41 46 01 F7`

---

### Importing / Restoring a Patch

**From the Web Configurator:**

1. Click **Import** in the Presets panel.
2. Select a `.syx` file. The configurator applies all CC pairs to the UI and sends an APPLY\_PATCH SysEx to the device immediately.

**From a DAW or SysEx tool:**
Load your `.syx` file and send it to the Alloy Flux MIDI port. The device processes the APPLY\_PATCH command and applies all values in real time. No reboot required.

**From a hardware MIDI controller with SysEx storage:**
Store the `.syx` file in your controller's SysEx bank and send it on patch recall. Useful for recalling Alloy Flux states alongside your controller's own presets.

---

### Preset Slots

Alloy Flux has 10 on-device preset slots (0–9). Slot 0 is the *live state* — the currently active parameters, always held in RAM and persisted to flash. Slots 1–9 are user presets stored in flash.

| Slot | Purpose              |
| ---- | -------------------- |
| 0    | Live state (working) |
| 1–9  | User preset slots    |

Use PRESET\_SAVE (cmd 0x04, payload = slot number) to write the current live state to a slot. Use PRESET\_LOAD (cmd 0x05) to recall one — the device automatically sends back a PATCH\_DUMP so the Web Configurator stays in sync.

PRESET\_RESET (cmd 0x06, payload = slot) restores factory defaults to that slot. Send `0x7F` as the slot byte to reset all slots at once.

---

### Live Parameter Feedback (device → host)

Alloy Flux does not only accept parameter changes, it reports them. Every 250 ms the firmware builds the same CC snapshot used by PATCH\_DUMP, diffs it against the last one sent, and emits a plain CC message for each value that changed — typically none, and only a handful while something is being moved.

The effect is that anything changing a parameter on the module — a panel knob, a button combo, a preset recall, the serial console — shows up on the host within one feedback tick. The Web Configurator uses exactly this to mirror the hardware; any DAW or controller sees it as ordinary CC input.

Two rules keep the loop from feeding on itself:

- **The device does not echo the host.** A CC received from the host is recorded as if the device had sent it, so it is not immediately transmitted back. This matters most for log or wide-range parameters (filter cutoff, delay time), where the 7-bit round-trip can land a step away from what the host sent and would otherwise nudge the host's control.
- **A PATCH\_DUMP seeds the same cache.** After a dump the host already has every value, so the next feedback tick does not repeat the whole patch as individual CCs.

The host side is expected to be symmetrical: ignore an inbound CC that merely repeats what you last sent, and ignore inbound CC for a control the user is currently dragging (the Web Configurator uses a 400 ms window — see `shouldIgnoreInbound()` in `src/lib/midi.ts`).

CC 16 (ROOT pitch, ±4 V/Oct) is part of both the dump and the feedback diff, so the ROOT knob position reaches the host like every other control.

---

### DAW Integration Examples

**Ableton Live:** Use the *SysEx* editor in a MIDI clip or Max for Live's *SysEx* device. Record a REQUEST\_DUMP response onto a MIDI track; drop-in that clip to recall the patch at the start of a session.

**Logic Pro:** Use the *SysEx Buffer* in the Environment. Store patches as environment objects keyed to song positions.

**Bitwig Studio:** The MIDI CC module can automate individual CC parameters in real time. For full patch recall, use the Note Expression → SysEx output in a hardware instrument track.

**MIDI-OX (Windows):** *Actions → Send SysEx File* to send a `.syx`. *Actions → Receive SysEx* to capture a dump. Right-click the SysEx window → *Save as .syx* to export.

**Sysex Librarian (macOS):** Drop your `.syx` file into any bank slot, select the Alloy Flux output, and click **Send**. To capture: set the input to Alloy Flux, click **Receive One**, then send `F0 7D 41 46 01 F7` from any SysEx sender.

---

### Automating Parameters from a DAW

Every parameter in the CC map can be automated directly. Since Alloy Flux appears as a standard USB MIDI device, any DAW can record and play back CC automation on the Alloy Flux MIDI track. No SysEx is needed for per-parameter automation — use standard MIDI CC messages.

For parameters with unusual ranges (e.g. CC 104 Transpose encodes −24…+24 st as CC values 0–48, with CC 24 = 0 semitones) refer to the MIDI CC Map at the user manual: [Manual.md#midi-cc-map](../Manual.md#midi-cc-map).
