<script lang="ts">
  import type { CCParam } from "../lib/paramMap.js";
  import { midi } from "../lib/midi.js";

  let {
    param,
    onchange,
  }: { param: CCParam; onchange?: (ccVal: number) => void } = $props();

  // Determine starting option index from the param's default CC value.
  // param is static config so reading outside a closure is intentional.
  // eslint-disable-next-line svelte/valid-compile
  const opts = param.options!;
  const defCC = param.default ?? 0;
  const defaultIdx = opts.findIndex(
    (o) => defCC >= o.ccMin && defCC <= o.ccMax,
  );

  let selectedIdx = $state(defaultIdx >= 0 ? defaultIdx : 0);

  function select(idx: number) {
    selectedIdx = idx;
    // Send the lower bound of the option's CC range; the firmware accepts the full range
    const ccVal = opts[idx].ccMin;
    midi.sendCC(param.cc, ccVal);
    onchange?.(ccVal);
  }

  /** Called from App.svelte when an incoming MIDI CC arrives for this param. */
  export function applyCC(ccVal: number) {
    const idx = opts.findIndex((o) => ccVal >= o.ccMin && ccVal <= o.ccMax);
    if (idx >= 0) {
      selectedIdx = idx;
      onchange?.(ccVal);
    }
  }
</script>

<div class="param-select">
  <span class="select-label">{param.label}</span>
  {#if opts.length > 6}
    <select
      class="opt-dropdown"
      value={selectedIdx}
      onchange={(e) => select(Number((e.target as HTMLSelectElement).value))}
    >
      {#each opts as opt, idx}
        <option value={idx}>{opt.label}</option>
      {/each}
    </select>
  {:else}
    <div class="btn-group">
      {#each opts as opt, idx}
        <button
          class="opt-btn"
          class:active={selectedIdx === idx}
          onclick={() => select(idx)}>{opt.label}</button
        >
      {/each}
    </div>
  {/if}
</div>

<style>
  .param-select {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    background: #1c1c30;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 0.5rem 0.75rem;
    width: fit-content;
  }

  .select-label {
    font-size: 0.75rem;
    font-weight: 600;
    color: #aaa;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    white-space: nowrap;
    min-width: 72px;
  }

  .btn-group {
    display: flex;
    gap: 3px;
  }

  .opt-btn {
    font-size: 0.78rem;
    padding: 0.25rem 0.9rem;
    cursor: pointer;
    border-radius: 4px;
    border: 1px solid #444;
    background: #1e1e32;
    color: #777;
    transition:
      background 80ms,
      color 80ms,
      border-color 80ms;
  }

  .opt-btn:hover {
    background: #2a2a50;
    color: #bbb;
  }

  .opt-btn.active {
    background: #1a3d1a;
    color: #6fcf6f;
    border-color: #3a7040;
  }

  .opt-dropdown {
    font-size: 0.78rem;
    padding: 0.25rem 0.5rem;
    cursor: pointer;
    border-radius: 4px;
    border: 1px solid #444;
    background: #1e1e32;
    color: #ccc;
    appearance: auto;
    min-width: 10rem;
  }

  .opt-dropdown:focus {
    outline: 1px solid #3a7040;
    border-color: #3a7040;
  }
</style>
