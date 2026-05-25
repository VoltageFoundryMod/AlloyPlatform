<script lang="ts">
  import {
    PARAM_MAP,
    PARAM_CATEGORIES,
    PARAMS_BY_CATEGORY,
    ccToFloat,
    floatToCC,
  } from "./lib/paramMap";
  import { midi } from "./lib/midi";
  import { serial } from "./lib/serial";
  import {
    SysexCmd,
    parseSysExBody,
    parseSerialDump,
    buildSyxBlob,
    downloadFile,
    type CCPair,
  } from "./lib/patchSync";
  import ConnectionBar from "./components/ConnectionBar.svelte";
  import ParamSlider from "./components/ParamSlider.svelte";
  import ParamSelect from "./components/ParamSelect.svelte";
  import MidiKeyboard from "./components/MidiKeyboard.svelte";
  import PresetManager from "./components/PresetManager.svelte";
  import FxChainVisual from "./components/FxChainVisual.svelte";
  import EnvelopeGraph from "./components/EnvelopeGraph.svelte";

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

  // MIDI receive channel (0=omni, 1-16). Set by CC 110 in patch dump; sent via SysEx 0x07.
  let midiChannel = $state(0);

  function sendMidiChannel(ch: number) {
    midiChannel = ch;
    if ($midi.deviceConnected)
      midi.sendSysEx(SysexCmd.SET_MIDI_CHANNEL, [ch & 0x7f]);
  }

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

  // ---------------------------------------------------------------------------
  // Patch apply — shared by all sync sources (MIDI SysEx, serial dump, file import)
  // ---------------------------------------------------------------------------

  /**
   * Apply a batch of CC pairs to the UI.
   * Pass sendToDevice=true to also replay each CC to the connected MIDI output.
   */
  function applyPatch(pairs: CCPair[], sendToDevice = false) {
    for (const { cc, value } of pairs) {
      // CC 110 is the MIDI receive channel — not in PARAM_MAP, handled separately.
      if (cc === 110) {
        midiChannel = value; // 0=omni, 1-16
        continue;
      }
      const param = PARAM_MAP.find((p) => p.cc === cc);
      if (!param) continue;
      if (param.type === "select") {
        selectValues[cc] = value;
        selectRefs[cc]?.applyCC(value);
      } else {
        paramValues[cc] = ccToFloat(param, value);
        sliderRefs[cc]?.applyCC(value);
      }
      if (sendToDevice) midi.sendCC(cc, value);
    }
  }

  /**
   * Snapshot the current UI state as CC pairs (for file export or SysEx send).
   */
  function getPatchSnapshot(): CCPair[] {
    const pairs: CCPair[] = [];
    for (const param of PARAM_MAP) {
      if (!param.type || param.type === "slider") {
        const val = paramValues[param.cc] ?? param.default;
        pairs.push({ cc: param.cc, value: floatToCC(param, val) });
      } else if (param.type === "select") {
        pairs.push({
          cc: param.cc,
          value: selectValues[param.cc] ?? param.default ?? 0,
        });
      }
    }
    return pairs;
  }

  /**
   * Apply factory defaults to the web UI (no device send).
   * Called immediately after the web sends PRESET_RESET so the UI syncs
   * without needing a PATCH_DUMP reply from the device (which VCV may not
   * send if midiOutput isn't configured).
   * Hardware will follow up with a real PATCH_DUMP that confirms the same
   * values; it arrives later and overwrites cleanly.
   */
  function applyDefaults() {
    const defaultPairs: CCPair[] = PARAM_MAP.map((p) => ({
      cc: p.cc,
      value:
        !p.type || p.type === "slider"
          ? floatToCC(p, p.default ?? 0)
          : (p.default ?? 0),
    }));
    applyPatch(defaultPairs, false);
  }

  /**
   * Import a patch from a file: update the UI and send APPLY_PATCH to the
   * device via MIDI SysEx (if MIDI is connected).
   */
  function applyFromFile(pairs: CCPair[]) {
    applyPatch(pairs, false); // update UI
    if ($midi.deviceConnected) {
      // Send all pairs in one APPLY_PATCH SysEx message
      const payload = pairs.flatMap(({ cc, value }) => [
        cc & 0x7f,
        value & 0x7f,
      ]);
      midi.sendSysEx(SysexCmd.APPLY, payload);
    }
  }

  // ---------------------------------------------------------------------------
  // Auto-sync on MIDI connect — send REQUEST_DUMP, apply PATCH_DUMP response
  // ---------------------------------------------------------------------------
  $effect(() => {
    if (!$midi.deviceConnected) return;
    // Small delay so the device has time to finish USB enumeration
    const timer = setTimeout(() => midi.sendSysEx(SysexCmd.REQUEST, []), 300);
    const unsubSysEx = midi.onSysEx((body: Uint8Array) => {
      const pairs = parseSysExBody(body);
      if (pairs && pairs.length > 0) applyPatch(pairs, false);
    });
    return () => {
      clearTimeout(timer);
      unsubSysEx();
    };
  });

  // ---------------------------------------------------------------------------
  // Auto-sync on serial connect — send "dump", parse cc:N=V lines
  // ---------------------------------------------------------------------------
  $effect(() => {
    if (!$serial.connected) return;
    let dumpLines: string[] = [];
    let inDump = false;
    serial.send("dump");
    const unsubLine = serial.onLine((line: string) => {
      if (line === "dump_begin") {
        inDump = true;
        dumpLines = [];
        return;
      }
      if (line === "dump_end" && inDump) {
        inDump = false;
        const pairs = parseSerialDump(dumpLines);
        if (pairs.length > 0) applyPatch(pairs, false);
        dumpLines = [];
        return;
      }
      if (inDump) dumpLines.push(line);
    });
    return unsubLine;
  });

  // ── Relation slider — mode-contextual display ──────────────────────────────
  // CC 115 bands: 0-31=PAIR, 32-63=CLOUD, 64-95=CHORD, 96-127=POLY
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
  // PAIR / POLY mode: integer-semitone interval names, 0–24 st.
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

  type RelMode = "pair" | "cloud" | "chord" | "cascade" | "string" | "poly";
  let relMode = $derived.by((): RelMode => {
    const m = selectValues[115] ?? 0;
    if (m < 21) return "pair";
    if (m < 42) return "cloud";
    if (m < 63) return "chord";
    if (m < 84) return "cascade";
    if (m < 105) return "string";
    return "poly";
  });

  let relHint = $derived(
    (() => {
      const rel = paramValues[94] ?? 0;
      if (relMode === "chord") {
        const idx = Math.min(10, Math.round((rel / 24) * 10));
        return CHORD_NAMES[idx];
      }
      if (relMode === "cloud") {
        const cents = Math.round((rel / 24) * 50);
        return `±${(cents / 2).toFixed(0)} ¢ / voice`;
      }
      if (relMode === "cascade") {
        const zones = ["1:1", "4:3", "3:2", "2:1", "5:2", "3:1"];
        const zoneIdx = Math.min(5, Math.floor((rel / 24) * 6));
        return `ratio ${zones[zoneIdx]}`;
      }
      if (relMode === "string") {
        const cents = Math.round((rel / 24) * 30);
        return `±${(cents / 2).toFixed(0)} ¢ / voice`;
      }
      if (relMode === "poly") return "sub oct";
      // PAIR — show interval name
      const st = Math.min(24, Math.max(0, Math.round(rel)));
      return INTERVAL_NAMES[st];
    })(),
  );

  let relDisplayOverride = $derived(
    (() => {
      const rel = paramValues[94] ?? 0;
      if (relMode === "chord") {
        // Show the chord-table index as "shape X / 11"
        const idx = Math.min(10, Math.round((rel / 24) * 10));
        return `${idx} / 10`;
      }
      if (relMode === "cloud") {
        // Total spread in cents: rel/24 * 50
        const cents = Math.round((rel / 24) * 50);
        return `${cents} ¢`;
      }
      if (relMode === "cascade") {
        // Show FM ratio zone
        const zones = ["1:1", "4:3", "3:2", "2:1", "5:2", "3:1"];
        const zoneIdx = Math.min(5, Math.floor((rel / 24) * 6));
        return zones[zoneIdx];
      }
      if (relMode === "string") {
        // Total spread in cents: rel/24 * 30
        const cents = Math.round((rel / 24) * 30);
        return `${cents} ¢`;
      }
      if (relMode === "poly") return undefined;
      // PAIR: semitones (integer)
      const st = Math.min(24, Math.max(0, Math.round(rel)));
      return `${st} st`;
    })(),
  );

  // COLOR (CC 92): hint and display vary by mode.
  //   PAIR/CASCADE → FM depth (0–100%)
  //   ensemble     → Hz fine spread (± up to 25 Hz outer voices)
  let colorHint = $derived(
    relMode === "pair" || relMode === "cascade" ? "FM depth" : "fine detune",
  );
  let colorDisplayOverride = $derived(
    (() => {
      const v = paramValues[92] ?? 0;
      if (relMode === "pair" || relMode === "cascade") {
        return `${Math.round(v * 100)}%`;
      }
      return `±${Math.round(v * 25)} Hz`;
    })(),
  );
  // ── Serial console drawer ────────────────────────────────────────────────
  let serialLines = $state<string[]>([]);
  let consoleOpen = $state(false);
  let consoleBodyEl = $state<HTMLElement | null>(null);
  let consoleInput = $state("");
  let cmdHistory = $state<string[]>([]);
  let historyIdx = $state(-1); // -1 = current (not browsing history)

  function handleConsoleKeydown(e: KeyboardEvent) {
    if (e.key === "Enter") {
      sendConsoleCommand();
      historyIdx = -1;
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      if (cmdHistory.length === 0) return;
      const next =
        historyIdx === -1 ? 0 : Math.min(historyIdx + 1, cmdHistory.length - 1);
      historyIdx = next;
      consoleInput = cmdHistory[cmdHistory.length - 1 - next];
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      if (historyIdx <= 0) {
        historyIdx = -1;
        consoleInput = "";
      } else {
        historyIdx -= 1;
        consoleInput = cmdHistory[cmdHistory.length - 1 - historyIdx];
      }
    }
  }

  $effect(() => {
    return serial.onLine((line: string) => {
      serialLines = [...serialLines.slice(-499), line];
      // Auto-scroll to bottom when open
      if (consoleBodyEl) {
        setTimeout(() => {
          if (consoleBodyEl)
            consoleBodyEl.scrollTop = consoleBodyEl.scrollHeight;
        }, 0);
      }
    });
  });

  function sendConsoleCommand() {
    const cmd = consoleInput.trim();
    if (!cmd || !$serial.connected) return;
    serial.send(cmd);
    // Push to history (dedupe consecutive identical)
    if (cmdHistory.length === 0 || cmdHistory[cmdHistory.length - 1] !== cmd) {
      cmdHistory = [...cmdHistory.slice(-99), cmd];
    }
    historyIdx = -1;
    consoleInput = "";
  }
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
          {#if cat === "FX Chain"}
            <FxChainVisual
              filterPost={(selectValues[79] ?? 0) >= 64}
              delayPost={(selectValues[80] ?? 0) >= 64}
            />
          {/if}
          {#if cat === "Envelope"}
            <EnvelopeGraph
              isAdsr={(selectValues[81] ?? 0) >= 64}
              attack={paramValues[73] ?? 0.05}
              decay={paramValues[82] ?? 0.1}
              sustain={paramValues[83] ?? 0.8}
              release={paramValues[72] ?? 0.3}
              curve={paramValues[71] ?? 0.5}
              curveTime={paramValues[88] ?? 1.0}
            />
          {/if}
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
                {#if param.rowBreakBefore}
                  <div class="row-break"></div>
                {/if}
                <ParamSlider
                  {param}
                  bind:value={paramValues[param.cc]}
                  bind:this={sliderRefs[param.cc]}
                  hint={param.cc === 94
                    ? relHint
                    : param.cc === 92
                      ? colorHint
                      : undefined}
                  displayOverride={param.cc === 94
                    ? relDisplayOverride
                    : param.cc === 92
                      ? colorDisplayOverride
                      : undefined}
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
        <PresetManager {getPatchSnapshot} {applyFromFile} {applyDefaults} />
      </section>

      <section class="midi-settings-panel cat-section">
        <h2 class="panel-title">MIDI Settings</h2>
        <div class="midi-channel-row">
          <label class="midi-channel-label" for="midi-channel-select"
            >Receive Channel</label
          >
          <select
            id="midi-channel-select"
            class="midi-channel-select"
            value={midiChannel}
            onchange={(e) =>
              sendMidiChannel(
                parseInt((e.target as HTMLSelectElement).value, 10),
              )}
            disabled={!$midi.deviceConnected}
          >
            <option value={0}>Omni (All)</option>
            {#each Array.from({ length: 16 }, (_, i) => i + 1) as ch}
              <option value={ch}>{ch}</option>
            {/each}
          </select>
        </div>
      </section>
    </aside>
  </main>
  <div class="footer cat-section">
    <small class="footer-label"
      >Alloy Flux Web Configurator — Voltage Foundry Modular - ©2026</small
    >
  </div>

  <!-- Serial console drawer -->
  <div class="console-drawer" class:open={consoleOpen}>
    <button class="console-tab" onclick={() => (consoleOpen = !consoleOpen)}>
      <span class="console-conn-dot" class:connected={$serial.connected}></span>
      Serial Console {consoleOpen ? "▼" : "▲"}
    </button>
    {#if consoleOpen}
      <div class="console-body" bind:this={consoleBodyEl}>
        {#each serialLines as line}
          <div class="console-line">{line}</div>
        {/each}
        {#if serialLines.length === 0}
          <div class="console-empty">No output yet.</div>
        {/if}
      </div>
      <div class="console-input-row">
        <input
          class="console-input"
          type="text"
          placeholder={$serial.connected ? "Type a command…" : "Not connected"}
          disabled={!$serial.connected}
          bind:value={consoleInput}
          onkeydown={handleConsoleKeydown}
        />
        <button
          class="console-send"
          disabled={!$serial.connected}
          onclick={sendConsoleCommand}>Send</button
        >
        <button
          class="console-send"
          disabled={!$serial.connected}
          onclick={() => {
            serial.send("help");
          }}>Help</button
        >
      </div>
    {/if}
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
  .params-grid .row-break {
    grid-column: 1 / -1; /* spans full width, forcing subsequent items to a new row */
    height: 0;
    margin: 0;
    padding: 0;
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
  .midi-settings-panel {
    display: flex;
    flex-direction: column;
  }
  .midi-channel-row {
    display: flex;
    align-items: center;
    gap: 0.75rem;
  }
  .midi-channel-label {
    font-size: 0.75rem;
    color: #aaa;
    white-space: nowrap;
  }
  .midi-channel-select {
    background: #1a1a30;
    color: #ccc;
    border: 1px solid #444;
    border-radius: 4px;
    padding: 0.2rem 0.4rem;
    font-size: 0.8rem;
    cursor: pointer;
  }
  .midi-channel-select:disabled {
    opacity: 0.4;
    cursor: not-allowed;
  }
  .footer-label {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    padding: 0.5rem;
    color: #888;
    min-width: 3rem;
  }
  .console-drawer {
    position: sticky;
    bottom: 0;
    background: #111120;
    border-top: 1px solid #333;
    z-index: 100;
  }
  .console-tab {
    width: 100%;
    padding: 0.35rem 1rem;
    background: #1a1a30;
    border: none;
    border-bottom: 1px solid #2a2a44;
    color: #888;
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    cursor: pointer;
    text-align: left;
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .console-conn-dot {
    display: inline-block;
    width: 0.55rem;
    height: 0.55rem;
    border-radius: 50%;
    background: #444;
    flex-shrink: 0;
    transition: background 0.3s;
  }
  .console-conn-dot.connected {
    background: #4caf50;
    box-shadow: 0 0 5px #4caf5088;
  }
  .console-tab:hover {
    background: #22223a;
    color: #aaa;
  }
  .console-body {
    height: 180px;
    overflow-y: auto;
    padding: 0.4rem 0.75rem;
    font-family: monospace;
    font-size: 0.75rem;
    color: #9f9;
    background: #0b0b18;
  }
  .console-line {
    white-space: pre-wrap;
    word-break: break-all;
    line-height: 1.4;
  }
  .console-empty {
    color: #444;
    font-style: italic;
  }
  .console-input-row {
    display: flex;
    gap: 0.4rem;
    padding: 0.4rem 0.75rem;
    background: #111120;
    border-top: 1px solid #222;
  }
  .console-input {
    flex: 1;
    background: #0d0d20;
    border: 1px solid #333;
    border-radius: 4px;
    color: #ccc;
    font-family: monospace;
    font-size: 0.8rem;
    padding: 0.25rem 0.5rem;
  }
  .console-input:disabled {
    opacity: 0.4;
  }
  .console-send {
    padding: 0.25rem 0.75rem;
    font-size: 0.8rem;
    background: #2a2a50;
    color: #aab;
    border: 1px solid #555;
    border-radius: 4px;
    cursor: pointer;
  }
  .console-send:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .console-send:not(:disabled):hover {
    background: #3a3a70;
  }
</style>
