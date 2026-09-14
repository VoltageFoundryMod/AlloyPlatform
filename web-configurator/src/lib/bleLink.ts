/**
 * The BLE transport seam — Milestone 80.
 *
 * `bleMidi.ts` owns the BLE MIDI packet framing and the inbound parser, and
 * neither of those cares one bit how the packets reach the radio. This file is
 * the line between them: a GATT link reduced to the four things the framing
 * actually needs — connect, write, notify, disconnect.
 *
 * **Why the seam exists.** Web Bluetooth is absent from WebKit and there is no
 * sign of it arriving (Apple has refused it over fingerprinting for years), so
 * the Controller cannot reach a module from any browser on iOS — Safari,
 * Chrome and Firefox there are all WebKit and all equally blind. The only route
 * to an iPhone is a native BLE stack, which means a native shell, which means
 * Capacitor. Capacitor runs the *same* Vite build inside a WKWebView and hands
 * it a plugin bridge to CoreBluetooth.
 *
 * So there are two implementations of one interface, and exactly one decision
 * point (`bleLink()` below). Everything above this file — framing, parser,
 * `MidiTransport`, the traffic tap, the discovery probe — is written once and
 * runs unmodified on all three platforms.
 *
 * ⚠ **Do not widen this interface to match Web Bluetooth.** It is deliberately
 * the smaller of the two APIs. Every method here has a faithful equivalent in
 * `@capacitor-community/bluetooth-le`, which is itself modelled on Web
 * Bluetooth — that overlap is the whole reason this adapter is 80 lines rather
 * than a rewrite. Reaching for something only one side has (MTU negotiation,
 * `watchAdvertisements`, raw descriptors) breaks the other platform silently.
 */

/**
 * A live GATT link to one module.
 *
 * Handed back by `connect()`, and dead once `disconnect()` resolves or the
 * `onDisconnect` callback fires — whichever happens first. Never reused.
 */
export interface BleSession {
  /** What the chooser called it — "Alloy Flux" for a module at defaults. */
  readonly deviceName: string;
  /**
   * Write one BLE MIDI packet, already framed.
   *
   * Callers serialise these themselves (see `bleMidi.ts`'s write chain);
   * neither implementation queues on your behalf.
   */
  write(packet: Uint8Array): Promise<void>;
  /** Idempotent. Must not throw on an already-dead link. */
  disconnect(): Promise<void>;
}

export interface BleConnectOptions {
  /** Lowercase UUID. Web Bluetooth rejects uppercase; Capacitor tolerates it. */
  service: string;
  characteristic: string;
  /** One inbound GATT notification, exactly as it came off the radio. */
  onPacket(packet: Uint8Array): void;
  /** The link dropped from the far end. Not called for a local disconnect. */
  onDisconnect(): void;
}

export interface BleLink {
  /** Which implementation this is — surfaced in the UI, and in bug reports. */
  readonly kind: "web" | "native";
  /**
   * Whether a chooser can be opened at all.
   *
   * False on Firefox and on every browser on iOS. Note this is a *capability*
   * check, not a permission or radio-power check: on native it is true before
   * the user has granted Bluetooth permission, because the permission prompt is
   * part of connecting.
   */
  readonly available: boolean;
  /** Opens the chooser. Throws `BleChooserCancelled` if the user backs out. */
  connect(opts: BleConnectOptions): Promise<BleSession>;
}

/**
 * The user closed the chooser without picking anything.
 *
 * A decision, not a fault, so it must not leave an error banner on screen. Both
 * implementations normalise to this because they report it differently —
 * Web Bluetooth throws `NotFoundError`, the Capacitor plugin throws a plain
 * `Error` whose message differs per platform. Sniffing that at the call site
 * would put platform detection back above the seam, which is the one thing
 * this file exists to prevent.
 */
export class BleChooserCancelled extends Error {
  constructor() {
    super("Device chooser cancelled");
    this.name = "BleChooserCancelled";
  }
}

/**
 * True when running inside a Capacitor native shell (iOS or Android app).
 *
 * Deliberately a hand-rolled check rather than `Capacitor.isNativePlatform()`:
 * importing `@capacitor/core` at module scope pulls the runtime into the plain
 * web bundle, which is the one build where none of this is wanted. The global
 * is set by the native bridge before any app code runs.
 */
export function isNativeShell(): boolean {
  if (typeof window === "undefined") return false;
  const cap = (window as any).Capacitor;
  return Boolean(cap?.isNativePlatform?.() ?? cap?.isNative);
}

let cached: BleLink | null = null;

/**
 * The link implementation for whatever this build is running inside.
 *
 * Resolved once and memoised — the answer cannot change over a page lifetime,
 * and `bleMidi.ts` reads `.available` during store construction.
 *
 * The native implementation is loaded through a dynamic `import()` so the
 * Capacitor plugin stays in its own chunk and never ships in the bytes a
 * browser downloads. That is also why `connect()` is async on the interface:
 * the plugin is not resolved until someone actually presses connect.
 */
export function bleLink(): BleLink {
  if (cached) return cached;
  cached = isNativeShell() ? createNativeLink() : createWebLink();
  return cached;
}

