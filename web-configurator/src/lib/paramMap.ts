/**
 * Parameter map — mirrors src/param_map.cpp in the firmware.
 *
 * Any parameter added to kCCParams[] in the firmware should be added here too.
 * The `name` field matches the serial command name exactly.
 */

export interface CCParam {
  cc: number;
  name: string;
  label: string;
  min: number;
  max: number;
  default: number;
  unit?: string;
  step?: number; // slider granularity, defaults to 0.001
}

export const PARAM_MAP: CCParam[] = [
  {
    cc: 1,
    name: "motion",
    label: "Motion",
    min: 0,
    max: 1,
    default: 0,
    unit: "",
  },
  {
    cc: 7,
    name: "vol",
    label: "Volume",
    min: 0,
    max: 1,
    default: 0.8,
    unit: "",
  },
  {
    cc: 71,
    name: "curve",
    label: "Curve",
    min: 0,
    max: 1,
    default: 0.5,
    unit: "",
  },
  {
    cc: 72,
    name: "curvetime",
    label: "Curve Time",
    min: 0.25,
    max: 4,
    default: 1,
    unit: "×",
  },
  {
    cc: 73,
    name: "dspeed",
    label: "Drift Speed",
    min: 0.001,
    max: 0.1,
    default: 0.025,
  },
  {
    cc: 74,
    name: "shape",
    label: "Shape",
    min: 0,
    max: 1,
    default: 0.25,
    unit: "",
  },
  {
    cc: 91,
    name: "space",
    label: "Space",
    min: 0,
    max: 2,
    default: 1,
    unit: "",
  },
  {
    cc: 92,
    name: "detune",
    label: "Detune",
    min: 0,
    max: 200,
    default: 0,
    unit: "Hz",
    step: 0.1,
  },
  {
    cc: 93,
    name: "fat",
    label: "Fatness",
    min: 0,
    max: 1,
    default: 0,
    unit: "",
  },
  {
    cc: 94,
    name: "rel",
    label: "Relation",
    min: 0,
    max: 24,
    default: 0,
    unit: "st",
    step: 0.1,
  },
  {
    cc: 112,
    name: "revmodspeed",
    label: "Rev Mod Speed",
    min: 0.1,
    max: 4,
    default: 1,
    unit: "",
  },
  {
    cc: 113,
    name: "revmoddepth",
    label: "Rev Mod Depth",
    min: 0,
    max: 1,
    default: 1,
    unit: "",
  },
];

/** Convert a 0–127 MIDI CC value to the parameter's float range. */
export function ccToFloat(param: CCParam, ccValue: number): number {
  return param.min + (ccValue / 127) * (param.max - param.min);
}

/** Convert a float parameter value to a 0–127 MIDI CC value. */
export function floatToCC(param: CCParam, value: number): number {
  return Math.round(((value - param.min) / (param.max - param.min)) * 127);
}
