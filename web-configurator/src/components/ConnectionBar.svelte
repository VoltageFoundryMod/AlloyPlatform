<script lang="ts">
  /**
   * ConnectionBar — shows MIDI + Serial connection status and port selectors.
   */
  import { onMount } from "svelte";
  import {
    activeModule,
    nextModule,
    MODULE_LIST,
    type ModuleInfo,
  } from "../lib/activeModule";
  import { midi } from "../lib/midi";
  import { serial } from "../lib/serial";

  interface Props {
    /**
     * Show another module's interface. The same call the discovery probe makes
     * when a module identifies itself, so a preview lands in exactly the state
     * a real connection would. App owns it because switching has to clear the
     * CC-keyed values belonging to the outgoing module — the same CC means
     * something different on the other one.
     */
    onPreviewModule: (info: ModuleInfo) => void;
  }
  let { onPreviewModule }: Props = $props();

  // The badge is only a control while nothing has answered. Once a module
  // identifies itself the name is a reading rather than a guess, and letting a
  // click contradict the hardware would make it a worse badge than it is a
  // switch. A module answering mid-preview overrides it the same way — the
  // discovery handler in App calls the same switch.
  const canPreview = $derived(!$midi.moduleAnswered && MODULE_LIST.length > 1);
  const previewTarget = $derived(nextModule($activeModule));

  const sysexSig = $derived(
    $activeModule.sysexDev.map((b) => b.toString(16).toUpperCase()).join(" "),
  );

  const badgeTitle = $derived(
    `Module: ${$activeModule.name}\nSysEx signature: ${sysexSig}\n\n` +
      ($midi.moduleAnswered
        ? "Confirmed — this module answered the discovery probe."
        : "Last module seen. Nothing has answered on this port, so these controls are a guess.") +
      (canPreview
        ? `\n\nClick to preview the ${previewTarget.name} interface.`
        : ""),
  );

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
  /**
   * Held true for a beat after a click so the button visibly does something.
   *
   * A rescan that turns up the same ports changes nothing on screen, and with
   * no acknowledgement at all the button reads as broken precisely when the
   * user is leaning on it hardest — right after a re-flash, when they are
   * already unsure whether the page or the module is at fault.
   */
  let refreshing = $state(false);

  async function refreshMidi() {
    if (refreshing) return;
    refreshing = true;
    try {
      await midi.scan();
      midi.resync();
      // A flash drops both links at once, so the serial side gets a nudge from
      // the same click. It reuses the already-granted port, so there is no
      // picker dialog and nothing happens if serial is already up.
      if ($serial.supported) await serial.autoConnect();
    } finally {
      setTimeout(() => (refreshing = false), 600);
    }
  }

  async function connectSerial() {
    await serial.connect();
  }

  /**
   * Retake a port this page has already been granted — no picker.
   *
   * Separate from Connect because requestPort() puts a dialog in front of the
   * user, and after a re-flash the permission is still there: the port just
   * needs opening again. Auto-reconnect normally gets there first; this is the
   * manual way when it has run out of retries.
   */
  async function retrySerial() {
    await serial.autoConnect();
  }

  /**
   * Web Bluetooth, Web MIDI and Web Serial are all **secure-context** APIs.
   *
   * On a plain-HTTP origin the browser does not merely refuse them — it does
   * not define them at all, so every feature-detect in this file reports "not
   * supported" on a browser that supports them perfectly well. `make web-host`
   * serves `http://<LAN-IP>:5173`, which is exactly that case, and is how
   * anyone reaches the page from a phone. Telling someone on Android Chrome to
   * "use Chrome/Edge" is the worst possible advice at that moment.
   *
   * Not reactive: an origin cannot become secure while the page is open.
   */
  const insecureOrigin =
    typeof window !== "undefined" && !window.isSecureContext;

  // --- Bluetooth (M78c) ------------------------------------------------------
  // Connect has to run straight off the click: Web Bluetooth only opens its
  // chooser from a user gesture, so there is no auto-connect and no retry loop
  // to write here — which is also why there is no pairing dialog to sit through.
  let btBusy = $state(false);

  async function connectBluetooth() {
    if (btBusy) return;
    btBusy = true;
    try {
      await midi.connectBluetooth();
    } finally {
      btBusy = false;
    }
  }
</script>

<!-- Whether a *module* has answered, as opposed to whether a link exists.
     Rendered by whichever transport is currently sending — see the call sites.
     It is a snippet rather than duplicated markup because the four states and
     their explanations are the same question over any wire. -->
