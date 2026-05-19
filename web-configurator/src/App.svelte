<script lang="ts">
  import {
    PARAM_MAP,
    PARAM_CATEGORIES,
    PARAMS_BY_CATEGORY,
    ccToFloat,
  } from "./lib/paramMap";
  import { midi } from "./lib/midi";
  import ConnectionBar from "./components/ConnectionBar.svelte";
  import ParamSlider from "./components/ParamSlider.svelte";
  import ParamSelect from "./components/ParamSelect.svelte";
  import MidiKeyboard from "./components/MidiKeyboard.svelte";
  import PresetManager from "./components/PresetManager.svelte";

  // Float values, keyed by CC — only meaningful for slider-type params
  let paramValues = $state(
    Object.fromEntries(
      PARAM_MAP.filter((p) => !p.type || p.type === "slider").map((p) => [
        p.cc,
        p.default,
      ]),
    ),
  );

  // Raw CC values for select params (needed for derived computations like chord name)
  let selectValues = $state(
    Object.fromEntries(
      PARAM_MAP.filter((p) => p.type === "select").map((p) => [
        p.cc,
        p.default ?? 0,
      ]),
    ),
  );

  // Refs for pushing incoming MIDI-in to the right component
  let sliderRefs: Record<number, { applyCC: (v: number) => void }> = {};
  let selectRefs: Record<number, { applyCC: (v: number) => void }> = {};

  // Subscribe to incoming MIDI CC messages and route to the right component
  $effect(() => {
    if (!$midi.connected) return;
    const unsubscribe = midi.onCC((cc: number, value: number) => {
      const param = PARAM_MAP.find((p) => p.cc === cc);
      if (!param) return;
      if (param.type === "select") {
        selectValues[cc] = value;
        selectRefs[cc]?.applyCC(value);
      } else {
        paramValues[cc] = ccToFloat(param, value);
        sliderRefs[cc]?.applyCC(value);
      }
    });
    return unsubscribe;
  });

  // ── Chord / interval hint for the Relation slider ──────────────────────────
  // CHORD mode: CC 115 ≥ 64.  Index maps exactly to kChordTable rows in firmware.
  const CHORD_NAMES = [
    "Unison",
    "Power",
    "Minor",
    "Major",
    "Sus2",
    "Sus4",
    "Maj 7",
    "Min 7",
    "Dom 7",
    "Dim",
    "Octaves",
  ];
  // PAIR mode: integer-semitone interval names, 0–24 st.
  const INTERVAL_NAMES = [
    "Unison",
    "Min 2nd",
    "Maj 2nd",
    "Min 3rd",
    "Maj 3rd",
    "P 4th",
    "Tritone",
    "P 5th",
    "Min 6th",
    "Maj 6th",
    "Min 7th",
    "Maj 7th",
    "Octave",
    "m9",
    "M9",
    "m10",
    "M10",
    "P11",
    "#11",
    "P12",
    "m13",
    "M13",
    "m14",
    "M14",
    "+Oct",
  ];

  let relHint = $derived(
    (() => {
      const isChord = (selectValues[115] ?? 0) >= 64;
      const rel = paramValues[94] ?? 0;
      if (isChord) {
        const idx = Math.min(10, Math.round((rel / 24) * 10));
        return CHORD_NAMES[idx];
      } else {
        const st = Math.min(24, Math.max(0, Math.round(rel)));
        return INTERVAL_NAMES[st];
      }
    })(),
  );
</script>

<div class="app-shell">
  <!-- Top connection bar -->
  <ConnectionBar />

  <!-- Main content -->
  <main class="main-content">
    <!-- Left: parameters grouped by category -->
    <section class="params-panel">
      {#each PARAM_CATEGORIES as cat}
        {@const catParams = PARAMS_BY_CATEGORY[cat]}
        {@const selects = catParams.filter((p) => p.type === "select")}
        {@const sliders = catParams.filter(
          (p) => !p.type || p.type === "slider",
        )}
        <div class="cat-section">
          <h3 class="cat-title">{cat}</h3>
          {#if selects.length}
            <div class="select-row">
              {#each selects as param}
                <ParamSelect
                  {param}
                  bind:this={selectRefs[param.cc]}
                  onchange={(v) => {
                    selectValues[param.cc] = v;
                  }}
                />
              {/each}
            </div>
          {/if}
          {#if sliders.length}
            <div class="params-grid">
              {#each sliders as param}
                <ParamSlider
                  {param}
                  bind:value={paramValues[param.cc]}
                  bind:this={sliderRefs[param.cc]}
                  hint={param.cc === 94 ? relHint : undefined}
                />
              {/each}
            </div>
          {/if}
        </div>
      {/each}
    </section>

    <!-- Right: keyboard + presets -->
    <aside class="right-panel">
      <section class="\">
        <h2 class="panel-title">Keyboard</h2>
        <MidiKeyboard />
      </section>

      <section class="presets-panel">
        <PresetManager />
      </section>
    </aside>
  </main>
  <div class="footer">
    <small>Alloy Flux Web Configurator — Voltage Foundry Modular - 2026</small>
  </div>
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
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
  .cat-section {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .cat-title {
    font-size: 0.65rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: #555;
    margin: 0;
    padding-bottom: 0.25rem;
    border-bottom: 1px solid #222;
  }
  .panel-title {
    font-size: 0.65rem;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: #555;
    margin: 0 0 0.5rem 0;
  }
  .select-row {
    display: flex;
    flex-wrap: wrap;
    gap: 0.5rem;
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
  .keyboard-panel {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .presets-panel {
    display: flex;
    flex-direction: column;
  }
</style>
