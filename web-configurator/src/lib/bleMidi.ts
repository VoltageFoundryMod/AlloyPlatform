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
 * ⚠ iOS reaches none of this *from a browser*. WebKit ships neither Web
 * Bluetooth, Web MIDI nor Web Serial, and an installed PWA on iOS is still
 * WebKit — so the Controller is blind there however it is launched. The route
 * that does work is a native BLE stack under the same page, which is what
 * `lib/bleLink.ts` exists for: inside the Capacitor shell the packets below go
 * to CoreBluetooth instead of `navigator.bluetooth`, and nothing in this file
 * changes. (An iPad also still *plays* the module perfectly well through any
 * BLE-MIDI-aware app — that goes through the OS, not the browser.)
 *
 * This module owns the packet framing and nothing else — `lib/bleLink.ts` owns
 * the GATT link and which radio API reaches it. It hands
 * `midi.ts` whole MIDI messages in exactly the shape a `MIDIMessageEvent`
 * would have carried, which is what lets the discovery probe, the echo
 * suppression, the traffic tap and the byte counters stay in one place and work
 * over any transport.
 */

import { writable, get, type Writable } from "svelte/store";
import { bleLink, BleChooserCancelled, type BleSession } from "./bleLink";

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
  /** A BLE chooser can be opened. False on Firefox and on iOS *in a browser*;
   *  true in the native shells, where the radio is reached natively. */
  supported: boolean;
  /** Which transport backs the link — "native" inside the iOS/Android app. */
  kind: "web" | "native";
  /** A GATT link is up and notifications are flowing. */
  connected: boolean;
  /** True between the chooser closing and the characteristic being ready. */
  connecting: boolean;
  /** What the chooser called it — "Alloy Flux" for a module at defaults. */
  deviceName: string | null;
  error: string | null;
}

type MessageListener = (bytes: Uint8Array) => void;

/**
 * The last module we had a link to, so it can be reopened without a chooser.
 *
 * ⚠ The id is **origin- and platform-scoped and not portable** — a Web
 * Bluetooth device id means nothing to CoreBluetooth and vice versa. Stored
 * under a key that names the transport so the two shells cannot read each
 * other's, which on a shared `localStorage` (the native WebView keeps its own,
 * but a PWA and a tab do not) would otherwise produce a reconnect attempt
 * against an identifier the platform has never seen.
 */
const LAST_DEVICE_KEY = "ble-last-device";

interface LastDevice {
  kind: "web" | "native";
  id: string;
  name: string;
}

function readLastDevice(kind: "web" | "native"): LastDevice | null {
  try {
    const raw = localStorage.getItem(LAST_DEVICE_KEY);
    if (!raw) return null;
    const d = JSON.parse(raw) as Partial<LastDevice>;
    if (d.kind !== kind || typeof d.id !== "string" || !d.id) return null;
    return { kind, id: d.id, name: typeof d.name === "string" ? d.name : "" };
  } catch {
    return null;
  }
}

function writeLastDevice(d: LastDevice | null): void {
  try {
    if (d) localStorage.setItem(LAST_DEVICE_KEY, JSON.stringify(d));
    else localStorage.removeItem(LAST_DEVICE_KEY);
  } catch {
    // Storage blocked. Reconnect then needs the chooser, as it always did.
  }
}

