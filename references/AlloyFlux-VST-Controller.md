# Alloy Controller as a Plugin

Design notes for **M48** — shipping the existing [Alloy Controller](../web-configurator/README.md)
as a VST3 plugin so the module can be driven, automated and recalled from a DAW
session.

Reference documents:

- [Development Milestones](./AlloyFlux-Development_Milestones.md)
- [MIDI Reference](./AlloyFlux-MIDI-reference.md) — the CC map and SysEx protocol the plugin speaks
- [Desktop Version](./AlloyFlux-Desktop.md) — supersedes its two software bullets; the standalone
  build described here is what removes the need for an Electron wrapper

---

## Scope

**In.** One plugin that is a _control surface_ for an Alloy module, doing what the
web controller already does — CC in both directions, SysEx patch dump/apply,
preset slots, live panel mirroring — plus the two things only a DAW can add:
**parameter automation** and **patch recall with the session**.

Two things it talks to, and they are the same conversation:

| Target                | Transport                                                                                               |
| --------------------- | ------------------------------------------------------------------------------------------------------- |
| **Hardware module**   | its USB MIDI port, opened directly by the plugin                                                        |
| **VCV Rack instance** | a virtual MIDI port (loopMIDI / IAC), which Rack's `rack::midi::InputQueue` already consumes — see M37i |

**Out.**

- ⚠️ **Porting the DSP to VST3 is not in discussion.** The engine stays where it is:
  firmware and VCV. This plugin renders no audio and contains no `SynthEngine`.
  (It remains the obvious later move — `AlloyFlux.cpp` already runs the real engine
  through `IHardwareIO`/`IOBridge`, so a third implementation is all it would take —
  but it is a different product and it is not this one.)
- **The serial transport is dropped.** [`lib/serial.ts`](../web-configurator/src/lib/serial.ts)
  and the console drawer are development tooling; they do not follow into the
  plugin. The web build keeps them.
- Audio from the module never enters the DAW through this plugin.

---

## The constraint that shapes everything: VST3 will not carry the protocol

This has to be settled first because it decides the whole architecture.

- **VST3 deliberately does not pass raw MIDI.** Plugin CC output exists only as
  `kLegacyMIDICCOutEvent` (added in 3.6.12, December 2018) and Steinberg marked it
  _legacy_ on arrival — which is why several DAW vendors never implemented it.
  Hosts that silently never call it are common enough that it cannot be relied on.
- **SysEx output is effectively broken.** The `F0`/`F7` framing does not reliably
  survive the wrapper's output event list in any host. Our entire patch sync —
  `REQUEST` / `DUMP` / `APPLY` / `PRESET_*` / `SET_MIDI_CHANNEL`, see
  [`patchSync.ts`](../web-configurator/src/lib/patchSync.ts) — is SysEx. This is
  disqualifying, not inconvenient.

**So the plugin opens the MIDI port itself** and the DAW is not in the MIDI path at
all. This is what every hardware editor plugin does (AURA's Access Virus Editor is
the reference case: its own MIDI in/out pickers in its own preferences). It costs
nothing we want and buys identical behaviour in every host — no support matrix, no
"works in Cubase, silent in Live".

### The Windows port-exclusivity trap, and its resolution

⚠️ The legacy Windows MIDI stack is **one application per port**. If the DAW holds
the module's port open as a track output, the plugin cannot open it, and vice
versa. macOS CoreMIDI has always been multi-client, and Windows MIDI Services
(Windows 11, 2026) makes every endpoint multi-client and retires this — but users
below that will hit it, and the failure is silent unless we name it.

The resolution is a design decision, not a workaround: **the plugin is the sole
owner of the port, and forwards the host's MIDI to it.**

- Note events arriving from the track go in as ordinary VST3 note events —
  _input_ MIDI is the half of VST3 that works properly.
- The plugin merges them with its own CC/SysEx stream and writes both to the port.
- The user gets a normal MIDI track that plays the module, with the panel GUI and
  automation on the same track. Nobody needs loopMIDI for the hardware case.

Rack is a separate process, so the **VCV case still needs a virtual port** — that
one is unavoidable and belongs in the docs rather than in the code.

---

## Architecture: one GUI, two shells

This is the requirement the design exists to serve. A change to a knob, a readout
or a panel section must land in the web app and the plugin at once, with no step
that can be forgotten.

The mechanism is the one `params.json` already uses: **not two copies kept in
step, but one artifact consumed twice.**

```
web-configurator/src/          ← the only GUI source that exists
        │
   make web  (vite build)
        │
        ▼
web-configurator/dist/         ← the only GUI build that exists
        │                 │
   deployed as a site     embedded as a binary resource
        │                 │
        ▼                 ▼
    browser            plugin (WebView)
```

`make vst` does not build the UI. It **embeds the exact `dist/` that `make web`
produced**, the way the firmware and the web build both consume
`param_manifest.generated.h` and `paramMapAlloyFlux.ts` from one `params.json`.
Two Svelte trees would drift within a month; one bundle cannot drift at all.

