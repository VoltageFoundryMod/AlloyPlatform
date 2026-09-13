/**
 * BLE MIDI transport — Milestone 78c.
 *
 * The browser half of `platform/src/ble_midi.cpp`. Speaks the standard BLE MIDI
 * GATT service over Web Bluetooth, so the page can drive a module with no
 * cable, no driver and no OS pairing: `requestDevice()` puts up the browser's
 * own chooser and the connection is a GATT link from there.
 *
 * **Why Web Bluetooth rather than Web MIDI.** Chrome on Android has no Web
 * Serial at all, and no dependable route from Web MIDI to a BLE MIDI
 * peripheral. Going at the GATT service directly is the only thing that works
 * on a phone — and it costs nothing on the desktop, where it becomes a third
 * way to reach the module alongside USB MIDI and the serial console.
 *
 * ⚠ iOS reaches none of this. WebKit ships neither Web Bluetooth, Web MIDI nor
 * Web Serial. An iPad still *plays* the module perfectly well through any
 * BLE-MIDI-aware app, because that goes through the OS rather than the browser;
 * it is the Controller specifically that cannot run there.
 *
 * This module owns the link and the packet framing and nothing else. It hands
 * `midi.ts` whole MIDI messages in exactly the shape a `MIDIMessageEvent`
 * would have carried, which is what lets the discovery probe, the echo
 * suppression, the traffic tap and the byte counters stay in one place and work
 * over any transport.
 */

import { writable, get, type Writable } from "svelte/store";

/** Fixed by the MIDI Manufacturers Association — every BLE MIDI device on
 *  every platform uses exactly these, which is why no driver is involved.
 *  Web Bluetooth requires them lowercase. */
const BLE_MIDI_SERVICE = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";
const BLE_MIDI_CHAR = "7772e5db-3868-4112-a1a9-f2669d106bf3";

/**
 * Bytes of payload per packet.
 *
 * The default ATT MTU is 23, leaving 20, and Web Bluetooth exposes no way to
 * read back what the connection actually negotiated — so, exactly as the
 * firmware does on its side, every packet we *send* is sized for the floor.
 * Inbound packets may be larger and the parser does not care.
 */
const MAX_TX_PACKET = 20;

export interface BleMidiStore {
  /** Web Bluetooth exists in this browser. False on Firefox and all of iOS. */
  supported: boolean;
  /** A GATT link is up and notifications are flowing. */
  connected: boolean;
  /** True between the chooser closing and the characteristic being ready. */
  connecting: boolean;
  /** What the chooser called it — "Alloy Flux" for a module at defaults. */
  deviceName: string | null;
  error: string | null;
}

type MessageListener = (bytes: Uint8Array) => void;

