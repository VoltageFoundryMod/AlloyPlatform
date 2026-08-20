<script lang="ts">
  /**
   * PresetManager — 10-slot preset panel (slot 0 = live state, 1–9 = user presets).
   * Also exposes patch file export (.syx) and import.
   */
  import { presets, type PresetSlot } from "../lib/presets";
  import { serial } from "../lib/serial";
  import { activeModule } from "../lib/activeModule";
  import { midi } from "../lib/midi";
  import {
    buildSyxBlob,
    downloadFile,
    parseSyxBuffer,
    type CCPair,
  } from "../lib/patchSync";

  let {
    getPatchSnapshot,
    applyFromFile,
    applyDefaults,
  }: {
    getPatchSnapshot: () => CCPair[];
    applyFromFile: (pairs: CCPair[]) => void;
    applyDefaults: () => void;
  } = $props();

  /** Svelte action: focus element on mount */
  function focusOnMount(node: HTMLElement) {
    node.focus();
  }

  let editingSlot = $state<number | null>(null);
  let editName = $state("");
  let importError = $state<string | null>(null);
  // Inline confirmation: tracks which reset is pending a second click.
  // Value is the slot number (or "all"), null = no pending confirm.
  let confirmPending = $state<number | "all" | null>(null);
  let confirmTimer = $state<ReturnType<typeof setTimeout> | null>(null);

  function startRename(slot: PresetSlot) {
    editingSlot = slot.slot;
    editName = slot.name;
  }

  function commitRename(slot: number) {
    presets.rename(slot, editName.trim() || `Preset ${slot}`);
    editingSlot = null;
  }

  function cancelRename() {
    editingSlot = null;
  }

  function handleRenameKey(e: KeyboardEvent, slot: number) {
    if (e.key === "Enter") commitRename(slot);
    if (e.key === "Escape") cancelRename();
  }

  async function handleSave(slot: number) {
    presets.save(slot);
  }

  async function handleLoad(slot: number) {
    presets.load(slot);
  }

  function handleReset(slot: number | "all") {
    if (confirmPending === slot) {
      // Second click — confirmed, execute reset
      if (confirmTimer) clearTimeout(confirmTimer);
      confirmPending = null;
      confirmTimer = null;
      presets.reset(slot);
      // Apply defaults immediately to the web UI so it syncs without
      // needing a PATCH_DUMP reply (VCV may not send one unless midiOutput
      // is configured; hardware will send a real dump shortly that confirms
      // the same values and overwrites cleanly).
      if (slot === 0 || slot === "all") applyDefaults();
    } else {
      // First click — arm confirmation, auto-cancel after 3 s
      if (confirmTimer) clearTimeout(confirmTimer);
      confirmPending = slot;
      confirmTimer = setTimeout(() => {
        confirmPending = null;
        confirmTimer = null;
      }, 3000);
    }
  }

  // ── Patch file export ──────────────────────────────────────────────────────
  function handleExport() {
    const pairs = getPatchSnapshot();
    const blob = buildSyxBlob(pairs);
    const ts = new Date().toISOString().slice(0, 16).replace(/[T:]/g, "-");
    downloadFile(blob, `${$activeModule.id}-patch-${ts}.syx`);
  }

  // ── Patch file import ──────────────────────────────────────────────────────
  let fileInputEl: HTMLInputElement;

  function handleImportClick() {
    importError = null;
    fileInputEl.value = "";
    fileInputEl.click();
  }

  function handleFileSelected(e: Event) {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = (ev) => {
      const buf = ev.target?.result as ArrayBuffer;
      const patch = parseSyxBuffer(buf);
      if (!patch || patch.pairs.length === 0) {
        importError = "Not a valid Alloy .syx patch file";
        return;
      }
      // A patch carries the signature of the module that wrote it, and CC
      // numbers mean different things on different modules — so a foreign file
      // is refused by name rather than silently applied as nonsense.
      if (patch.module && patch.module.id !== $activeModule.id) {
        importError = `That is a ${patch.module.name} patch — this is ${$activeModule.name}`;
        return;
      }
      importError = null;
      applyFromFile(patch.pairs);
    };
    reader.onerror = () => {
      importError = "File read error";
    };
    reader.readAsArrayBuffer(file);
  }

  const isSerialConnected = $derived($serial.connected);
  const isMidiConnected = $derived($midi.deviceConnected);
  const isConnected = $derived(isSerialConnected || isMidiConnected);
