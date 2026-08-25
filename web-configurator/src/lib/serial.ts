/**
 * Web Serial connection layer — talks to the AlloyFlux serial console.
 *
 * Used for:
 *  - `config save [slot]` / `config load [slot]` / `config reset`
 *  - `status` — read back all current parameters
 *  - Any command not expressible as a MIDI CC (e.g. mode, filter type, fxorder)
 *
 * Usage:
 *   import { serial } from '$lib/serial';
 *   await serial.connect();
 *   await serial.send('status');
 *   serial.subscribe(line => console.log(line));
 */

import { writable, type Writable } from "svelte/store";

export interface SerialStore {
  supported: boolean;
  connected: boolean;
  /** True while an open attempt is in flight. The UI reports it rather than
   *  offering a button that would race with the attempt already running. */
  connecting: boolean;
  error: string | null;
}

const BAUD_RATE = 115200;
/** Raspberry Pi / RP2350 TinyUSB CDC vendor id. */
const VID_RASPBERRY_PI = 0x2e8a;

function createSerial() {
  const store: Writable<SerialStore> = writable({
    supported: typeof navigator !== "undefined" && "serial" in navigator,
    connected: false,
    connecting: false,
    error: null,
  });

  // Subscribers receive each complete line received from the device
  const lineListeners: Array<(line: string) => void> = [];

  let port: SerialPort | null = null;
  let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  let activeReader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  /** Guards two open attempts overlapping — the USB `connect` event and a
   *  click on Connect can land within milliseconds of each other, and both
   *  would otherwise open the same port. */
  let opening = false;
  /** Set by the ✕ button. An explicit disconnect has to stay disconnected;
   *  without it the auto-reconnect below would immediately undo the click. */
  let userDisconnected = false;
  let retryTimer: ReturnType<typeof setTimeout> | null = null;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  /**
   * Take ownership of `p` and start reading from it.
   *
   * The unconditional close() first is the whole reason a re-flash no longer
   * needs a page reload. getPorts() hands back the *same* SerialPort object
   * across a re-enumeration, and a port whose device vanished while open is
   * still open as far as the browser is concerned — the streams error, but the
   * handle stays ours until close() is called. open() on it throws
   * InvalidStateError, so the page could never retake a port it had already
   * used. Reloading worked only because it threw the stale handle away.
   *
   * close() on an already-closed port rejects harmlessly, so this is safe on
   * the first connect too.
   */
  async function _attach(p: SerialPort) {
    try {
      await p.close();
    } catch {
      /* was not open — the normal case on a first connect */
    }
    await p.open({ baudRate: BAUD_RATE });
    // Assigned only once open() has succeeded. Setting it before the await was
    // the bug that wedged the page for the rest of the session: a failed open
    // left a port we did not own in the variable, and every later attempt
    // short-circuited on the `if (port) return` guard below without so much as
    // an error message.
    port = p;
    writer = p.writable!.getWriter();
    userDisconnected = false;
    store.update((s) => ({ ...s, connected: true, error: null }));
    _startReader();
  }

  /**
   * The link is gone — release everything and let the port go.
   *
   * Idempotent: `port` is cleared first, so a drop noticed by both the reader
   * and the `disconnect` event only tears down once.
   */
  async function _onDrop() {
    const p = port;
    port = null;
    try {
      writer?.releaseLock();
    } catch {
      /* stream already errored */
    }
    writer = null;
    if (p) {
      try {
        await p.close();
      } catch {
        /* already closed, or the device is gone */
      }
    }
    store.update((s) => ({ ...s, connected: false }));
    if (!userDisconnected) _scheduleReconnect();
  }

  /**
   * Try to reclaim the port a few times, spaced out.
   *
   * One delayed shot was not enough. After a flash the device re-enumerates
   * while the old handle is still being torn down, and the OS needs a moment
   * beyond that to finish configuring the CDC ACM interface — an attempt
   * landing in either window fails, and nothing asked again. Retrying is free:
   * each attempt returns immediately once we are connected.
   */
  const RETRY_DELAYS_MS = [250, 750, 1500, 3000, 5000];
  function _scheduleReconnect(step = 0) {
    if (retryTimer !== null) clearTimeout(retryTimer);
    retryTimer = null;
    if (step >= RETRY_DELAYS_MS.length) return;
    retryTimer = setTimeout(async () => {
      retryTimer = null;
      if (port || userDisconnected) return;
      await autoConnect();
      if (!port && !userDisconnected) _scheduleReconnect(step + 1);
    }, RETRY_DELAYS_MS[step]);
  }

  function _cancelReconnect() {
    if (retryTimer !== null) clearTimeout(retryTimer);
    retryTimer = null;
  }

  /**
   * Silently reconnect to a previously-granted serial port on page load.
   * Uses getPorts() — no picker dialog. If no port was previously granted,
   * or the port is in use, this does nothing and the user can click Connect.
   */
  async function autoConnect() {
    if (port || opening) return;
    opening = true;
    store.update((s) => ({ ...s, connecting: true }));
    try {
      const ports: SerialPort[] = await (navigator as any).serial.getPorts();
      // Prefer a Raspberry Pi / RP2350 VID; fall back to first available.
      const match =
        ports.find((p) => {
          const info = (p as any).getInfo?.() ?? {};
          return info.usbVendorId === VID_RASPBERRY_PI;
        }) ?? ports[0];
      if (!match) return;
      await _attach(match);
    } catch {
      // No previously-granted port, or the device is not ready yet — stay
      // silent. _scheduleReconnect() is what asks again.
    } finally {
      opening = false;
      store.update((s) => ({ ...s, connecting: false }));
    }
  }

  async function connect() {
    if (opening) return;
    opening = true;
    store.update((s) => ({ ...s, connecting: true, error: null }));
    try {
      const chosen: SerialPort = await (navigator as any).serial.requestPort({
        filters: [
          // Raspberry Pi Pico / RP2350 TinyUSB CDC VID/PID
          { usbVendorId: VID_RASPBERRY_PI },
        ],
      });
      await _attach(chosen);
    } catch (e) {
      const msg = String(e);
      // Now that _attach() closes before opening, "Failed to open" no longer
      // covers the page's own stale handle — it really does mean another
      // application holds the port: PlatformIO monitor, the Arduino IDE serial
      // monitor, or a terminal session. Close those first, then retry.
      const friendly = msg.includes("Failed to open")
        ? "Port in use — close PlatformIO monitor / Arduino serial monitor and retry"
        : msg.includes("No port selected")
          ? "No port selected"
          : msg;
      store.update((s) => ({ ...s, error: friendly }));
    } finally {
      opening = false;
      store.update((s) => ({ ...s, connecting: false }));
    }
  }

  async function disconnect() {
    userDisconnected = true;
    _cancelReconnect();
    try {
      await activeReader?.cancel();
    } catch {
      /* ignore */
    }
    activeReader = null;
    await _onDrop();
  }

  async function send(command: string) {
    if (!writer) return;
    try {
      await writer.write(encoder.encode(command + "\r\n"));
    } catch {
      // The device went away mid-write. Tear down here too: a write is often
      // the first thing to notice a drop, and letting it throw would put the
      // failure in every caller instead of in the connection state.
      void _onDrop();
    }
  }

  function onLine(fn: (line: string) => void) {
    lineListeners.push(fn);
    return () => {
      const idx = lineListeners.indexOf(fn);
      if (idx !== -1) lineListeners.splice(idx, 1);
    };
  }

  async function _startReader() {
    if (!port?.readable) return;
    const reader = port.readable.getReader();
    activeReader = reader;
    let buf = "";
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += decoder.decode(value);
        const lines = buf.split(/\r?\n/);
        buf = lines.pop() ?? "";
        for (const line of lines) {
          if (line.trim()) lineListeners.forEach((fn) => fn(line.trim()));
        }
      }
    } catch {
      // port closed or cancelled
    } finally {
      try {
        reader.releaseLock();
      } catch {
        /* ignore */
      }
      activeReader = null;
      void _onDrop();
    }
  }

  if (typeof navigator !== "undefined" && "serial" in navigator) {
    const nav = (navigator as any).serial;

    // A serial port (re-)appeared — after a firmware flash, a replug, or a hub
    // waking up. Reconnect without requiring a page refresh.
    nav.addEventListener("connect", () => {
      if (userDisconnected) return; // ✕ means stay off until asked
      _scheduleReconnect();
    });

    // The device went away. The reader usually notices on its own, but not
    // reliably: a read() sitting on a port whose device vanished can fail to
    // settle at all, and then `port` stays non-null and every reconnect
    // attempt short-circuits on the guard in autoConnect(). Handling the event
    // makes the drop observable either way — and it fires *before* the
    // matching `connect` on a re-flash, so the port is free when that lands.
    nav.addEventListener("disconnect", (e: any) => {
      if (!port) return;
      const gone = e?.port ?? e?.target;
      if (gone && gone !== port) return; // some other serial device
      void _onDrop();
    });
  }

  return {
    subscribe: store.subscribe,
    autoConnect,
    connect,
    disconnect,
    send,
    onLine,
  };
}

export const serial = createSerial();
