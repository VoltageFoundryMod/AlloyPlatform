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
  error: string | null;
}

function createSerial() {
  const store: Writable<SerialStore> = writable({
    supported: typeof navigator !== "undefined" && "serial" in navigator,
    connected: false,
    error: null,
  });

  // Subscribers receive each complete line received from the device
  const lineListeners: Array<(line: string) => void> = [];

  let port: SerialPort | null = null;
  let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  const encoder = new TextEncoder();
  const decoder = new TextDecoder();

  async function connect() {
    try {
      port = await (navigator as any).serial.requestPort({
        filters: [
          // Raspberry Pi Pico / RP2350 TinyUSB CDC VID/PID
          { usbVendorId: 0x2e8a },
        ],
      });
      await port.open({ baudRate: 115200 });
      writer = port.writable!.getWriter();
      store.update((s) => ({ ...s, connected: true, error: null }));
      _startReader();
    } catch (e) {
      store.update((s) => ({ ...s, error: String(e) }));
    }
  }

  async function disconnect() {
    try {
      writer?.releaseLock();
      await port?.close();
    } catch {
      // ignore
    }
    port = null;
    writer = null;
    store.update((s) => ({ ...s, connected: false }));
  }

  async function send(command: string) {
    if (!writer) return;
    await writer.write(encoder.encode(command + "\r\n"));
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
      // port closed
    } finally {
      reader.releaseLock();
      store.update((s) => ({ ...s, connected: false }));
    }
  }

  return {
    subscribe: store.subscribe,
    connect,
    disconnect,
    send,
    onLine,
  };
}

export const serial = createSerial();
