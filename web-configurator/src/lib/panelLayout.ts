/**
 * Panel layout — where each parameter sits in the panel view, and how loudly
 * it is drawn.
 *
 * Deliberately *not* in `modules/<m>/params.json`. That file is the contract
 * between the firmware's C++ descriptor table and this app; it says what a
 * parameter *is*, and gen_params.py emits both sides from it. Where a knob
 * lands on a screen is not that — it is art direction, it changes without the
 * firmware changing, and pushing it into params.json would mean regenerating
 * the C++ table to nudge a control one column left.
 *
 * So layout is authored here, against the `name` field, which params.json
 * calls out as the stable API. The safety property that makes this workable is
 * `layoutFor()`: anything in PARAM_MAP that no section claims is appended in
 * an auto-generated section grouped by category. Adding a parameter and
 * forgetting to place it costs you a slightly ugly trailing group, never a
 * control that silently is not there.
 *
 * `span` is a share of a twelve-column grid on a **fixed-width stage** that
 * PanelView scales to the window (see PanelLayout.stageWidth). That is what makes hand-tuned
 * spans safe: the section that fits its controls exactly on the design width
 * fits them at every window size, because the whole panel zooms rather than
 * reflowing. Spans in each band should total 12.
 */

import type { CCParam } from "./paramMapTypes";
import type { GlyphName } from "../components/panel/Glyph.svelte";

export interface PanelControl {
  /** `name` from params.json — the stable identity, not the CC or the label. */
  name: string;
  /**
   * How to draw it. Omitted, a slider becomes a knob and a select picks its
   * own shape from its option count (see PanelSelect).
   */
  control?: "knob" | "segmented" | "ladder" | "dropdown";
  /**
   * Visual weight. This is the main tool for making a section say what it is
   * about: `lg` for the one or two controls you actually perform with, `md`
   * for the rest of the voice, `sm` for set-and-forget trim. Sizing everything
   * the same makes a panel that is technically complete and tells you nothing.
   */
  size?: "sm" | "md" | "lg";
  /** Panel-style parenthetical sublabel, e.g. "V/OCT" under ROOT. */
  sub?: string;
  /** Shorter legend than param.label, when the full one will not set well. */
  label?: string;
  /**
   * Glyphs for a control whose positions are shapes rather than numbers — the
   * wave morph is the obvious one, but any sweep through named states can
   * carry them.
   *
   * On a knob they spread across the travel with the nearest one lit; on a
   * segmented switch they sit above the option labels, one glyph per option
   * (any other count is ignored — see PanelSelect).
   */
  icons?: GlyphName[];
  /** Start a new line within the section before this control. */
  breakBefore?: boolean;
}

export interface PanelSectionDef {
  title: string;
  /** Width in columns of the stage's 12-column grid. */
  span: number;
  /**
   * Stretch to the height of the tallest section in the same band.
   *
   * Off by default, because forcing it everywhere is what left Voice as three
   * controls adrift in a box sized by Oscillator. Worth turning on for a band
   * whose sections are already close in height — the effects row, where the
   * even outlines read as deliberate rather than as padding.
   */
  stretch?: boolean;
  /** An inline diagram drawn above the controls. */
  visual?: "fxchain" | "envelope" | "scale" | "filter";
  controls: PanelControl[];
}

export interface PanelLayout {
  /**
   * Design width of the stage in px, which PanelView scales to the window.
   *
   * Per module, because it sets the density: AlloyFlux's forty parameters need
   * room, Alloy Coil's twelve do not, and giving the small module the wide
   * stage just spread five sections thin. A narrower stage for a smaller
   * module simply zooms up further.
   *
   * Aim for roughly twice the natural height — that is about the aspect of a
   * browser window's usable area once the chrome and the dock are taken off,
   * and whichever axis fits worse is the one that decides the zoom.
   */
  stageWidth: number;
  sections: PanelSectionDef[];
}

/** Dial diameters in px, by size name. Mirrors the widths in Knob.svelte. */
const SIZE_PX = { sm: 66, md: 88, lg: 118 } as const;

/**
 * AlloyFlux — three bands of twelve columns.
 *
 * Order follows the signal, left to right and top to bottom: what the voice
 * is, then the oscillators, how they move, the envelope shaping them, the
 * filter, the output, and finally the effects.
 *
 * The large knobs are the ones that are large on the hardware — ROOT,
 * RELATION, SHAPE and MOTION — plus the one control that dominates each
 * downstream section (cutoff, level, and each effect's mix).
 */