</script>

<div class="preset-manager">
  <div class="pm-header">
    <span class="pm-title">Presets</span>
    <div class="pm-header-actions">
      <!-- hidden file input for .syx import -->
      <input
        type="file"
        accept=".syx"
        style="display:none"
        bind:this={fileInputEl}
        onchange={handleFileSelected}
      />
      <button
        class="btn-sm btn-file"
        onclick={handleExport}
        title="Export current patch as .syx file"
      >
        Export .syx
      </button>
      <button
        class="btn-sm btn-file"
        onclick={handleImportClick}
        title="Import patch from .syx file"
      >
        Import .syx
      </button>
      <button
        class="btn-danger"
        class:btn-confirm={confirmPending === 0}
        onclick={() => handleReset(0)}
        disabled={!isConnected}
        title={isConnected
          ? "Reset live state to factory defaults"
          : "Connect MIDI or Serial"}
      >
        {confirmPending === 0 ? "Confirm Reset?" : "Defaults Reset"}
      </button>
    </div>
  </div>

  {#if importError}
    <div class="import-error">{importError}</div>
  {/if}

  {#if isMidiConnected && !isSerialConnected}
    <div class="sync-note">
      MIDI connected — UI synced on connect. Import also sends to device.
    </div>
  {:else if isSerialConnected && !isMidiConnected}
    <div class="sync-note">
      Serial connected — UI synced on connect. Import updates UI only (MIDI
      needed to send to device).
    </div>
  {/if}

  <div class="slot-list">
    {#each $presets as slot}
      <div class="preset-slot" class:live-slot={slot.slot === 0}>
        <div class="slot-left">
          <span class="slot-num">{slot.slot === 0 ? "●" : slot.slot}</span>
          {#if editingSlot === slot.slot}
            <input
              class="name-input"
              type="text"
              bind:value={editName}
              onblur={() => commitRename(slot.slot)}
              onkeydown={(e) => handleRenameKey(e, slot.slot)}
              maxlength={24}
              use:focusOnMount
            />
          {:else}
            <!-- svelte-ignore a11y_no_static_element_interactions -->
            <span
              class="slot-name"
              ondblclick={() => slot.slot !== 0 && startRename(slot)}
              title={slot.slot !== 0
                ? "Double-click to rename"
                : "Auto-saved live state"}>{slot.name}</span
            >
          {/if}
          {#if slot.savedAt}
            <span class="slot-date"
              >{new Date(slot.savedAt).toLocaleTimeString()}</span
            >
          {/if}
        </div>

        <div class="slot-actions">
          {#if slot.slot !== 0}
            <button
              class="btn-sm"
              onclick={() => handleSave(slot.slot)}
              disabled={!isConnected}
              title={isConnected
                ? "Save current state to this slot"
                : "Connect MIDI or Serial"}>Save</button
            >
          {/if}
          <button
            class="btn-sm btn-load"
            onclick={() => handleLoad(slot.slot)}
            disabled={!isConnected}
            title={isConnected ? "Load this preset" : "Connect MIDI or Serial"}
            >Load</button
          >
          {#if slot.slot !== 0}
            <button
              class="btn-sm btn-danger-sm"
              class:btn-confirm={confirmPending === slot.slot}
              onclick={() => handleReset(slot.slot)}
              disabled={!isConnected}
              title={isConnected
                ? "Reset this slot to defaults"
                : "Connect MIDI or Serial"}
              >{confirmPending === slot.slot ? "Sure?" : "✕"}</button
            >
          {/if}
        </div>
      </div>
    {/each}
  </div>
</div>

<style>
  .preset-manager {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    background: #1a1a2e;
    border: 1px solid #333;
    border-radius: 8px;
    padding: 0.75rem;
    min-width: 300px;
  }
  .pm-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 0.25rem;
  }
  .pm-header-actions {
    display: flex;
    gap: 0.35rem;
    align-items: center;
    flex-wrap: wrap;
    justify-content: flex-end;
  }
  .pm-title {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #888;
    letter-spacing: 0.06em;
  }
  .import-error {
    font-size: 0.7rem;
    color: #cf7f7f;
    background: #3a1a1a;
    border: 1px solid #703030;
    border-radius: 4px;
    padding: 0.25rem 0.5rem;
  }
  .sync-note {
    font-size: 0.65rem;
    color: #6a9f8a;
    background: #0d2018;
    border: 1px solid #1a4030;
    border-radius: 4px;
    padding: 0.2rem 0.5rem;
    line-height: 1.4;
  }
  .btn-file {
    border-color: #2a4a6a;
    color: #7ab8df;
  }
  .btn-file:hover:not(:disabled) {
    background: #1a2a4a;
  }
  .slot-list {
    display: flex;
    flex-direction: column;
    gap: 0.25rem;
  }
  .preset-slot {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 0.3rem 0.5rem;
    border-radius: 5px;
    background: #14142a;
    border: 1px solid #2a2a42;
    gap: 0.5rem;
  }
  .live-slot {
    border-color: #3a3a60;
    background: #1c1c3a;
  }
  .slot-left {
    display: flex;
    align-items: center;
    gap: 0.4rem;
    flex: 1;
    min-width: 0;
    overflow: hidden;
  }
  .slot-num {
    font-size: 0.75rem;
    color: #555;
    min-width: 1rem;
    text-align: center;
  }
  .slot-name {
    font-size: 0.8rem;
    color: #ccc;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    cursor: default;
  }
  .slot-date {
    font-size: 0.65rem;
    color: #555;
    margin-left: auto;
    white-space: nowrap;
  }
  .name-input {
    font-size: 0.8rem;
    background: #252545;
    color: #eee;
    border: 1px solid #7cb8ff;
    border-radius: 3px;
    padding: 0.1rem 0.3rem;
    flex: 1;
    min-width: 0;
  }
  .slot-actions {
    display: flex;
    gap: 0.25rem;
    flex-shrink: 0;
  }
  .btn-sm {
    font-size: 0.7rem;
    padding: 0.15rem 0.4rem;
    border-radius: 3px;
    border: 1px solid #444;
    background: #252542;
    color: #aab;
    cursor: pointer;
  }
  .btn-sm:hover:not(:disabled) {
    background: #35357a;
  }
  .btn-sm:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }
  .btn-load {
    border-color: #2a5a3a;
    color: #6fcf9f;
  }
  .btn-load:hover:not(:disabled) {
    background: #1a3a2a;
  }
  .btn-danger-sm {
    border-color: #5a2a2a;
    color: #cf7f7f;
  }
  .btn-danger-sm:hover:not(:disabled) {
    background: #3a1a1a;
  }
  .btn-danger {
    font-size: 0.7rem;
    padding: 0.2rem 0.6rem;
    border-radius: 4px;
    border: 1px solid #703030;
    background: #2a1010;
    color: #cf6f6f;
    cursor: pointer;
  }
  .btn-danger:hover:not(:disabled) {
    background: #3d1a1a;
  }
  .btn-danger:disabled {
    opacity: 0.35;
    cursor: not-allowed;
  }
  .btn-confirm {
    border-color: #b06020 !important;
    background: #3a2000 !important;
    color: #ffb84d !important;
    animation: pulse-confirm 0.6s ease-in-out infinite alternate;
  }
  @keyframes pulse-confirm {
    from {
      opacity: 0.8;
    }
    to {
      opacity: 1;
    }
  }
</style>
