/**
 * Which module this build of the configurator targets.
 *
 * Selected at build time from VITE_MODULE:
 *   make web                  → AlloyFlux (default)
 *   make web MODULE=alloycoil    → Alloy Coil
 *
 * Build-time rather than runtime detection, deliberately and for now only. The
 * firmware already announces itself — the two SysEx signature bytes in every
 * message are the module's identity, and `SYSEX_DEV` below is exactly what a
 * runtime detector would compare against. What makes auto-switching a bigger
 * change than it looks is the UI: `PARAM_MAP` is a static import in App.svelte
 * and in every component, so making it follow the connected device means
 * turning all of that into reactive state. Worth doing; not worth blocking a
 * first Alloy Coil bring-up on.
 *
 * The consequence to know: point an AlloyFlux build at an Alloy Coil module and
 * the SysEx header will not match, so it simply will not connect — it fails
 * closed rather than showing the wrong controls.
 */

export interface ModuleInfo {
  /** VITE_MODULE value. */
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

const requested = (import.meta.env.VITE_MODULE as string | undefined) ?? "alloyflux";

export const ACTIVE_MODULE: ModuleInfo = MODULES[requested] ?? MODULES.alloyflux;

if (!MODULES[requested]) {
  console.warn(
    `[activeModule] unknown VITE_MODULE "${requested}" — falling back to AlloyFlux. ` +
      `Known: ${Object.keys(MODULES).join(", ")}`,
  );
}
