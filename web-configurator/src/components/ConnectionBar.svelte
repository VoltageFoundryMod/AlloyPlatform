<script lang="ts">
  /**
   * ConnectionBar — shows MIDI + Serial connection status and port selectors.
   */
  import { onMount } from "svelte";
  import { activeModule } from "../lib/activeModule";
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
  <img src="/AlloyFlux_Logo.svg" alt="Logo" class="logo-img" width="50px" />
  <!-- Which module the controls belong to. Detected from the SysEx signature
       in the module's patch dump, so this follows whatever is actually on the
       port; with nothing connected it is the last module seen. Worth a glance
       before wondering why a knob does nothing. -->
  <div class="conn-title">{$activeModule.name} Web Configurator</div>
  <span
    class="module-badge"
    class:detected={$midi.moduleAnswered}
    title="Module: {$activeModule.name}
SysEx signature: {$activeModule.sysexDev
      .map((b) => b.toString(16).toUpperCase())
      .join(' ')}

{$midi.moduleAnswered
      ? 'Confirmed — this module answered the discovery probe.'
      : 'Last module seen. Nothing has answered on this port, so these controls are a guess.'}"
  >
    {$activeModule.name}
  </span>
  <!-- MIDI -->
  <div class="conn-section">
    <span class="conn-label">MIDI</span>
    {#if !$midi.supported}
      <span class="badge error">Not supported</span>
    {:else if !$midi.scanned}
      <!-- Not yet scanned — only shown briefly before onMount scan completes -->
      <button onclick={scanMidi}>Scan for MIDI Devices</button>
    {:else if $midi.outputs.length === 0}
      <span class="badge warn">No devices found</span>
      <button onclick={scanMidi} title="Refresh device list">Rescan</button>
    {:else}
      <!-- Dropdown + Rescan, no disconnect.  There is nothing useful a manual
           disconnect does here: a port either exists or it does not, and
           choosing where to send is the only real decision.  The ✕ used to
           drop into a state whose only escape was Rescan, which just re-ran
           auto-selection — so it could strand the user on the wrong port.

           The badge reports whether a module has *answered*, not whether a
           port exists.  Those are different facts and the old single
           "Connected" conflated them: a virtual port with nothing behind it, a
           stale port left after a re-flash, and Rack with its MIDI output
           unset all read as connected while the page was talking to nobody. -->
      {#if $midi.moduleAnswered}
        <span
          class="badge ok"
          title="{$midi.moduleName} answered the discovery probe on this port. Recheck confirms it is still there."
          >{$midi.moduleName} responding</span
        >
      {:else if $midi.probing}
        <span
          class="badge probing"
          title="Port open, asking what is on it. A module normally answers within a second."
          >Looking for a module…</span
        >
      {:else if $midi.probeFailed}
        <span
          class="badge warn"
          title="The port is open and this page is sending, but nothing has answered.
Usual causes: Rack's MIDI output is not set to this port, the module is still booting, or this is the wrong port.
Asking again every 5 s — it will pick up on its own once something answers."
          >Port open, no module answering</span
        >
      {:else}
        <span class="badge">Idle</span>
      {/if}
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
        onclick={() => midi.resync()}
        title="Ask the module to identify itself again — a live test of the link, without reloading the page"
        >Recheck</button
      >
      <button onclick={scanMidi} title="Refresh device list">Rescan</button>
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
  /* Deliberately loud. It marks which firmware this page can talk to at all,
     so it has to survive a glance rather than blend into the bar. */
  .module-badge {
    font-size: 0.7rem;
    font-weight: 700;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    padding: 0.2rem 0.5rem;
    border-radius: 999px;
    border: 1px solid #4a7fb5;
    background: #1b2c3e;
    color: #8fc0f0;
    white-space: nowrap;
    cursor: help;
  }
  /* Muted until a module has actually identified itself: with nothing on the
     port the name is only the last one seen, and it should not look like a
     statement about what is plugged in. */
  .module-badge:not(.detected) {
    border-color: #3a4553;
    background: #1a1f28;
    color: #7d8894;
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
    background: #252535;
    color: #8a8a9a;
    cursor: help;
  }
  /* Deliberately not green: the port is open and we are asking, but nothing
     has confirmed it is there yet, and the badge should not imply otherwise. */
  .badge.probing {
    background: #1a2c3d;
    color: #6fa8cf;
  }
  @media (prefers-reduced-motion: no-preference) {
    .badge.probing {
      animation: badge-pulse 1.4s ease-in-out infinite;
    }
  }
  @keyframes badge-pulse {
    50% {
      opacity: 0.55;
    }
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
