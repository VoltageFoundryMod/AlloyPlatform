/**
 * MIDI connection layer — Web MIDI API wrapper.
 *
 * Responsibilities:
 *  - Enumerate MIDI ports and expose them reactively via Svelte stores
 *  - Send Note On/Off, CC, Program Change
 *  - Receive incoming MIDI (parameter feedback if firmware ever sends it)
 *
 * Usage:
 *   import { midi } from '$lib/midi';
 *   await midi.connect();
 *   midi.sendCC(74, 64);   // shape = 0.5
 *   midi.sendNote(60, 64); // C4
 */

import { writable, get, type Writable } from "svelte/store";

export type MidiPortInfo = { id: string; name: string };

/** One message observed on the wire, either direction. See onTraffic(). */
export interface MidiTrafficEvent {
  dir: "in" | "out";
  bytes: number[];
  t: number; // performance.now() at capture
  port: string; // port name the message went to / came from
}

export interface MidiStore {
  supported: boolean;
  scanned: boolean;
  connected: boolean;
  deviceConnected: boolean;
  outputs: MidiPortInfo[];
  inputs: MidiPortInfo[];
  selectedOutput: string | null;
  selectedInput: string | null;
  /** True once the user has picked an output from the dropdown themselves.
   *  Auto-selection must not override a deliberate choice — but until one is
   *  made, a hardware port appearing later has to be allowed to win over an
   *  earlier automatic pick.  See pickPort(). */
  userPickedOutput: boolean;
  /** Bumped every time a fresh sync with the device is warranted — first
   *  connection, reconnection, or a switch to a different output port.
   *  Consumers depend on this rather than watching `deviceConnected` for a
   *  false→true edge, because those edges are not reliably observable: a
   *  re-enumeration that completes inside one microtask leaves the flag true
   *  the whole time, and a reactive effect batching over it sees no change at
   *  all.  A monotonic counter always changes. */
  syncNonce: number;
  error: string | null;
  /** Bytes actually handed to a MIDIOutput. Compare against the counter in
   *  loopMIDI / your MIDI monitor: if this climbs and theirs does not, the
   *  bytes are going to a different port than you think. */
  txBytes: number;
  /** Bytes received across all input ports.  Counted independently of the
   *  monitor's traffic tap, so both totals keep running while the monitor is
   *  closed — the asymmetry between them is the diagnostic. */
  rxBytes: number;
}

