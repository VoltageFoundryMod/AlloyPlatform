<script lang="ts" module>
  /**
   * Arc geometry, shared by every knob on the page.
   *
   * A 270-degree sweep starting at 7:30 and ending at 4:30 — the same travel
   * the physical knob has, so the panel and this agree about where "half way"
   * points.
   */
  const START_DEG = -135;
  const SWEEP_DEG = 270;
  const CX = 50;
  const CY = 50;

  /** Point on the sweep at control position `pos` (0-1), radius `r`. */
  function polar(pos: number, r: number): [number, number] {
    const a = ((START_DEG + pos * SWEEP_DEG) * Math.PI) / 180;
    return [CX + r * Math.sin(a), CY - r * Math.cos(a)];
  }

  /**
   * SVG arc path between two control positions. Handles either direction, so a
   * bipolar knob can sweep left of centre as well as right.
   */
  function arcPath(fromPos: number, toPos: number, r: number): string {
    const [x0, y0] = polar(fromPos, r);
    const [x1, y1] = polar(toPos, r);
    const large = Math.abs(toPos - fromPos) * SWEEP_DEG > 180 ? 1 : 0;
    const sweep = toPos >= fromPos ? 1 : 0;
    return (
      `M ${x0.toFixed(3)} ${y0.toFixed(3)} ` +
      `A ${r} ${r} 0 ${large} ${sweep} ${x1.toFixed(3)} ${y1.toFixed(3)}`
    );
  }

  const clamp01 = (v: number) => (v < 0 ? 0 : v > 1 ? 1 : v);

  /** Silkscreen ticks: eleven around the sweep, longer at the stops and centre. */
  const TICKS = Array.from({ length: 11 }, (_, i) => {
    const t = i / 10;
    const [x1, y1] = polar(t, 41);
    const [x2, y2] = polar(t, i === 0 || i === 10 || i === 5 ? 47 : 45);
    return { x1, y1, x2, y2 };
  });

  /** Pixels of vertical drag for the full sweep. Roughly a hand of travel. */
  const TRAVEL_PX = 190;
  /** Shift multiplier for fine adjustment. */
  const FINE = 0.2;
  /** Sweep fraction per wheel notch / arrow key. */
  const NUDGE = 0.02;
</script>

