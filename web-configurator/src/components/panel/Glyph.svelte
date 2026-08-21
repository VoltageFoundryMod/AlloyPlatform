<script lang="ts" module>
  /**
   * Silkscreen glyphs.
   *
   * Drawn as paths on a 24x16 box so they line up on one baseline whatever the
   * shape, and stroked in `currentColor` so the caller decides lit vs unlit
   * without this file knowing anything about the palette.
   *
   * Grouped below by what they say rather than by what they draw — a glyph
   * earns a place here when a control's positions mean *shapes* rather than
   * numbers, and the readout alone cannot show which shape you are on.
   */
  export type GlyphName =
    // Oscillator waveforms
    | "sine"
    | "triangle"
    | "saw"
    | "ramp"
    | "pulse"
    | "square"
    | "noise"
    // Curves
    | "exp"
    | "lin"
    | "log"
    // Modulation depth / rate
    | "flat"
    | "wave-shallow"
    | "wave-deep"
    | "wave-slow"
    | "wave-fast"
    // Filter responses
    | "lp"
    | "hp"
    | "bp"
    | "notch"
    | "slope2"
    | "slope4"
    // Unison spread
    | "uni1"
    | "uni2"
    | "uni3"
    // Stereo width
    | "mono"
    | "stereo"
    | "wide";

  const PATHS: Record<GlyphName, string> = {
    sine: "M1 8 C 4 1, 8 1, 11 8 S 19 15, 23 8",
    triangle: "M1 13 L 7 3 L 13 13 L 19 3 L 23 9",
    saw: "M1 13 L 9 3 L 9 13 L 17 3 L 17 13 L 22 8",
    ramp: "M1 13 L 23 3",
    // Hollow pulse — a narrow duty cycle, as at the top of a wavefolder sweep.
    pulse: "M1 13 L 5 13 L 5 3 L 8 3 L 8 13 L 16 13 L 16 3 L 19 3 L 19 13 L 23 13",
    square: "M1 13 L 1 3 L 9 3 L 9 13 L 17 13 L 17 3 L 23 3",
    noise: "M1 11 L 3 5 L 5 12 L 7 4 L 9 10 L 11 3 L 13 12 L 15 6 L 17 13 L 19 5 L 21 11 L 23 7",
    exp: "M1 14 C 12 14, 20 12, 23 2",
    lin: "M1 14 L 23 2",
    log: "M1 14 C 4 4, 12 2, 23 2",

    // Depth: a dead line, then the same wave twice at a third and at full
    // amplitude. Read as a strip they are one shape growing, which is what a
    // depth control does.
    flat: "M1 8 L 23 8",
    "wave-shallow": "M1 8 Q 6.5 5 12 8 T 23 8",
    "wave-deep": "M1 8 Q 6.5 1 12 8 T 23 8",
    // Rate: same amplitude, more cycles across the same box.
    "wave-slow": "M1 8 Q 4.7 1 8.3 8 T 15.7 8 T 23 8",
    "wave-fast":
      "M1 8 Q 2.6 2.5 4.1 8 T 7.3 8 T 10.4 8 T 13.6 8 T 16.7 8 T 19.9 8 T 23 8",

    // Filter responses, drawn as magnitude curves: passband high, stopband low.
    lp: "M1 5 L 11 5 C 15 5, 16 8, 17 10 L 22 15",
    hp: "M2 15 L 7 10 C 8 8, 9 5, 13 5 L 23 5",
    bp: "M1 15 C 7 15, 8 4, 12 4 C 16 4, 17 15, 23 15",
    notch: "M1 5 C 7 5, 8 15, 12 15 C 16 15, 17 5, 23 5",
    // Two- vs four-pole: the difference is the tail, so one rolls away across
    // the whole box and the other falls off a cliff.
    slope2: "M1 4 L 9 4 C 12 4, 13 6, 14 8 L 23 14",
    slope4: "M1 4 L 11 4 C 13.5 4, 14 5, 14.5 7 L 17 15",

    // Unison: partials spreading out from one.
    uni1: "M12 3 L 12 13",
    uni2: "M12 3 L 12 13 M6 5 L 6 11 M18 5 L 18 11",
    uni3: "M12 2 L 12 14 M7 4 L 7 12 M17 4 L 17 12 M2 6 L 2 10 M22 6 L 22 10",

    // Stereo width: one source, then two, moving apart. The pair stops short
    // of the box edges — pushed all the way out, the two rings read as one
    // glyph each rather than as the ends of a spread.
    mono: "M9 8 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0",
    stereo: "M6 8 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0 M12 8 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0",
    wide: "M2 8 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0 M16 8 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0",
  };
</script>

<script lang="ts">
  let {
    name,
    size = 22,
  }: {
    name: GlyphName;
    size?: number;
  } = $props();
</script>

<svg
  viewBox="0 0 24 16"
  width={size}
  height={(size * 16) / 24}
  aria-hidden="true"
  focusable="false"
>
  <path d={PATHS[name]} />
</svg>

<style>
  svg {
    display: block;
    overflow: visible;
  }
  path {
    fill: none;
    stroke: currentColor;
    stroke-width: 1.6;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
</style>
