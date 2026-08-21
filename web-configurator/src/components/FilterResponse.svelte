<script lang="ts" module>
  /** Which output tap the SVF is taking. Order matches the option list in
   *  paramMapAlloyFlux and FilterMode in dsp/FilterEngine.h. */
  export type FilterModeName = "off" | "lp" | "hp" | "bp" | "notch";
</script>

<script lang="ts">
  /**
   * FilterResponse — SVG magnitude plot of the filter as it is currently set.
   *
   * The curves are the analogue prototypes the two engines are discretised
   * from, evaluated at the knob's own cutoff and resonance, so the drawing
   * moves with the controls rather than illustrating "a filter" in general.
   *
   *   SVF (dsp/SVFFilter.h) — Cytomic trapezoidal state-variable, damping
   *     k = 2·(1−res), normalised frequency w = f/fc:
   *       D  = |(1−w²) + jkw|
   *       LP = 1/D      HP = w²/D      BP = w/D      NOTCH = |1−w²|/D
   *     BP is the raw v1 tap the DSP outputs, not the unity-peak variant — so
   *     it draws −6 dB at the corner with resonance down, which is what the
   *     module actually does.
   *
   *   LADDER (dsp/OTALadder.h) — four one-poles inside a feedback loop of
   *     k = 4·res:
   *       H = 1/((1+jw)⁴ + k)
   *     The passband droop that appears as resonance comes up is not an error
   *     in the drawing; it is the ladder's own bass loss, and being able to see
   *     it is half the reason to plot this at all.
   *
   * Both engines' cutoff clamps are applied (16 kHz on the SVF, 8 kHz on the
   * ladder, which aliases above sr/4) so the marker stops where the DSP stops
   * rather than running on to the edge of the plot.
   */
  import { formatValue } from "../lib/paramMapTypes";
  import type { CCParam } from "../lib/paramMap";

  let {
    mode = "off",
    ladder = false,
    cutoff = 1000,
    resonance = 0,
    cutoffParam = undefined,
  }: {
    mode?: FilterModeName;
    /** True when the Ladder algorithm is selected — LP4 only, whatever `mode` says. */
    ladder?: boolean;
    /** Hz, as the knob holds it. */
    cutoff?: number;
    /** 0–1, as the knob holds it. */
    resonance?: number;
    /** Used only to format the cutoff readout the same way its knob does. */
    cutoffParam?: CCParam;
  } = $props();

  // ── Drawing constants ──────────────────────────────────────────────────
  const X0 = 8; // x at FMIN
  const W = 200; // width of the plot area
  const YT = 4; // y at DB_TOP
  const H = 52; // height of the plot area
  const YB = YT + H; // y at DB_BOT
  // Two label rows below the plot rather than one. Sharing a row is what
  // EnvelopeGraph does, and it can only do it because its axis is unlabelled
  // at the right-hand end — here the decade labels run the full width, and the
  // engine badge and the cutoff readout landed on top of "100" and "10k".
  const Y_AXIS = 64; // decade labels
  const Y_FOOT = 75; // engine badge, ladder note, cutoff readout

  const FMIN = 20;
  const FMAX = 20000;
  const LOG_SPAN = Math.log10(FMAX / FMIN);

  // Deep enough to show a 4-pole rolloff reaching silence, tall enough for a
  // resonant peak to have somewhere to go. Peaks past the ceiling are clamped
  // rather than rescaled — a self-oscillating filter would otherwise flatten
  // the whole curve every time the resonance knob passed 0.9.
  const DB_TOP = 18;
  const DB_BOT = -48;
  const DB_SPAN = DB_TOP - DB_BOT;

  /** Horizontal decade gridlines, the audible ones labelled. */
  const FTICKS = [
    { f: 100, label: "100" },
    { f: 1000, label: "1k" },
    { f: 10000, label: "10k" },
  ];
  /** Faint horizontal rules; 0 dB is drawn separately and brighter. */
  const DBTICKS = [12, -12, -24, -36];

  const xOf = (f: number) => X0 + (Math.log10(f / FMIN) / LOG_SPAN) * W;
  const yOf = (db: number) =>
    YT + ((DB_TOP - Math.min(Math.max(db, DB_BOT), DB_TOP)) / DB_SPAN) * H;

  const clamp = (v: number, lo: number, hi: number) =>
    v < lo ? lo : v > hi ? hi : v;

  // Mirrors the clamps at the top of each engine's setParams().
  const fc = $derived(
    ladder ? clamp(cutoff, 20, 8000) : clamp(cutoff, 20, 16000),
  );
  const res = $derived(ladder ? clamp(resonance, 0, 1) : clamp(resonance, 0, 0.999));

  /** Linear magnitude at `f`, for the engine and mode currently selected. */
  function magnitude(f: number): number {
    const w = f / fc;
    const w2 = w * w;

    if (ladder) {
      // (1+jw)⁴ + k, expanded.
      const k = 4 * res;
      const re = (1 - w2) * (1 - w2) - 4 * w2 + k;
      const im = 4 * w * (1 - w2);
      // Guarded: at k=4, w=1 both terms vanish — that is self-oscillation, and
      // the plot should hit the ceiling rather than divide by zero.
      return 1 / Math.max(Math.hypot(re, im), 1e-6);
    }

    const k = 2 * (1 - res);
    const d = Math.max(Math.hypot(1 - w2, k * w), 1e-6);
    switch (mode) {
      case "lp":
        return 1 / d;
      case "hp":
        return w2 / d;
      case "bp":
        return w / d;
      case "notch":
        return Math.abs(1 - w2) / d;
      default:
        return 1;
    }
  }

  const bypassed = $derived(!ladder && mode === "off");

  /** The curve, sampled evenly in x — i.e. logarithmically in frequency. */
  const N = 160;
  const points = $derived.by(() => {
    if (bypassed) {
      return [
        [X0, yOf(0)],
        [X0 + W, yOf(0)],
      ] as [number, number][];
    }
    const out: [number, number][] = [];
    for (let i = 0; i <= N; i++) {
      const x = X0 + (i / N) * W;
      const f = FMIN * Math.pow(10, (i / N) * LOG_SPAN);
      const db = 20 * Math.log10(Math.max(magnitude(f), 1e-6));
      out.push([x, yOf(db)]);
    }
    return out;
  });

  const curveD = $derived(
    points
      .map(([x, y], i) => `${i ? "L" : "M"} ${x.toFixed(2)},${y.toFixed(2)}`)
      .join(" "),
  );
  const fillD = $derived(`${curveD} L ${X0 + W},${YB} L ${X0},${YB} Z`);

  const cutoffX = $derived(xOf(fc));
  /** Where the curve actually crosses the cutoff, for the marker dot. */
  const cutoffY = $derived(
    yOf(bypassed ? 0 : 20 * Math.log10(Math.max(magnitude(fc), 1e-6))),
  );

  const badge = $derived(
    bypassed ? "BYPASS" : ladder ? "LADDER · LP4" : `SVF · ${mode.toUpperCase()}`,
  );
  // The ladder honours only LP4; anything else set on the mode switch is
  // silently ignored by the DSP, so say so rather than letting the switch and
  // the curve disagree in silence.
  const overridden = $derived(ladder && mode !== "lp" && mode !== "off");

  const cutoffLabel = $derived(
    cutoffParam
      ? formatValue(cutoffParam, fc)
      : fc >= 1000
        ? `${(fc / 1000).toFixed(2)} kHz`
        : `${Math.round(fc)} Hz`,
  );
