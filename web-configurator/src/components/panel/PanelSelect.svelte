<script lang="ts">
  /**
   * PanelSelect — an enum parameter, in whichever of three shapes suits it.
   *
   * The panel itself makes this distinction in hardware, and the app follows
   * it rather than rendering every enum as the same widget:
   *
   *   segmented  two to four short options, side by side like a toggle
   *   ladder     a vertical LED strip, as on the module's DRIFT..PEAK column
   *   dropdown   anything long enough that a ladder would dominate a section
   *
   * Exposes `applyCC` so App.svelte can push an inbound CC in without going
   * through the click handlers.
   */
  import type { CCParam } from "../../lib/paramMap";
  import { midi } from "../../lib/midi";

  let {
    param,
    initial = undefined,
    variant = undefined,
    label = undefined,
    onchange,
  }: {
    param: CCParam;
    /**
     * Starting CC value, when the parent already knows one — after a module
     * switch the patch has been applied before these components are built, so
     * seeding from param.default would show the wrong option until the next
     * feedback tick. Read once at construction; live updates come via
     * applyCC().
     */
    initial?: number;
    /** Overrides the shape picked from the option count. */
    variant?: "segmented" | "ladder" | "dropdown";
    label?: string;
    onchange?: (ccVal: number) => void;
  } = $props();

  // Both read once, deliberately: `param` is static config and `initial` is a
  // starting point rather than a live value. A module switch rebuilds this
  // component, so a fresh read of both happens exactly when it should.
  // svelte-ignore state_referenced_locally
  const opts = param.options ?? [];
  // svelte-ignore state_referenced_locally
  const defCC = initial ?? param.default ?? 0;
  // svelte-ignore state_referenced_locally
  const shape =
    variant ?? (opts.length <= 4 ? "segmented" : opts.length <= 8 ? "ladder" : "dropdown");
  // svelte-ignore state_referenced_locally
  const legend = label ?? param.label;

  const defaultIdx = opts.findIndex((o) => defCC >= o.ccMin && defCC <= o.ccMax);
  let selectedIdx = $state(defaultIdx >= 0 ? defaultIdx : 0);

  function select(idx: number) {
    selectedIdx = idx;
    // Send the lower bound of the option's CC range; the firmware accepts the
    // whole band.
    const ccVal = opts[idx].ccMin;
    midi.sendCC(param.cc, ccVal);
    onchange?.(ccVal);
  }

  /** Push an inbound CC from the module into this control. */
  export function applyCC(ccVal: number) {
    const idx = opts.findIndex((o) => ccVal >= o.ccMin && ccVal <= o.ccMax);
    if (idx >= 0) {
      selectedIdx = idx;
      onchange?.(ccVal);
    }
  }

  /**
   * LED colour for a rung, cycling the panel's six-colour strip. Spelled as a
   * var() reference so the strip stays defined in one place, in app.css.
   */
  const ledVar = (idx: number) => `var(--led-${(idx % 6) + 1})`;
</script>

<div class="panel-select" class:ladder={shape === "ladder"}>
  <div class="legend panel-legend">{legend}</div>

  {#if shape === "segmented"}
    <div class="segmented" role="radiogroup" aria-label={legend}>
      {#each opts as opt, idx}
        <button
          type="button"
          role="radio"
          aria-checked={selectedIdx === idx}
          class="seg"
          class:active={selectedIdx === idx}
          onclick={() => select(idx)}>{opt.label}</button
        >
      {/each}
    </div>
  {:else if shape === "ladder"}
    <div class="rungs" role="radiogroup" aria-label={legend}>
      {#each opts as opt, idx}
        <button
          type="button"
          role="radio"
          aria-checked={selectedIdx === idx}
          class="rung"
          class:active={selectedIdx === idx}
          style:--led={ledVar(idx)}
          onclick={() => select(idx)}
        >
          <span class="led"></span>
          <span class="rung-label">{opt.label}</span>
        </button>
      {/each}
    </div>
  {:else}
    <select
      class="dropdown"
      aria-label={legend}
      value={selectedIdx}
      onchange={(e) => select(Number((e.target as HTMLSelectElement).value))}
    >
      {#each opts as opt, idx}
        <option value={idx}>{opt.label}</option>
      {/each}
    </select>
  {/if}
</div>

<style>
  .panel-select {
    display: flex;
    flex-direction: column;
    min-width: 0;
  }
  /* Same reserved header height as Knob, so a switch and a knob standing side
     by side in a section start their controls on the same line. */
  .legend {
    display: flex;
    align-items: flex-end;
    min-height: 33px;
    margin-bottom: 2px;
    line-height: 1.15;
  }

  /* ── Segmented ─────────────────────────────────────────────────────────── */
  .segmented {
    display: flex;
    border: 1px solid var(--hairline-strong);
    border-radius: var(--radius);
    overflow: hidden;
    background: var(--bg-sunken);
    width: fit-content;
    max-width: 100%;
  }
  .seg {
    flex: 1 1 auto;
    font: inherit;
    font-size: 0.76rem;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    white-space: nowrap;
    padding: 6px 13px;
    border: 0;
    border-right: 1px solid var(--hairline);
    background: transparent;
    color: var(--text-faint);
    cursor: pointer;
    transition:
      color 90ms,
      background 90ms;
  }
  .seg:last-child {
    border-right: 0;
  }
  .seg:hover {
    color: var(--text);
  }
  .seg.active {
    background: rgba(192, 137, 74, 0.16);
    color: var(--copper-bright);
  }

  /* ── LED ladder ────────────────────────────────────────────────────────── */
  .rungs {
    display: flex;
    flex-direction: column;
    gap: 3px;
    padding: 7px 9px;
    border: 1px solid var(--hairline);
    border-radius: 9px;
    background: var(--bg-sunken);
    width: fit-content;
  }
  .rung {
    display: flex;
    align-items: center;
    gap: 9px;
    padding: 2px 3px;
    border: 0;
    background: transparent;
    cursor: pointer;
    font: inherit;
    text-align: left;
  }
  .led {
    width: 10px;
    height: 10px;
    flex: none;
    border-radius: 50%;
    background: var(--bg-raised);
    box-shadow: inset 0 0 0 1px var(--hairline-strong);
    transition:
      background 110ms,
      box-shadow 110ms;
  }
  .rung.active .led {
    background: var(--led);
    box-shadow:
      0 0 7px var(--led),
      inset 0 0 0 1px rgba(255, 255, 255, 0.35);
  }
  .rung-label {
    font-size: 0.76rem;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    white-space: nowrap;
    color: var(--text-faint);
    transition: color 110ms;
  }
  .rung:hover .rung-label {
    color: var(--text-dim);
  }
  .rung.active .rung-label {
    color: var(--text);
  }

  /* ── Dropdown ──────────────────────────────────────────────────────────── */
  .dropdown {
    font: inherit;
    font-size: 0.78rem;
    padding: 6px 8px;
    border-radius: var(--radius);
    border: 1px solid var(--hairline-strong);
    background: var(--bg-sunken);
    color: var(--text);
    cursor: pointer;
    max-width: 100%;
  }
  .dropdown:focus-visible {
    outline: none;
    border-color: var(--copper);
    box-shadow: 0 0 0 2px var(--copper-glow);
  }
</style>
