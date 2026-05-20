<script lang="ts">
  /**
   * EnvelopeGraph — SVG visualization of the current envelope shape.
   *
   * AR mode:   shape derived from `curve` (0–1) and `curveTime` (0.25–4×).
   *            curve=0 → pluck (instant attack, fast decay)
   *            curve=1 → swell (slow attack, long decay)
   * ADSR mode: independent attack / decay / sustain / release.
   *
   * The dashed vertical line marks the gate-off boundary.
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

  // ── Geometry helpers ──────────────────────────────────────────────────

  function adsrGeom(a: number, d: number, s: number, r: number) {
    const susW = 42; // fixed-width sustain plateau in display units
    const total = Math.max(a + d + r, 1e-6);
    const avail = W - susW;

    // Proportional widths, clamped to a usable minimum
    let aw = Math.max((a / total) * avail, 10);
    let dw = Math.max((d / total) * avail, 10);
    let rw = Math.max((r / total) * avail, 10);

    // Rescale so aw + dw + susW + rw = W exactly
    const sc = W / (aw + dw + susW + rw);
    aw *= sc;
    dw *= sc;
    rw *= sc;
    const sw = susW * sc;

    const xA = X0 + aw;
    const xD = xA + dw;
    const xSe = xD + sw;
    const xR = xSe + rw;
    const ys = YB - s * H; // sustain level y

    // Cubic bezier: cp at ±25 % of segment width for a natural exponential look
    const pathD = [
      `M ${X0},${YB}`,
      `C ${X0 + aw * 0.25},${YB} ${xA - aw * 0.25},${YT} ${xA},${YT}`,
      `C ${xA + dw * 0.25},${YT} ${xD - dw * 0.25},${ys} ${xD},${ys}`,
      `L ${xSe},${ys}`,
      `C ${xSe + rw * 0.25},${ys} ${xR - rw * 0.25},${YB} ${xR},${YB}`,
    ].join(" ");

    return { pathD, xA, xD, xSe, xR, ys, aw, dw, sw, rw };
  }

  function arGeom(c: number, ct: number) {
    // Approximate attack and release times from the curve knob
    //   c=0 → pluck : a≈0.005 s, r≈0.05 s  (narrow spike)
    //   c=1 → swell  : a≈2.5 s,  r≈2.0 s   (wide gradual rise)
    const a = (0.005 + Math.pow(c, 1.5) * 2.5) * ct;
    const r = (0.05 + c * 1.95) * ct;

    const holdW = 28; // gate-held visual segment width
    const total = Math.max(a + r, 1e-6);
    const avail = W - holdW;
    const aw = Math.max((a / total) * avail, 10);
    const rw = avail - aw; // fills the rest exactly → xR always equals X0 + W

    const xA = X0 + aw;
    const xHe = xA + holdW;
    const xR = xHe + rw;

    const pathD = [
      `M ${X0},${YB}`,
      `C ${X0 + aw * 0.25},${YB} ${xA - aw * 0.25},${YT} ${xA},${YT}`,
      `L ${xHe},${YT}`,
      `C ${xHe + rw * 0.25},${YT} ${xR - rw * 0.25},${YB} ${xR},${YB}`,
    ].join(" ");

    return { pathD, xA, xHe, xR, aw, rw, holdW };
  }

  const geom = $derived.by(() =>
    isAdsr
      ? { kind: "adsr" as const, ...adsrGeom(attack, decay, sustain, release) }
      : { kind: "ar" as const, ...arGeom(curve, curveTime) },
  );
</script>

<div class="env-graph">
  <!--
    viewBox 0 0 216 68 — envelope drawn in x:8–208, y:4–48;
    label row at y:60–66.  Width scales to container; height fixed at 64 px.
  -->
  <svg viewBox="0 0 216 68" width="100%" height="64" aria-hidden="true">
    <!-- Mode badge -->
    <text x={X0} y="62" class="mode-label">{isAdsr ? "ADSR" : "AR"}</text>

    <!-- Zero baseline -->
    <line
      x1={X0 - 2}
      y1={YB}
      x2={X0 + W + 2}
      y2={YB}
      stroke="#252535"
      stroke-width="1"
    />

    {#if geom.kind === "adsr"}
      <!-- Sustain level reference -->
      <line
        x1={X0}
        y1={geom.ys}
        x2={X0 + W}
        y2={geom.ys}
        stroke="#1e2030"
        stroke-width="1"
      />

      <!-- Gate-off boundary (dashed) -->
      <line
        x1={geom.xSe}
        y1={YT - 3}
        x2={geom.xSe}
        y2={YB + 3}
        stroke="#33344a"
        stroke-width="1"
        stroke-dasharray="3 2"
      />

      <!-- Fill under curve -->
      <path d="{geom.pathD} Z" fill="rgba(124,184,255,0.07)" />

      <!-- Envelope curve -->
      <path
        d={geom.pathD}
        fill="none"
        stroke="#7cb8ff"
        stroke-width="1.8"
        stroke-linecap="round"
        stroke-linejoin="round"
      />

      <!-- Segment labels: A D S R centred under each segment -->
      <text x={X0 + geom.aw * 0.5} y="62" class="seg-label">A</text>
      <text x={geom.xA + geom.dw * 0.5} y="62" class="seg-label">D</text>
      <text x={geom.xD + geom.sw * 0.5} y="62" class="seg-label">S</text>
      <text x={geom.xSe + geom.rw * 0.5} y="62" class="seg-label">R</text>
    {:else}
      <!-- Gate-off boundary (dashed) -->
      <line
        x1={geom.xHe}
        y1={YT - 3}
        x2={geom.xHe}
        y2={YB + 3}
        stroke="#33344a"
        stroke-width="1"
        stroke-dasharray="3 2"
      />

      <!-- Fill under curve -->
      <path d="{geom.pathD} Z" fill="rgba(124,184,255,0.07)" />

      <!-- Envelope curve -->
      <path
        d={geom.pathD}
        fill="none"
        stroke="#7cb8ff"
        stroke-width="1.8"
        stroke-linecap="round"
        stroke-linejoin="round"
      />

      <!-- Segment labels: ATK  HOLD  REL -->
      <text x={X0 + geom.aw * 0.5} y="62" class="seg-label">ATK</text>
      <text x={geom.xA + geom.holdW * 0.5} y="62" class="seg-label">HOLD</text>
      <text x={geom.xHe + geom.rw * 0.5} y="62" class="seg-label">REL</text>
    {/if}
  </svg>
</div>

<style>
  .env-graph {
    background: #0d0d1a;
    border: 1px solid #2a2a42;
    border-radius: 6px;
    padding: 0.3rem 0.5rem 0;
    width: 40%;
    align-self: center;
  }

  svg {
    display: block;
    overflow: visible;
  }

  .seg-label {
    fill: #3a3a5a;
    font-size: 7.5px;
    text-anchor: middle;
    font-family: monospace;
    letter-spacing: 0.06em;
    text-transform: uppercase;
  }

  .mode-label {
    fill: #4a4a6a;
    font-size: 7px;
    font-family: monospace;
    letter-spacing: 0.08em;
    text-transform: uppercase;
  }
</style>