</script>

<div class="filter-response" class:bypassed>
  <!--
    viewBox 0 0 216 80 — plot in x:8–208, y:4–56, then the two label rows.

    Sized by width alone, with the height left to the viewBox's aspect. Pinning
    a height instead makes the SVG letterbox itself inside whatever width it is
    given, which is where the dead black margins either side of the drawing
    came from — the box was 445px and the plot inside it 240.
  -->
  <svg viewBox="0 0 216 80" aria-hidden="true">
    <!-- Decade gridlines -->
    {#each FTICKS as tick (tick.f)}
      <line
        x1={xOf(tick.f)}
        y1={YT - 3}
        x2={xOf(tick.f)}
        y2={YB}
        stroke="var(--grid)"
        stroke-width="1"
      />
      <text x={xOf(tick.f)} y={Y_AXIS} class="tick-label">{tick.label}</text>
    {/each}

    <!-- Level rules, and 0 dB brighter as the reference the curve is read against -->
    {#each DBTICKS as db (db)}
      <line
        x1={X0}
        y1={yOf(db)}
        x2={X0 + W}
        y2={yOf(db)}
        stroke="var(--grid)"
        stroke-width="1"
      />
    {/each}
    <line
      x1={X0 - 2}
      y1={yOf(0)}
      x2={X0 + W + 2}
      y2={yOf(0)}
      stroke="var(--axis)"
      stroke-width="1"
    />
    <text x={X0 + W + 3} y={yOf(0) + 2.5} class="db-label">0</text>

    <!-- Cutoff marker -->
    {#if !bypassed}
      <line
        x1={cutoffX}
        y1={YT - 3}
        x2={cutoffX}
        y2={YB + 3}
        stroke="var(--cutoff)"
        stroke-width="1"
        stroke-dasharray="3 2"
      />
    {/if}

    <path d={fillD} fill="var(--curve-fill)" />
    <path
      d={curveD}
      fill="none"
      stroke="var(--curve)"
      stroke-width="1.8"
      stroke-linecap="round"
      stroke-linejoin="round"
    />

    {#if !bypassed}
      <circle cx={cutoffX} cy={cutoffY} r="2.2" class="cutoff-dot" />
    {/if}

    <!-- Engine badge (left), ladder note (middle), cutoff readout (right) -->
    <text x={X0} y={Y_FOOT} class="mode-label">{badge}</text>
    {#if overridden}
      <text x={X0 + 62} y={Y_FOOT} class="note-label">mode ignored</text>
    {/if}
    {#if !bypassed}
      <text x={X0 + W} y={Y_FOOT} class="total-label">{cutoffLabel}</text>
    {/if}
  </svg>
</div>

<style>
  /* Same palette split as EnvelopeGraph: only the response curve is allowed to
     be bright, everything else is scaffolding at or below gridline level. */
  .filter-response {
    --grid: rgba(192, 137, 74, 0.1);
    --axis: var(--hairline-strong);
    --cutoff: var(--hairline-strong);
    --curve: var(--copper-bright);
    --curve-fill: rgba(224, 176, 114, 0.09);

    background: var(--bg-sunken);
    border: 1px solid var(--hairline);
    border-radius: 6px;
    padding: 0.3rem 0.5rem 0;
  }

  /* Filter off: the flat line is still the truth, it just has nothing to say. */
  .filter-response.bypassed {
    --curve: var(--text-faint);
    --curve-fill: transparent;
  }

  /* Fills the width it is given and takes its height from the viewBox, so the
     bordered box is exactly the drawing and never a frame around it. */
  svg {
    display: block;
    width: 100%;
    height: auto;
    overflow: visible;
  }

  .cutoff-dot {
    fill: var(--copper-bright);
    stroke: var(--bg-sunken);
    stroke-width: 1;
  }

  .mode-label {
    fill: var(--copper-deep);
    font-size: 7px;
    font-family: monospace;
    letter-spacing: 0.08em;
    text-transform: uppercase;
  }

  .note-label {
    fill: var(--led-5);
    font-size: 6px;
    font-family: monospace;
    letter-spacing: 0.04em;
    text-transform: uppercase;
  }

  .tick-label {
    fill: var(--text-faint);
    font-size: 6px;
    text-anchor: middle;
    font-family: monospace;
  }

  .db-label {
    fill: var(--text-faint);
    font-size: 6px;
    font-family: monospace;
  }

  .total-label {
    fill: var(--text-dim);
    font-size: 7px;
    text-anchor: end;
    font-family: monospace;
  }
</style>
