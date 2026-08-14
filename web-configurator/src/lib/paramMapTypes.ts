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
  ccRange?: { min: number; max: number }; // map min…max onto a CC sub-range
  type?: ParamType; // defaults to "slider"
  options?: SelectOption[]; // select only
  rowBreakBefore?: boolean; // full-width row break before this param in the grid
}

/**
 * Convert a 0–127 MIDI CC value to a float (slider params only).
 * Mirrors ParamDescriptor::fromCC() in platform/include/ParamDescriptor.h —
 * both sides derive their ranges from the same params.json, so the two must
 * agree or a round trip through the module shifts the value.
 */
export function ccToFloat(param: CCParam, ccValue: number): number {
  const ccLo = param.ccRange?.min ?? 0;
  const ccHi = param.ccRange?.max ?? 127;
  const t = (ccValue - ccLo) / (ccHi - ccLo);
  if (param.scale === "log") {
    return param.min * Math.pow(param.max / param.min, t);
  }
  return param.min + t * (param.max - param.min);
}

/** Convert a float value to a 0–127 MIDI CC value (slider params only). */
export function floatToCC(param: CCParam, value: number): number {
  const ccLo = param.ccRange?.min ?? 0;
  const ccHi = param.ccRange?.max ?? 127;
  const t = (value - param.min) / (param.max - param.min);
  if (param.scale === "log") {
    return Math.round(
      ccLo +
        (Math.log(value / param.min) / Math.log(param.max / param.min)) *
          (ccHi - ccLo),
    );
  }
  return Math.round(ccLo + t * (ccHi - ccLo));
}
