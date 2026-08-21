<script lang="ts" module>
  /**
   * Silkscreen glyphs.
   *
   * Drawn as paths on a 24x16 box so they line up on one baseline whatever the
   * shape, and stroked in `currentColor` so the caller decides lit vs unlit
   * without this file knowing anything about the palette.
   */
  export type WaveIconName =
    | "sine"
    | "triangle"
    | "saw"
    | "ramp"
    | "pulse"
    | "square"
    | "noise"
    | "exp"
    | "lin"
    | "log";

  const PATHS: Record<WaveIconName, string> = {
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
  };
</script>

<script lang="ts">
  let {
    name,
    size = 22,
  }: {
    name: WaveIconName;
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