function createBleMidi() {
  const link = bleLink();

  const store: Writable<BleMidiStore> = writable({
    supported: link.available,
    kind: link.kind,
    connected: false,
    connecting: false,
    deviceName: null,
    error: null,
  });

  let session: BleSession | null = null;

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
      .then(() => session?.write(packet))
      .then(
        () => undefined,
        (e: unknown) => {
          store.update((s) => ({ ...s, error: String(e) }));
        },
      );
  }

  /** Fragment one complete MIDI message into packets and queue them. */
  function send(bytes: number[] | Uint8Array): void {
    if (!session) return;
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

  function onDisconnected(): void {
    session = null;
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
   * Put up the device chooser and connect to what the user picks.
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
        error:
          "Bluetooth is not available in this browser — use Chrome or Edge, " +
          "or install the Alloy Controller app on iOS",
      }));
      return false;
    }

    store.update((s) => ({ ...s, connecting: true, error: null }));
    try {
      session = await link.connect({
        service: BLE_MIDI_SERVICE,
        characteristic: BLE_MIDI_CHAR,
        onPacket: parsePacket,
        onDisconnect: onDisconnected,
      });

      resetParser();
      writeChain = Promise.resolve();
      if (session) {
        writeLastDevice({
          kind: link.kind,
          id: session.deviceId,
          name: session.deviceName,
        });
      }
      store.update((s) => ({
        ...s,
        connected: true,
        connecting: false,
        deviceName: session?.deviceName ?? "BLE MIDI device",
        error: null,
      }));
      return true;
    } catch (e: unknown) {
      // A cancelled chooser is a decision, not a fault, so it must not leave an
      // error banner on screen. Both transports normalise to this one type —
      // see the note on BleChooserCancelled.
      const cancelled = e instanceof BleChooserCancelled;
      session = null;
      store.update((s) => ({
        ...s,
        connected: false,
        connecting: false,
        error: cancelled ? null : String(e),
      }));
      return false;
    }
  }

  /**
   * Reopen the last link without a chooser. Safe to call at any time.
   *
   * **Why this exists.** Locking a phone or switching apps tears the GATT link
   * down, and on a phone that happens constantly — every glance away from a
   * module mid-set. Without this, coming back means pressing Connect and
   * picking the module out of a dialog again, which is the difference between
   * an instrument and a toy.
   *
   * ⚠ **Silent in every failure case, deliberately.** Nobody asked for this
   * connection, so a failed attempt must look exactly like not having tried:
   * no error banner, no state change, no chooser. `link.reconnect` returns null
   * rather than throwing for precisely that reason. The manual Connect button
   * is always still there.
   *
   * ⚠ Never runs while a link is up or an attempt is in flight. `connecting`
   * gates the button too, so the user cannot race it.
   */
  async function autoConnect(): Promise<boolean> {
    if (!link.available || session || get(store).connecting) return false;

    const last = readLastDevice(link.kind);
    if (!last) return false;

    store.update((s) => ({ ...s, connecting: true }));
    try {
      const s = await link.reconnect(last.id, {
        service: BLE_MIDI_SERVICE,
        characteristic: BLE_MIDI_CHAR,
        deviceName: last.name,
        onPacket: parsePacket,
        onDisconnect: onDisconnected,
      });

      if (!s) {
        store.update((st) => ({ ...st, connecting: false }));
        return false;
      }

      session = s;
      resetParser();
      writeChain = Promise.resolve();
      store.update((st) => ({
        ...st,
        connected: true,
        connecting: false,
        deviceName: s.deviceName || last.name || "BLE MIDI device",
        error: null,
      }));
      return true;
    } catch {
      session = null;
      store.update((st) => ({ ...st, connecting: false }));
      return false;
    }
  }

  /**
   * Try again whenever the app comes back to the foreground.
   *
   * `visibilitychange` is the one signal both shells agree on: iOS and Android
   * fire it for the WebView on resume, and browsers fire it on tab focus. A
   * Capacitor `appStateChange` listener would be the native-only equivalent and
   * is not worth a second code path for the same event.
   *
   * Only fires when something was already remembered *and* the link is down, so
   * a foreground with a healthy connection costs a comparison and nothing else.
   */
  if (typeof document !== "undefined") {
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "visible") void autoConnect();
    });
  }

  function disconnect(): void {
    // Fire-and-forget: the native teardown is async, but the UI must not wait
    // on a radio to reflect a button press, and nothing here can fail in a way
    // the user could act on. The state reset below is the part that matters.
    void session?.disconnect();
    session = null;
    // ⚠ Forget the device. This is the *deliberate* disconnect — the user
    // pressed the button — and without this the next foreground would silently
    // undo it, which reads as a control that does not work.
    writeLastDevice(null);
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
    return session !== null && get(store).connected;
  }

  return {
    subscribe: store.subscribe,
    connect,
    autoConnect,
    disconnect,
    isConnected,
    send,
    onMessage,
  };
}

export const bleMidi = createBleMidi();
