<script lang="ts">
  /**
   * ParamSlider — a labeled range slider bound to a single MIDI CC parameter.
   *
   * Props:
   *   param  — CCParam descriptor from paramMap.ts
   *   value  — current float value in param's native range
   *   onchange — callback when value changes
   */
  import type { CCParam } from "../lib/paramMap";
  import { ccToFloat, floatToCC } from "../lib/paramMap";
  import { midi } from "../lib/midi";

  let {
    param,
    value = $bindable(param.default),
  }: { param: CCParam; value?: number } = $props();

  // Derived display string
  let displayValue = $derived(
    param.unit ? `${value.toFixed(param.step && param.step >= 1 ? 0 : 2)} ${param.unit}` : value.toFixed(2)
  );

  function handleInput(e: Event) {
    const raw = parseFloat((e.target as HTMLInputElement).value);
    value = raw;
    const cc7bit = floatToCC(param, raw);
    midi.sendCC(param.cc, cc7bit);
  }

  // When we receive a CC message update the value externally
  function applyCC(ccVal: number) {
    value = ccToFloat(param, ccVal);
  }

  // Expose applyCC so parent can push MIDI-in updates
  export { applyCC };
</script>

<div class="param-slider">
  <div class="param-header">
    <span class="param-label">{param.label}</span>
    <span class="param-value">{displayValue}</span>
  </div>
  <input
    type="range"
    min={param.min}
    max={param.max}
    step={param.step ?? 0.001}
    {value}
    oninput={handleInput}
    aria-label={param.label}
  />
  <div class="param-meta">
    <span>{param.min}</span>
    <span class="cc-badge">CC {param.cc}</span>
    <span>{param.max}</span>
  </div>
</div>

<style>
  .param-slider {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
    background: #1c1c30;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 0.6rem 0.75rem;
    min-width: 160px;
  }
  .param-header {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
  }
  .param-label {
    font-size: 0.78rem;
    font-weight: 600;
    color: #bbb;
    text-transform: uppercase;
    letter-spacing: 0.04em;
  }
  .param-value {
    font-size: 0.78rem;
    color: #7cb8ff;
    font-variant-numeric: tabular-nums;
  }
  input[type="range"] {
    width: 100%;
    accent-color: #7cb8ff;
    cursor: pointer;
  }
  .param-meta {
    display: flex;
    justify-content: space-between;
    font-size: 0.65rem;
    color: #555;
  }
  .cc-badge {
    color: #555;
    font-size: 0.65rem;
  }
</style>
