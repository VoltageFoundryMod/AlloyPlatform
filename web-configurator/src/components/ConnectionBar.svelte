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

  /**
   * One button for "sort the MIDI link out".
   *
   * Rescan and Recheck were two buttons for two halves of the same question,
   * and which one you needed depended on internals — whether the port list was
   * stale (re-flash, hub replug) or the port was fine and the module had not
   * answered (Rack not started yet). Doing both in order covers either case:
   * refresh the port list first, then re-run the discovery probe against
   * whatever that turned up.
   */
  async function refreshMidi() {
    await midi.scan();
    midi.resync();
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
  <div class="conn-title">Alloy Controller</div>
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
      <button onclick={refreshMidi} title="Refresh device list">Refresh</button>
    {:else}
      <!-- Dropdown + Refresh, no disconnect.  There is nothing useful a manual
           disconnect does here: a port either exists or it does not, and
           choosing where to send is the only real decision.  The ✕ used to
           drop into a state whose only escape was a rescan, which just re-ran
           auto-selection — so it could strand the user on the wrong port.

           The badge reports whether a module has *answered*, not whether a
           port exists.  Those are different facts and the old single
           "Connected" conflated them: a virtual port with nothing behind it, a
           stale port left after a re-flash, and Rack with its MIDI output
           unset all read as connected while the page was talking to nobody. -->
      {#if $midi.moduleAnswered}
        <span
          class="badge ok"
          title="{$midi.moduleName} answered the discovery probe on this port. Refresh confirms it is still there."
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
        onclick={refreshMidi}
        title="Refresh the MIDI port list, then ask the module to identify itself again — a live test of the link, without reloading the page"
        >Refresh</button
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
    background: var(--bg-panel);
    border-bottom: 1px solid var(--hairline);
    flex-wrap: wrap;
  }
  .conn-section {
    display: flex;
    align-items: center;
    gap: 0.5rem;
  }
  /* Set as a wordmark rather than a page heading: the panel below is the
     subject, and a 1.5rem title was competing with it for the top of the
     screen. */
  .conn-title {
    font-size: 0.92rem;
    font-weight: 600;
    letter-spacing: 0.19em;
    text-transform: uppercase;
    color: var(--text);
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
    border: 1px solid var(--copper-deep);
    background: rgba(192, 137, 74, 0.14);
    color: var(--copper-bright);
    white-space: nowrap;
    cursor: help;
  }
  /* Muted until a module has actually identified itself: with nothing on the
     port the name is only the last one seen, and it should not look like a
     statement about what is plugged in. */
  .module-badge:not(.detected) {
    border-color: var(--hairline-strong);
    background: var(--bg-raised);
    color: var(--text-faint);
  }
  .conn-label {
    font-size: 0.75rem;
    font-weight: 600;
    text-transform: uppercase;
    color: var(--text-dim);
    min-width: 3rem;
  }
  .badge {
    font-size: 0.75rem;
    padding: 0.15rem 0.5rem;
    border-radius: 999px;
    background: var(--bg-raised);
    color: var(--text-dim);
    cursor: help;
  }
  /* Deliberately not green: the port is open and we are asking, but nothing
     has confirmed it is there yet, and the badge should not imply otherwise. */
  .badge.probing {
    background: rgba(53, 200, 216, 0.12);
    color: var(--led-3);
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
    background: rgba(88, 192, 106, 0.14);
    color: var(--ok);
  }
  .badge.error {
    background: rgba(208, 90, 82, 0.15);
    color: var(--err);
  }
  .badge.warn {
    background: rgba(224, 168, 58, 0.14);
    color: var(--warn);
  }
  select {
    background: var(--bg-sunken);
    color: var(--text);
    border: 1px solid var(--hairline-strong);
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
    background: var(--bg-raised);
    color: var(--text);
    border: 1px solid var(--hairline-strong);
  }
  button:hover {
    background: rgba(192, 137, 74, 0.16);
  }
  .btn-disconnect {
    background: transparent;
    border: 1px solid var(--hairline-strong);
    color: var(--text-faint);
    padding: 0.15rem 0.45rem;
    font-size: 0.75rem;
    line-height: 1;
    border-radius: 4px;
    cursor: pointer;
  }
  .btn-disconnect:hover {
    background: rgba(208, 90, 82, 0.15);
    color: var(--err);
  }
</style>
