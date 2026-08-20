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
  import { posToValue, valueToPos } from "../lib/paramMapTypes";
  import { midi } from "../lib/midi";

  let {
    param,
    value = $bindable(param.default),
    hint = undefined,
    displayOverride = undefined,
  }: {
    param: CCParam;
    value?: number;
    hint?: string;
    displayOverride?: string;
  } = $props();

  // Anything with a curve — `scale: "log"` or a `skew` — drives the HTML range
  // input in normalized [0,1] TRAVEL, so the slider moves like the hardware
  // knob and the CC does. Only a plain linear parameter can operate directly in
  // its native range, where travel and value are the same thing anyway.
  //
  // Skew used to be missing from this test, which left six of Alloy Coil's
  // knobs linear-in-value on screen while being curved everywhere else. The
  // value sent was always right — floatToCC() applies the skew — but the
  // travel was not, so the useful part of a control was bunched at one end here
  // and spread out on the module. Echo time was the worst: 0.05–0.5 s occupied
  // 11 % of this slider against 34 % of the knob.
  let isCurved = $derived(param.scale === "log" || (param.skew ?? 1) !== 1);
  let sliderMin = $derived(isCurved ? 0 : param.min);
  let sliderMax = $derived(isCurved ? 1 : param.max);
  let sliderStep = $derived(isCurved ? 0.0001 : (param.step ?? 0.001));

  let sliderPos = $derived(isCurved ? valueToPos(param, value) : value);

  // Derived display string — can be overridden by the parent for mode-contextual display
  let displayValue = $derived(
    displayOverride ??
      (param.unit
        ? `${value.toFixed(param.step && param.step >= 1 ? 0 : 2)} ${param.unit}`
        : value.toFixed(2)),
  );

  // Last 7-bit value actually put on the wire, so a drag only sends when the
  // CC changes. A range input fires `input` on every mousemove, but the slider
  // is typically a few hundred pixels wide against 128 CC steps — so most of
  // those events carry a value identical to the previous one. Sending them
  // anyway floods the module's inbound queue with duplicates for no effect.
  let lastSentCC: number | null = null;

  function handleInput(e: Event) {
    const pos = parseFloat((e.target as HTMLInputElement).value);
    const native = isCurved ? posToValue(param, pos) : pos;
    value = native;
    const cc7bit = floatToCC(param, native);
    if (cc7bit === lastSentCC) return;
    lastSentCC = cc7bit;
    midi.sendCC(param.cc, cc7bit);
  }

  // When we receive a CC message update the value externally
  function applyCC(ccVal: number) {
    value = ccToFloat(param, ccVal);
    // Keep the send-dedupe in step with externally-driven changes, or the next
    // local drag back to this value would be suppressed as a duplicate.
    lastSentCC = ccVal;
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
