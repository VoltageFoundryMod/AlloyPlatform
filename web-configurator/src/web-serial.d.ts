/**
 * Minimal ambient types for the Web Serial API.
 *
 * TypeScript's `dom` lib does not ship them, and the project pins
 * `types: ["svelte", "vite/client"]`, so `@types/w3c-web-serial` would not be
 * picked up even if it were installed. Only what `lib/serial.ts` actually
 * touches is declared here — this is a shim, not a spec transcription.
 *
 * `navigator.serial` itself is deliberately left off: the call sites cast
 * through `any` because the API is Chromium-only and the cast is where that
 * fact is documented.
 */

interface SerialPortInfo {
  usbVendorId?: number;
  usbProductId?: number;
}

interface SerialOptions {
  baudRate: number;
}

declare class SerialPort extends EventTarget {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: SerialOptions): Promise<void>;
  close(): Promise<void>;
  getInfo(): SerialPortInfo;
}
