/**
 * Patch sync — AlloyFlux SysEx / serial dump protocol.
 *
 * SysEx format  (body between F0 and F7, all bytes 7-bit safe):
 *   7D 41 46 <cmd> [cc0 val0 cc1 val1 ...]
 *   7D      = non-commercial manufacturer ID
 *   41 46   = 'A' 'F' (AlloyFlux device signature)
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
export const SYSEX_DEVA = 0x41 as const; // 'A'
export const SYSEX_DEVF = 0x46 as const; // 'F'

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
export function buildSysExBody(cmd: number, pairs: CCPair[]): number[] {
  const body: number[] = [SYSEX_MFR, SYSEX_DEVA, SYSEX_DEVF, cmd & 0x7f];
  for (const { cc, value } of pairs) {
    body.push(cc & 0x7f, value & 0x7f);
  }
  return body;
}

/**
 * Parse a SysEx body (without F0/F7) into CCPairs.
 * Accepts both DUMP (0x02) and APPLY (0x03) command types.
 * Returns null if the header doesn't match AlloyFlux format.
 */
export function parseSysExBody(data: Uint8Array): CCPair[] | null {
  if (data.length < 5) return null;
  if (data[0] !== SYSEX_MFR || data[1] !== SYSEX_DEVA || data[2] !== SYSEX_DEVF)
    return null;
  const cmd = data[3];
  if (cmd !== SysexCmd.DUMP && cmd !== SysexCmd.APPLY) return null;
  const pairs: CCPair[] = [];
  for (let i = 4; i + 1 < data.length; i += 2) {
    pairs.push({ cc: data[i] & 0x7f, value: data[i + 1] & 0x7f });
  }
  return pairs;
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
 * Format: F0 [AlloyFlux DUMP body] F7
 */
export function buildSyxBlob(pairs: CCPair[]): Blob {
  const body = buildSysExBody(SysexCmd.DUMP, pairs);
  const bytes = new Uint8Array([0xf0, ...body, 0xf7]);
  return new Blob([bytes], { type: "application/octet-stream" });
}

/**
 * Parse a .syx file ArrayBuffer.
 * Finds the first AlloyFlux SysEx message and returns its CC pairs.
 * Returns null if no valid AlloyFlux message is found.
 */
export function parseSyxBuffer(buf: ArrayBuffer): CCPair[] | null {
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
