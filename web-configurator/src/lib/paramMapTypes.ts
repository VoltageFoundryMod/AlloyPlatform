/**
 * Types for the generated parameter map.
 *
 * These live apart from `paramMap.ts` because that file is emitted by
 * `tools/gen_params.py` from `modules/alloyflux/params.json` — the same source
 * the firmware's CC table comes from. Keeping the types hand-written here means
 * the generator only has to produce data.
 */

export type ParamType = "slider" | "select";

export interface SelectOption {
  label: string;
  ccMin: number; // inclusive lower bound of the CC range for this option
  ccMax: number; // inclusive upper bound
}

export interface CCParam {
  cc: number;
  name: string;
  label: string;
  category: string;
  min: number; // slider: lower bound of the parameter range; select: 0
  max: number; // slider: upper bound of the parameter range; select: 127
  default: number; // slider: default value; select: default CC raw value
  unit?: string; // slider display suffix
  step?: number; // slider drag granularity (defaults to 0.001)
  scale?: "log"; // optional: logarithmic slider mapping
  /**
   * Travel skew — position^skew before the range is applied; 1 = none.
   * Mirrors ParamDescriptor::skew. For a range whose useful part is bunched
   * at one end: skew < 1 expands the top across the control, skew > 1 the
   * bottom. Unlike `scale` it works on ranges that cross zero.
   */
  skew?: number;
  ccRange?: { min: number; max: number }; // map min…max onto a CC sub-range
  type?: ParamType; // defaults to "slider"
  options?: SelectOption[]; // select only
  rowBreakBefore?: boolean; // full-width row break before this param in the grid
}

/**
 * Control position 0–1 → value.
 *
 * Mirrors ParamDescriptor::fromPos() in platform/include/ParamDescriptor.h.
 * "Control position" is knob travel, CC/127 and slider travel alike — they are
 * the same quantity, and routing all three through one curve is the only reason
 * a hardware knob, a MIDI CC and this UI land on the same value.
 */
export function posToValue(param: CCParam, pos: number): number {
  let t = pos < 0 ? 0 : pos > 1 ? 1 : pos;
  const skew = param.skew ?? 1;
  if (skew !== 1 && t > 0) t = Math.pow(t, skew);
  if (param.scale === "log") {
    return param.min * Math.pow(param.max / param.min, t);
  }
  return param.min + t * (param.max - param.min);
}

/** Value → control position 0–1. Mirrors ParamDescriptor::toPos(). */
export function valueToPos(param: CCParam, value: number): number {
  let t =
    param.scale === "log"
      ? Math.log(value / param.min) / Math.log(param.max / param.min)
      : (value - param.min) / (param.max - param.min);
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const skew = param.skew ?? 1;
  if (skew !== 1) t = Math.pow(t, 1 / skew);
  return t;
}

/**
 * Convert a 0–127 MIDI CC value to a float (slider params only).
 * Mirrors ParamDescriptor::fromCC() — both sides derive their ranges from the
 * same params.json, so the two must agree or a round trip through the module
 * shifts the value.
 */
export function ccToFloat(param: CCParam, ccValue: number): number {
  const ccLo = param.ccRange?.min ?? 0;
  const ccHi = param.ccRange?.max ?? 127;
  return posToValue(param, (ccValue - ccLo) / (ccHi - ccLo));
}

/** Convert a float value to a 0–127 MIDI CC value (slider params only). */
export function floatToCC(param: CCParam, value: number): number {
  const ccLo = param.ccRange?.min ?? 0;
  const ccHi = param.ccRange?.max ?? 127;
  const t = valueToPos(param, value);
  if (t <= 0) return ccLo;
  if (t >= 1) return ccHi;
  return Math.round(ccLo + t * (ccHi - ccLo));
}
