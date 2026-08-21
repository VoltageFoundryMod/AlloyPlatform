<script lang="ts">
  /**
   * EnvelopeGraph — SVG visualization of the current envelope shape.
   *
   * The horizontal axis is ABSOLUTE TIME on a log scale (≈5 ms … 32 s), so a
   * uniform time scale actually moves the drawing.  The previous version sized
   * every segment as a fraction of a fixed total width, which made any uniform
   * scaling (CURVETIME in AR, the CURVE tScale in ADSR) cancel out exactly and
   * so have no visible effect at all.
   *
   * Segment times mirror the DSP in common/include/dsp/CurveEngine.h:
   *   AR   — att = (0.001 + curve² · 0.799) · curveTime
   *          rel = (0.080 + curve² · 1.920) · curveTime
   *          sustain level morphs 0 → 1 across curve 0.20 → 0.40, so AR draws
   *          as a four-segment envelope whose decay and release are derived
   *          from the single release rate.  At sustain 0 that is pluck.
   *   ADSR — A/D/R scaled by CURVE: tScale = 4^(2·curve−1) (×0.25 … ×4), each
   *          clamped to 0.001–10 s like ADSREnvelope::_coeff().
   *
   * Segments are drawn at their nominal parameter values (the one-pole time
   * constants), not the ~4.6·τ a real ramp takes to reach peak — so the picture
   * matches the numbers on the knobs.
   *
   * The dashed vertical line marks gate-off, assuming a GATE_HOLD_S gate.
   */
  let {
    isAdsr = false,
    attack = 0.05,
    decay = 0.1,
    sustain = 0.8,
    release = 0.3,
    curve = 0.5,
    curveTime = 1.0,
  }: {
    isAdsr?: boolean;
    attack?: number;
    decay?: number;
    sustain?: number;
    release?: number;
    curve?: number;
    curveTime?: number;
  } = $props();

  // ── Drawing constants ──────────────────────────────────────────────────
  const W = 200; // usable envelope width (px in viewBox units)
  const H = 44; // amplitude span (px)
  const X0 = 8; // x of envelope start
  const YT = 4; // y at amplitude 1.0  (top)
  const YB = 48; // y at amplitude 0.0  (bottom)  YT + H = 48

  // ── Time axis ─────────────────────────────────────────────────────────
  // x(t) = X0 + W · log10(1 + t/T0) / log10(1 + TMAX/T0)
  // T0 sets the resolution near zero; TMAX is the right edge.  TMAX covers the
  // slowest reachable envelope (ADSR 10+10+10 s + gate hold).
  const T0 = 0.005;
  const TMAX = 32;
  const LOG_DEN = Math.log10(1 + TMAX / T0);

  // Assumed gate-on duration, drawn between the end of attack (AR) or decay
  // (ADSR) and gate-off.  The envelope itself has no opinion on gate length.
  const GATE_HOLD_S = 0.25;

  const MIN_LABEL_W = 11; // hide a segment label narrower than this

  function xOf(t: number): number {
    const u = Math.log10(1 + Math.max(t, 0) / T0) / LOG_DEN;
    return X0 + Math.min(u, 1) * W;
  }

  function fmtTime(t: number): string {
    if (t < 1) return `${Math.round(t * 1000)} ms`;
    return t < 10 ? `${t.toFixed(2)} s` : `${t.toFixed(1)} s`;
  }

  // Decade gridlines.  10 s is drawn but left unlabelled so it cannot collide
  // with the total-duration readout on the right.
  const ticks = [
    { t: 0.01, label: "10ms" },
    { t: 0.1, label: "100ms" },
    { t: 1, label: "1s" },
    { t: 10, label: "" },
  ];

  // ── Geometry helpers ──────────────────────────────────────────────────
  // Cubic bezier control points sit at ±25 % of each segment's width, which
  // reads as a natural exponential curve.

  function arGeom(c: number, ct: number) {
    const ts = Math.max(ct, 0.01); // AREnvelope::setCurve clamps the same way
    const c2 = c * c;
    const a = (0.001 + c2 * 0.799) * ts;
    const r = (0.08 + c2 * 1.92) * ts;

    // Sustain morph mirrors AREnvelope::setCurve(): 0 at curve 0.20, 1 at 0.40.
    const s = Math.min(Math.max((c - 0.2) / 0.2, 0), 1);

    // Decay (1.0 → s) and release (s → 0) both run at the single release rate,
    // so the nominal time r is split between them in proportion to the height
    // each one covers.  Linear in s, so no landmark jumps anywhere in the
    // morph, and the total stays exactly a + r at both ends and in between.
    // (Splitting by a one-pole's true settling time instead looks principled
    // but collapses almost entirely in the last 2 % of the morph, which just
    // moves the old cliff from curve 0.20 to curve 0.40.)
    //
    // AR is now structurally an ADSR whose decay and release are derived from
    // the single release rate, so it shares the same geometry.
    return { ...envGeom(a, r * (1 - s), s, r * s), pluck: s <= 0 };
  }

  function envGeom(a: number, d: number, s: number, r: number) {
    const tA = a;
    const tD = tA + d;
    const tS = tD + GATE_HOLD_S;
    const tR = tS + r;

    const xA = xOf(tA);
    const xD = xOf(tD);
    const xSe = xOf(tS);
    const xR = xOf(tR);
    const aw = xA - X0;
    const dw = xD - xA;
    const sw = xSe - xD;
    const rw = xR - xSe;
    const ys = YB - s * H; // sustain level y

    const pathD = [
      `M ${X0},${YB}`,
      `C ${X0 + aw * 0.25},${YB} ${xA - aw * 0.25},${YT} ${xA},${YT}`,
      `C ${xA + dw * 0.25},${YT} ${xD - dw * 0.25},${ys} ${xD},${ys}`,
      `L ${xSe},${ys}`,
      `C ${xSe + rw * 0.25},${ys} ${xR - rw * 0.25},${YB} ${xR},${YB}`,
    ].join(" ");

    return { pathD, xA, xD, xSe, xR, ys, aw, dw, sw, rw, total: a + d + r };
  }

  // ADSREnvelope::_coeff() clamps every time to 0.001–10 s; mirror that so the
  // graph stops growing exactly where the DSP does.
  const clampT = (t: number) => Math.min(Math.max(t, 0.001), 10);

  const geom = $derived.by(() => {
    if (isAdsr) {
      // CURVE is a global time scale in ADSR mode: 4^(2·curve−1)
      const tScale = Math.pow(4, 2 * curve - 1);
      return {
        labels: ["A", "D", "S", "R"],
        pluck: false,
        ...envGeom(
          clampT(attack * tScale),
          clampT(decay * tScale),
          sustain,
          clampT(release * tScale),
        ),
      };
    }
    return {
      labels: ["ATK", "DEC", "HOLD", "REL"],
      ...arGeom(curve, curveTime),
    };
  });
