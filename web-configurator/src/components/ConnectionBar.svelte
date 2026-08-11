<script lang="ts">
  /**
   * ConnectionBar — shows MIDI + Serial connection status and port selectors.
   */
  import { onMount } from "svelte";
  import { midi } from "../lib/midi";
  import { serial } from "../lib/serial";

  // On page load: scan MIDI (uses cached permission — no prompt if already granted)
  // and silently reconnect any previously-granted serial port.
  onMount(async () => {
    if ($midi.supported) await midi.scan();
    if ($serial.supported) await serial.autoConnect();
  });

  async function scanMidi() {
    await midi.scan();
  }

  async function connectSerial() {
    await serial.connect();
  }
</script>

<header class="connection-bar">
  <img src="/VFM_Logo_Stripped.png" alt="Logo" class="logo-img" width="50px" />
  <div class="conn-title">Alloy Flux Web Configurator</div>
  <!-- MIDI -->
  <div class="conn-section">
    <span class="conn-label">MIDI</span>
    {#if !$midi.supported}
      <span class="badge error">Not supported</span>
    {:else if !$midi.scanned}
      <!-- Not yet scanned — only shown briefly before onMount scan completes -->
      <button onclick={scanMidi}>Scan for MIDI Devices</button>
    {:else if !$midi.deviceConnected}
      <!-- Scanned but no port available -->
      {#if $midi.outputs.length === 0}
        <span class="badge warn">No devices found</span>
      {:else}
        <!-- Ports exist but none currently connected (e.g. device just unplugged) -->
        <select
          value={$midi.selectedOutput}
          onchange={(e) =>
            midi.selectOutput((e.target as HTMLSelectElement).value)}
        >
          {#each $midi.outputs as port}
            <option value={port.id}>{port.name}</option>
          {/each}
        </select>
      {/if}
      <button onclick={scanMidi} title="Refresh device list">Rescan</button>
    {:else}
      <!-- Connected — show status + port switcher -->
      <span class="badge ok">Connected</span>
      <select
        value={$midi.selectedOutput}
        title="MIDI output — the port this page sends on. For VCV Rack, pick the same virtual port Rack's MIDI input is set to (loopMIDI / IAC). Incoming MIDI is received on every port regardless."
        onchange={(e) =>
          midi.selectOutput((e.target as HTMLSelectElement).value)}
      >
        {#each $midi.outputs as port}
          <option value={port.id}>{port.name}</option>
        {/each}
      </select>
      <!-- TX/RX byte counters live on the MIDI Monitor tab at the bottom of
           the page, next to the traffic they describe. -->
      <button
        class="btn-disconnect"
        onclick={() => midi.disconnect()}
        title="Disconnect MIDI">✕</button
      >
    {/if}
    {#if $midi.error}
      <span class="badge error">{$midi.error}</span>
    {/if}
  </div>

  <!-- Serial -->
  <div class="conn-section">
    <span class="conn-label">Serial</span>
    {#if !$serial.supported}
      <span class="badge warn">Not supported (use Chrome/Edge)</span>
    {:else if !$serial.connected}
      <button onclick={connectSerial}>Connect Serial</button>
    {:else}
      <span class="badge ok">Connected</span>
      <button
        class="btn-disconnect"
        onclick={() => serial.disconnect()}
        title="Disconnect Serial">✕</button
      >
    {/if}
    {#if $serial.error}
      <span class="badge error">{$serial.error}</span>
    {/if}
  </div>
</header>

<style>
  .connection-bar {
    display: flex;
    gap: 1.5rem;
    align-items: center;
    padding: 0.5rem 1rem;
    background: #1a1a2e;
    border-bottom: 1px solid #333;
    flex-wrap: wrap;
  }
  .conn-section {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  .conn-title {
    font-size: 1.5rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #888;
    min-width: 3rem;
  }
  .conn-label {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: #888;
    min-width: 3rem;
  }
  .badge {
    font-size: 0.75rem;
    padding: 0.15rem 0.5rem;
    border-radius: 999px;
  }
  .badge.ok {
    background: #1a3d1a;
    color: #6fcf6f;
  }
  .badge.error {
    background: #3d1a1a;
    color: #cf6f6f;
  }
  .badge.warn {
    background: #3d3010;
    color: #cfb86f;
  }
  select {
    background: #252540;
    color: #ccc;
    border: 1px solid #444;
    border-radius: 4px;
    padding: 0.2rem 0.4rem;
    font-size: 0.8rem;
    max-width: 200px;
  }
  button {
    font-size: 0.8rem;
    padding: 0.25rem 0.75rem;
    cursor: pointer;
    border-radius: 4px;
    background: #2a2a50;
    color: #aab;
    border: 1px solid #555;
  }
  button:hover {
    background: #3a3a70;
  }
  .btn-disconnect {
    background: transparent;
    border: 1px solid #554;
    color: #a66;
    padding: 0.15rem 0.45rem;
    font-size: 0.75rem;
    line-height: 1;
    border-radius: 4px;
    cursor: pointer;
  }
  .btn-disconnect:hover {
    background: #3d1a1a;
    color: #cf6f6f;
  }
</style>
