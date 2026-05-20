/**
 * Parameter map — mirrors src/param_map.cpp + special CC handlers in the firmware.
 *
 * CCParam.type:
 *   "slider"  — float parameter driven by MIDI CC; rendered as a range slider
 *   "select"  — enum parameter; CC value range determines the active option;
 *               rendered as a segmented button group
 *
 * For "select" params, options[].ccMin / ccMax define the CC range for each
 * option.  The firmware maps the full range (e.g. 0–63 = PAIR, 64–127 = CHORD).
 * Sending a CC at options[i].ccMin activates option i.
 *
 * Adding a new parameter:
 *   1. Add a row here in the appropriate category.
 *   2. If it is a float, also add it to kCCParams[] in src/param_map.cpp.
 *   3. If it is an enum / special case, add it to the switch in usb_midi.cpp.
 */

export type ParamType = "slider" | "select";

export interface SelectOption {
  label: string;
  ccMin: number; // inclusive lower bound of CC range for this option
  ccMax: number; // inclusive upper bound
}

export interface CCParam {
  cc: number;
  name: string;
  label: string;
  category: string;
  min: number; // slider: lower bound of parameter range; select: 0
  max: number; // slider: upper bound of parameter range; select: 127
  default: number; // slider: default float value; select: default CC raw value
  unit?: string; // slider display suffix
  step?: number; // slider drag granularity (defaults to 0.001)
  scale?: "log"; // optional: use logarithmic mapping for the slider
  type?: ParamType; // defaults to "slider"
  options?: SelectOption[]; // select type only
}

// Ordered list of category labels — controls display order in the UI.
export const PARAM_CATEGORIES = [
  "Voice",
  "Oscillator",
  "Animation",
  "Envelope",
  "Filter",
  "FX Chain",
  "Output",
  "Chorus",
  "Reverb",
  "Delay",
] as const;

export type ParamCategory = (typeof PARAM_CATEGORIES)[number];

