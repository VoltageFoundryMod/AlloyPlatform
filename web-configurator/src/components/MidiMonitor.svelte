<script lang="ts">
  /**
   * MidiMonitor — a drawer showing decoded MIDI traffic in both directions.
   *
   * Mirrors the serial console drawer, but for the MIDI transport. Every
   * message is tagged with its direction and the port it went to or came from,
   * which is what makes the common wiring mistakes visible: traffic arriving
   * but none leaving, or leaving on a port nothing is listening to.
   *
   * The traffic tap in midi.ts is only subscribed while this drawer is open —
   * parameter feedback can run to hundreds of messages a second, and there is
   * no reason to pay for that when the panel is closed.
   */
  import { midi, type MidiTrafficEvent } from "../lib/midi";
  import { decodeMidi, toHex } from "../lib/midiDecode";

  interface Row {
    id: number;
    dir: "in" | "out";
    ms: number; // milliseconds since the monitor was opened
    port: string;
    hex: string;
    kind: string;
    detail: string;
    channel: number | null;
  }

  const MAX_ROWS = 500; // ring cap — a knob sweep produces a lot of CC
  const FLUSH_MS = 100; // batch UI updates rather than one per message

  let open = $state(false);
  let paused = $state(false);
  let showIn = $state(true);
  let showOut = $state(true);
  let showHex = $state(false);
  // Message classes. SysEx defaults off: the configurator re-requests a patch
  // dump on every reconnect edge, so REQUEST_DUMP / PATCH_DUMP pairs are the
  // noisiest thing on the wire and rarely what you are looking at.
  let showCC = $state(true);
  let showNotes = $state(true);
  let showSysEx = $state(false);
  let showOther = $state(true);
  /** Comma-separated substrings; a row matching any of them is hidden. */
  let hideText = $state("");
  let rows = $state<Row[]>([]);
  let captured = $state(0); // total seen this session, including trimmed rows
  let bodyEl = $state<HTMLElement | null>(null);

  let pending: Row[] = [];
  let nextId = 0;

  /** Which checkbox governs a decoded message type. */
  function messageClass(kind: string): "cc" | "note" | "sysex" | "other" {
    if (kind === "CC") return "cc";
    if (kind === "SysEx") return "sysex";
    if (
      kind === "Note On" ||
      kind === "Note Off" ||
      kind === "Aftertouch" ||
      kind === "Ch Pressure" ||
      kind === "Pitch Bend"
    )
      return "note";
    return "other";
  }

  let hideTerms = $derived(
    hideText
      .split(",")
      .map((s) => s.trim().toLowerCase())
      .filter((s) => s.length > 0),
  );

  // Filtering is applied to the view, not to capture, so relaxing a filter
  // reveals history that was already recorded rather than only affecting
  // what comes next.
  let visible = $derived(
    rows.filter((r) => {
      if (!(r.dir === "in" ? showIn : showOut)) return false;
      const cls = messageClass(r.kind);
      if (cls === "cc" && !showCC) return false;
      if (cls === "note" && !showNotes) return false;
      if (cls === "sysex" && !showSysEx) return false;
      if (cls === "other" && !showOther) return false;
      if (hideTerms.length > 0) {
        const hay = `${r.kind} ${r.detail}`.toLowerCase();
        if (hideTerms.some((term) => hay.includes(term))) return false;
      }
      return true;
    }),
  );

  function flush() {
    if (pending.length === 0) return;
    const merged = [...rows, ...pending];
    pending = [];
    rows = merged.length > MAX_ROWS ? merged.slice(-MAX_ROWS) : merged;
    // Stick to the bottom; the DOM has not updated yet at this point.
    setTimeout(() => {
      if (bodyEl) bodyEl.scrollTop = bodyEl.scrollHeight;
    }, 0);
  }

  $effect(() => {
    if (!open) return;
    const t0 = performance.now();
    const unsubscribe = midi.onTraffic((e: MidiTrafficEvent) => {
      if (paused) return; // freeze capture, not just the view
      const d = decodeMidi(e.bytes);
      pending.push({
        id: nextId++,
        dir: e.dir,
        ms: e.t - t0,
        port: e.port,
        hex: toHex(e.bytes),
        kind: d.kind,
        detail: d.detail,
        channel: d.channel,
      });
      captured++;
    });
    const timer = setInterval(flush, FLUSH_MS);
    return () => {
      unsubscribe();
      clearInterval(timer);
    };
  });

  function clear() {
    pending = [];
    rows = [];
    captured = 0;
    // Zero the byte totals too, so Clear establishes a baseline: do a thing,
    // watch whether either counter moves.
    midi.resetCounters();
  }

  function stamp(ms: number): string {
    return (ms / 1000).toFixed(3).padStart(8, " ");
  }

  /** Compact byte count — 0, 847, 12.3k, 1.4M. */
  function fmtBytes(n: number): string {
    if (n < 1000) return String(n);
    if (n < 1_000_000) return `${(n / 1000).toFixed(1)}k`;
    return `${(n / 1_000_000).toFixed(1)}M`;
  }
