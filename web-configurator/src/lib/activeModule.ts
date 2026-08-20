/**
 * Which module the configurator is currently showing.
 *
 * Detected at runtime, not chosen at build time. The firmware announces itself
 * in the two SysEx signature bytes of every message it sends, so on connect the
 * page broadcasts a REQUEST_DUMP addressed to the wildcard signature `7F 7F`
 * (see SYSEX_DEV_ANY) and reads the module out of the header of the PATCH_DUMP
 * that answers. One round trip, and it delivers the patch as well as the
 * identity — see the sync effect in App.svelte.
 *
 * With nothing connected the page shows the last module it saw, remembered in
 * localStorage across sessions. Failing that it falls back to VITE_MODULE, so
 * `make web MODULE=alloycoil` still decides what a fresh browser opens on; it is
 * a default now rather than a lock, and a build for one module happily drives
 * the other.
 */

import { writable } from "svelte/store";

export interface ModuleInfo {
  /** VITE_MODULE value, and the key in MODULES. */
  readonly id: string;
  /** Shown in the UI header. */
  readonly name: string;
  /** Two-byte SysEx device signature following the 0x7D manufacturer byte. */
  readonly sysexDev: readonly [number, number];
}

export const MODULES: Readonly<Record<string, ModuleInfo>> = {
  alloyflux: {
    id: "alloyflux",
    name: "AlloyFlux",
    sysexDev: [0x41, 0x46], // 'A','F'
  },
  alloycoil: {
    id: "alloycoil",
    name: "Alloy Coil",
    sysexDev: [0x41, 0x43], // 'A','C'
  },
} as const;

/**
 * Wildcard signature byte. `7D 7F 7F <cmd>` addresses every Alloy module at
 * once; the firmware honours it for REQUEST_DUMP only, since a broadcast that
 * changed state would hit every module sharing the port.
 */
export const SYSEX_DEV_ANY = 0x7f;

/** Identify a module from the two signature bytes. Null if unrecognised. */
export function moduleForSysExDev(a: number, b: number): ModuleInfo | null {
  return (
    Object.values(MODULES).find(
      (m) => m.sysexDev[0] === a && m.sysexDev[1] === b,
    ) ?? null
  );
}

const STORAGE_KEY = "alloy-active-module";

function initialModule(): ModuleInfo {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    if (saved && MODULES[saved]) return MODULES[saved];
  } catch {
    // Private mode / storage disabled — fall through to the build default.
  }
  const requested =
    (import.meta.env.VITE_MODULE as string | undefined) ?? "alloyflux";
  if (!MODULES[requested]) {
    console.warn(
      `[activeModule] unknown VITE_MODULE "${requested}" — falling back to AlloyFlux. ` +
        `Known: ${Object.keys(MODULES).join(", ")}`,
    );
  }
  return MODULES[requested] ?? MODULES.alloyflux;
}

// Resolved once: initialModule() touches localStorage and can warn about a bad
// VITE_MODULE, and neither should happen twice.
const startingModule = initialModule();

export const activeModule = writable<ModuleInfo>(startingModule);

// Plain snapshot alongside the store. The MIDI monitor decodes every message
// that crosses the wire, which during a knob move is hundreds a second, and
// svelte/store's get() subscribes and unsubscribes on each call. Non-reactive
// readers take this instead.
let current: ModuleInfo = startingModule;
activeModule.subscribe((m) => {
  current = m;
});

/** The active module, for code that cannot subscribe (parsers, encoders). */
export function currentModule(): ModuleInfo {
  return current;
}

/**
 * Switch to `info` and remember it for next time.
 * Returns true if this was an actual change, so callers can rebuild the UI
 * state that is keyed to the parameter map only when they have to.
 */
export function setActiveModule(info: ModuleInfo): boolean {
  if (info.id === current.id) return false;
  activeModule.set(info);
  try {
    localStorage.setItem(STORAGE_KEY, info.id);
  } catch {
    // Not being able to remember it is not a reason to refuse the switch.
  }
  return true;
}
