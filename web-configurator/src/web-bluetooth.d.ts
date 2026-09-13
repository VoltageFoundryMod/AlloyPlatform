/**
 * Minimal ambient types for the Web Bluetooth API.
 *
 * Same shape and same reasoning as `web-serial.d.ts`: TypeScript's `dom` lib
 * does not ship them, the project pins `types: ["svelte", "vite/client"]` so
 * `@types/web-bluetooth` would not be picked up even if installed, and only
 * what `lib/bleMidi.ts` actually touches is declared. A shim, not a spec
 * transcription.
 *
 * `navigator.bluetooth` is deliberately left off for the same reason it is left
 * off there: the call site casts through `any`, and the cast is where "this is
 * Chromium-only" is documented rather than being quietly typed away.
 */

interface BluetoothRemoteGATTCharacteristic extends EventTarget {
  readonly value?: DataView;
  startNotifications(): Promise<BluetoothRemoteGATTCharacteristic>;
  stopNotifications(): Promise<BluetoothRemoteGATTCharacteristic>;
  /** Declared as Uint8Array rather than the spec's BufferSource on purpose:
   *  that alias resolves to ArrayBufferView<ArrayBuffer>, which a plain
   *  Uint8Array (generic over ArrayBufferLike since TS 5.7) does not satisfy.
   *  Only what bleMidi.ts actually passes needs declaring. */
  writeValueWithoutResponse(value: Uint8Array): Promise<void>;
}

interface BluetoothRemoteGATTService {
  getCharacteristic(
    uuid: string,
  ): Promise<BluetoothRemoteGATTCharacteristic>;
}

interface BluetoothRemoteGATTServer {
  readonly connected: boolean;
  connect(): Promise<BluetoothRemoteGATTServer>;
  disconnect(): void;
  getPrimaryService(uuid: string): Promise<BluetoothRemoteGATTService>;
}

interface BluetoothDevice extends EventTarget {
  readonly id: string;
  readonly name?: string;
  readonly gatt?: BluetoothRemoteGATTServer;
  forget?(): Promise<void>;
}

interface RequestDeviceOptions {
  filters?: Array<{ services?: string[]; name?: string; namePrefix?: string }>;
  optionalServices?: string[];
  acceptAllDevices?: boolean;
}
