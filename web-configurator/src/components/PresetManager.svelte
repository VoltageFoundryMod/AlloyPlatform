<script lang="ts">
  /**
   * PresetManager — 10-slot preset panel (slot 0 = live state, 1–9 = user presets).
   */
  import { presets, type PresetSlot } from "../lib/presets";
  import { serial } from "../lib/serial";

  /** Svelte action: focus element on mount */
  function focusOnMount(node: HTMLElement) {
    node.focus();
  }

  let editingSlot = $state<number | null>(null);
  let editName = $state("");

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

  async function handleReset(slot: number | "all") {
    const label = slot === "all" ? "all presets" : slot === 0 ? "live state" : `preset ${slot}`;
    if (!confirm(`Reset ${label}? This cannot be undone.`)) return;
    presets.reset(slot);
  }

  const isSerialConnected = $derived($serial.connected);
</script>

<div class="preset-manager">
  <div class="pm-header">
    <span class="pm-title">Presets</span>
    <button class="btn-danger" onclick={() => handleReset("all")} disabled={!isSerialConnected}>
      Factory Reset
    </button>
  </div>

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
              title={slot.slot !== 0 ? "Double-click to rename" : "Auto-saved live state"}
            >{slot.name}</span>
          {/if}
          {#if slot.savedAt}
            <span class="slot-date">{new Date(slot.savedAt).toLocaleTimeString()}</span>
          {/if}
        </div>

        <div class="slot-actions">
          {#if slot.slot !== 0}
            <button
              class="btn-sm"
              onclick={() => handleSave(slot.slot)}
              disabled={!isSerialConnected}
              title="Save current state to this slot"
            >Save</button>
          {/if}
          <button
            class="btn-sm btn-load"
            onclick={() => handleLoad(slot.slot)}
            disabled={!isSerialConnected}
            title="Load this preset"
          >Load</button>
          {#if slot.slot !== 0}
            <button
              class="btn-sm btn-danger-sm"
              onclick={() => handleReset(slot.slot)}
              disabled={!isSerialConnected}
              title="Reset this slot to defaults"
            >✕</button>
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
  .pm-title {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #888;
    letter-spacing: 0.06em;
  }
  .slot-list { display: flex; flex-direction: column; gap: 0.25rem; }
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
  .live-slot { border-color: #3a3a60; background: #1c1c3a; }
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
  .slot-date { font-size: 0.65rem; color: #555; margin-left: auto; white-space: nowrap; }
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
  .slot-actions { display: flex; gap: 0.25rem; flex-shrink: 0; }
  .btn-sm {
    font-size: 0.7rem;
    padding: 0.15rem 0.4rem;
    border-radius: 3px;
    border: 1px solid #444;
    background: #252542;
    color: #aab;
    cursor: pointer;
  }
  .btn-sm:hover:not(:disabled) { background: #35357a; }
  .btn-sm:disabled { opacity: 0.35; cursor: not-allowed; }
  .btn-load { border-color: #2a5a3a; color: #6fcf9f; }
  .btn-load:hover:not(:disabled) { background: #1a3a2a; }
  .btn-danger-sm { border-color: #5a2a2a; color: #cf7f7f; }
  .btn-danger-sm:hover:not(:disabled) { background: #3a1a1a; }
  .btn-danger {
    font-size: 0.7rem;
    padding: 0.2rem 0.6rem;
    border-radius: 4px;
    border: 1px solid #703030;
    background: #2a1010;
    color: #cf6f6f;
    cursor: pointer;
  }
  .btn-danger:hover:not(:disabled) { background: #3d1a1a; }
  .btn-danger:disabled { opacity: 0.35; cursor: not-allowed; }
</style>
