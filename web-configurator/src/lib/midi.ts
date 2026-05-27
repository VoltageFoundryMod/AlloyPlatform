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

export interface MidiStore {
  supported: boolean;
  scanned: boolean;
  connected: boolean;
  deviceConnected: boolean;
  outputs: MidiPortInfo[];
  inputs: MidiPortInfo[];
  selectedOutput: string | null;
  selectedInput: string | null;
  error: string | null;
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
    error: null,
  });

  let access: MIDIAccess | null = null;
  let channel = 0; // 0-based, i.e. MIDI channel 1

  // CC listeners registered by consumers
  type CCListener = (cc: number, value: number) => void;
  const ccListeners = new Set<CCListener>();

  // SysEx listeners — receive the body bytes between F0 and F7
  type SysExListener = (body: Uint8Array) => void;
  const sysexListeners = new Set<SysExListener>();

  function handleMidiMessage(event: MIDIMessageEvent) {
    const data = event.data;
    if (!data || data.length < 1) return;
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
    const out = getOutput();
    if (!out) return;
    // Full message: F0 7D 41 46 <cmd> [payload] F7
    out.send(
      new Uint8Array([0xf0, 0x7d, 0x41, 0x46, cmd & 0x7f, ...payload, 0xf7]),
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
      const selectedOutput =
        s.selectedOutput && outputs.some((o) => o.id === s.selectedOutput)
          ? s.selectedOutput
          : (outputs.find((o) => /alloy/i.test(o.name))?.id ??
            outputs[0]?.id ??
            null);
      const selectedInput =
        s.selectedInput && inputs.some((i) => i.id === s.selectedInput)
          ? s.selectedInput
          : (inputs.find((i) => /alloy/i.test(i.name))?.id ??
            inputs[0]?.id ??
            null);
      // Auto-connect: if a port is available, mark connected immediately.
      // No manual "Connect" click required — mirrors VCV behaviour where
      // selecting a port is sufficient.  Explicit disconnect (user clicks ✕)
      // sets connected=false; it stays false until the next port-change event
      // brings a port back, at which point we auto-reconnect.
      const portAvailable = selectedOutput !== null;
      const connected = portAvailable ? true : false;
      const deviceConnected = connected;
      return {
        ...s,
        scanned: true,
        connected,
        outputs,
        inputs,
        selectedOutput,
        selectedInput,
        deviceConnected,
      };
    });
    if (get(store).connected) subscribeInputs();
  }

  /** Request Web MIDI access and enumerate available ports. */
  async function scan() {
    try {
      store.update((s) => ({ ...s, error: null }));
      access = await navigator.requestMIDIAccess({ sysex: true });
      access.onstatechange = (e: Event) => {
        refreshList();
        // Force a deviceConnected false→true edge whenever an output port
        // becomes available.  Without this, if Chrome never fires a
        // 'disconnected' event for the port (fast re-enumeration after a
        // firmware flash), deviceConnected stays true→true and the $effect
        // in App.svelte never re-fires to request a fresh config dump.
        const portEvent = e as MIDIConnectionEvent;
        if (
          portEvent.port.type === "output" &&
          portEvent.port.state === "connected"
        ) {
          store.update((s) => ({ ...s, deviceConnected: false }));
          // Next microtask: restore true so the $effect sees the edge.
          Promise.resolve().then(() =>
            store.update((s) => ({
              ...s,
              deviceConnected: get(store).connected,
            })),
          );
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

  function sendCC(cc: number, value: number /* 0-127 */) {
    getOutput()?.send([0xb0 | channel, cc & 0x7f, value & 0x7f]);
  }

  function sendNoteOn(note: number, velocity = 100) {
    getOutput()?.send([0x90 | channel, note & 0x7f, velocity & 0x7f]);
  }

  function sendNoteOff(note: number) {
    getOutput()?.send([0x80 | channel, note & 0x7f, 0]);
  }

  function sendProgramChange(program: number /* 1-5 → VoiceMode */) {
    getOutput()?.send([0xc0 | channel, (program - 1) & 0x7f]);
  }

  /** CC 64 sustain pedal — keeps gate high */
  function sendSustain(on: boolean) {
    sendCC(64, on ? 127 : 0);
  }

  /** CC 123 — All Notes Off / panic */
  function sendPanic() {
    sendCC(123, 0);
  }

  function disconnect() {
    // Keep access and port lists so the user can reconnect without rescanning.
    store.update((s) => ({
      ...s,
      connected: false,
      deviceConnected: false,
      error: null,
    }));
  }

  return {
    subscribe: store.subscribe,
    scan,
    connect,
    disconnect,
    setChannel,
    sendCC,
    sendNoteOn,
    sendNoteOff,
    sendProgramChange,
    sendSustain,
    sendPanic,
    sendSysEx,
    selectOutput: (id: string) =>
      store.update((s) => ({ ...s, selectedOutput: id })),
    selectInput: (id: string) =>
      store.update((s) => ({ ...s, selectedInput: id })),
    onCC,
    onSysEx,
  };
}

export const midi = createMidi();