</script>

<div class="env-graph">
  <!--
    viewBox 0 0 216 76 — envelope drawn in x:8–208, y:4–48;
    segment labels at y:58, time axis at y:70.
  -->
  <svg viewBox="0 0 216 76" aria-hidden="true">
    <!-- Decade gridlines + labels -->
    {#each ticks as tick (tick.t)}
      <line
        x1={xOf(tick.t)}
        y1={YT - 3}
        x2={xOf(tick.t)}
        y2={YB}
        stroke="var(--grid)"
        stroke-width="1"
      />
      {#if tick.label}
        <text x={xOf(tick.t)} y="70" class="tick-label">{tick.label}</text>
      {/if}
    {/each}

    <!-- Zero baseline -->
    <line
      x1={X0 - 2}
      y1={YB}
      x2={X0 + W + 2}
      y2={YB}
      stroke="var(--axis)"
      stroke-width="1"
    />

    <!-- Sustain level reference -->
    <line
      x1={X0}
      y1={geom.ys}
      x2={X0 + W}
      y2={geom.ys}
      stroke="var(--sustain-ref)"
      stroke-width="1"
    />

    <!--
      Gate-off boundary (dashed).  In AR pluck mode the envelope has already
      decayed to silence before this line, which is exactly what "gate hold
      ignored" looks like.
    -->
    <line
      x1={geom.xSe}
      y1={YT - 3}
      x2={geom.xSe}
      y2={YB + 3}
      stroke={geom.pluck ? "var(--gate-dim)" : "var(--gate)"}
      stroke-width="1"
      stroke-dasharray="3 2"
    />

    <!-- Fill under curve -->
    <path d="{geom.pathD} Z" fill="var(--curve-fill)" />

    <!-- Envelope curve -->
    <path
      d={geom.pathD}
      fill="none"
      stroke="var(--curve)"
      stroke-width="1.8"
      stroke-linecap="round"
      stroke-linejoin="round"
    />

    <!-- Segment labels centred under each segment, hidden when too narrow -->
    {#if geom.aw >= MIN_LABEL_W}
      <text x={X0 + geom.aw * 0.5} y="58" class="seg-label"
        >{geom.labels[0]}</text
      >
    {/if}
    {#if geom.dw >= MIN_LABEL_W}
      <text x={geom.xA + geom.dw * 0.5} y="58" class="seg-label"
        >{geom.labels[1]}</text
      >
    {/if}
    {#if geom.sw >= MIN_LABEL_W}
      <text x={geom.xD + geom.sw * 0.5} y="58" class="seg-label"
        >{geom.labels[2]}</text
      >
    {/if}
    {#if geom.rw >= MIN_LABEL_W}
      <text x={geom.xSe + geom.rw * 0.5} y="58" class="seg-label"
        >{geom.labels[3]}</text
      >
    {/if}

    <!-- Mode badge (left) and total envelope time (right) -->
    <text x={X0} y="70" class="mode-label">
      {isAdsr ? "ADSR" : geom.pluck ? "AR PLUCK" : "AR"}
    </text>
    <text x={X0 + W} y="70" class="total-label">{fmtTime(geom.total)}</text>
  </svg>
</div>

<style>
  /* The drawing's own colours, declared here and picked up by the `stroke` /
     `fill` presentation attributes on the shapes inside — custom properties
     inherit into SVG the same way they do into HTML. Only the envelope curve
     is allowed to be bright; everything else is scaffolding and stays at or
     below the gridline level. */
  .env-graph {
    --grid: rgba(192, 137, 74, 0.1);
    --axis: var(--hairline);
    --sustain-ref: rgba(154, 164, 179, 0.12);
    --gate: var(--hairline-strong);
    --gate-dim: var(--hairline);
    --curve: var(--copper-bright);
    --curve-fill: rgba(224, 176, 114, 0.09);

    background: var(--bg-sunken);
    border: 1px solid var(--hairline);
    border-radius: 6px;
    padding: 0.3rem 0.5rem 0;
  }

  /* Fills the width it is given and takes its height from the viewBox. A fixed
     height instead makes the SVG letterbox itself inside a percentage width —
     which is what left the drawing sitting in the middle of a box half again
     as wide as itself, with dead black either side. */
  svg {
    display: block;
    width: 100%;
    height: auto;
    overflow: visible;
  }

  .seg-label {
    fill: var(--text-faint);
    font-size: 7.5px;
    text-anchor: middle;
    font-family: monospace;
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }

  .mode-label {
    fill: var(--copper-deep);
    font-size: 7px;
    font-family: monospace;
    letter-spacing: 0.08em;
    text-transform: uppercase;
  }

  .tick-label {
    fill: var(--text-faint);
    font-size: 6px;
    text-anchor: middle;
    font-family: monospace;
  }

  .total-label {
    fill: var(--text-dim);
    font-size: 7px;
    text-anchor: end;
    font-family: monospace;
  }
</style>
