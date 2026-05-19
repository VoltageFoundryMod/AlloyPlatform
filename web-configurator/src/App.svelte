<script lang="ts">
  import { PARAM_MAP, ccToFloat, type CCParam } from "./lib/paramMap";
  import { midi } from "./lib/midi";
  import ConnectionBar from "./components/ConnectionBar.svelte";
  import ParamSlider from "./components/ParamSlider.svelte";
  import MidiKeyboard from "./components/MidiKeyboard.svelte";
  import PresetManager from "./components/PresetManager.svelte";

  // Per-param float values (reactive state)
  let paramValues = $state(
    Object.fromEntries(PARAM_MAP.map((p) => [p.cc, p.default]))
  );

  // Keep slider refs so we can push incoming MIDI-in updates
  let sliderRefs: Record<number, { applyCC: (v: number) => void }> = {};

  // Subscribe to incoming MIDI CC messages
  $effect(() => {
    if (!$midi.connected) return;
    const unsubscribe = midi.onCC((cc: number, value: number) => {
      const param = PARAM_MAP.find((p) => p.cc === cc);
      if (!param) return;
      paramValues[cc] = ccToFloat(param, value);
      sliderRefs[cc]?.applyCC(value);
    });
    return unsubscribe;
  });
</script>

<div class="app-shell">
  <!-- Top connection bar -->
  <ConnectionBar />

  <!-- Main content -->
  <main class="main-content">
    <!-- Left: parameter sliders grid -->
    <section class="params-panel">
      <h2 class="panel-title">Parameters</h2>
      <div class="params-grid">
        {#each PARAM_MAP as param}
          <ParamSlider
            {param}
            bind:value={paramValues[param.cc]}
            bind:this={sliderRefs[param.cc]}
          />
        {/each}
      </div>
    </section>

    <!-- Right: keyboard + presets -->
    <aside class="right-panel">
      <section class="keyboard-panel">
        <h2 class="panel-title">Keyboard</h2>
        <MidiKeyboard />
      </section>

      <section class="presets-panel">
        <PresetManager />
      </section>
    </aside>
  </main>
</div>

<style>
  .app-shell {
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    background: #0f0f1a;
    color: #ddd;
    font-family: "Inter", system-ui, sans-serif;
  }
  .main-content {
    display: flex;
    flex: 1;
    gap: 1rem;
    padding: 1rem;
    align-items: flex-start;
    flex-wrap: wrap;
  }
  .params-panel {
    flex: 1 1 480px;
    min-width: 300px;
  }
  .panel-title {
    font-size: 0.7rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    color: #666;
    margin: 0 0 0.5rem 0;
  }
  .params-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 0.5rem;
  }
  .right-panel {
    display: flex;
    flex-direction: column;
    gap: 1rem;
    flex: 0 1 400px;
    min-width: 300px;
  }
  .keyboard-panel { display: flex; flex-direction: column; gap: 0.5rem; }
  .presets-panel  { display: flex; flex-direction: column; }
</style>