</script>

<div class="midi-drawer" class:open>
  <button
    class="midi-tab"
    onclick={() => (open = !open)}
    title="MIDI bytes sent (TX) and received (RX) since the page loaded, or since Clear. Traffic in only one direction usually means the output port is wrong or held by another app."
  >
    <span class="midi-conn-dot" class:connected={$midi.connected}></span>
    MIDI Monitor {open ? "▼" : "▲"}
    <!-- Byte totals stay on the tab so they are readable with the drawer shut,
         and they keep counting while it is: the traffic tap is gated on the
         drawer being open, these are not. One direction moving while the other
         sits still is the fastest read on a mis-wired port. -->
    <span class="midi-meters">
      {#if open}
        <span class="midi-count">
          {visible.length === rows.length
            ? `${captured} msg`
            : `${visible.length}/${captured} msg`}
        </span>
      {/if}
      <span class="midi-byte tx" class:idle={$midi.txBytes === 0}
        >TX {fmtBytes($midi.txBytes)}</span
      >
      <span class="midi-byte rx" class:idle={$midi.rxBytes === 0}
        >RX {fmtBytes($midi.rxBytes)}</span
      >
    </span>
  </button>
  {#if open}
    <div class="midi-toolbar">
      <span class="midi-group">
        <label><input type="checkbox" bind:checked={showIn} /> In</label>
        <label><input type="checkbox" bind:checked={showOut} /> Out</label>
      </span>
      <span class="midi-sep"></span>
      <span class="midi-group">
        <label><input type="checkbox" bind:checked={showCC} /> CC</label>
        <label><input type="checkbox" bind:checked={showNotes} /> Notes</label>
        <label><input type="checkbox" bind:checked={showSysEx} /> SysEx</label>
        <label><input type="checkbox" bind:checked={showOther} /> Other</label>
      </span>
      <span class="midi-sep"></span>
      <input
        class="midi-hide"
        type="text"
        placeholder="hide: dump, clock…"
        title="Comma-separated text; any message whose decoded line contains one of these is hidden."
        bind:value={hideText}
      />
      <label><input type="checkbox" bind:checked={showHex} /> Hex</label>
      <span class="midi-spacer"></span>
      <button
        class="midi-btn"
        class:active={paused}
        onclick={() => (paused = !paused)}
        title="Stop capturing so the list holds still">
        {paused ? "Resume" : "Pause"}
      </button>
      <button class="midi-btn" onclick={clear}>Clear</button>
    </div>
    <div class="midi-body" bind:this={bodyEl}>
      {#each visible as row (row.id)}
        <div class="midi-line" class:out={row.dir === "out"}>
          <span class="midi-time">{stamp(row.ms)}</span>
          <span class="midi-dir">{row.dir === "in" ? "◀ IN " : "OUT ▶"}</span>
          <span class="midi-kind">{row.kind}</span>
          {#if row.channel !== null}
            <span class="midi-chan">ch{row.channel}</span>
          {/if}
          <span class="midi-detail">{row.detail}</span>
          {#if showHex}
            <span class="midi-hex">{row.hex}</span>
          {/if}
          <span class="midi-port">{row.port}</span>
        </div>
      {/each}
      {#if visible.length === 0}
        <div class="midi-empty">
          {#if !$midi.connected}
            No MIDI port connected.
          {:else if captured === 0}
            Listening… move a control here or on the module.
          {:else}
            All {rows.length} captured messages are filtered out — adjust the toggles
            or the hide box above.
          {/if}
        </div>
      {/if}
    </div>
  {/if}
</div>

<style>
  /* Positioning is owned by .drawer-dock in App.svelte, which pins both
     drawers to the viewport as one stack. */
  .midi-drawer {
    background: var(--bg-panel);
    border-top: 1px solid var(--hairline);
  }
  .midi-tab {
    width: 100%;
    padding: 0.35rem 1rem;
    background: var(--bg-raised);
    border: none;
    border-bottom: 1px solid var(--hairline);
    color: var(--text-dim);
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
  .midi-tab:hover {
    background: rgba(192, 137, 74, 0.10);
    color: var(--text);
  }
  .midi-conn-dot {
    display: inline-block;
    width: 0.55rem;
    height: 0.55rem;
    border-radius: 50%;
    background: var(--hairline-strong);
    flex-shrink: 0;
    transition: background 0.3s;
  }
  .midi-conn-dot.connected {
    background: var(--ok);
    box-shadow: 0 0 5px var(--ok);
  }
  .midi-meters {
    margin-left: auto;
    display: flex;
    align-items: baseline;
    gap: 0.75rem;
    font-weight: 400;
    text-transform: none;
    letter-spacing: 0;
    font-variant-numeric: tabular-nums;
  }
  .midi-count {
    color: var(--text-faint);
  }
  .midi-byte {
    font-family: monospace;
  }
  /* Same colours the log rows use for each direction. */
  .midi-byte.tx {
    color: var(--led-4);
  }
  .midi-byte.rx {
    color: var(--led-3);
  }
  .midi-byte.idle {
    color: var(--err);
  }
  .midi-toolbar {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 0.5rem 0.75rem;
    padding: 0.3rem 0.75rem;
    background: var(--bg-panel);
    border-bottom: 1px solid var(--hairline);
    font-size: 0.72rem;
    color: var(--text-dim);
  }
  .midi-toolbar label {
    display: flex;
    align-items: center;
    gap: 0.25rem;
    cursor: pointer;
  }
  .midi-group {
    display: flex;
    align-items: center;
    gap: 0.6rem;
  }
  .midi-sep {
    width: 1px;
    align-self: stretch;
    background: var(--hairline);
  }
  .midi-hide {
    width: 11rem;
    background: var(--bg-sunken);
    border: 1px solid var(--hairline-strong);
    border-radius: 4px;
    color: var(--text);
    font-family: monospace;
    font-size: 0.72rem;
    padding: 0.15rem 0.4rem;
  }
  .midi-spacer {
    flex: 1;
  }
  .midi-btn {
    padding: 0.15rem 0.6rem;
    font-size: 0.72rem;
    background: var(--bg-sunken);
    color: var(--text-dim);
    border: 1px solid var(--hairline-strong);
    border-radius: 4px;
    cursor: pointer;
  }
  .midi-btn:hover {
    background: rgba(192, 137, 74, 0.14);
  }
  .midi-btn.active {
    background: rgba(192, 137, 74, 0.20);
    border-color: var(--copper);
    color: var(--copper-bright);
  }
  .midi-body {
    /* Capped against the viewport so both drawers open at once cannot take
       over a short screen. */
    height: min(180px, 28vh);
    overflow-y: auto;
    padding: 0.4rem 0.75rem;
    font-family: monospace;
    font-size: 0.72rem;
    background: var(--bg-sunken);
  }
  .midi-line {
    display: flex;
    gap: 0.6rem;
    line-height: 1.45;
    white-space: pre;
    color: var(--led-3);
  }
  .midi-line.out {
    color: var(--led-4);
  }
  .midi-time {
    color: var(--text-faint);
    font-variant-numeric: tabular-nums;
  }
  .midi-dir {
    opacity: 0.8;
  }
  .midi-kind {
    min-width: 5.5rem;
  }
  .midi-chan {
    color: var(--text-faint);
  }
  .midi-detail {
    color: var(--text);
    white-space: pre-wrap;
  }
  .midi-hex {
    color: var(--copper-deep);
  }
  .midi-port {
    margin-left: auto;
    color: var(--text-faint);
    padding-left: 1rem;
  }
  .midi-empty {
    color: var(--text-faint);
    font-style: italic;
  }
</style>