⚠️ **The one rule that keeps this true: no component may know which shell it is
in.** No `isPlugin` prop threaded through `PanelView`, no `#if` in a `.svelte`
file. Everything shell-specific lives behind the transport interface below, and
the two features that differ (the serial drawer; the port picker) are decided by
what the transport reports it supports, not by asking where we are.

### The transport seam

Every component today reaches the device through the single `midi` object exported
by [`lib/midi.ts`](../web-configurator/src/lib/midi.ts). That object _is_ the
interface — it just has one implementation. The work is to name it and add a
second:

```
lib/transport.ts        the interface + runtime selection
lib/transport/web.ts    today's midi.ts — Web MIDI API      (browser)
lib/transport/host.ts   native bridge to the plugin          (WebView)
```

Selection is a feature check at startup — the native shell injects a global before
loading the bundle; its absence means browser. Nothing else in the app asks.

The surface to reproduce is already small and already stable:

| Group     | Members                                                                                                                                      |
| --------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Ports     | `scan`, `connect`, `selectOutput`, `selectInput`, `adoptAnsweringPort`                                                                       |
| Send      | `sendCC`, `sendNoteOn`, `sendNoteOff`, `sendProgramChange`, `sendSustain`, `sendSysEx`, `broadcastSysExWide`, `sendPanic`, `sendDroneReturn` |
| Receive   | `onCC`, `onSysEx`, `onTraffic`, `shouldIgnoreInbound`                                                                                        |
| Discovery | `probeStarted`, `probeAnswered`, `probeUnanswered`, `resync`                                                                                 |
| State     | the `MidiStore` shape, via `subscribe`                                                                                                       |

Note what is _not_ in that list: nothing above is Web MIDI-specific. The store's
`probing` / `moduleAnswered` / `probeFailed` discovery state and the whole
[`patchSync.ts`](../web-configurator/src/lib/patchSync.ts) protocol layer are
transport-agnostic already and move across untouched. So does
[`midiDecode.ts`](../web-configurator/src/lib/midiDecode.ts) and the MIDI monitor.

The `host.ts` implementation is a thin marshalling layer: byte arrays and port
lists across the WebView bridge, and the native side does the actual `MidiOutput`
work. It is the only genuinely new UI-side code in the project.

---

## The native shell

### Parameters and automation

Each entry in `params.json` becomes one host-automatable parameter. `gen_params.py`
gains a fourth emitter alongside the C++ and TypeScript ones, so the plugin's
parameter list is generated from the same source as the firmware's CC table and
cannot fall out of step.

The mapping is unusually clean, and it is worth noticing why: **a VST3 parameter is
normalised 0–1, which is exactly our "control position".** `ParamDescriptor::fromPos()`
/ `posToValue()` — the curve a knob, a CC and a slider all already share — is
precisely the denormalisation a host parameter needs. So:

```
host automation value (0–1) ──► floatToCC()/toCC() ──► CC out
CC feedback from module ─────► ccToFloat()/fromPos() ──► host parameter value
```

Skew, log scaling, `ccRange` sub-ranges and the display transform all come along
for free, and a knob on the module, a slider in the panel and an automation lane in
the DAW land on the same value by construction — the property the `skew` work in
M63d existed to protect.

⚠️ **The echo hazard.** Inbound CC feedback (a hardware knob being turned) must
update the host parameter _without_ re-emitting CC to the module and without
writing an automation point. The web app already carries `shouldIgnoreInbound` for
half of this; the automation half is new and is the likeliest source of a
feedback loop that manifests as a knob crawling on its own.

### Session state

`getState()`/`setState()` serialises the same CC-pair payload as a `PATCH_DUMP`.
That makes session recall and the existing `.syx` export the same format, and gives
DAW project recall of the whole patch for a few lines of code. The plugin must load
and show its UI with **no device connected** — a session opened away from the
hardware cannot fail to instantiate.

### Threading

MIDI is written from a dedicated sender thread fed by a lock-free FIFO from the
audio thread — the same discipline as the firmware's control→audio publishing, and
for the same reason: **no blocking I/O on the audio thread, ever.** Host-forwarded
notes are therefore quantised to the block boundary (±1 buffer). That is inherent
to the arrangement and is fine for a control surface; it is not fine for a hard-timed
sequencer, and we should say so rather than let someone discover it.

### Plugin category

Register as an **instrument** (`kInstrument`) with a stereo output that emits
silence. It costs an idle bus and buys the three things that matter: it lands on a
MIDI track in every host, it can receive host note events for forwarding, and it is
automatable. An effect-category build would sit on an audio track, where MIDI
cannot reach it.

---

## Framework choice — open decision