function createBleMidi() {
  const store: Writable<BleMidiStore> = writable({
    supported:
      typeof navigator !== "undefined" &&
      typeof (navigator as any).bluetooth !== "undefined",
    connected: false,
    connecting: false,
    deviceName: null,
    error: null,
  });

  let device: BluetoothDevice | null = null;
  let characteristic: BluetoothRemoteGATTCharacteristic | null = null;

  const listeners = new Set<MessageListener>();

  /** Register a sink for reassembled MIDI messages. Returns an unsubscriber. */
  function onMessage(fn: MessageListener): () => void {
    listeners.add(fn);
    return () => listeners.delete(fn);
  }

  function emit(bytes: Uint8Array): void {
    // One listener must not be able to stop the others, for the same reason
    // midi.ts wraps its own dispatch.
    listeners.forEach((fn) => {
      try {
        fn(bytes);
      } catch (e) {
        console.error("[bleMidi] listener threw", e);
      }
    });
  }

  // ---------------------------------------------------------------------------
  // Outbound
  //
  // A BLE MIDI packet is a header byte carrying the high 6 bits of a 13-bit
  // millisecond timestamp, then per-message timestamp bytes, then the MIDI
  // bytes. SysEx is the awkward one: F0 follows a timestamp in the first
  // packet, continuation packets carry a header byte and nothing but data, and
  // F7 gets a timestamp byte of its own immediately before it.
  // ---------------------------------------------------------------------------

  function headerByte(): number {
    return 0x80 | ((Date.now() >> 7) & 0x3f);
  }

  function timestampByte(): number {
    return 0x80 | (Date.now() & 0x7f);
  }

  /**
   * Writes are serialised through one promise chain.
   *
   * Chrome rejects with "GATT operation already in progress" if a second write
   * starts before the first settles, and a patch dump is five packets back to
   * back — so this is the normal path, not an edge case. Dropping a packet of a
   * SysEx would corrupt the whole message rather than lose one event.
   */
  let writeChain: Promise<void> = Promise.resolve();

  function enqueueWrite(packet: Uint8Array): void {
    writeChain = writeChain
      .then(() => characteristic?.writeValueWithoutResponse(packet))
      .then(
        () => undefined,
        (e: unknown) => {
          store.update((s) => ({ ...s, error: String(e) }));
        },
      );
  }

  /** Fragment one complete MIDI message into packets and queue them. */
  function send(bytes: number[] | Uint8Array): void {
    if (!characteristic) return;
    const data = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
    if (data.length === 0) return;

    if (data[0] !== 0xf0) {
      // Channel / system message — always fits in one packet.
      const pkt = new Uint8Array(data.length + 2);
      pkt[0] = headerByte();
      pkt[1] = timestampByte();
      pkt.set(data, 2);
      enqueueWrite(pkt);
      return;
    }

    // SysEx. Strip the caller's F0/F7 and re-add them where the framing wants
    // them; midi.ts hands over a complete F0…F7 message.
    let end = data.length;
    if (data[end - 1] === 0xf7) end--;
    const body = data.subarray(1, end);

    let src = 0;
    let first = true;
    for (;;) {
      const pkt: number[] = [headerByte()];
      if (first) {
        pkt.push(timestampByte(), 0xf0);
        first = false;
      }
      while (src < body.length && pkt.length < MAX_TX_PACKET) {
        pkt.push(body[src++] & 0x7f);
      }

      if (src >= body.length && pkt.length + 2 <= MAX_TX_PACKET) {
        pkt.push(timestampByte(), 0xf7);
        enqueueWrite(Uint8Array.from(pkt));
        return;
      }

      enqueueWrite(Uint8Array.from(pkt));

      if (src >= body.length) {
        // Body done but the trailer did not fit — send it alone.
        enqueueWrite(Uint8Array.from([headerByte(), timestampByte(), 0xf7]));
        return;
      }
    }
  }

  // ---------------------------------------------------------------------------
  // Inbound
  //
  // Mirror of the firmware's parser, and awkward for the same reason: status
  // bytes and timestamp bytes both have bit 7 set, so they can only be told
  // apart by position. After the header, and after a completed message, the
  // next high byte is a timestamp and the one after it is the status.
  //
  // Messages are emitted in exactly the shape a MIDIMessageEvent would have
  // carried — SysEx with its F0 and F7 intact — so midi.ts cannot tell where
  // they came from.
  // ---------------------------------------------------------------------------

  let inSysEx = false;
  let sysExBuf: number[] = [];
  let runningStatus = 0;

  function dataBytesFor(status: number): number {
    const t = status & 0xf0;
    return t === 0xc0 || t === 0xd0 ? 1 : 2;
  }

  function resetParser(): void {
    inSysEx = false;
    sysExBuf = [];
    runningStatus = 0;
  }

  function parsePacket(pkt: Uint8Array): void {
    if (pkt.length < 1) return;
    let i = 1; // [0] is the header byte

    while (i < pkt.length) {
      if (inSysEx) {
        const b = pkt[i];
        if (b & 0x80) {
          // A high byte inside SysEx is the timestamp before F7.
          i++;
          if (i < pkt.length && pkt[i] === 0xf7) {
            i++;
            emit(Uint8Array.from([0xf0, ...sysExBuf, 0xf7]));
          }
          // Anything else is malformed: drop the message whole rather than
          // hand half a patch to the parser.
          inSysEx = false;
          sysExBuf = [];
        } else {
          sysExBuf.push(b);
          i++;
        }
        continue;
      }

      if (!(pkt[i] & 0x80)) {
        // Running status: data bytes with no timestamp of their own.
        if (runningStatus === 0) {
          i++;
          continue;
        }
        const need = dataBytesFor(runningStatus);
        if (i + need > pkt.length) break;
        emit(Uint8Array.from([runningStatus, ...pkt.subarray(i, i + need)]));
        i += need;
        continue;
      }

      // A timestamp byte; the status normally follows it.
      i++;
      if (i >= pkt.length) break;

      if (pkt[i] & 0x80) {
        const status = pkt[i++];
        if (status === 0xf0) {
          inSysEx = true;
          sysExBuf = [];
          continue;
        }
        if (status >= 0xf8) continue; // realtime — no data bytes
        if (status === 0xf7) continue; // stray terminator
        if (status >= 0xf1 && status <= 0xf6) {
          // System common. Skip its data rather than misread it as a channel
          // message.
          i += status === 0xf2 ? 2 : status === 0xf1 || status === 0xf3 ? 1 : 0;
          runningStatus = 0;
          continue;
        }
        runningStatus = status;
      }

      if (runningStatus === 0) continue;
      const need = dataBytesFor(runningStatus);
      if (i + need > pkt.length) break;
      emit(Uint8Array.from([runningStatus, ...pkt.subarray(i, i + need)]));
      i += need;
    }
  }

  function onCharacteristicValue(event: Event): void {
    const target = event.target as BluetoothRemoteGATTCharacteristic | null;
    const view = target?.value;
    if (!view) return;
    parsePacket(new Uint8Array(view.buffer, view.byteOffset, view.byteLength));
  }

  function onDisconnected(): void {
    characteristic = null;
    resetParser();
    store.update((s) => ({
      ...s,
      connected: false,
      connecting: false,
      error: "Bluetooth device disconnected",
    }));
  }

  // ---------------------------------------------------------------------------
  // Link
  // ---------------------------------------------------------------------------

  /**
   * Put up the browser's device chooser and connect to what the user picks.
   *
   * **Must be called from a user gesture** — Web Bluetooth refuses otherwise,
   * which is why this is wired to a button and never to an effect or a retry
   * loop. That constraint is also the feature: there is no scanning in the
   * background and no pairing dialog, just a list the user chose to open.
   */
  async function connect(): Promise<boolean> {
    if (!get(store).supported) {
      store.update((s) => ({
        ...s,
        error: "Web Bluetooth is not available in this browser",
      }));
      return false;
    }

    store.update((s) => ({ ...s, connecting: true, error: null }));
    try {
      device = await (navigator as any).bluetooth.requestDevice({
        // Filtering on the service is what keeps the chooser to BLE MIDI
        // devices rather than every radio in the room, and it is also what
        // grants access to that service afterwards.
        filters: [{ services: [BLE_MIDI_SERVICE] }],
        optionalServices: [BLE_MIDI_SERVICE],
      });
      if (!device?.gatt) throw new Error("device has no GATT server");

      device.addEventListener("gattserverdisconnected", onDisconnected);

      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(BLE_MIDI_SERVICE);
      characteristic = await service.getCharacteristic(BLE_MIDI_CHAR);
      characteristic.addEventListener(
        "characteristicvaluechanged",
        onCharacteristicValue,
      );
      await characteristic.startNotifications();

      resetParser();
      writeChain = Promise.resolve();
      store.update((s) => ({
        ...s,
        connected: true,
        connecting: false,
        deviceName: device?.name ?? "BLE MIDI device",
        error: null,
      }));
      return true;
    } catch (e: unknown) {
      // A cancelled chooser throws NotFoundError; that is a decision, not a
      // fault, so it must not leave an error banner on screen.
      const cancelled = (e as { name?: string })?.name === "NotFoundError";
      characteristic = null;
      store.update((s) => ({
        ...s,
        connected: false,
        connecting: false,
        error: cancelled ? null : String(e),
      }));
      return false;
    }
  }

  function disconnect(): void {
    try {
      characteristic?.removeEventListener(
        "characteristicvaluechanged",
        onCharacteristicValue,
      );
      device?.removeEventListener("gattserverdisconnected", onDisconnected);
      device?.gatt?.disconnect();
    } catch {
      // Already gone. Nothing to report — the state update below is the point.
    }
    characteristic = null;
    device = null;
    resetParser();
    store.update((s) => ({
      ...s,
      connected: false,
      connecting: false,
      deviceName: null,
      error: null,
    }));
  }

  function isConnected(): boolean {
    return characteristic !== null && get(store).connected;
  }

  return {
    subscribe: store.subscribe,
    connect,
    disconnect,
    isConnected,
    send,
    onMessage,
  };
}

export const bleMidi = createBleMidi();