<script lang="ts">
  /**
   * Knob — a rotary control bound to one MIDI CC parameter.
   *
   * Works in normalized *travel* rather than the parameter's native range —
   * that is the quantity a hardware knob and a CC both carry, and routing all
   * three through ParamDescriptor's curve is the only reason they land on the
   * same value.
   *
   * Exposes `applyCC` so the patch-sync paths in App.svelte can push an
   * inbound value in without going through the pointer handlers.
   */
  import type { CCParam } from "../../lib/paramMap";
  import { ccToFloat, floatToCC } from "../../lib/paramMap";
  import { formatValue, posToValue, valueToPos } from "../../lib/paramMapTypes";
  import { midi } from "../../lib/midi";
  import Glyph, { type GlyphName } from "./Glyph.svelte";

  let {
    param,
    value = $bindable(param.default),
    hint = undefined,
    displayOverride = undefined,
    sub = undefined,
    label = undefined,
    size = "md",
    icons = undefined,
    disabled = false,
  }: {
    param: CCParam;
    value?: number;
    /** Mode-contextual note under the readout (e.g. "FM depth"). */
    hint?: string;
    /** Replaces the computed readout entirely. */
    displayOverride?: string;
    /** Panel-style parenthetical sublabel, e.g. "V/OCT". */
    sub?: string;
    /** Overrides param.label, for when the panel legend is shorter. */
    label?: string;
    size?: "sm" | "md" | "lg";
    /**
     * Glyphs spread across the knob's travel, lighting the one nearest the
     * current position — the silkscreen legend for a control whose positions
     * mean shapes rather than numbers.
     */
    icons?: GlyphName[];
    /**
     * Greyed and inert: the parameter exists but the current mode ignores it.
     * Left in place rather than hidden so the panel does not reflow every time
     * a mode switch is thrown.
     */
    disabled?: boolean;
  } = $props();

  // A curved parameter (log scale or skew) is continuous in travel; only a
  // plain linear one carries a meaningful step in its native range.
  const isCurved = $derived(param.scale === "log" || (param.skew ?? 1) !== 1);

  const pos = $derived(clamp01(valueToPos(param, value)));

  // Bipolar parameters sweep out from the centre detent rather than from the
  // left stop, so "no offset" reads as an unlit knob at 12 o'clock.
  const bipolar = $derived(param.min < 0 && param.max > 0);
  const originPos = $derived(bipolar ? valueToPos(param, 0) : 0);

  // Precision and the value→readout transform both live in paramMapTypes, so
  // the knob, the slider and the VCV tooltip say the same thing about the same
  // parameter — the readout was the last place the three still disagreed.
  const readout = $derived(displayOverride ?? formatValue(param, value));

  const legend = $derived(label ?? param.label);

  const pointer = $derived.by(() => {
    const [x1, y1] = polar(pos, 9);
    const [x2, y2] = polar(pos, 23);
    return { x1, y1, x2, y2 };
  });

  // Last 7-bit value put on the wire. A drag emits far more events than there
  // are CC steps, and sending the duplicates floods the module's inbound queue
  // for no effect.
  let lastSentCC: number | null = null;

  let dragging = $state(false);
  // Travel is accumulated across the gesture rather than re-derived from
  // `value` each frame: a stepped parameter quantises its value, and reading
  // the position back out of it would swallow every sub-step movement.
  let dragPos = 0;
  let lastY = 0;

  // Which glyph the knob is currently nearest, when it carries a set.
  const activeIcon = $derived(
    icons && icons.length > 1 ? Math.round(pos * (icons.length - 1)) : 0,
  );

  function commit(nextPos: number) {
    // One gate for every input path — pointer, wheel, keyboard, double-click.
    // A disabled control must not put CC on the wire; the mode that disabled
    // it is ignoring the parameter, and sending anyway would write a value the
    // user cannot see the effect of.
    if (disabled) return;
    const p = clamp01(nextPos);
    let native = posToValue(param, p);
    if (!isCurved) {
      const st = param.step ?? 0.001;
      native = Math.round(native / st) * st;
      native = Math.min(param.max, Math.max(param.min, native));
    }
    value = native;
    const cc = floatToCC(param, native);
    if (cc === lastSentCC) return;
    lastSentCC = cc;
    midi.sendCC(param.cc, cc);
  }

  function onPointerDown(e: PointerEvent) {
    if (e.button !== 0 || disabled) return;
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
    dragging = true;
    dragPos = pos;
    lastY = e.clientY;
    e.preventDefault();
  }

  function onPointerMove(e: PointerEvent) {
    if (!dragging) return;
    // Rebased every frame, so taking or releasing Shift mid-drag changes the
    // gain from that point on rather than rescaling travel already covered.
    const dy = lastY - e.clientY;
    lastY = e.clientY;
    dragPos = clamp01(dragPos + (dy / TRAVEL_PX) * (e.shiftKey ? FINE : 1));
    commit(dragPos);
  }

  function endDrag(e: PointerEvent) {
    if (!dragging) return;
    dragging = false;
    (e.currentTarget as HTMLElement).releasePointerCapture(e.pointerId);
  }

  function onWheel(e: WheelEvent) {
    if (disabled) return;
    e.preventDefault();
    const dir = e.deltaY < 0 ? 1 : -1;
    commit(pos + dir * NUDGE * (e.shiftKey ? FINE : 1));
  }

  function onDblClick() {
    commit(valueToPos(param, param.default));
  }

  function onKeyDown(e: KeyboardEvent) {
    const step = e.shiftKey ? NUDGE * FINE : NUDGE;
    switch (e.key) {
      case "ArrowUp":
      case "ArrowRight":
        commit(pos + step);
        break;
      case "ArrowDown":
      case "ArrowLeft":
        commit(pos - step);
        break;
      case "Home":
        commit(0);
        break;
      case "End":
        commit(1);
        break;
      case "PageUp":
        commit(pos + NUDGE * 5);
        break;
      case "PageDown":
        commit(pos - NUDGE * 5);
        break;
      default:
        return;
    }
    e.preventDefault();
  }

  /** Push an inbound CC from the module into this control. */
  export function applyCC(ccVal: number) {
    value = ccToFloat(param, ccVal);
    // Keep the dedupe in step, or the next local drag back through this value
    // would be suppressed as a duplicate.
    lastSentCC = ccVal;
  }