// -----------------------------------------------------------------------------
// Web Bluetooth
//
// Chrome and Edge, on desktop and on Android. This is the original transport,
// moved here verbatim in behaviour — the `gattserverdisconnected` listener, the
// service filter that doubles as the access grant, and the user-gesture
// requirement are all unchanged.
// -----------------------------------------------------------------------------

function createWebLink(): BleLink {
  return {
    kind: "web",
    available:
      typeof navigator !== "undefined" &&
      typeof (navigator as any).bluetooth !== "undefined",

    async connect(opts: BleConnectOptions): Promise<BleSession> {
      let device: BluetoothDevice;
      try {
        device = await (navigator as any).bluetooth.requestDevice({
          // Filtering on the service keeps the chooser to BLE MIDI devices
          // rather than every radio in the room, and it is also what grants
          // access to that service afterwards.
          filters: [{ services: [opts.service] }],
          optionalServices: [opts.service],
        });
      } catch (e: unknown) {
        if ((e as { name?: string })?.name === "NotFoundError") {
          throw new BleChooserCancelled();
        }
        throw e;
      }
      if (!device?.gatt) throw new Error("device has no GATT server");

      const onGattDisconnected = () => opts.onDisconnect();
      const onValue = (event: Event) => {
        const view = (event.target as BluetoothRemoteGATTCharacteristic | null)
          ?.value;
        if (!view) return;
        opts.onPacket(
          new Uint8Array(view.buffer, view.byteOffset, view.byteLength),
        );
      };

      device.addEventListener("gattserverdisconnected", onGattDisconnected);

      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(opts.service);
      const characteristic = await service.getCharacteristic(
        opts.characteristic,
      );
      characteristic.addEventListener("characteristicvaluechanged", onValue);
      await characteristic.startNotifications();

      return {
        deviceName: device.name ?? "BLE MIDI device",
        write: (packet) => characteristic.writeValueWithoutResponse(packet),
        async disconnect() {
          try {
            characteristic.removeEventListener(
              "characteristicvaluechanged",
              onValue,
            );
            device.removeEventListener(
              "gattserverdisconnected",
              onGattDisconnected,
            );
            device.gatt?.disconnect();
          } catch {
            // Already gone. Nothing to report; the caller resets its own state.
          }
        },
      };
    },
  };
}

// -----------------------------------------------------------------------------
// Capacitor / CoreBluetooth / Android BLE
//
// The iOS and Android apps. `BleClient` is deliberately close to Web Bluetooth
// in shape, but differs in three ways that matter and are easy to get wrong:
//
//  ⓵ It is **device-id addressed, not object addressed.** There is no
//    characteristic object to hold; every call repeats deviceId + service +
//    characteristic. Hence the closure over `deviceId` below.
//  ⓶ `initialize()` must be called before anything else, and it is what
//    triggers the OS permission prompt. Calling it twice is harmless.
//  ⓷ Notification payloads arrive as `DataView`, and the underlying buffer is
//    **reused between callbacks on Android**. Copying into a fresh `Uint8Array`
//    before handing it upward is not defensive style, it is required — the
//    parser is re-entrant across packets and would otherwise read bytes that
//    have already been overwritten by the next notification.
// -----------------------------------------------------------------------------

function createNativeLink(): BleLink {
  return {
    kind: "native",
    // The shell would not exist if the platform lacked a BLE stack. Whether the
    // radio is on, and whether the user grants permission, are discovered at
    // connect time and reported as errors like any other failure.
    available: true,

    async connect(opts: BleConnectOptions): Promise<BleSession> {
      const { BleClient, numbersToDataView } = await import(
        "@capacitor-community/bluetooth-le"
      );

      await BleClient.initialize();

      let deviceId: string;
      let deviceName: string;
      try {
        const device = await BleClient.requestDevice({
          services: [opts.service],
          optionalServices: [opts.service],
        });
        deviceId = device.deviceId;
        deviceName = device.name ?? "BLE MIDI device";
      } catch (e: unknown) {
        // The plugin has no dedicated cancel error and the wording differs
        // between iOS and Android, so this is a message match. It is allowed to
        // be imperfect: a false positive costs a missing error banner on a
        // failure the user just caused, a false negative shows a banner saying
        // the chooser was cancelled — which it was.
        const msg = String((e as { message?: string })?.message ?? e);
        if (/cancel|dismiss|no device selected/i.test(msg)) {
          throw new BleChooserCancelled();
        }
        throw e;
      }

      await BleClient.connect(deviceId, () => opts.onDisconnect());
      await BleClient.startNotifications(
        deviceId,
        opts.service,
        opts.characteristic,
        (value: DataView) => {
          // Copy — see ⓷ above.
          opts.onPacket(
            new Uint8Array(
              value.buffer.slice(
                value.byteOffset,
                value.byteOffset + value.byteLength,
              ),
            ),
          );
        },
      );

      return {
        deviceName,
        write: (packet) =>
          BleClient.writeWithoutResponse(
            deviceId,
            opts.service,
            opts.characteristic,
            numbersToDataView(Array.from(packet)),
          ),
        async disconnect() {
          try {
            await BleClient.stopNotifications(
              deviceId,
              opts.service,
              opts.characteristic,
            );
          } catch {
            // Link already down; the disconnect below is what matters.
          }
          try {
            await BleClient.disconnect(deviceId);
          } catch {
            // Already gone.
          }
        },
      };
    },
  };
}