const ALLOYFLUX: PanelLayout = {
  stageWidth: 1700,
  sections: [
    // ── Band 1 ────────────────────────────────────────────────── 3 + 5 + 4
    {
      title: "Voice",
      span: 3,
      stretch: true,
      visual: "scale",
      controls: [
        { name: "mode", control: "ladder", label: "Mode" },
        { name: "scale", control: "dropdown", label: "Quantize" },
        { name: "transpose", size: "sm", sub: "semitones" },
        // Lives here rather than in Oscillator: it is a voice-level choice,
        // and a sixth control in Oscillator pushed the switch onto a line of
        // its own under five knobs.
        { name: "suboct", control: "segmented", label: "Sub Octave" },
      ],
    },
    {
      title: "Oscillator",
      span: 5,
      stretch: true,
      controls: [
        { name: "root", size: "lg", sub: "V/Oct" },
        { name: "rel", size: "lg", sub: "interval" },
        {
          name: "shape",
          size: "lg",
          sub: "wave morph",
          icons: ["sine", "triangle", "saw", "square", "pulse"],
        },
        // No sub: COLOR carries a mode-contextual hint ("FM depth" / "fine
        // detune") from App.svelte, and a fixed sublabel saying the same thing
        // would contradict it in half the voice modes.
        { name: "color" },
        {
          name: "fat",
          sub: "unison",
          icons: ["uni1", "uni2", "uni3"],
        },
      ],
    },
    {
      title: "Animation",
      span: 4,
      stretch: true,
      controls: [
        {
          name: "motion",
          size: "lg",
          sub: "drift / chorus",
          icons: ["flat", "wave-shallow", "wave-deep"],
        },
        {
          name: "dspeed",
          size: "sm",
          label: "Drift Rate",
          icons: ["wave-slow", "wave-fast"],
        },
        { name: "glidetime", size: "sm", label: "Glide Time" },
        // Off steps straight to the new note; on, it ramps there.
        { name: "glide", control: "segmented", icons: ["step", "slide"] },
        // Chorus lives here rather than in a section of its own. It is a
        // single parameter, so its own outline was three quarters empty — and
        // it belongs next to MOTION regardless: SynthEngine.cpp sets
        // `chorusDepth = _sMotion`, so MOTION *is* the chorus depth and this
        // selects which chorus that depth drives.
        { name: "chorusmode", control: "ladder", label: "Chorus" },
      ],
    },

    // ── Band 2 ────────────────────────────────────────────────────── 7 + 5
    {
      title: "Envelope",
      span: 7,
      stretch: true,
      visual: "envelope",
      controls: [
        // The two topologies as their own outlines — a rise and a fall against
        // one that holds a sustain in between.
        {
          name: "envtype",
          control: "segmented",
          label: "Type",
          icons: ["env-ar", "env-adsr"],
        },
        // Flat line vs ramp: the two things velocity can do to the level.
        {
          name: "veloc",
          control: "segmented",
          label: "Velocity",
          icons: ["flat", "ramp"],
        },
        { name: "adsrattack", label: "Attack", breakBefore: true },
        { name: "adsrdecay", label: "Decay" },
        { name: "adsrsustain", label: "Sustain" },
        { name: "adsrrelease", label: "Release" },
        // "(ENV / RESPONSE)" is what the hardware silkscreen says under CURVE.
        { name: "curve", sub: "env / response" },
        { name: "curvetime", size: "sm", label: "Scale" },
        { name: "gatelen", size: "sm", label: "Gate" },
      ],
    },
    {
      title: "Filter",
      span: 5,
      stretch: true,
      visual: "filter",
      controls: [
        // The two knobs ride beside the plot, because they are the two the
        // plot draws — move one and the curve under your hand moves with it.
        // The switches, which pick *which* curve, take the line below. That
        // also keeps the two rows near enough in width (about 530 and 310) to
        // read as a stack rather than as a full-bleed row above a pair of
        // knobs adrift in the middle of the section.
        { name: "filtercutoff", size: "lg", label: "Cutoff" },
        { name: "filterres", label: "Resonance" },
        // The four SVF taps as the shapes they are, plus a flat line for OFF —
        // which is exactly what the plot draws when it is selected.
        {
          name: "filtermode",
          control: "segmented",
          label: "Mode",
          icons: ["flat", "lp", "hp", "bp", "notch"],
          breakBefore: true,
        },
        // Two poles against four: the difference the switch makes is the
        // steepness of the tail, so that is what the glyphs show.
        {
          name: "filtertype",
          control: "segmented",
          label: "Algorithm",
          icons: ["slope2", "slope4"],
        },
      ],
    },
    // ── Band 3 ────────────────────────────────────────────── 3 + 4 + 3 + 2
    {
      title: "Delay",
      span: 3,
      stretch: true,
      controls: [
        { name: "delaymix", size: "lg", label: "Mix" },
        // Repeats tightening up or spreading out.
        { name: "delaytime", label: "Time", icons: ["echo-near", "echo-far"] },
        { name: "delayfb", label: "Feedback" },
      ],
    },
    {
      title: "Reverb",
      span: 4,
      stretch: true,
      controls: [
        { name: "revmix", size: "lg", label: "Mix" },
        // The space around the source opening up.
        {
          name: "revsize",
          label: "Size",
          icons: ["room-sm", "room-md", "room-lg"],
        },
        // Damping is a low-pass on the tail, so it is drawn as one: flat at
        // zero, rolled off at full.
        { name: "revdamping", label: "Damping", icons: ["flat", "lp"] },
        // Rate and depth reuse the vocabulary MOTION and DRIFT RATE already
        // use, because they mean the same two things. Learn the pair once.
        {
          name: "revmodspeed",
          size: "sm",
          label: "Mod Rate",
          icons: ["wave-slow", "wave-fast"],
        },
        {
          name: "revmoddepth",
          size: "sm",
          label: "Mod Depth",
          icons: ["flat", "wave-shallow", "wave-deep"],
        },
      ],
    },
    {
      title: "FX Chain",
      span: 3,
      stretch: true,
      visual: "fxchain",
      controls: [
        { name: "fxfilterpos", control: "segmented", label: "Filter" },
        { name: "fxdelaypos", control: "segmented", label: "Delay" },
      ],
    },
    // Last, as the signal's last stop.
    {
      title: "Output",
      span: 2,
      stretch: true,
      controls: [
        { name: "vol", size: "lg", label: "Level" },
        // params.json labels this "Space (Stereo Width)"; on the panel the
        // parenthetical is the sublabel, not part of the legend.
        {
          name: "space",
          label: "Space",
          sub: "stereo width",
          icons: ["mono", "stereo", "wide"],
        },
      ],
    },
  ],
};

