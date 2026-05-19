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
    hint = undefined,
  }: { param: CCParam; value?: number; hint?: string } = $props();

  // For log-scale params the HTML range input operates in normalized [0,1] space.
  // For linear params it operates directly in the param's native range.
  let sliderMin = $derived(param.scale === "log" ? 0 : param.min);
  let sliderMax = $derived(param.scale === "log" ? 1 : param.max);
  let sliderStep = $derived(
    param.scale === "log" ? 0.0001 : (param.step ?? 0.001),
  );

  /** Convert native value → slider position */
  function valueToPos(v: number): number {
    if (param.scale === "log") {
      return Math.log(v / param.min) / Math.log(param.max / param.min);
    }
    return v;
  }

  /** Convert slider position → native value */
  function posToValue(pos: number): number {
    if (param.scale === "log") {
      return param.min * Math.pow(param.max / param.min, pos);
    }
    return pos;
  }

  let sliderPos = $derived(valueToPos(value));

  // Derived display string
  let displayValue = $derived(
    param.unit
      ? `${value.toFixed(param.step && param.step >= 1 ? 0 : 2)} ${param.unit}`
      : value.toFixed(2),
  );

  function handleInput(e: Event) {
    const pos = parseFloat((e.target as HTMLInputElement).value);
    const native = posToValue(pos);
    value = native;
    const cc7bit = floatToCC(param, native);
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
    min={sliderMin}
    max={sliderMax}
    step={sliderStep}
    value={sliderPos}
    oninput={handleInput}
    aria-label={param.label}
  />
  <div class="param-meta">
    <span>{param.min}</span>
    <span class="cc-badge">CC {param.cc}</span>
    <span>{param.max}</span>
  </div>
  {#if hint}
    <div class="param-hint">{hint}</div>
  {/if}
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
  .param-hint {
    text-align: center;
    font-size: 0.7rem;
    color: #6fcf6f;
    font-weight: 600;
    letter-spacing: 0.04em;
    margin-top: -0.1rem;
  }
</style>