function createMidi() {
  const store: Writable<MidiStore> = writable({
    supported:
      typeof navigator !== "undefined" && "requestMIDIAccess" in navigator,
    scanned: false,
    connected: false,
    deviceConnected: false,
    outputs: [],
    inputs: [],
    selectedOutput: null,
    selectedInput: null,
    userPickedOutput: false,
    syncNonce: 0,
    error: null,
    txBytes: 0,
    rxBytes: 0,
  });

  let access: MIDIAccess | null = null;
  let channel = 0; // 0-based, i.e. MIDI channel 1

  // ---------------------------------------------------------------------------
  // Local-edit tracking — the other half of the module's echo suppression.
  //
  // The module pushes a CC feedback diff every 250 ms, so anything we send is
  // liable to come straight back at us.  Two problems follow, and one record
  // per CC solves both:
  //
  //   Echo      — the value returns unchanged, or a step off after the 7-bit
  //               round-trip on log/wide-range params (filter cutoff, delay
  //               time), and visibly nudges the control the user just set.
  //   Drag war  — while a slider is being dragged, feedback for that same CC
  //               arrives mid-gesture and fights the pointer.
  //
  // So a CC is ignored on the way in when it merely repeats what we last sent,
  // or when we touched that control within the last LOCAL_EDIT_MS.  Full syncs
  // (SysEx dump, serial dump, file import) deliberately bypass this — they are
  // explicit "the device is the truth" moments, not feedback.
  // ---------------------------------------------------------------------------
  const LOCAL_EDIT_MS = 400;
  const lastSent = new Map<number, { value: number; t: number }>();

  /** True when an inbound CC is our own echo or lands mid-gesture. */
  function shouldIgnoreInbound(cc: number, value: number): boolean {
    const rec = lastSent.get(cc);
    if (!rec) return false;
    if (rec.value === value) return true;
    return performance.now() - rec.t < LOCAL_EDIT_MS;
  }

  // ---------------------------------------------------------------------------
  // Traffic tap — every message, both directions, undecoded.
  //
  // Feeds the MIDI monitor.  Gated on there being a listener: parameter
  // feedback can run to hundreds of messages a second while a knob is moving,
  // and there is no reason to allocate an event per message for a panel nobody
  // has open.  Subscribe only while the monitor is visible.
  // ---------------------------------------------------------------------------
  type TrafficListener = (e: MidiTrafficEvent) => void;
  const trafficListeners = new Set<TrafficListener>();
  let trafficTapped = false;

  function emitTraffic(
    dir: "in" | "out",
    bytes: ArrayLike<number>,
    port: string,
  ): void {
    if (!trafficTapped) return;
    const ev: MidiTrafficEvent = {
      dir,
      bytes: Array.from(bytes),
      t: performance.now(),
      port,
    };
    trafficListeners.forEach((fn) => fn(ev));
  }

  /** Zero the TX/RX byte totals — lets the monitor's Clear act as a baseline
   *  reset, so "did that action send anything?" is answerable at a glance. */
  function resetCounters(): void {
    store.update((s) => ({ ...s, txBytes: 0, rxBytes: 0 }));
  }

  /** Observe raw MIDI traffic in both directions. Returns an unsubscribe fn. */
  function onTraffic(fn: TrafficListener): () => void {
    trafficListeners.add(fn);
    trafficTapped = true;
    return () => {
      trafficListeners.delete(fn);
      trafficTapped = trafficListeners.size > 0;
    };
  }

  // CC listeners registered by consumers
  type CCListener = (cc: number, value: number) => void;
  const ccListeners = new Set<CCListener>();

  // SysEx listeners — receive the body bytes between F0 and F7
  type SysExListener = (body: Uint8Array) => void;
  const sysexListeners = new Set<SysExListener>();

  function handleMidiMessage(event: MIDIMessageEvent) {
    const data = event.data;
    if (!data || data.length < 1) return;
    emitTraffic("in", data, (event.target as MIDIInput | null)?.name ?? "?");
    store.update((s) => ({ ...s, rxBytes: s.rxBytes + data.length }));
    const status = data[0];

    // SysEx: data[0] = 0xF0, data includes F0 and trailing F7
    if (status === 0xf0) {
      const endIdx =
        data[data.length - 1] === 0xf7 ? data.length - 1 : data.length;
      const body = data.slice(1, endIdx);
      sysexListeners.forEach((fn) => fn(body));
      return;
    }

    if (data.length < 3) return;
    const type = status & 0xf0;
    if (type === 0xb0) {
      // Control Change
      ccListeners.forEach((fn) => fn(data[1], data[2]));
    }
  }

  function subscribeInputs() {
    if (!access) return;
    access.inputs.forEach((input) => {
      input.onmidimessage = handleMidiMessage;
    });
  }

  /** Register a listener for incoming CC. Returns an unsubscribe function. */
  function onCC(fn: CCListener): () => void {
    ccListeners.add(fn);
    return () => ccListeners.delete(fn);
  }

  /** Register a listener for incoming SysEx. Receives the body (without F0/F7). */
  function onSysEx(fn: SysExListener): () => void {
    sysexListeners.add(fn);
    return () => sysexListeners.delete(fn);
  }

  /**
   * Send a SysEx message.  Automatically wraps the payload in F0/F7.
   * cmd   — command byte (e.g. SysexCmd.REQUEST = 0x01)
   * payload — additional data bytes after the AlloyFlux header (7-bit safe)
   */
  function sendSysEx(cmd: number, payload: number[]): void {
    // Full message: F0 7D 41 46 <cmd> [payload] F7
    sendRaw(
      new Uint8Array([0xf0, 0x7d, 0x41, 0x46, cmd & 0x7f, ...payload, 0xf7]),
    );
  }

  // ---------------------------------------------------------------------------
  // Port auto-selection.
  //
  // Hardware announces itself as "Alloy Flux", so that always wins.  Failing
  // that, prefer a virtual/loopback port: the VCV Rack workflow runs the module
  // and this page through loopMIDI (Windows), the IAC Driver (macOS) or a
  // similar virtual cable, and that port is what Rack is listening on.
  //
  // The avoid-list exists because Windows enumerates "Microsoft GS Wavetable
  // Synth" as an output and it is usually *first* in the list.  Falling back to
  // outputs[0] therefore aimed the whole configurator at the Windows softsynth:
  // inbound MIDI still worked (we subscribe to every input port, see
  // subscribeInputs), so the UI looked connected and mirrored the module, while
  // everything it sent disappeared into a synth nobody was listening to.
  //
  // Inbound is not port-filtered at all — selectedInput is display only.
  // ---------------------------------------------------------------------------
  const NAME_ALLOY = /alloy/i;
  const NAME_VIRTUAL = /loop(be|midi)|iac|virtual|rack|through/i;
  const NAME_AVOID = /wavetable|microsoft gs/i;

  function pickPort(
    ports: MidiPortInfo[],
    current: string | null,
    userPicked = false,
  ): string | null {
    const present = (id: string | null) =>
      !!id && ports.some((p) => p.id === id);

    // A deliberate choice from the dropdown is never overridden.
    if (userPicked && present(current)) return current;

    // Hardware outranks a stale automatic pick.  Without this, the common
    // startup order — page open with loopMIDI/IAC already present, module
    // enumerating a second later — auto-selected the virtual port, and the
    // sticky-selection rule below then kept it forever: the Alloy Flux port
    // showed in the dropdown but could never be chosen, and only a browser
    // restart (which clears selectedOutput) let the preference chain run
    // again.  That was the "I have to close and reopen the browser" bug.
    const alloy = ports.find((p) => NAME_ALLOY.test(p.name))?.id;
    if (alloy) return alloy;

    // Otherwise keep an existing selection as long as that port is present.
    if (present(current)) return current;

    return (
      ports.find((p) => NAME_VIRTUAL.test(p.name))?.id ??
      ports.find((p) => !NAME_AVOID.test(p.name))?.id ??
      ports[0]?.id ??
      null
    );
  }

  function refreshList() {
    if (!access) return;
    const outputs: MidiPortInfo[] = [];
    const inputs: MidiPortInfo[] = [];
    // Only include ports that are currently connected.
    // After a USB re-enumeration, Chrome keeps disconnected ports in the map
    // with state='disconnected'. If we include them the selected ID still
    // matches, deviceConnected never goes false→true, and the $effect in
    // App.svelte never re-fires to request a fresh dump from the device.
    access.outputs.forEach((o) => {
      if (o.state !== "disconnected")
        outputs.push({ id: o.id, name: o.name ?? o.id });
    });
    access.inputs.forEach((i) => {
      if (i.state !== "disconnected")
        inputs.push({ id: i.id, name: i.name ?? i.id });
    });
    store.update((s) => {
      // Keep existing selection if port still present; auto-select otherwise.
      const selectedOutput = pickPort(
        outputs,
        s.selectedOutput,
        s.userPickedOutput,
      );
      const selectedInput = pickPort(inputs, s.selectedInput);
      // Auto-connect: if a port is available, mark connected immediately.
      // No manual "Connect" click required — mirrors VCV behaviour where
      // selecting a port is sufficient.  Explicit disconnect (user clicks ✕)
      // sets connected=false; it stays false until the next port-change event
      // brings a port back, at which point we auto-reconnect.
      const portAvailable = selectedOutput !== null;
      const connected = portAvailable ? true : false;
      const deviceConnected = connected;
      // A fresh sync is warranted when we go from no usable port to having
      // one, or when the port we would send on changes underneath us.
      const needsSync =
        (connected && !s.connected) || selectedOutput !== s.selectedOutput;
      return {
        ...s,
        scanned: true,
        connected,
        outputs,
        inputs,
        selectedOutput,
        selectedInput,
        deviceConnected,
        syncNonce: s.syncNonce + (needsSync ? 1 : 0),
      };
    });
    if (get(store).connected) {
      subscribeInputs();
      stopAutoRescan();
    } else {
      startAutoRescan();
    }
  }

  // ---------------------------------------------------------------------------
  // Auto-rescan while there is nothing to talk to.
  //
  // Chrome does not reliably surface a MIDI device that disappears and comes
  // back. After re-flashing the module, `onstatechange` often never fires for
  // the returning port and `access.outputs` keeps serving the stale entry — so
  // the page sits there believing it is connected to something that is gone,
  // and only a reload (which builds a fresh MIDIAccess) recovers.
  //
  // Re-requesting access is precisely what a reload does, minus the reload. The
  // permission is already granted so it neither prompts nor costs anything
  // visible. This runs *only* while no usable port is present, so a healthy
  // session generates no traffic whatsoever.
  // ---------------------------------------------------------------------------
  const kRescanIntervalMs = 2000;
  let rescanTimer: ReturnType<typeof setInterval> | null = null;

  function startAutoRescan() {
    if (rescanTimer !== null) return;
    rescanTimer = setInterval(() => {
      if (get(store).connected) {
        stopAutoRescan();
        return;
      }
      void scan();
    }, kRescanIntervalMs);
  }

  function stopAutoRescan() {
    if (rescanTimer === null) return;
    clearInterval(rescanTimer);
    rescanTimer = null;
  }

  /** Request Web MIDI access and enumerate available ports. */
  async function scan() {
    try {
      store.update((s) => ({ ...s, error: null }));
      access = await navigator.requestMIDIAccess({ sysex: true });
      access.onstatechange = (e: Event) => {
        refreshList();
        // An output port appearing always warrants a fresh dump, even when
        // refreshList() saw no change worth syncing — a fast re-enumeration
        // after a firmware flash can return the same port id with the flags
        // never observably dropping.  Bumping the nonce directly replaces the
        // old trick of forcing a deviceConnected false→true edge across a
        // microtask, which was unreliable: reactive batching could collapse
        // the pair and the consumer would see no change at all.  That is why
        // the page had to be reloaded to pick the module up.
        const portEvent = e as MIDIConnectionEvent;
        if (
          portEvent.port?.type === "output" &&
          portEvent.port?.state === "connected" &&
          get(store).connected
        ) {
          store.update((s) => ({ ...s, syncNonce: s.syncNonce + 1 }));
        }
      };
      refreshList();
    } catch (e) {
      store.update((s) => ({ ...s, error: String(e) }));
    }
  }

  /** Connect to the currently selected (or specified) output port. */
  function connect(outputId?: string) {
    const state = get(store);
    const id = outputId ?? state.selectedOutput;
    if (!id) {
      store.update((s) => ({ ...s, error: "No device selected" }));
      return;
    }
    store.update((s) => ({
      ...s,
      connected: true,
      deviceConnected: true,
      selectedOutput: id,
      // An id passed in explicitly is a deliberate choice; falling back to the
      // stored selection is not.
      userPickedOutput: outputId !== undefined || s.userPickedOutput,
      error: null,
    }));
    subscribeInputs();
  }

  function getOutput(): MIDIOutput | null {
    if (!access) return null;
    const { selectedOutput } = get(store);
    if (!selectedOutput) return null;
    return access.outputs.get(selectedOutput) ?? null;
  }

  function setChannel(ch: number) {
    channel = Math.max(0, Math.min(15, ch - 1)); // user passes 1-16
  }

  /**
   * The single outbound path.  Every send goes through here so that a message
   * can never be dropped silently: previously each sender did `getOutput()?.
   * send(...)`, and when the port failed to resolve the optional-chain turned
   * the whole thing into a no-op with no error and no clue — the UI still read
   * "Connected", because that badge only reports that *some* port exists.
   * txBytes gives a counter to compare directly against loopMIDI's.
   */
  function sendRaw(bytes: number[] | Uint8Array): void {
    const out = getOutput();
    if (!out) {
      store.update((s) => ({
        ...s,
        error: "No MIDI output resolved — message not sent",
      }));
      return;
    }
    out.send(bytes as number[]);
    emitTraffic("out", bytes, out.name ?? "?");
    store.update((s) => ({
      ...s,
      txBytes: s.txBytes + bytes.length,
      error: null,
    }));
  }

  function sendCC(cc: number, value: number /* 0-127 */) {
    lastSent.set(cc & 0x7f, { value: value & 0x7f, t: performance.now() });
    sendRaw([0xb0 | channel, cc & 0x7f, value & 0x7f]);
  }

  function sendNoteOn(note: number, velocity = 100) {
    sendRaw([0x90 | channel, note & 0x7f, velocity & 0x7f]);
  }

  function sendNoteOff(note: number) {
    sendRaw([0x80 | channel, note & 0x7f, 0]);
  }

  function sendProgramChange(program: number /* 1-5 → VoiceMode */) {
    sendRaw([0xc0 | channel, (program - 1) & 0x7f]);
  }

  /** CC 64 sustain pedal — keeps gate high */
  function sendSustain(on: boolean) {
    sendCC(64, on ? 127 : 0);
  }

  /**
   * CC 119 — drone return: release the gate and let the voices run free.
   *
   * Not CC 64. Sustain is the standard "hold the gate high" pedal message, so
   * on hardware it does the *opposite* of returning to drone — it latches the
   * gate open. Both platforms already implement CC 119 as drone return, which
   * is the message this actually means.
   */
  function sendDroneReturn() {
    sendCC(119, 127);
  }

  /** CC 123 — All Notes Off / panic */
  function sendPanic() {
    sendCC(123, 0);
  }

  return {
    subscribe: store.subscribe,
    scan,
    connect,
    setChannel,
    sendCC,
    sendNoteOn,
    sendNoteOff,
    sendProgramChange,
    sendSustain,
    sendDroneReturn,
    sendPanic,
    sendSysEx,
    // Choosing a port from the dropdown is also the way back from ✕ — the
    // disconnected branch of ConnectionBar offers only this select and Rescan,
    // and Rescan just re-runs auto-selection.  Previously this set the id and
    // nothing else, so after disconnecting there was no route back to a
    // connected state at all.  Marks the choice explicit so auto-selection
    // stops second-guessing it.
    selectOutput: (id: string) => {
      store.update((s) => ({
        ...s,
        selectedOutput: id,
        userPickedOutput: true,
        connected: true,
        deviceConnected: true,
        syncNonce: s.syncNonce + 1,
        error: null,
      }));
      subscribeInputs();
    },
    selectInput: (id: string) =>
      store.update((s) => ({ ...s, selectedInput: id })),
    onCC,
    onSysEx,
    onTraffic,
    resetCounters,
    shouldIgnoreInbound,
  };
}

export const midi = createMidi();
