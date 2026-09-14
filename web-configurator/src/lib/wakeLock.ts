/**
 * Keep the screen awake while a module is connected — M80.
 *
 * A Controller on a phone or tablet spends most of a set being *looked at*
 * rather than touched: a knob gets moved, then nothing for two minutes, then
 * another. Every one of those gaps is long enough for the screen to dim and
 * lock, and on a phone that also drops the BLE link. So the lock is tied to
 * having a connection, not to a timer or a button.
 *
 * ⚠ **The lock is released by the browser whenever the page is hidden, and is
 * not restored on its own.** That is specified behaviour, not a bug — so the
 * visibility handler below is required rather than defensive. Without it the
 * lock survives exactly one trip to the lock screen.
 *
 * ⚠ **Requires a secure context and, on some engines, a recent user gesture.**
 * `request()` rejects rather than returning null when refused, and every
 * rejection here is swallowed: a screen that dims is a minor annoyance, and
 * nothing about it is worth an error in the UI or a thrown promise on a path
 * the user did not ask for.
 *
 * Absent entirely in Firefox and in older WebKit. `supported` reports that; the
 * rest of the module is inert there.
 */

export const wakeLockSupported =
  typeof navigator !== "undefined" && "wakeLock" in navigator;

let sentinel: WakeLockSentinel | null = null;
/** What the app has *asked* for, as opposed to what the browser is granting. */
let wanted = false;

async function acquire(): Promise<void> {
  if (!wakeLockSupported || sentinel || !wanted) return;
  if (typeof document !== "undefined" && document.visibilityState !== "visible")
    return;
  try {
    sentinel = await (navigator as any).wakeLock.request("screen");
    // Fires on an OS-level release too (battery saver, the user hitting the
    // power button), not only on ours — so the reference must be dropped here
    // rather than only in release(), or we would believe we still hold a lock
    // that is gone and never re-request it.
    sentinel?.addEventListener("release", () => {
      sentinel = null;
    });
  } catch {
    // Refused: not visible, no gesture, battery saver, insecure origin.
    sentinel = null;
  }
}

async function drop(): Promise<void> {
  const held = sentinel;
  sentinel = null;
  try {
    await held?.release();
  } catch {
    // Already released by the browser. Nothing to do.
  }
}

/**
 * Turn the request on or off. Idempotent, and safe to call before the API is
 * known to exist.
 */
export function setWakeLock(on: boolean): void {
  wanted = on;
  if (on) void acquire();
  else void drop();
}

/** Whether a lock is actually held right now — for the UI, not for control. */
export function wakeLockHeld(): boolean {
  return sentinel !== null;
}

// Re-acquire on every return to the foreground, for as long as the app still
// wants one. Registered once, at module scope: there is a single screen and a
// single lock, so this is not per-component state.
if (typeof document !== "undefined" && wakeLockSupported) {
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") void acquire();
  });
}
