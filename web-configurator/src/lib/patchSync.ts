import {
  currentModule,
  moduleForSysExDev,
  SYSEX_DEV_ANY,
  type ModuleInfo,
} from "./activeModule";

/**
 * Patch sync — Alloy platform SysEx / serial dump protocol.
 *
 * SysEx format  (body between F0 and F7, all bytes 7-bit safe):
 *   7D 41 46 <cmd> [cc0 val0 cc1 val1 ...]
 *   7D      = non-commercial manufacturer ID
 *   41 46   = the module's device signature ('A','F' AlloyFlux; 'A','C' Alloy Coil)
 *   7F 7F   = wildcard: every module answers, REQUEST_DUMP only.  Used to
 *             discover which module is on the port — the PATCH_DUMP that comes
 *             back carries the real signature.
 *   cmd:
 *     0x01  REQUEST_DUMP  — host → device: please send your current patch
 *     0x02  PATCH_DUMP    — device → host: here are all the CC pairs
 *     0x03  APPLY_PATCH   — host → device: apply these CC pairs
 *
 * Serial dump format:
 *   send "dump\r\n", device responds:
 *     dump_begin
 *     cc:1=45
 *     cc:7=90
 *     ...
 *     dump_end
 *
 * File format (.syx):
 *   Standard raw SysEx file — F0 [body with cmd=0x02] F7.
 *   Compatible with DAW SysEx tools.
 */

export const SYSEX_MFR = 0x7d as const; // non-commercial manufacturer ID

/** Signature bytes to address a message with. Defaults to the active module. */
export type SysExDev = readonly [number, number];

/** The wildcard every module answers — see activeModule.ts. */
export const SYSEX_DEV_BROADCAST: SysExDev = [SYSEX_DEV_ANY, SYSEX_DEV_ANY];

/** Signature of the module currently being shown. */
export function activeDev(): SysExDev {
  return currentModule().sysexDev;
}

export const SysexCmd = {
  REQUEST: 0x01, // host → device: send current patch
  DUMP: 0x02, // device → host: full patch as CC pairs
  APPLY: 0x03, // host → device: load CC pairs into params
  PRESET_SAVE: 0x04, // host → device: save current params to slot (arg0 = slot 0-9)
  PRESET_LOAD: 0x05, // host → device: load slot into params + auto-dump (arg0 = slot 0-9)
  PRESET_RESET: 0x06, // host → device: reset slot to defaults (arg0 = slot 0-9, or 0x7F = all)
  SET_MIDI_CHANNEL: 0x07, // host → device: set MIDI receive channel (arg0 = 0 omni, 1-16)
} as const;

export interface CCPair {
  cc: number; // 0–127 MIDI CC number
  value: number; // 0–127 raw CC value
}

// ---------------------------------------------------------------------------
// SysEx body builders / parsers
// ---------------------------------------------------------------------------

/**
 * Build a SysEx body (bytes between F0 and F7) for the given command and
 * CC pair payload.  All bytes are 7-bit safe.
 */
export function buildSysExBody(
  cmd: number,
  pairs: CCPair[],
  dev: SysExDev = activeDev(),
): number[] {
  const body: number[] = [SYSEX_MFR, dev[0], dev[1], cmd & 0x7f];
  for (const { cc, value } of pairs) {
    body.push(cc & 0x7f, value & 0x7f);
  }
  return body;
}

/** A patch message with the identity of the module that sent it. */
export interface ParsedPatch {
  /** Null when the signature belongs to no module this build knows about. */
  module: ModuleInfo | null;
  /** The two signature bytes as received, for reporting an unknown module. */
  dev: SysExDev;
  /** DUMP or APPLY. Only a DUMP can have come from a device: APPLY is a
   *  host→device command, and on a loopback port (loopMIDI, IAC, Rack) our own
   *  APPLY comes straight back at us — which would otherwise read as a module
   *  answering, and confirm a link to nobody. */
  cmd: number;
  pairs: CCPair[];
}

/**
 * Parse a SysEx body (without F0/F7) into CCPairs plus the sender's identity.
 * Accepts both DUMP (0x02) and APPLY (0x03) command types.
 * Returns null if this is not an Alloy patch message at all.
 *
 * Note what is deliberately *not* checked: whether the sender is the module
 * currently on screen.  Answering that is the caller's job — a dump from a
 * different module is the discovery result, not an error, and it is how the
 * page learns what it is connected to.
 */
export function parseSysExBody(data: Uint8Array): ParsedPatch | null {
  if (data.length < 5) return null;
  if (data[0] !== SYSEX_MFR) return null;
  const cmd = data[3];
  if (cmd !== SysexCmd.DUMP && cmd !== SysexCmd.APPLY) return null;
  const dev: SysExDev = [data[1], data[2]];
  const pairs: CCPair[] = [];
  for (let i = 4; i + 1 < data.length; i += 2) {
    pairs.push({ cc: data[i] & 0x7f, value: data[i + 1] & 0x7f });
  }
  return { module: moduleForSysExDev(dev[0], dev[1]), dev, cmd, pairs };
}

// ---------------------------------------------------------------------------
// Serial dump parser
// ---------------------------------------------------------------------------

/**
 * Parse the lines collected between "dump_begin" and "dump_end" from the
 * serial console into CCPairs.
 */
export function parseSerialDump(lines: string[]): CCPair[] {
  const pairs: CCPair[] = [];
  for (const line of lines) {
    const m = /^cc:(\d{1,3})=(\d{1,3})$/.exec(line.trim());
    if (m) {
      const cc = parseInt(m[1], 10);
      const value = parseInt(m[2], 10);
      if (cc >= 0 && cc <= 127 && value >= 0 && value <= 127) {
        pairs.push({ cc, value });
      }
    }
  }
  return pairs;
}

// ---------------------------------------------------------------------------
// .syx file export / import
// ---------------------------------------------------------------------------

/**
 * Build a standard .syx Blob containing a PATCH_DUMP message.
 * Format: F0 [DUMP body signed with the active module's signature] F7
 */
export function buildSyxBlob(pairs: CCPair[]): Blob {
  const body = buildSysExBody(SysexCmd.DUMP, pairs);
  const bytes = new Uint8Array([0xf0, ...body, 0xf7]);
  return new Blob([bytes], { type: "application/octet-stream" });
}

/**
 * Parse a .syx file ArrayBuffer.
 * Finds the first Alloy SysEx message and returns its CC pairs along with the
 * module that wrote it — a file exported from another module parses fine and
 * reports that module, so the caller can say so instead of "invalid file".
 * Returns null if there is no Alloy patch message in the buffer.
 */
export function parseSyxBuffer(buf: ArrayBuffer): ParsedPatch | null {
  const data = new Uint8Array(buf);
  let start = -1;
  for (let i = 0; i < data.length; i++) {
    if (data[i] === 0xf0) {
      start = i;
      break;
    }
  }
  if (start < 0) return null;
  let end = -1;
  for (let i = start + 1; i < data.length; i++) {
    if (data[i] === 0xf7) {
      end = i;
      break;
    }
  }
  if (end < 0) return null;
  // Body is the slice between F0 and F7 (exclusive)
  return parseSysExBody(data.slice(start + 1, end));
}

// ---------------------------------------------------------------------------
// Browser file helpers
// ---------------------------------------------------------------------------

/** Trigger a browser file download. */
export function downloadFile(blob: Blob, filename: string): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