|                                                                                                       | Implementation cost | Licence                   | Notes                                                                                                                                                                                                            |
| ----------------------------------------------------------------------------------------------------- | ------------------- | ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **JUCE 8**                                                                                            | **lowest**          | ⚠️ AGPLv3, or paid tier   | `WebBrowserComponent` with `WebSliderRelay`/`WebSliderParameterAttachment` is built for exactly this; ships a `WebViewPluginDemo`. VST3 + AU + standalone from one target. `MidiOutput` included.                |
| **VST3 SDK + [choc](https://github.com/Tracktion/choc) + [RtMidi](https://github.com/thestk/rtmidi)** | high                | GPLv3 / ISC / MIT — clean | No framework, but the VST3 COM-style boilerplate and the JS↔C++ bridge are hand-written.                                                                                                                         |
| **CLAP + [clap-wrapper](https://github.com/free-audio/clap-wrapper) + choc + RtMidi**                 | medium              | MIT / ISC — clean         | CLAP's API is far less boilerplate than VST3's; the wrapper emits VST3, AU and standalone. Also the only format with working `CLAP_EVENT_MIDI_SYSEX` in _and_ out, if host-routed MIDI ever becomes interesting. |

**Recommendation: JUCE 8**, on implementation cost — the WebView parameter-relay
machinery is the single largest piece of this project and JUCE has it written,
documented and demoed. Take CLAP + clap-wrapper instead if the licence below is
judged unacceptable.

⚠️ **JUCE 8's open-source option is AGPLv3, not GPLv3.** Combining with our
GPL-3.0-or-later tree is permitted (GPLv3 §13), but the distributed plugin then
carries AGPL obligations. For a desktop plugin with no network interaction those
obligations are practically inert — but it is a licence change on one build target
and it must be recorded in [LICENSING.md](../LICENSING.md) if we go that way.
JUCE Personal (free, revenue-capped) sidesteps AGPL entirely; the CLAP route
avoids the question.

The VST3 SDK itself is dual GPLv3/proprietary and its GPLv3 side fits us either way.

### WebView notes

- Windows uses WebView2 (present on all Windows 11, most Windows 10 since 2022) —
  ⚠️ plugin builds **must** set a non-default user-data folder (e.g. the temp
  directory) or WebView2 misbehaves on being denied its default location.
- macOS uses WKWebView, no runtime dependency.
- Linux falls back to WebKitGTK and is the flakiest of the three. Rack already
  serves Linux users; a Linux plugin build is best-effort.

---

## Build integration

| Target     | Command            | Notes                                                                         |
| ---------- | ------------------ | ----------------------------------------------------------------------------- |
| Plugin     | `make vst`         | ⚠️ depends on `make web` — embeds `web-configurator/dist/`, never rebuilds it |
|            | `make vst-install` | into the system VST3 folder                                                   |
|            | `make vst-dist`    | packaged, signed/notarised                                                    |
| Everything | `make everything`  | gains `vst` after `web`                                                       |

Standalone falls out of the same build, which is what closes the "Desktop Alloy
Controller (Electron)" line in [AlloyFlux-Desktop.md](./AlloyFlux-Desktop.md) — one
binary, no second stack.

---

## Effort, honestly

The instinct that this is not a big implementation is right about the interesting
part and wrong about the tail.

| Piece                                                                                     | Estimate                              |
| ----------------------------------------------------------------------------------------- | ------------------------------------- |
| Plugin skeleton + `dist/` embedding + WebView bring-up                                    | 2–3 days                              |
| Extract `lib/transport.ts`; today's `midi.ts` becomes `transport/web.ts`                  | 2–3 days                              |
| `transport/host.ts` + the native bridge                                                   | 3–4 days                              |
| Native MIDI backend: port enumeration, hot-plug, exclusivity errors, host-MIDI forwarding | 4–6 days                              |
| `gen_params.py` parameter emitter + automation bridge + echo suppression                  | 3–5 days                              |
| Session state, presets, `.syx` parity                                                     | 1–2 days                              |
| ⚠️ Cross-platform packaging, WebView2 redistributable, code signing and notarisation      | **the long tail — budget generously** |

Nothing above is research. The only unknown is the last row, and it is unknown in
the same way it is unknown for everyone shipping a plugin.

---

## Open questions

1. **Framework** — JUCE 8 and the AGPL question, or CLAP and the extra work. Decide before anything else; it is the only choice here that is expensive to reverse.
   A: We can go with JUCE as it's a no-brainer
2. **Both modules, one plugin?** The web app already switches on the answering module via [`activeModule.ts`](../web-configurator/src/lib/activeModule.ts) and the SysEx signature. One plugin that follows the port, as Rack ships one binary with every module in it, is the consistent answer — confirm it.
   A: Yes, one plugin for both modules is the preferred approach, following the model of Rack and leveraging the existing web app's module switching logic.
3. **Automation write behaviour** — does turning a knob on the hardware write automation when the track is armed, or only when the user touches the plugin's own GUI? Cheapest correct answer is the latter.
   A: Only when the user interacts with the plugin's own GUI.
4. **Port sharing on legacy Windows** — is "the plugin owns the port, the web controller must be closed" an acceptable stated limitation, or do we need a detection-and-message path? It is worth one clear error string either way.
   A: Yes, the plugin will display a clear error message if the web controller is open while it owns the port.
5. **MIDI channel** — the plugin has both the module's configured receive channel (`SET_MIDI_CHANNEL`) and the host's track channel to reconcile. Follow the module, ignore the host's.
   A: Yes, follow the module's configured receive channel and ignore the host's track channel.