</script>

<div
  class="knob"
  class:sm={size === "sm"}
  class:lg={size === "lg"}
  class:disabled
>
  <!-- Legend above the control, as on the panel, and reserving a fixed height
       so a two-line legend does not push its dial out of line with the rest of
       the row. -->
  <div class="head">
    <div class="legend panel-legend">{legend}</div>
    {#if sub}
      <div class="sub">({sub})</div>
    {/if}
  </div>

  <!-- Fixed-height slot sized to the section's largest knob, with the dial
       centred in it, so mixed sizes share a centre line and a readout
       baseline. -->
  <div class="dial-box">
    <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
    <div
      class="dial"
      class:dragging
      role="slider"
      tabindex={disabled ? -1 : 0}
      aria-disabled={disabled}
      aria-label={legend}
      aria-valuemin={param.min}
      aria-valuemax={param.max}
      aria-valuenow={value}
      aria-valuetext={readout}
      title={disabled
        ? `${legend} — not used in this mode`
        : `${legend} — CC ${param.cc} — drag to set, Shift for fine, double-click to reset`}
      onpointerdown={onPointerDown}
      onpointermove={onPointerMove}
      onpointerup={endDrag}
      onpointercancel={endDrag}
      onwheel={onWheel}
      ondblclick={onDblClick}
      onkeydown={onKeyDown}
    >
      <svg viewBox="0 0 100 100" aria-hidden="true">
        {#each TICKS as t}
          <line x1={t.x1} y1={t.y1} x2={t.x2} y2={t.y2} class="tick" />
        {/each}

        <!-- Unlit travel -->
        <path d={arcPath(0, 1, 34)} class="arc-track" />
        <!-- Lit travel, from the left stop or from the centre detent -->
        <path d={arcPath(originPos, pos, 34)} class="arc-value" />

        <!-- Cap: barely there, just enough to read as a grabbable object -->
        <circle cx={CX} cy={CY} r="26" class="cap" />
        <circle cx={CX} cy={CY} r="26" class="cap-edge" />

        <line
          x1={pointer.x1}
          y1={pointer.y1}
          x2={pointer.x2}
          y2={pointer.y2}
          class="pointer"
        />
      </svg>
    </div>
  </div>

  <!-- Always rendered; its height is 0 unless the section reserves the row.

       The sizes are set by the worst case in the layout: SHAPE puts five
       glyphs under a 118px `lg` knob, and the strip must not grow wider than
       the dial it labels. Five at 20px plus four 3px gaps is 112 of those 118,
       which is as large as they go without the row overhanging — the reason
       the gap below is tighter than the type around it. 20px also lands them
       on the same size as the glyphs inside a segmented switch. -->
  <div class="icons">
    {#each icons ?? [] as icon, i}
      <span class="icon" class:on={i === activeIcon}>
        <Glyph name={icon} size={size === "sm" ? 17 : 20} />
      </span>
    {/each}
  </div>

  <div class="readout panel-readout">{readout}</div>
  {#if hint}
    <div class="hint">{hint}</div>
  {/if}
</div>

<style>
  /* Three sizes, spread far enough apart to be read as a hierarchy rather than
     as three slightly different knobs. `lg` is nearly double `sm`, which is
     what lets a section say which control you reach for first. */
  .knob {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 1px;
    width: var(--knob-w, 88px);
  }
  .knob.sm {
    --knob-w: 66px;
  }
  .knob.lg {
    --knob-w: 118px;
  }

  /* Height is set once per section (--dial-box) to its largest knob, so a
     small knob sits centred in the same slot a large one fills. */
  .dial-box {
    display: flex;
    align-items: center;
    justify-content: center;
    height: var(--dial-box, 88px);
    width: 100%;
  }

  .dial {
    width: 100%;
    aspect-ratio: 1;
    cursor: ns-resize;
    touch-action: none;
    border-radius: 50%;
    outline: none;
  }
  .dial:focus-visible {
    box-shadow: 0 0 0 2px var(--copper-glow);
  }
  .dial svg {
    display: block;
    width: 100%;
    height: 100%;
    overflow: visible;
  }

  .tick {
    stroke: var(--copper-deep);
    stroke-width: 1.4;
    opacity: 0.5;
    stroke-linecap: round;
  }

  .arc-track {
    fill: none;
    stroke: var(--copper-dim);
    stroke-width: 3;
    stroke-linecap: round;
  }
  .arc-value {
    fill: none;
    stroke: var(--copper-bright);
    stroke-width: 3;
    stroke-linecap: round;
  }
  .dragging .arc-value {
    filter: drop-shadow(0 0 3px var(--copper-glow));
  }

  .cap {
    fill: var(--bg-sunken);
  }
  .cap-edge {
    fill: none;
    stroke: var(--hairline-strong);
    stroke-width: 1;
  }
  .dial:hover .cap-edge {
    stroke: var(--copper-deep);
  }

  .pointer {
    stroke: var(--pointer);
    stroke-width: 3;
    stroke-linecap: round;
  }

  /* Reserved height, bottom-aligned: legends are one or two lines and may or
     may not carry a sublabel, and without this the dials in a row sat at four
     different heights. */
  .head {
    display: flex;
    flex-direction: column;
    justify-content: flex-end;
    min-height: 33px;
    margin-bottom: 3px;
  }
  .legend {
    font-size: 0.78rem;
    text-align: center;
    line-height: 1.15;
  }
  .sub {
    font-size: 0.65rem;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-faint);
    text-align: center;
    line-height: 1.1;
  }
  .readout {
    margin-top: 2px;
  }
  .hint {
    font-size: 0.66rem;
    letter-spacing: 0.04em;
    color: var(--led-4);
    text-align: center;
    line-height: 1.1;
  }

  /* Glyph strip: the shapes the travel passes through, with the nearest lit.
     Reads as silkscreen rather than as a control — it is not clickable. */
  .icons {
    display: flex;
    align-items: center;
    justify-content: center;
    /* Tight, and paying for the glyph size above — see the note on the strip.
       The glyphs are drawn with slack inside their own 24x16 box, so they read
       as separated at 3px even though the boxes nearly touch. */
    gap: 3px;
    height: var(--icon-row, 0px);
  }
  .icon {
    color: var(--copper-deep);
    opacity: 0.5;
    transition:
      color 120ms,
      opacity 120ms;
  }
  .icon.on {
    color: var(--copper-bright);
    opacity: 1;
  }

  /* Disabled: the parameter is real but this mode ignores it. Dimmed rather
     than removed, so throwing a mode switch does not reflow the panel. */
  .knob.disabled {
    opacity: 0.32;
    filter: grayscale(0.6);
  }
  .knob.disabled .dial {
    cursor: not-allowed;
  }
  .knob.disabled .dial:hover .cap-edge {
    stroke: var(--hairline-strong);
  }
</style>