/**
 * Alloy Coil — two bands of twelve.
 *
 * The feedback loop is the instrument here: gain and body are what you play,
 * and the two filters are how you keep the loop from running away. They are
 * sized accordingly. Twelve parameters against AlloyFlux's forty, so the
 * sections are wider per control rather than more numerous.
 *
 * Every section stretches. Left to hug their contents, the ones carrying a
 * glyph strip stand a row taller than the ones that do not, and with only two
 * or three outlines to a band the mismatch is the first thing you see.
 */
const ALLOYCOIL: PanelLayout = {
  stageWidth: 1050,
  sections: [
    // ── Band 1 ────────────────────────────────────────────────────── 5 + 7
    {
      title: "Resonator",
      span: 5,
      stretch: true,
      controls: [
        { name: "pitch", size: "lg", label: "Pitch", sub: "string" },
        { name: "excite", size: "lg", label: "Exciter" },
      ],
    },
    {
      title: "Feedback",
      span: 7,
      stretch: true,
      controls: [
        { name: "fbgain", size: "lg", label: "Gain" },
        // BODY is the length of the feedback delay — MANUAL.md calls short "a
        // tight metallic ping" and long "a hollow, tube-like resonance", which
        // is the space around the source opening up.
        {
          name: "fbbody",
          size: "lg",
          label: "Body",
          icons: ["room-sm", "room-md", "room-lg"],
        },
        // Both filters are drawn by what they leave: the LPF opens from dark to
        // flat as it sweeps up, the HPF goes the other way and thins out.
        { name: "fblpf", size: "sm", label: "LPF", icons: ["lp", "flat"] },
        { name: "fbhpf", size: "sm", label: "HPF", icons: ["flat", "hp"] },
      ],
    },

    // ── Band 2 ────────────────────────────────────────────────── 5 + 4 + 3
    {
      title: "Echo",
      span: 5,
      stretch: true,
      controls: [
        { name: "echosend", size: "lg", label: "Send" },
        // Same pair as AlloyFlux's delay TIME — one echo vocabulary, two
        // modules.
        { name: "echotime", label: "Time", icons: ["echo-near", "echo-far"] },
        { name: "echofb", label: "Feedback" },
      ],
    },
    {
      title: "Reverb",
      span: 4,
      stretch: true,
      controls: [
        { name: "revmix", size: "lg", label: "Mix" },
        // How long the tail runs on.
        {
          name: "revdecay",
          label: "Decay",
          icons: ["decay-short", "decay-long"],
        },
      ],
    },
    {
      title: "Output",
      span: 3,
      stretch: true,
      controls: [{ name: "vol", size: "lg", label: "Level" }],
    },
  ],
};

