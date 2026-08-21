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
  /**
   * How the value is *shown*, as opposed to how it is stored. Mirrors
   * ParamDescriptor::display / ParamDisplay.
   *
   * Not the same lever as `scale` or `skew`: those decide where a value sits on
   * the control, this decides only the number printed beside it. Nothing on the
   * wire changes. Alloy Coil's exciter level is the one user — the engine wants
   * a multiplier, so it is stored 0–2 with unity at 1.0, and it reads far
   * better as −inf…+6 dB with unity at 0.
   */
  displayTransform?: "gain-db";
  ccRange?: { min: number; max: number }; // map min…max onto a CC sub-range
  type?: ParamType; // defaults to "slider"
  options?: SelectOption[]; // select only
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

/**
 * Value → the number shown to the user. Mirrors ParamDescriptor::toDisplay().
 *
 * Returns -Infinity for a zero gain, which is the honest answer; `formatValue`
 * renders it as "−inf" rather than letting toFixed() produce "-Infinity".
 */
export function valueToDisplay(param: CCParam, value: number): number {
  if (param.displayTransform === "gain-db") {
    return value > 0 ? 20 * Math.log10(value) : -Infinity;
  }
  return value;
}

/** The inverse of valueToDisplay(). Mirrors ParamDescriptor::fromDisplay(). */
export function displayToValue(param: CCParam, display: number): number {
  if (param.displayTransform === "gain-db") return Math.pow(10, display / 20);
  return display;
}

/**
 * How many decimals to show. Mirrors ParamDescriptor::displayDigits().
 *
 * Taken from the parameter's *range and CC resolution*, never from the current
 * value, so a control keeps one precision all the way round instead of gaining
 * and losing digits as it is turned.
 *
 * The resolution half matters more than it looks. A CC is 7 bits, so a
 * parameter is only known to one part in 127 of its range, and printing past
 * that claims accuracy the wire cannot carry. ROOT is the case that gives it
 * away: it is ±48 semitones across 128 steps, so exact centre would need CC
 * 63.5 and an initialised patch comes back as CC 64 — 0.378 st. Shown to one
 * decimal that reads as a module that is 38 cents sharp on a default patch. One
 * CC step there is 0.76 st, so the honest rendering is a whole number, and
 * 0.378 rounds to the 0 the user expects.
 *
 * A superset of the C++ side by exactly two clauses — `step` and `ccRange` are
 * columns this map carries and ParamDescriptor does not. Every parameter that
 * declares neither rounds identically here and in the VCV tooltip.
 */
export function displayDigits(param: CCParam): number {
  // A dB readout has no single resolution to derive: the same CC step is about
  // 0.14 dB at the top of the exciter's travel and tens of dB near the bottom,
  // because the transform is logarithmic and the taper is square-law. One
  // decimal is the finest reading that is honest anywhere on the sweep.
  if (param.displayTransform === "gain-db") return 1;
  if (param.step && param.step >= 1) return 0;
  const span = Math.max(Math.abs(param.min), Math.abs(param.max));
  if (span >= 100) return 0;
  // Native units covered by one CC step. A log parameter's step varies across
  // its travel, so it falls back to the range test above.
  if (param.scale === "log") return span >= 10 ? 1 : 2;
  const ccLo = param.ccRange?.min ?? 0;
  const ccHi = param.ccRange?.max ?? 127;
  const res = (param.max - param.min) / Math.max(1, ccHi - ccLo);
  return res >= 0.5 ? 0 : res >= 0.05 ? 1 : res >= 0.005 ? 2 : 3;
}

/** The readout for a value: transformed, rounded, and suffixed with its unit. */
export function formatValue(
  param: CCParam,
  value: number,
  digits: number = displayDigits(param),
): string {
  const d = valueToDisplay(param, value);
  const unit = param.unit ? ` ${param.unit}` : "";
  if (!Number.isFinite(d)) return `${d < 0 ? "−" : ""}inf${unit}`;
  return `${d.toFixed(digits)}${unit}`;
}
