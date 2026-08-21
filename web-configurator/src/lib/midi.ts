import { SYSEX_MFR, activeDev, type SysExDev } from "./patchSync";
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

  // --- Link state -----------------------------------------------------------
  // `connected` above only ever meant "an output port exists", which is a much
  // weaker claim than the UI was making with it: a virtual port with nothing
  // behind it, a stale entry Chrome kept after a re-flash, or Rack with its
  // MIDI output unset all look identical to a live module.  Whether a module
  // is actually *there* is a different question, and the discovery probe
  // answers it — so it is tracked separately rather than folded into a badge
  // that cannot tell the two apart.

  /** True while the discovery probe is running and nothing has answered yet. */
  probing: boolean;
  /** True once a module has answered on this connection. */
  moduleAnswered: boolean;
  /** Name of the module that answered, for the status line. */
  moduleName: string | null;
  /** performance.now() when it last answered — a dump or a feedback CC. */
  lastAnswerAt: number | null;
  /** True when the probe ran to exhaustion with no reply. */
  probeFailed: boolean;
  /** Output port id twinned with the input a module last answered on, or null.
   *  Outranks port-name heuristics in pickPort() — see adoptAnsweringPort(). */
  answeredOutput: string | null;
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
    probing: false,
    moduleAnswered: false,
    moduleName: null,
    lastAnswerAt: null,
    probeFailed: false,
    answeredOutput: null,
  });

  let access: MIDIAccess | null = null;
  let channel = 0; // 0-based, i.e. MIDI channel 1

  /**
   * Output ports already known to be present, by id.
   *
   * `statechange` does not only mean "a device arrived": Web MIDI fires it for
   * open/close transitions too, and `send()` opens a port implicitly — with
   * `state` still "connected", since that field reports device presence, not
   * whether the port is open.  Bumping syncNonce on the bare `state` check
   * therefore made the page restart its own discovery probe every time the
   * probe *sent* something to a port it had not written to before, and with a
   * new MIDIAccess resetting every port to closed, that happened on every
   * cycle: the probe re-entered its 800 ms fast phase forever instead of
   * settling into the 5 s retry.
   *
   * Comparing against this roster is what separates the two.  Ids are stable
   * across MIDIAccess instances — pickPort() already relies on that to keep a
   * selection across a rescan — so it deliberately survives scan().
   */
  const knownOutputs = new Set<string>();

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
  // So a CC is ignored on the way in when we touched that control within the
  // last LOCAL_EDIT_MS.  Full syncs (SysEx dump, serial dump, file import)
  // deliberately bypass this — they are explicit "the device is the truth"
  // moments, not feedback.
  //
  // ⚠ The window is the *whole* rule, and it used to have a second clause: drop
  // an inbound CC whenever it repeated the value we last sent, with no time
  // bound at all.  That is not echo suppression, it is a permanent veto — the
  // record is only ever written on send, so once the page had sent a value the
  // device could never report that value again for as long as the tab was open.
  //
  // Alloy Coil's WARP switch is where it became obvious, because a two-state
  // control revisits its values constantly: click WARP off on the panel, then
  // hold the button on the module, and the press (127) arrives but the release
  // (0) matches what the page last sent and is thrown away.  The module warps
  // and un-warps correctly, the switch on screen latches on and stays there.
  //
  // The same trap was always there for knobs, just quieter — a knob returning
  // to a value the page had previously set would stick.  Both clauses covered
  // the echo, since an echo arrives within one 250 ms feedback tick and lands
  // well inside the window; only the unbounded one could reject the truth.
  // ---------------------------------------------------------------------------
  const LOCAL_EDIT_MS = 400;
  /** performance.now() of the last CC the page sent, per CC number. The value
   *  itself is deliberately not kept — see the warning above. */
  const lastSentAt = new Map<number, number>();

  /** True when an inbound CC is our own echo or lands mid-gesture. */
  function shouldIgnoreInbound(cc: number): boolean {
    const t = lastSentAt.get(cc);
    if (t === undefined) return false;
    return performance.now() - t < LOCAL_EDIT_MS;
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

  // SysEx listeners — receive the body bytes between F0 and F7, plus the name
  // of the input port it arrived on.  The port name is what tells the discovery
  // probe which cable the module is actually on; see adoptAnsweringPort().
  type SysExListener = (body: Uint8Array, portName: string) => void;
  const sysexListeners = new Set<SysExListener>();

  /**
   * Run one listener, isolated.
   *
   * Every inbound message goes through a single handler shared by every
   * listener, so a throw in any one of them propagates out of the MIDI event
   * and takes the rest of the message — and, for a fault that repeats, every
   * message after it — with it.  The page then receives nothing and sends
   * nothing, with no error surfaced anywhere, which reads as a dead MIDI link
   * rather than as the bug it is.  Logging beats swallowing: the console names
   * the listener that failed.
   */
  function runListener(fn: () => void): void {
    try {
      fn();
    } catch (e) {
      console.error("[midi] listener threw on an inbound message:", e);
    }
  }

  function handleMidiMessage(event: MIDIMessageEvent) {
    const data = event.data;
    if (!data || data.length < 1) return;
    const portName = (event.target as MIDIInput | null)?.name ?? "?";
    emitTraffic("in", data, portName);
    store.update((s) => ({ ...s, rxBytes: s.rxBytes + data.length }));
    const status = data[0];

    // SysEx: data[0] = 0xF0, data includes F0 and trailing F7
    if (status === 0xf0) {
      const endIdx =
        data[data.length - 1] === 0xf7 ? data.length - 1 : data.length;
      const body = data.slice(1, endIdx);
      sysexListeners.forEach((fn) => runListener(() => fn(body, portName)));
      return;
    }

    if (data.length < 3) return;
    const type = status & 0xf0;
    if (type === 0xb0) {
      // Control Change
      noteDeviceAlive(data[1]);
      ccListeners.forEach((fn) => runListener(() => fn(data[1], data[2])));
    }
  }

  // ---------------------------------------------------------------------------
  // Liveness — "when did the module last say something of its own?"
  //
  // Feedback CC is the only continuous evidence a module is still there, but it
  // is not proof on its own: on a loopback port (loopMIDI, IAC, Rack) every CC
  // this page sends comes straight back, and counting that as the module
  // talking would make a dead port look alive.  Our own echo is excluded the
  // same way the UI excludes it.
  //
  // Throttled because a knob sweep is hundreds of messages a second and each
  // store write wakes every subscriber; the timestamp is read by a status line
  // that updates once a second.
  // ---------------------------------------------------------------------------
  const ALIVE_THROTTLE_MS = 500;
  let lastAliveNote = 0;

  function noteDeviceAlive(cc: number): void {
    if (shouldIgnoreInbound(cc)) return;
    const now = performance.now();
    if (now - lastAliveNote < ALIVE_THROTTLE_MS) return;
    lastAliveNote = now;
    store.update((s) => (s.moduleAnswered ? { ...s, lastAnswerAt: now } : s));
  }

  function subscribeInputs() {
    if (!access) return;
    access.inputs.forEach((input) => {
      input.onmidimessage = handleMidiMessage;
    });
  }

  /**
   * Detach every handler on a MIDIAccess we are about to stop using.
   *
   * Each requestMIDIAccess() hands back a *new* MIDIAccess with its own set of
   * MIDIInput objects, and the ones from the previous call keep delivering:
   * their onmidimessage is still assigned, and the listener registration keeps
   * them alive. So every rescan added a complete extra copy of the inbound
   * path. The symptom is one outgoing message coming back five times in the
   * monitor — one per MIDIAccess accumulated over the session — and it is not
   * cosmetic: each copy runs the CC and SysEx listeners again, so the patch
   * dump was applied N times and the byte counters read N× high.
   */
  function releaseAccess(a: MIDIAccess | null) {
    if (!a) return;
    a.onstatechange = null;
    a.inputs.forEach((input) => {
      input.onmidimessage = null;
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
   * cmd     — command byte (e.g. SysexCmd.REQUEST = 0x01)
   * payload — additional data bytes after the header (7-bit safe)
   * dev     — signature to address.  Defaults to the module on screen; pass
   *           SYSEX_DEV_BROADCAST to reach whichever module is out there, which
   *           only REQUEST_DUMP may do.
   */
  function sendSysEx(
    cmd: number,
    payload: number[],
    dev: SysExDev = activeDev(),
  ): void {
    // Full message: F0 7D <id0> <id1> <cmd> [payload] F7
    sendRaw(
      new Uint8Array([
        0xf0,
        SYSEX_MFR,
        dev[0],
        dev[1],
        cmd & 0x7f,
        ...payload,
        0xf7,
      ]),
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
    answered: string | null = null,
  ): string | null {
    const present = (id: string | null) =>
      !!id && ports.some((p) => p.id === id);

    // A deliberate choice from the dropdown is never overridden.
    if (userPicked && present(current)) return current;

    // A port a module actually answered on outranks every guess below it: those
    // are name heuristics, this is evidence.  Without it a rack running
    // alongside hardware loses the port it was found on the next time anything
    // re-enumerates, because the name rule right below prefers the hardware.
    if (present(answered)) return answered;

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
      if (o.state !== "disconnected") {
        outputs.push({ id: o.id, name: o.name ?? o.id });
        knownOutputs.add(o.id);
      } else {
        // Gone as far as enumeration is concerned, so coming back counts as an
        // arrival even if the disconnect event itself was never delivered.
        knownOutputs.delete(o.id);
      }
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
        s.answeredOutput,
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
        // A different port, or no port at all, is a different device until one
        // says otherwise. Carrying the old answer across is how the UI ends up
        // claiming a module that is no longer on the other end.
        ...(needsSync || !connected
          ? {
              moduleAnswered: false,
              moduleName: null,
              lastAnswerAt: null,
              probeFailed: false,
              answeredOutput: null,
            }
          : {}),
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
      const previous = access;
      access = await navigator.requestMIDIAccess({ sysex: true });
      // Order matters: adopt the new access first, then release the old one.
      // Releasing first would leave a window with no inbound path at all, and
      // if the request throws we would have torn down a working subscription
      // to replace it with nothing.
      if (previous && previous !== access) releaseAccess(previous);
      access.onstatechange = (e: Event) => {
        // An output port *arriving* always warrants a fresh dump, even when
        // refreshList() saw no change worth syncing — a fast re-enumeration
        // after a firmware flash can return the same port id with the flags
        // never observably dropping.  Bumping the nonce directly replaces the
        // old trick of forcing a deviceConnected false→true edge across a
        // microtask, which was unreliable: reactive batching could collapse
        // the pair and the consumer would see no change at all.  That is why
        // the page had to be reloaded to pick the module up.
        //
        // Arriving, not merely opening: see knownOutputs.  Settled here rather
        // than after refreshList(), which re-seeds the roster from the port
        // list and would make every arrival look like one already known.
        const port = (e as MIDIConnectionEvent).port;
        let arrived = false;
        if (port?.type === "output") {
          if (port.state === "connected") {
            arrived = !knownOutputs.has(port.id);
            knownOutputs.add(port.id);
          } else {
            knownOutputs.delete(port.id);
          }
        }
        refreshList();
        if (arrived && get(store).connected) {
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
    sendRawTo(out, bytes);
  }

  /** Hand bytes to one specific port and account for them. */
  function sendRawTo(out: MIDIOutput, bytes: number[] | Uint8Array): void {
    out.send(bytes as number[]);
    emitTraffic("out", bytes, out.name ?? "?");
    store.update((s) => ({
      ...s,
      txBytes: s.txBytes + bytes.length,
      error: null,
    }));
  }

  /**
   * Widen the discovery probe: the selected port, then the other ports an Alloy
   * module could plausibly be behind.  For the *slow* phase of the probe only —
   * the fast phase asks the selected port alone, so a correct guess is never
   * raced (see the sync effect in App.svelte).
   *
   * The asymmetry this repairs: inbound has never been port-filtered —
   * subscribeInputs() listens on all of them — while outbound goes to a single
   * port pickPort() had to guess.  Guess wrong and the result is the symptom
   * that is hard to read: the module's own dumps arrive and the page looks
   * connected, but nothing the page *asks* is ever heard, so the automatic sync
   * never happens and only a manual "Sync to web" from the module's menu
   * appears to work.  Guessing wrong is not exotic — a VCV Rack setup on two
   * one-way virtual cables has the page's inbound and outbound ports under
   * different names, and hardware plugged in alongside Rack outranks the
   * loopback port by name, so the probe goes to the board while Rack, which is
   * what you are looking at, never sees it.
   *
   * Candidates, not every port.  An earlier version of this did send to all of
   * them, and that is not safe on a machine with a lot of gear: an unknown
   * SysEx handed to a stranger's driver can stall it, and `send()` is
   * synchronous, so one bad port takes the page's main thread with it — the
   * page then detects nothing and controls nothing, which looks nothing like a
   * MIDI routing problem.  An Alloy module is either named for itself or behind
   * a virtual cable, so those two names plus whatever is already selected cover
   * every real case and nothing else gets spoken to.
   *
   * The selected port is always first and is sent exactly what it was sent
   * before, so the path that already worked cannot regress.  Probe only:
   * REQUEST_DUMP asks and changes nothing.  An APPLY_PATCH fanned out this way
   * would reach modules the user is not driving.
   */
  function broadcastSysExWide(
    cmd: number,
    payload: number[],
    dev: SysExDev,
  ): void {
    const msg = new Uint8Array([
      0xf0,
      SYSEX_MFR,
      dev[0],
      dev[1],
      cmd & 0x7f,
      ...payload,
      0xf7,
    ]);
    // Unchanged behaviour first: whatever the page would have asked anyway.
    sendRaw(msg);
    if (!access) return;

    const { selectedOutput, userPickedOutput } = get(store);
    // Choosing a port from the dropdown means "talk to *this* module", and the
    // fan-out must not second-guess that — the same rule pickPort() applies to
    // the selection itself.
    //
    // Overriding it is worse than useless, because only the first answer owns
    // the session.  With a board on USB and Rack on a loopback cable, asking
    // both means Rack answers first every time — a virtual port round-trips in
    // microseconds where USB takes milliseconds — so selecting the board got
    // you Rack, and the board's dump arrived a moment later and was discarded
    // for coming from a module that had not won the race.  The board's CC
    // feedback still arrives, because inbound is never port-filtered, so the
    // page looks connected to something that answers while insisting it cannot
    // find the module you picked.
    if (userPickedOutput) return;

    access.outputs.forEach((out) => {
      if (out.id === selectedOutput) return; // already sent, above
      if (out.state === "disconnected") return; // stale entry Chrome kept
      const name = out.name ?? "";
      if (!NAME_ALLOY.test(name) && !NAME_VIRTUAL.test(name)) return;
      try {
        sendRawTo(out, msg);
      } catch {
        // A port that refuses the write must not stop the ones after it.
      }
    });
  }

  /**
   * A module answered on `inputPortName`; make the page talk back to it.
   *
   * The reply proves which cable the module is on, which is better evidence
   * than any name heuristic — so the matching output port (same name, as
   * virtual cables and USB devices both pair them) becomes the selected one.
   * Without this the page would identify the module correctly over one cable
   * and go on sending its knob moves down another.
   *
   * A deliberate choice from the dropdown still wins, and nothing happens when
   * the answering port is already selected or has no output twin.
   */
  function adoptAnsweringPort(inputPortName: string): void {
    if (!access) return;
    const s = get(store);
    const match = s.outputs.find((o) => o.name === inputPortName);
    if (!match) return;
    // Remembered even when the user picked the port themselves: the record of
    // where the answer came from is worth keeping either way, and pickPort()
    // still puts a deliberate choice first.
    if (s.userPickedOutput || match.id === s.selectedOutput) {
      store.update((cur) => ({ ...cur, answeredOutput: match.id }));
      return;
    }
    // Not a syncNonce bump: the probe that led here is mid-flight and has just
    // succeeded, and restarting it would tear down the listener reading this.
    store.update((cur) => ({
      ...cur,
      selectedOutput: match.id,
      answeredOutput: match.id,
      error: null,
    }));
  }

  function sendCC(cc: number, value: number /* 0-127 */) {
    lastSentAt.set(cc & 0x7f, performance.now());
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

  // ---------------------------------------------------------------------------
  // Discovery probe state, reported by App.svelte as it runs.
  //
  // Kept here rather than in the component because the connection bar and the
  // module badge both need it, and because it belongs with the rest of what is
  // known about the link.
  // ---------------------------------------------------------------------------

  /** The probe has started and nothing has answered yet. */
  function probeStarted(): void {
    store.update((s) => ({
      ...s,
      probing: true,
      probeFailed: false,
      moduleAnswered: false,
      moduleName: null,
      lastAnswerAt: null,
    }));
  }

  /** A module identified itself. This is the only thing that proves the link. */
  function probeAnswered(moduleName: string): void {
    lastAliveNote = performance.now();
    store.update((s) => ({
      ...s,
      probing: false,
      probeFailed: false,
      moduleAnswered: true,
      moduleName,
      lastAnswerAt: performance.now(),
    }));
  }

  /** The fast probe ran out of attempts. A slow retry keeps running. */
  function probeUnanswered(): void {
    store.update((s) =>
      s.moduleAnswered ? s : { ...s, probing: false, probeFailed: true },
    );
  }

  /** Ask for a fresh sync — the Retry button. Bumping the nonce re-runs the
   *  probe effect in App.svelte from the start. */
  function resync(): void {
    store.update((s) => ({ ...s, syncNonce: s.syncNonce + 1 }));
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
    broadcastSysExWide,
    adoptAnsweringPort,
    probeStarted,
    probeAnswered,
    probeUnanswered,
    resync,
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
