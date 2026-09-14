/**
 * Which of the three shells the Controller is running in — Milestone 80.
 *
 * One build now reaches users three ways, and they do not have the same
 * capabilities:
 *
 *   • **Browser (Chrome/Edge, desktop or Android)** — everything works. Web
 *     MIDI, Web Serial on desktop, Web Bluetooth everywhere. Installable as a
 *     PWA, which changes nothing functionally; it just loses the tab strip.
 *   • **Native shell (Capacitor, iOS or Android app)** — Bluetooth works,
 *     through CoreBluetooth / Android BLE rather than the browser. No Web MIDI
 *     and no Web Serial, because there is no USB cable in the picture.
 *   • **Browser on iOS** — *nothing* works. WebKit ships none of the three
 *     APIs, and Chrome and Firefox on iOS are WebKit too, so the browser choice
 *     is not the user's mistake and telling them to switch browsers is wrong
 *     advice. They need the app.
 *
 * The last case is the reason this file exists. Without it the Controller
 * greets an iPhone with three greyed-out transports and no explanation, which
 * reads as broken rather than as "there is an app for this".
 */

import { isNativeShell } from "./bleLink";

export { isNativeShell };

/**
 * Where to send an iOS visitor who has reached the web build.
 *
 * ⚠ Placeholder until the app is through review — `PwaStatus.svelte` renders
 * the explanation with no link while this is null, which is the honest state.
 * Set it to the App Store URL and the same banner grows a button.
 */
export const IOS_APP_STORE_URL: string | null = null;

/** Running as an installed PWA (or in the native shell) rather than in a tab. */
export function isStandalone(): boolean {
  if (typeof window === "undefined") return false;
  return (
    window.matchMedia?.("(display-mode: standalone)").matches ||
    // Safari's own, non-standard and iOS-only. Kept because it is the only
    // signal there, and iOS is precisely the platform this file cares about.
    (navigator as any).standalone === true ||
    isNativeShell()
  );
}

/**
 * A WebKit browser on an Apple mobile device — the dead end described above.
 *
 * Two wrinkles make this uglier than it should be. iPadOS 13+ reports a
 * *desktop* Mac user-agent by default, so an iPad is only distinguishable from
 * a MacBook by the presence of touch points; and every browser on iOS reports
 * itself as Chrome or Firefox while running WebKit underneath, so the brand in
 * the UA string says nothing about the engine. Hence: Apple hardware, with
 * touch, and not the native shell.
 *
 * False on macOS Safari, which has no Web Bluetooth either but is not a dead
 * end — a user there can install Chrome and everything works.
 */
export function isIosBrowser(): boolean {
  if (typeof navigator === "undefined" || isNativeShell()) return false;
  const ua = navigator.userAgent;
  const iosDevice = /iPad|iPhone|iPod/.test(ua);
  const ipadAsDesktop = /Macintosh/.test(ua) && navigator.maxTouchPoints > 1;
  return iosDevice || ipadAsDesktop;
}

// ── Install tip ──────────────────────────────────────────────────────────────
//
// Whether to offer "install this for offline use", and how insistently.
//
// Not a preference the user ever goes looking for, so it is not in the settings
// panel — it is written only by the tip's own "Don't show again" box, and read
// only to decide whether to render it.
// -----------------------------------------------------------------------------

const INSTALL_TIP_KEY = "install-tip";

/** How many times to offer unprompted before giving up on our own. */
export const INSTALL_TIP_MAX_SHOWS = 3;

interface InstallTipState {
  /** The user ticked "Don't show again". */
  dismissed: boolean;
  /** Times the tip has actually been rendered. */
  shown: number;
}

/**
 * Every accessor here is wrapped, and the fallback is always "behave as if this
 * is the first visit" rather than throwing. `localStorage` is not merely
 * missing in a private window — reading it can *throw* when site data is
 * blocked, and a nag bar is not worth taking the page down for.
 */
function readInstallTip(): InstallTipState {
  try {
    const raw = localStorage.getItem(INSTALL_TIP_KEY);
    if (!raw) return { dismissed: false, shown: 0 };
    const parsed = JSON.parse(raw) as Partial<InstallTipState>;
    return {
      dismissed: parsed.dismissed === true,
      shown: typeof parsed.shown === "number" ? parsed.shown : 0,
    };
  } catch {
    return { dismissed: false, shown: 0 };
  }
}

function writeInstallTip(state: InstallTipState): void {
  try {
    localStorage.setItem(INSTALL_TIP_KEY, JSON.stringify(state));
  } catch {
    // Storage blocked. The tip then reappears next visit, which is the same
    // behaviour as before it was remembered at all — mildly annoying, not broken.
  }
}

/**
 * Should the install tip be offered at all this visit?
 *
 * Deliberately *not* the whole condition — the caller still needs an actual
 * `beforeinstallprompt`, and must not be in a native shell or already
 * standalone. This answers only the "has the user told us to stop?" half.
 */
export function installTipAllowed(): boolean {
  const s = readInstallTip();
  return !s.dismissed && s.shown < INSTALL_TIP_MAX_SHOWS;
}

/** Record that it was rendered, for the soft cap above. */
export function noteInstallTipShown(): void {
  const s = readInstallTip();
  writeInstallTip({ ...s, shown: s.shown + 1 });
}

/** "Don't show again" — permanent until site data is cleared. */
export function dismissInstallTipForever(): void {
  writeInstallTip({ ...readInstallTip(), dismissed: true });
}

/**
 * Ask the browser to exempt our storage from eviction pressure.
 *
 * Presets live in `localStorage` (see `lib/presets.ts`), and a browser under
 * disk pressure is entitled to clear that for an origin it considers
 * uninteresting. Installed PWAs are usually granted this automatically and the
 * call is then a no-op; in a plain tab it may show no prompt and simply return
 * false. Either way it is advisory — losing presets must not be *possible*,
 * which is what preset export exists for, not merely unlikely.
 *
 * Never throws: an unsupported browser is a resolved `false`.
 */
export async function requestPersistentStorage(): Promise<boolean> {
  try {
    if (!navigator.storage?.persist) return false;
    if (await navigator.storage.persisted()) return true;
    return await navigator.storage.persist();
  } catch {
    return false;
  }
}
