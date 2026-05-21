/**
 * Preset store — manages 10 preset slots locally in the browser
 * and syncs to the device via serial `config save/load` commands
 * or, if only MIDI is connected, via AlloyFlux SysEx preset commands.
 *
 * Slot 0 = live auto-save state (device-side).
 * Slots 1-9 = user presets saved on the device; browser holds a name cache.
 */

import { writable, get } from "svelte/store";
import { serial } from "./serial";
import { midi } from "./midi";
import { SysexCmd } from "./patchSync";

export interface PresetSlot {
  slot: number; // 0-9
  name: string; // user-visible label (stored in localStorage)
  savedAt: number | null; // Date.now() timestamp when last saved
}

const STORAGE_KEY = "alloyflux-preset-names";

function loadNames(): Record<number, string> {
  try {
    return JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "{}");
  } catch {
    return {};
  }
}

function saveNames(names: Record<number, string>) {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(names));
}

function createPresets() {
  const names = loadNames();
  const initial: PresetSlot[] = Array.from({ length: 10 }, (_, i) => ({
    slot: i,
    name: names[i] ?? (i === 0 ? "Live State" : `Preset ${i}`),
    savedAt: null,
  }));

  const store = writable<PresetSlot[]>(initial);

  function rename(slot: number, name: string) {
    store.update((ps) => ps.map((p) => (p.slot === slot ? { ...p, name } : p)));
    const names = loadNames();
    names[slot] = name;
    saveNames(names);
  }

  async function save(slot: number) {
    if (get(serial).connected) {
      const cmd = slot === 0 ? "config save" : `config save ${slot}`;
      await serial.send(cmd);
    } else if (get(midi).deviceConnected) {
      midi.sendSysEx(SysexCmd.PRESET_SAVE, [slot & 0x7f]);
    }
    store.update((ps) =>
      ps.map((p) => (p.slot === slot ? { ...p, savedAt: Date.now() } : p)),
    );
  }

  async function load(slot: number) {
    if (get(serial).connected) {
      const cmd = slot === 0 ? "config load" : `config load ${slot}`;
      await serial.send(cmd);
      // Re-request dump so the UI reflects the newly loaded preset
      await serial.send("dump");
    } else if (get(midi).deviceConnected) {
      // PRESET_LOAD (0x05) triggers configStore_load on the device and
      // the firmware automatically responds with a PATCH_DUMP, so the
      // App.svelte onSysEx handler will apply the new state automatically.
      midi.sendSysEx(SysexCmd.PRESET_LOAD, [slot & 0x7f]);
    }
  }

  async function reset(slot: number | "all") {
    if (get(serial).connected) {
      const cmd =
        slot === "all"
          ? "config reset all"
          : slot === 0
            ? "config reset"
            : `config reset ${slot}`;
      await serial.send(cmd);
      // For live slot or full reset, re-request dump so the UI reflects defaults.
      if (slot === 0 || slot === "all") {
        await serial.send("dump");
      }
    } else if (get(midi).deviceConnected) {
      const arg = slot === "all" ? 0x7f : (slot as number) & 0x7f;
      midi.sendSysEx(SysexCmd.PRESET_RESET, [arg]);
      // MIDI path: firmware now auto-responds with PATCH_DUMP for slot 0 / all reset.
    }
    if (slot === "all") {
      store.update((ps) => ps.map((p) => ({ ...p, savedAt: null })));
    } else {
      store.update((ps) =>
        ps.map((p) => (p.slot === slot ? { ...p, savedAt: null } : p)),
      );
    }
  }

  return { subscribe: store.subscribe, rename, save, load, reset };
}

export const presets = createPresets();