export const PARAM_MAP: CCParam[] = [
  // ── Voice ────────────────────────────────────────────────────────────────
  // CC 115: voice mode — 4 bands of 32: 0-31=PAIR, 32-63=CLOUD, 64-95=CHORD, 96-127=POLY
  {
    cc: 115,
    name: "mode",
    label: "Voice Mode",
    category: "Voice",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Pair", ccMin: 0, ccMax: 31 },
      { label: "Cloud", ccMin: 32, ccMax: 63 },
      { label: "Chord", ccMin: 64, ccMax: 95 },
      { label: "Poly", ccMin: 96, ccMax: 127 },
    ],
  },

  // ── Oscillator ───────────────────────────────────────────────────────────
  {
    cc: 74,
    name: "shape",
    label: "Shape",
    category: "Oscillator",
    min: 0,
    max: 1,
    default: 0.25,
  },
  {
    cc: 93,
    name: "fat",
    label: "Fatness",
    category: "Oscillator",
    min: 0,
    max: 1,
    default: 0,
  },
  {
    cc: 92,
    name: "detune",
    label: "Detune",
    category: "Oscillator",
    min: 0,
    max: 200,
    default: 0,
    unit: "Hz",
    step: 0.1,
  },
  // PAIR: integer semitones above root (0=unison…24=+2 oct).
  // CHORD: maps 0–24 onto 11 chord shapes (Unison/Power/Minor/Major/Sus2/Sus4/Maj7/Min7/Dom7/Dim/Oct).
  {
    cc: 94,
    name: "rel",
    label: "Relation",
    category: "Oscillator",
    min: 0,
    max: 24,
    default: 0,
    unit: "st",
    step: 1,
  },
  // Sub octave — CC 90: 0–63 = 1 oct below, 64–127 = 2 oct below
  {
    cc: 90,
    name: "suboct",
    label: "Sub Octave",
    category: "Oscillator",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "−1 Oct", ccMin: 0, ccMax: 63 },
      { label: "−2 Oct", ccMin: 64, ccMax: 127 },
    ],
  },

  // ── Animation ────────────────────────────────────────────────────────────
  {
    cc: 1,
    name: "motion",
    label: "Motion",
    category: "Animation",
    min: 0,
    max: 1,
    default: 0,
  },
  {
    cc: 73,
    name: "dspeed",
    label: "Drift Speed",
    category: "Animation",
    min: 0.001,
    max: 0.1,
    default: 0.025,
  },

  // ── Envelope ─────────────────────────────────────────────────────────────
  // CC 81: envelope type — 0–63 = AR, 64–127 = ADSR
  {
    cc: 81,
    name: "envtype",
    label: "Type",
    category: "Envelope",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "AR", ccMin: 0, ccMax: 63 },
      { label: "ADSR", ccMin: 64, ccMax: 127 },
    ],
  },
  {
    cc: 71,
    name: "curve",
    label: "Shape",
    category: "Envelope",
    min: 0,
    max: 1,
    default: 0.5,
  },
  {
    cc: 72,
    name: "curvetime",
    label: "Time Scale",
    category: "Envelope",
    min: 0.25,
    max: 4,
    default: 1,
    unit: "×",
  },
  // ADSR params — only active when envelope type is ADSR
  {
    cc: 82,
    name: "adsrattack",
    label: "Attack",
    category: "Envelope",
    min: 0.001,
    max: 4,
    default: 0.05,
    unit: "s",
    step: 0.001,
  },
  {
    cc: 83,
    name: "adsrdecay",
    label: "Decay",
    category: "Envelope",
    min: 0.001,
    max: 4,
    default: 0.1,
    unit: "s",
    step: 0.001,
  },
  {
    cc: 84,
    name: "adsrsustain",
    label: "Sustain",
    category: "Envelope",
    min: 0,
    max: 1,
    default: 0.8,
  },
  {
    cc: 95,
    name: "adsrrelease",
    label: "Release",
    category: "Envelope",
    min: 0.001,
    max: 4,
    default: 0.3,
    unit: "s",
    step: 0.001,
  },

  // ── Filter ───────────────────────────────────────────────────────────────
  // CC 77: filter mode — thresholds match firmware usb_midi.cpp case 77
  {
    cc: 77,
    name: "filtermode",
    label: "Mode",
    category: "Filter",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Off", ccMin: 0, ccMax: 25 },
      { label: "LP", ccMin: 26, ccMax: 50 },
      { label: "HP", ccMin: 51, ccMax: 76 },
      { label: "BP", ccMin: 77, ccMax: 101 },
      { label: "Notch", ccMin: 102, ccMax: 127 },
    ],
  },
  // CC 78: filter algorithm — 0–63 = SVF, 64–127 = Ladder
  {
    cc: 78,
    name: "filtertype",
    label: "Algorithm",
    category: "Filter",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "SVF", ccMin: 0, ccMax: 63 },
      { label: "Ladder", ccMin: 64, ccMax: 127 },
    ],
  },
  {
    cc: 75,
    name: "filtercutoff",
    label: "Cutoff",
    category: "Filter",
    min: 20,
    max: 16000,
    default: 983, // CC 74 on log 20-16000 Hz scale = kDefaultFilterCutoff in firmware
    unit: "Hz",
    step: 1,
    scale: "log",
  },
  {
    cc: 76,
    name: "filterres",
    label: "Resonance",
    category: "Filter",
    min: 0,
    max: 1,
    default: 0,
  },

  // ── FX Chain ─────────────────────────────────────────────────────────────
  // CC 79: filter position in chain — pre or post chorus
  {
    cc: 79,
    name: "fxfilterpos",
    label: "Filter Position",
    category: "FX Chain",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Pre Chorus", ccMin: 0, ccMax: 63 },
      { label: "Post Chorus", ccMin: 64, ccMax: 127 },
    ],
  },
  // CC 80: delay position in chain — pre or post reverb
  {
    cc: 80,
    name: "fxdelaypos",
    label: "Delay Position",
    category: "FX Chain",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Pre Reverb", ccMin: 0, ccMax: 63 },
      { label: "Post Reverb", ccMin: 64, ccMax: 127 },
    ],
  },

  // ── Output ───────────────────────────────────────────────────────────────
  {
    cc: 7,
    name: "vol",
    label: "Volume",
    category: "Output",
    min: 0,
    max: 1,
    default: 0.8,
  },
  {
    cc: 91,
    name: "space",
    label: "Stereo Width",
    category: "Output",
    min: 0,
    max: 2,
    default: 1,
  },

  // ── Chorus ───────────────────────────────────────────────────────────────
  // CC 89: 0–31=OFF, 32–63=I, 64–95=II, 96–127=I+II (firmware: usb_midi.cpp case 89)
  {
    cc: 89,
    name: "chorusmode",
    label: "Mode",
    category: "Chorus",
    min: 0,
    max: 127,
    default: 96,
    type: "select",
    options: [
      { label: "Off", ccMin: 0, ccMax: 31 },
      { label: "I", ccMin: 32, ccMax: 63 },
      { label: "II", ccMin: 64, ccMax: 95 },
      { label: "I+II", ccMin: 96, ccMax: 127 },
    ],
  },

  // ── Reverb ───────────────────────────────────────────────────────────────
  // CC 116: reverb on/off (≥64=on) — firmware: usb_midi.cpp case 116
  {
    cc: 116,
    name: "revon",
    label: "Enable",
    category: "Reverb",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Off", ccMin: 0, ccMax: 63 },
      { label: "On", ccMin: 64, ccMax: 127 },
    ],
  },
  {
    cc: 117,
    name: "revmix",
    label: "Mix",
    category: "Reverb",
    min: 0,
    max: 1,
    default: 0.35,
  },
  {
    cc: 118,
    name: "revsize",
    label: "Size",
    category: "Reverb",
    min: 0,
    max: 1,
    default: 0.5,
  },
  {
    cc: 120,
    name: "revdamping",
    label: "Damping",
    category: "Reverb",
    min: 0,
    max: 1,
    default: 0.5,
  },
  {
    cc: 112,
    name: "revmodspeed",
    label: "Mod Speed",
    category: "Reverb",
    min: 0.1,
    max: 4,
    default: 1,
  },
  {
    cc: 113,
    name: "revmoddepth",
    label: "Mod Depth",
    category: "Reverb",
    min: 0,
    max: 1,
    default: 1,
  },

  // ── Delay ────────────────────────────────────────────────────────────────
  // CC 85: delay on/off (≥64=on, mix stored/restored) — firmware: usb_midi.cpp case 85
  {
    cc: 85,
    name: "delayon",
    label: "Enable",
    category: "Delay",
    min: 0,
    max: 127,
    default: 0,
    type: "select",
    options: [
      { label: "Off", ccMin: 0, ccMax: 63 },
      { label: "On", ccMin: 64, ccMax: 127 },
    ],
  },
  {
    cc: 86,
    name: "delaytime",
    label: "Time",
    category: "Delay",
    min: 10,
    max: 500,
    default: 100,
    unit: "ms",
    step: 1,
  },
  {
    cc: 87,
    name: "delayfb",
    label: "Feedback",
    category: "Delay",
    min: 0,
    max: 0.95,
    default: 0.5,
  },
  {
    cc: 88,
    name: "delaymix",
    label: "Mix",
    category: "Delay",
    min: 0,
    max: 1,
    default: 0.0,
  },
];

/** Params grouped by category in PARAM_CATEGORIES display order. */
export const PARAMS_BY_CATEGORY = Object.fromEntries(
  PARAM_CATEGORIES.map((cat) => [
    cat,
    PARAM_MAP.filter((p) => p.category === cat),
  ]),
) as Record<ParamCategory, CCParam[]>;

/** Convert a 0–127 MIDI CC value to the parameter's float range (slider params only). */
export function ccToFloat(param: CCParam, ccValue: number): number {
  if (param.scale === "log") {
    // Log mapping: CC 0 → min, CC 127 → max
    return param.min * Math.pow(param.max / param.min, ccValue / 127);
  }
  return param.min + (ccValue / 127) * (param.max - param.min);
}

/** Convert a float value to a 0–127 MIDI CC value (slider params only). */
export function floatToCC(param: CCParam, value: number): number {
  if (param.scale === "log") {
    return Math.round(
      (Math.log(value / param.min) / Math.log(param.max / param.min)) * 127,
    );
  }
  return Math.round(((value - param.min) / (param.max - param.min)) * 127);
}
