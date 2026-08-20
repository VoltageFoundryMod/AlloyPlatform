/**
 * MIDI message decoding for the monitor panel.
 *
 * Turns raw bytes into something readable at a glance — "CC 74  Filter Cutoff
 * = 64" rather than "B0 4A 40".  Parameter names come from PARAM_MAP, so the
 * monitor stays in step with the CC map automatically; AlloyFlux SysEx is
 * decoded against the same command table the transport uses.
 */

import { currentParams } from "./paramMap";
import { moduleForSysExDev, SYSEX_DEV_ANY } from "./activeModule";
import { SYSEX_MFR, SysexCmd } from "./patchSync";

export interface DecodedMidi {
  /** Short message type, e.g. "CC", "Note On", "SysEx". */
  kind: string;
  /** Human-readable body: parameter name, value, note name, … */
  detail: string;
  /** MIDI channel 1–16, or null for channel-less messages (SysEx, realtime). */
  channel: number | null;
}

const NOTE_NAMES = [
  "C",
  "C#",
  "D",
  "D#",
  "E",
  "F",
  "F#",
  "G",
  "G#",
  "A",
  "A#",
  "B",
];

/** MIDI note number → scientific pitch name (60 = C4). */
export function noteName(n: number): string {
  return `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
}

/** Byte array → "B0 4A 40" */
export function toHex(bytes: number[]): string {
  return bytes
    .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
    .join(" ");
}

const SYSEX_CMD_NAMES: Record<number, string> = {
  [SysexCmd.REQUEST]: "REQUEST_DUMP",
  [SysexCmd.DUMP]: "PATCH_DUMP",
  [SysexCmd.APPLY]: "APPLY_PATCH",
  [SysexCmd.PRESET_SAVE]: "PRESET_SAVE",
  [SysexCmd.PRESET_LOAD]: "PRESET_LOAD",
  [SysexCmd.PRESET_RESET]: "PRESET_RESET",
  [SysexCmd.SET_MIDI_CHANNEL]: "SET_MIDI_CHANNEL",
};

// CCs that are not plain parameters — these never appear in PARAM_MAP.
const CC_SPECIAL: Record<number, string> = {
  64: "Sustain / gate hold",
  120: "All Sound Off",
  121: "Reset All Controllers",
  123: "All Notes Off (panic)",
};

function describeCC(cc: number, value: number): string {
  const param = currentParams().PARAM_MAP.find((p) => p.cc === cc);
  if (param) {
    // Selects carry a band index rather than a magnitude; show the option name
    // when one matches, since the raw number means nothing on its own.
    if (param.type === "select" && param.options) {
      const opt = param.options.find(
        (o) => value >= o.ccMin && value <= o.ccMax,
      );
      if (opt) return `CC ${cc}  ${param.label} = ${opt.label} (${value})`;
    }
    return `CC ${cc}  ${param.label} = ${value}`;
  }
  const special = CC_SPECIAL[cc];
  if (special) return `CC ${cc}  ${special} = ${value}`;
  return `CC ${cc} = ${value}`;
}

function describeSysEx(bytes: number[]): DecodedMidi {
  // F0 7D <id0> <id1> <cmd> [payload] F7
  if (bytes.length < 5 || bytes[1] !== SYSEX_MFR) {
    return {
      kind: "SysEx",
      detail: `foreign device, ${bytes.length} bytes`,
      channel: null,
    };
  }
  // Which module — shown for every message, because on a shared port that is
  // the whole question the monitor is being opened to answer.  The broadcast
  // probe is labelled as such rather than as an unknown device.
  const broadcast = bytes[2] === SYSEX_DEV_ANY && bytes[3] === SYSEX_DEV_ANY;
  const sender = moduleForSysExDev(bytes[2], bytes[3]);
  const who = broadcast
    ? "any module"
    : (sender?.name ??
      `unknown ${bytes[2].toString(16).toUpperCase()} ${bytes[3]
        .toString(16)
        .toUpperCase()}`);
  const cmd = bytes[4];
  const name = `${who}: ${SYSEX_CMD_NAMES[cmd] ?? `cmd 0x${cmd.toString(16)}`}`;
  // Payload sits between the command byte and the terminating F7.
  const end = bytes[bytes.length - 1] === 0xf7 ? bytes.length - 1 : bytes.length;
  const payload = bytes.slice(5, end);
  let detail = name;
  if (cmd === SysexCmd.DUMP || cmd === SysexCmd.APPLY) {
    detail += `  ${Math.floor(payload.length / 2)} params`;
  } else if (payload.length > 0) {
    detail += `  arg ${payload[0]}`;
  }
  return { kind: "SysEx", detail, channel: null };
}

/** Decode one raw MIDI message. Never throws — unknown input degrades to hex. */
export function decodeMidi(bytes: number[]): DecodedMidi {
  if (bytes.length === 0) return { kind: "?", detail: "", channel: null };
  const status = bytes[0];

  if (status === 0xf0) return describeSysEx(bytes);

  // System realtime / common — no channel nibble.
  if (status >= 0xf8) {
    const rt: Record<number, string> = {
      0xf8: "Clock",
      0xfa: "Start",
      0xfb: "Continue",
      0xfc: "Stop",
      0xfe: "Active Sensing",
      0xff: "Reset",
    };
    return { kind: rt[status] ?? "System", detail: "", channel: null };
  }

  const type = status & 0xf0;
  const channel = (status & 0x0f) + 1;
  const d1 = bytes[1] ?? 0;
  const d2 = bytes[2] ?? 0;

  switch (type) {
    case 0x80:
      return { kind: "Note Off", detail: `${noteName(d1)}`, channel };
    case 0x90:
      // Note On with velocity 0 is the conventional Note Off.
      return d2 === 0
        ? { kind: "Note Off", detail: `${noteName(d1)}`, channel }
        : { kind: "Note On", detail: `${noteName(d1)}  vel ${d2}`, channel };
    case 0xa0:
      return { kind: "Aftertouch", detail: `${noteName(d1)}  ${d2}`, channel };
    case 0xb0:
      return { kind: "CC", detail: describeCC(d1, d2), channel };
    case 0xc0:
      return { kind: "Program", detail: `${d1 + 1}`, channel };
    case 0xd0:
      return { kind: "Ch Pressure", detail: `${d1}`, channel };
    case 0xe0:
      return { kind: "Pitch Bend", detail: `${((d2 << 7) | d1) - 8192}`, channel };
    default:
      return { kind: "?", detail: toHex(bytes), channel: null };
  }
}