<!-- Why an API is missing, when the reason is the origin rather than the
     browser. Same badge for all three sections: one cause, one fix. -->
{#snippet needsHttps(api: string)}
  <span
    class="badge warn"
    title="{api} needs a secure context, and this page is on plain HTTP — so the browser does not expose it at all. Nothing is wrong with the browser.

Fixes, easiest first:
  • Chrome DevTools on the desktop → chrome://inspect/#devices → Port forwarding → 5173. The phone then loads http://localhost:5173, which counts as secure.
  • chrome://flags/#unsafely-treat-insecure-origin-as-secure on the phone, with this page's origin added.
  • Serve the page over HTTPS."
    >Needs HTTPS</span
  >
{/snippet}

{#snippet linkState()}
  {#if $midi.moduleAnswered}
    <span
      class="badge ok"
      title="{$midi.moduleName} answered the discovery probe on this link. Refresh confirms it is still there."
      >{$midi.moduleName} responding</span
    >
  {:else if $midi.probing}
    <span
      class="badge probing"
      title="Link open, asking what is on it. A module normally answers within a second."
      >Looking for a module…</span
    >
  {:else if $midi.probeFailed}
    <span
      class="badge warn"
      title="The link is open and this page is sending, but nothing has answered.
Usual causes: Rack's MIDI output is not set to this port, the module is still booting, or this is the wrong port.
Asking again every 5 s — it will pick up on its own once something answers."
      >Link open, no module answering</span
    >
  {:else}
    <span class="badge">Idle</span>
  {/if}
{/snippet}

<header class="connection-bar">
  <img src="/AlloyFlux_Logo.svg" alt="Logo" class="logo-img" width="50px" />
  <!-- Which module the controls belong to. Detected from the SysEx signature
       in the module's patch dump, so this follows whatever is actually on the
       port; with nothing connected it is the last module seen. Worth a glance
       before wondering why a knob does nothing.

       With nothing on the port it is also the way to change that guess: both
       modules' parameter tables and panel layouts are in the bundle, so the
       page can show either one without hardware. That makes the badge a
       preview switch for anyone reading the docs, choosing between modules, or
       working on the panel itself. -->
  <div class="conn-title">Alloy Controller</div>
  {#if canPreview}
    <button
      type="button"
      class="module-badge swap"
      title={badgeTitle}
      aria-label="Module: {$activeModule.name}. Preview the {previewTarget.name} interface."
      onclick={() => onPreviewModule(previewTarget)}
    >
      {$activeModule.name}
      <span class="swap-glyph" aria-hidden="true">⇄</span>
    </button>
  {:else}
    <span
      class="module-badge"
      class:detected={$midi.moduleAnswered}
      title={badgeTitle}
    >
      {$activeModule.name}
    </span>
  {/if}
  <!-- MIDI -->
  <div class="conn-section">
    <span class="conn-label">MIDI</span>
    {#if !$midi.supported}
      {#if insecureOrigin}
        {@render needsHttps("Web MIDI")}
      {:else}
        <span class="badge error">Not supported</span>
      {/if}
    {:else if !$midi.scanned}
      <!-- Not yet scanned — only shown briefly before onMount scan completes -->
      <button onclick={scanMidi}>Scan for MIDI Devices</button>
    {:else if $midi.outputs.length === 0}
      <span class="badge warn">No devices found</span>
      <button onclick={refreshMidi} disabled={refreshing} title="Refresh device list"
        >{refreshing ? "Refreshing…" : "Refresh"}</button
      >
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
      <!-- The link-state badge describes whichever transport is sending, so it
           renders in that transport's section and nowhere else. Showing it here
           while the page is on Bluetooth would read as a claim about the USB
           port, which is precisely the conflation this badge exists to undo. -->
      {#if $midi.transport === "webmidi"}
        {@render linkState()}
      {:else}
        <span
          class="badge"
          title="Sends are going over Bluetooth. This port is still listening — inbound MIDI is never filtered by port."
          >Bluetooth active</span
        >
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
        disabled={refreshing}
        title="Refresh the MIDI port list, then ask the module to identify itself again — a live test of the link, without reloading the page. Also retries the serial link, which drops alongside MIDI when the module is re-flashed."
        >{refreshing ? "Refreshing…" : "Refresh"}</button
      >
    {/if}
    {#if $midi.error}
      <span class="badge error">{$midi.error}</span>
    {/if}
  </div>

  <!-- Bluetooth (M78c) — a third way to reach the module, alongside USB MIDI
       and the serial console, and the only one that works from a phone.
       Chrome on Android has no Web Serial and no dependable route from Web MIDI
       to a BLE peripheral, so the page goes at the GATT service directly.

       Hold MODE on the module for ~3 s first: it advertises for 60 s and is
       invisible the rest of the time, which is what keeps a rack on a stage
       from being discoverable all night. There is no PIN — the module pairs
       with no bonding at all. -->
  <div class="conn-section">
    <span class="conn-label">BT</span>
    {#if !$midi.bleSupported}
      {#if insecureOrigin}
        {@render needsHttps("Web Bluetooth")}
      {:else}
        <span
          class="badge error"
          title="Web Bluetooth is Chrome/Edge only. Firefox and everything on iOS ship no Web Bluetooth, Web MIDI or Web Serial — an iPad can still play the module through any BLE MIDI app, it just cannot run this page."
          >Not supported</span
        >
      {/if}
    {:else if $midi.transport === "ble"}
      {@render linkState()}
      <span class="badge ok" title="Connected over Bluetooth LE MIDI."
        >{$midi.bleDeviceName ?? "BLE device"}</span
      >
      <button
        onclick={() => midi.disconnectBluetooth()}
        title="Drop the Bluetooth link and go back to sending over the selected MIDI port."
        >Disconnect</button
      >
    {:else}
      <button
        onclick={connectBluetooth}
        disabled={btBusy}
        title="Open the browser's device chooser. Hold MODE on the module for ~3 s first so it is advertising — no PIN, no OS pairing."
        >{btBusy ? "Connecting…" : "Connect Bluetooth"}</button
      >
    {/if}
  </div>

  <!-- Serial -->
  <div class="conn-section">
    <span class="conn-label">Serial</span>
    {#if !$serial.supported}
      {#if insecureOrigin}
        {@render needsHttps("Web Serial")}
      {:else}
        <span
          class="badge warn"
          title="Web Serial is desktop Chrome/Edge only — it does not exist on Android or iOS at all. Not a problem: it is the fallback console, and everything the page needs works over MIDI."
          >Not supported (use Chrome/Edge)</span
        >
      {/if}
    {:else if $serial.connecting}
      <!-- An open attempt is in flight — usually auto-reconnect working
           through its retries after a re-flash. Saying so beats showing a
           Connect button that would race with it. -->
      <span class="badge probing" title="Opening the serial port…"
        >Connecting…</span
      >
    {:else if !$serial.connected}
      <button onclick={connectSerial}>Connect Serial</button>
      <!-- Retakes an already-granted port with no picker dialog. Auto-reconnect
           normally beats the user to it; this is the way back once it has run
           out of retries. -->
      <button
        onclick={retrySerial}
        title="Retry the port this page was already granted — no dialog">Retry</button
      >
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

  /* Phone layout (M78d). The bar already wrapped; what it did not do is wrap
     *within* a section, so a long port name dragged the whole row past the
     screen edge and took a horizontal scrollbar with it.

     Keyed on the ancestor `.app-shell.stacked` rather than a media query of its
     own. A second breakpoint here would be a second opinion about how small is
     small, and it would be wrong for one of the two modules: the real question
     is "can the panel be shown whole", which depends on that module's stage
     width. App.svelte answers it once. */
  :global(.app-shell.stacked) .connection-bar {
    gap: 0.55rem 0.9rem;
    padding: 0.5rem 0.7rem;
  }
  :global(.app-shell.stacked) .conn-section {
    flex-wrap: wrap;
  }
  /* The one control here with no natural ceiling — "Alloy Flux (loopMIDI
     Port 1)" is wider than a phone on its own. */
  :global(.app-shell.stacked) select {
    max-width: 46vw;
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
    /* Set explicitly because the swap variant is a <button>, which does not
       inherit the page font. */
    font-family: inherit;
    line-height: 1.5;
  }
  /* Muted until a module has actually identified itself: with nothing on the
     port the name is only the last one seen, and it should not look like a
     statement about what is plugged in. */
  .module-badge:not(.detected) {
    border-color: var(--hairline-strong);
    background: var(--bg-raised);
    color: var(--text-faint);
  }
  /* Only the swap variant is interactive, so only it advertises it. It is
     always in the muted state — it exists precisely when nothing has answered
     — so hover lights it copper, which reads as "this is live" against a
     resting badge that deliberately does not. */
  .module-badge.swap {
    display: inline-flex;
    align-items: center;
    gap: 0.3rem;
    cursor: pointer;
  }
  .module-badge.swap:hover,
  .module-badge.swap:focus-visible {
    border-color: var(--copper-deep);
    background: rgba(192, 137, 74, 0.14);
    color: var(--copper-bright);
  }
  .module-badge.swap:focus-visible {
    outline: 2px solid var(--copper);
    outline-offset: 2px;
  }
  .swap-glyph {
    font-size: 0.85em;
    opacity: 0.7;
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
  /* The refresh buttons latch disabled for a beat after a click so the label
     change is legible; without this the hover tint makes it look live. */
  button:disabled,
  button:disabled:hover {
    opacity: 0.55;
    background: var(--bg-raised);
    cursor: default;
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