const LAYOUTS: Readonly<Record<string, PanelLayout>> = {
  alloyflux: ALLOYFLUX,
  alloycoil: ALLOYCOIL,
};

/** A section resolved against a parameter map, ready to render. */
export interface ResolvedSection {
  title: string;
  span: number;
  /**
   * Diameter of the largest knob in the section. Every dial slot inside is
   * given this height, so mixed sizes share a centre line and a readout
   * baseline instead of stepping down as the knobs get smaller.
   */
  dialBox: number;
  /**
   * Whether any *knob* here carries a glyph strip. When one does, every knob
   * in the section reserves the row — otherwise the one knob with icons sits
   * taller than its neighbours and drops its readout below their baseline,
   * undoing what dialBox is for.
   *
   * Selects are excluded: their glyphs sit inside the switch, so a section
   * whose only glyphs are on a switch would reserve an empty strip under every
   * knob in it for nothing.
   */
  iconRow: boolean;
  /** Stretch to the tallest section in the band. */
  stretch: boolean;
  visual?: "fxchain" | "envelope" | "scale" | "filter";
  items: { param: CCParam; control: PanelControl }[];
}

/**
 * Sections for a module, resolved against its parameter map.
 *
 * Two kinds of drift are absorbed here rather than being allowed to reach the
 * screen: a control naming a parameter the map does not have is dropped, and a
 * parameter no section places is appended in a per-category catch-all. Between
 * them, this never renders a control with nothing behind it and never loses
 * one that params.json declares.
 */
/** Design width of a module's stage, in px. Falls back to AlloyFlux's. */
export function stageWidthFor(moduleId: string): number {
  return LAYOUTS[moduleId]?.stageWidth ?? ALLOYFLUX.stageWidth;
}

export function layoutFor(moduleId: string, map: CCParam[]): ResolvedSection[] {
  const byName = new Map(map.map((p) => [p.name, p]));
  const placed = new Set<string>();
  const layout = LAYOUTS[moduleId];

  /** Largest dial in a set of controls; selects do not have one. */
  const dialBoxFor = (items: { param: CCParam; control: PanelControl }[]) =>
    items.reduce<number>(
      (max, { param, control }) =>
        param.type === "select"
          ? max
          : Math.max(max, SIZE_PX[control.size ?? "md"]),
      SIZE_PX.sm,
    );

  const sections: ResolvedSection[] = (layout?.sections ?? []).flatMap((s) => {
    const items = s.controls.flatMap((control) => {
      const param = byName.get(control.name);
      if (!param) return [];
      placed.add(control.name);
      return [{ param, control }];
    });
    // A section whose parameters have all gone is not worth an empty outline,
    // unless it exists to carry a diagram.
    return items.length || s.visual
      ? [
          {
            title: s.title,
            span: s.span,
            dialBox: dialBoxFor(items),
            iconRow: items.some(
              (i) =>
                i.param.type !== "select" && (i.control.icons?.length ?? 0) > 0,
            ),
            stretch: s.stretch ?? false,
            visual: s.visual,
            items,
          },
        ]
      : [];
  });

  const unplaced = map.filter((p) => !placed.has(p.name));
  if (unplaced.length === 0) return sections;

  // Catch-all, grouped by the category the parameter already declares. Order
  // follows first appearance in the map so it is at least deterministic.
  const byCategory = new Map<string, CCParam[]>();
  for (const p of unplaced) {
    const list = byCategory.get(p.category);
    if (list) list.push(p);
    else byCategory.set(p.category, [p]);
  }
  for (const [category, ps] of byCategory) {
    const items = ps.map((param) => ({
      param,
      control: { name: param.name } as PanelControl,
    }));
    sections.push({
      title: category,
      span: Math.min(12, Math.max(3, Math.ceil(ps.length / 2) * 2)),
      dialBox: dialBoxFor(items),
      // Nothing auto-placed carries glyphs — those are authored per control.
      iconRow: false,
      stretch: false,
      items,
    });
  }
  return sections;
}
