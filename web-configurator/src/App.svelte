<script lang="ts">
  import {
    params,
    paramsFor,
    ccToFloat,
    floatToCC,
    type CCParam,
  } from "./lib/paramMap";
  import {
    activeModule,
    setActiveModule,
    MODULES,
    type ModuleInfo,
  } from "./lib/activeModule";
  import { midi } from "./lib/midi";
  import { serial } from "./lib/serial";
  import {
    SysexCmd,
    parseSysExBody,
    parseSerialDump,
    buildSyxBlob,
    downloadFile,
    SYSEX_DEV_BROADCAST,
    type CCPair,
  } from "./lib/patchSync";
  import ConnectionBar from "./components/ConnectionBar.svelte";
  import MidiKeyboard from "./components/MidiKeyboard.svelte";
  import PresetManager from "./components/PresetManager.svelte";
  import MidiMonitor from "./components/MidiMonitor.svelte";
  import PanelView from "./components/PanelView.svelte";
  import DockPanel from "./components/panel/DockPanel.svelte";

  // The parameter table of whichever module is on the port, following it as
  // the discovery probe identifies one. Everything below reads this rather
  // than a build-time import.
  //
  // The tables also carry PARAM_CATEGORIES / PARAMS_BY_CATEGORY, which nothing
  // reads any more: they existed for the old list view's category headings,
  // and the panel places controls by name through lib/panelLayout.ts instead.
  const PARAM_MAP = $derived($params.PARAM_MAP);

  function seedSliders(map: CCParam[]): Record<number, number> {
    return Object.fromEntries(
      map
        .filter((p) => !p.type || p.type === "slider")
        .map((p) => [p.cc, p.default]),
    );
  }

  function seedSelects(map: CCParam[]): Record<number, number> {
    return Object.fromEntries(
      map.filter((p) => p.type === "select").map((p) => [p.cc, p.default ?? 0]),
    );
  }

  // Float values, keyed by CC — only meaningful for slider-type params
  let paramValues = $state(seedSliders($params.PARAM_MAP));

  // Raw CC values for select params (needed for derived computations like chord name)
  let selectValues = $state(seedSelects($params.PARAM_MAP));

  // Signature of a module this build has no map for — see the SysEx handler.
  let unknownDev = $state<string | null>(null);

  // Utility panels in the right-hand rail. Any number can be open at once —
  // so this is a flag each rather than a single selection. The rail takes its
  // width from the panel, which re-zooms to whatever is left.
  let utility = $state({ presets: false, keyboard: false, settings: false });

  type Utility = keyof typeof utility;
  const toggleUtility = (u: Utility) => (utility[u] = !utility[u]);

  // The keyboard is deliberately not in this list. It is the one utility whose
  // natural shape is landscape — fourteen white keys and a toolbar — and in a
  // 400px rail it got 26px keys and a toolbar three rows deep. It lives in the
  // bottom dock instead, where it has the width it wants; `utility.keyboard`
  // still toggles it, so the toolbar button works either way.
  const railOpen = $derived(utility.presets || utility.settings);

  // MIDI receive channel (0=omni, 1-16). Set by CC 110 in patch dump; sent via SysEx 0x07.
  let midiChannel = $state(0);

  function sendMidiChannel(ch: number) {
    midiChannel = ch;
    if ($midi.deviceConnected)
      midi.sendSysEx(SysexCmd.SET_MIDI_CHANNEL, [ch & 0x7f]);
  }

  // Refs for pushing incoming MIDI-in to the right component. Deliberately not
  // $state: nothing renders from them, they are only called imperatively, and
  // the reassignment on a module switch is a cleanup rather than an update
  // anything needs to react to.
  // svelte-ignore non_reactive_update
  let sliderRefs: Record<number, { applyCC: (v: number) => void }> = {};
  // svelte-ignore non_reactive_update
  let selectRefs: Record<number, { applyCC: (v: number) => void }> = {};

  // ---------------------------------------------------------------------------
  // Narrow reactive keys for the MIDI effects below.
  //
  // The traffic counters (txBytes / rxBytes) live in the same store as the
  // connection state, and reading *any* field of a Svelte store subscribes to
  // the whole thing. An effect that both reads $midi and sends MIDI therefore
  // retriggers itself on its own traffic — which turned the one-shot dump
  // request into a ~345 ms poll loop, re-requesting forever. Depending on a
  // derived value instead means these only re-run when the value they actually
  // care about changes.
  // ---------------------------------------------------------------------------
  const ccStreamOn = $derived($midi.connected);
  const syncKey = $derived($midi.deviceConnected ? $midi.syncNonce : -1);
  const serialOn = $derived($serial.connected);

  // Subscribe to incoming MIDI CC messages and route to the right component.
  // This is the live feedback path: the module emits a CC whenever a parameter
  // changes on its side — a panel knob being turned, a button combo, a preset
  // recall — so the UI follows the hardware within one feedback tick (250 ms).
  $effect(() => {
    if (!ccStreamOn) return;
    const unsubscribe = midi.onCC((cc: number, value: number) => {
      // Drop our own echo, and anything arriving mid-gesture for a control the
      // user is currently working. Full syncs go through applyPatch() instead.
      if (midi.shouldIgnoreInbound(cc)) return;
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
   * Switch the UI to another module and return its tables.
   *
   * Returns even when nothing changed, so the caller can decode against the
   * tables it asked for rather than re-reading a `$derived` that has not
   * recomputed yet.
   */
  function switchModule(info: ModuleInfo) {
    const tables = paramsFor(info.id);
    if (setActiveModule(info)) {
      // Values are keyed by CC, and the same CC means something different on
      // another module — carrying them across would show one module's settings
      // under the other's labels. Refs go too: the {#key} block in the template
      // destroys every slider and select, and stale entries would otherwise be
      // written to components that no longer exist.
      paramValues = seedSliders(tables.PARAM_MAP);
      selectValues = seedSelects(tables.PARAM_MAP);
      sliderRefs = {};
      selectRefs = {};
      unknownDev = null;
    }
    return tables;
  }

  /**
   * Apply a batch of CC pairs to the UI.
   * Pass sendToDevice=true to also replay each CC to the connected MIDI output.
   * `map` defaults to the active module's, and is passed explicitly by the
   * discovery path, which decodes a dump against the map of the module that
   * just identified itself.
   */
  function applyPatch(
    pairs: CCPair[],
    sendToDevice = false,
    map: CCParam[] = PARAM_MAP,
  ) {
    for (const { cc, value } of pairs) {
      // CC 110 is the MIDI receive channel — not in PARAM_MAP, handled separately.
      if (cc === 110) {
        midiChannel = value; // 0=omni, 1-16
        continue;
      }
      const param = map.find((p) => p.cc === cc);
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
  // Auto-sync on MIDI connect — broadcast REQUEST_DUMP, identify the module
  // from the PATCH_DUMP that answers, then apply it.
  //
  // The probe is addressed to the wildcard signature 7F 7F, which every Alloy
  // module answers, so the page does not have to know what is on the port
  // beforehand — the reply's header says which module it is and the payload is
  // the patch. Detection and sync are the same round trip.
  // ---------------------------------------------------------------------------
  $effect(() => {
    // syncKey folds deviceConnected and syncNonce into one number: the store
    // bumps the nonce on every connection, reconnection and output-port switch,
    // so this still re-runs when the connected flag itself never observably
    // changed — but not on unrelated store traffic.
    if (syncKey < 0) return;

    // Retry until the module answers.  Its MIDI port enumerates as soon as USB
    // comes up, but the firmware does not service MIDI until setup() has
    // finished loading flash config and generating wavetables and the main loop
    // is running.  A single request fired into that window is simply dropped,
    // and nothing ever asks again — which is why a freshly flashed module
    // needed a page reload before the UI would populate.
    let timer: ReturnType<typeof setTimeout>;
    let attempts = 0;
    let done = false;
    // The module that answered the probe. A broadcast reaches everything on the
    // port, so two modules behind a merger both reply — the first to answer owns
    // the session and the other is ignored, rather than the two taking turns
    // rebuilding the page. Its own later dumps (a preset load) still apply.
    let ownerId: string | null = null;

    const FAST_ATTEMPTS = 8;
    const FAST_INTERVAL_MS = 800;
    const SLOW_INTERVAL_MS = 5000;

    midi.probeStarted();

    const request = () => {
      if (done) return;
      // The fast phase asks the selected port and nothing else.  Widening the
      // search is for when that port turns out to be the wrong guess, and it
      // must not happen before then: only the first answer owns the session, so
      // asking two ports at once hands the page to whichever is quicker rather
      // than to the one it is aimed at.  See broadcastSysExWide().
      if (attempts < FAST_ATTEMPTS)
        midi.sendSysEx(SysexCmd.REQUEST, [], SYSEX_DEV_BROADCAST);
      else midi.broadcastSysExWide(SysexCmd.REQUEST, [], SYSEX_DEV_BROADCAST);

      if (++attempts <= FAST_ATTEMPTS) {
        timer = setTimeout(request, FAST_INTERVAL_MS);
        return;
      }

      // Past the fast phase nothing has answered, which the UI now says out
      // loud rather than sitting on a "Connected" badge that only ever meant
      // "a port exists".
      midi.probeUnanswered();

      // Nothing has answered, and there are two different reasons that
      // happens — the port is wrong, or the module is not listening yet. Both
      // need asking again, so from here every slow cycle does both.

      // Re-enumerate. No answer usually means a stale port Chrome kept after a
      // re-flash, which still reports as connected — so the store's auto-rescan
      // never fires, because that only runs when no port is present at all, and
      // nothing else here would ever go looking for the returning port.
      //
      // This used to run once, at the fast/slow transition, and one look is not
      // enough: the module re-enumerates a few seconds after the flash and
      // routinely misses that single window. Past it the page sat sending into
      // a dead port forever, which is why recovering needed a page reload. A
      // rescan every 5 s costs nothing — the permission is already granted, so
      // it neither prompts nor shows anything — and it only runs while nothing
      // is answering. If it turns up a different port the store bumps
      // syncNonce and this effect restarts.
      void midi.scan();

      // Firmware older than the wildcard ignores a broadcast, so from here
      // also ask each known module by name. Only in the slow phase: it is
      // legacy discovery, it costs one message per module, and new firmware
      // has already answered the broadcast long before this.
      for (const m of Object.values(MODULES))
        midi.sendSysEx(SysexCmd.REQUEST, [], m.sysexDev);

      // Keep asking, slowly, for as long as the port is there. This is what
      // makes starting Rack, or flashing the module, recover on its own — the
      // old code gave up after ~6 s and the only way back was a page reload.
      timer = setTimeout(request, SLOW_INTERVAL_MS);
    };
    timer = setTimeout(request, 300);

    const unsubSysEx = midi.onSysEx((body: Uint8Array, portName: string) => {
      const patch = parseSysExBody(body);
      if (!patch || patch.pairs.length === 0) return;
      // Only a PATCH_DUMP is evidence of a device. On a loopback port every
      // APPLY_PATCH this page sends arrives back as input, and treating that as
      // a reply would have the page confirming a module that is not there.
      if (patch.cmd !== SysexCmd.DUMP) return;
      if (!patch.module) {
        // An Alloy patch dump from something this build has no map for. Keep
        // probing — a second module on the same port may still answer — but
        // say so, because the alternative is a page that silently shows the
        // wrong module's controls.
        unknownDev = patch.dev
          .map((b) => b.toString(16).toUpperCase().padStart(2, "0"))
          .join(" ");
        return;
      }
      // While the probe is running, the first answer owns the session: a
      // broadcast reaches every module on the port, so two behind a merger both
      // reply and would otherwise take turns rebuilding the page.
      //
      // Once it has settled that rule has to lift, because nothing is asking
      // any more — a dump arriving now is unsolicited, which makes it a
      // deliberate act: "Sync to web" from a module's context menu, or a preset
      // recall on the module itself. Refusing it married the page to whichever
      // module happened to answer first, and with a rack holding both an
      // AlloyFlux and an Alloy Coil that is a coin toss the user then cannot
      // overrule — "Sync to web" did nothing, and the module they were actually
      // working showed neither its values nor its knob moves, because its CC
      // feedback was being decoded against the other module's map.
      if (!done && ownerId && patch.module.id !== ownerId) return;
      ownerId = patch.module.id;
      done = true;
      clearTimeout(timer);
      // The reply names the cable the module is on, which beats any guess made
      // from port names — everything the page sends from here goes back the way
      // the answer came.
      midi.adoptAnsweringPort(portName);
      midi.probeAnswered(patch.module.name);
      // Switch first, then decode against the map we just switched to: the
      // `$derived` tables have not recomputed at this point in the tick.
      const tables = switchModule(patch.module);
      applyPatch(patch.pairs, false, tables.PARAM_MAP);
    });
    return () => {
      done = true;
      clearTimeout(timer);
      unsubSysEx();
    };
  });

  // ---------------------------------------------------------------------------
  // Auto-sync on serial connect — send "dump", parse cc:N=V lines
  // ---------------------------------------------------------------------------
  $effect(() => {
    if (!serialOn) return;
    let dumpLines: string[] = [];
    let inDump = false;
    // Small delay mirrors the MIDI path (300 ms): hardware USB CDC needs a
    // moment after port.open() before it reliably receives commands.
    const dumpTimer = setTimeout(() => serial.send("dump"), 400);
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
    return () => {
      clearTimeout(dumpTimer);
      unsubLine();
    };
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

  // ── Per-slider overrides ─────────────────────────────────────────────────
  // Both of the above encode AlloyFlux's voice-mode semantics, which mean
  // nothing on another module. They used to be selected by raw CC number, so
  // any module reusing those CCs silently inherited them — Alloy Coil's revdecay
  // is CC 92 and was showing COLOR's "FM depth" hint.
  //
  // Keyed by param *name* now, which params.json calls out as the stable API,
  // and gated on the module as well so a future name collision cannot bring
  // the bug back.
  const isAlloyFlux = $derived($activeModule.id === "alloyflux");

  let sliderHints = $derived<Record<string, string | undefined>>(
    isAlloyFlux ? { rel: relHint, color: colorHint } : {},
  );
  let sliderDisplays = $derived<Record<string, string | undefined>>(
    isAlloyFlux ? { rel: relDisplayOverride, color: colorDisplayOverride } : {},
  );

  // ── Controls the current mode ignores ─────────────────────────────────────
  // The envelope type picks which set of timing controls is live, and the
  // other set does nothing at all — mirrors CurveEngine.h:
  //   AR   — times come from CURVE and TIME SCALE; the four ADSR knobs are
  //          not read.
  //   ADSR — the four ADSR knobs are the times, and CURVE is reinterpreted as
  //          a multiplier over them, which leaves TIME SCALE unused.
  // Greying the inactive set is the difference between a panel that documents
  // the engine and one that just exposes every CC it has.
  const ccByName = $derived(new Map(PARAM_MAP.map((p) => [p.name, p.cc])));
  const envIsAdsr = $derived(
    (selectValues[ccByName.get("envtype") ?? -1] ?? 0) >= 64,
  );

  const ADSR_ONLY = ["adsrattack", "adsrdecay", "adsrsustain", "adsrrelease"];

  let disabledParams = $derived(
    isAlloyFlux
      ? new Set(envIsAdsr ? ["curvetime"] : ADSR_ONLY)
      : new Set<string>(),
  );
  // ── Bottom dock sizing ───────────────────────────────────────────────────
  // The dock is position:fixed, so it is out of flow and would otherwise cover
  // whatever the page has scrolled to.  Track its height and reserve the same
  // amount of space at the end of the content.
  let dockEl = $state<HTMLElement | null>(null);
  let dockHeight = $state(0);

  $effect(() => {
    if (!dockEl) return;
    const ro = new ResizeObserver((entries) => {
      dockHeight = entries[0].contentRect.height;
    });
    ro.observe(dockEl);
    return () => ro.disconnect();
  });

  /**
   * Room to keep clear below the panel stage: the dock as it currently stands,
   * plus the footer's own line.
   *
   * Measured rather than a constant in PanelView, because the dock is no longer
   * three collapsed tabs of known height — opening the keyboard adds well over
   * a hundred pixels, and the panel has to give that ground rather than be sat
   * on. With every drawer shut this comes out near the 96px the constant used
   * to be, so nothing moves until a drawer is actually opened.
   */
  const panelBottomReserve = $derived(dockHeight + 42);

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

<!-- One definition, rendered by both views: the list keeps it in the right-hand
     column, the panel opens it from the toolbar. -->
{#snippet midiChannelRow()}
  <div class="midi-channel-row">
    <label class="midi-channel-label" for="midi-channel-select"
      >Receive Channel</label
    >
    <select
      id="midi-channel-select"
      class="midi-channel-select"
      value={midiChannel}
      onchange={(e) =>
        sendMidiChannel(parseInt((e.target as HTMLSelectElement).value, 10))}
      disabled={!$midi.deviceConnected}
    >
      <option value={0}>Omni (All)</option>
      {#each Array.from({ length: 16 }, (_, i) => i + 1) as ch}
        <option value={ch}>{ch}</option>
      {/each}
    </select>
  </div>
{/snippet}

<div class="app-shell">
  <!-- Top connection bar. Its module badge is a preview switch while nothing
       has answered on the port, and switching is the same operation the
       discovery probe performs — hence the same function. -->
  <ConnectionBar onPreviewModule={switchModule} />

  {#if unknownDev}
    <div class="module-warning">
      A module answered with SysEx signature <code>{unknownDev}</code>, which
      this build has no parameter map for. Showing {$activeModule.name} controls
      — they will not match. Update the configurator.
    </div>
  {/if}

  <!-- Panel toolbar — opens the utilities that used to occupy a permanent
       right-hand column. They dock to the right on demand instead, which is
       most of what buys the panel its single screen. -->
  <div class="panel-bar">
    <span class="bar-module">{$activeModule.name}</span>
    <span class="bar-spacer"></span>

    <div class="bar-group">
      <button
        class="bar-btn"
        class:on={utility.presets}
        aria-pressed={utility.presets}
        onclick={() => toggleUtility("presets")}>Presets</button
      >
      <button
        class="bar-btn"
        class:on={utility.keyboard}
        aria-pressed={utility.keyboard}
        onclick={() => toggleUtility("keyboard")}>Keyboard</button
      >
      <button
        class="bar-btn"
        class:on={utility.settings}
        aria-pressed={utility.settings}
        onclick={() => toggleUtility("settings")}>Settings</button
      >
    </div>
  </div>

  <!-- Keyed on the module for the same reason the list view is: a Knob's
         curve geometry and a PanelSelect's option bands are read from their
         param at construction, and neither can be re-pointed at a different
         parameter in place. -->
  <!-- Panel and utility rail share the row. The rail takes its width out of
         the panel's, and PanelView measures what it is left and zooms to fit —
         so opening a utility shrinks the control surface instead of covering
         part of it. -->
  <div class="panel-row">
    <div class="panel-col">
      {#key $activeModule.id}
        <PanelView
          moduleId={$activeModule.id}
          map={PARAM_MAP}
          bind:paramValues
          bind:selectValues
          {sliderRefs}
          {selectRefs}
          {sliderHints}
          {sliderDisplays}
          {disabledParams}
          bottomReserve={panelBottomReserve}
        />
      {/key}
    </div>

    {#if railOpen}
      <aside class="dock-rail">
        {#if utility.presets}
          <DockPanel title="Presets" onclose={() => (utility.presets = false)}>
            <PresetManager {getPatchSnapshot} {applyFromFile} {applyDefaults} />
          </DockPanel>
        {/if}

        {#if utility.settings}
          <DockPanel
            title="Settings"
            onclose={() => (utility.settings = false)}
          >
            <div class="settings-body">
              <h4 class="settings-group">MIDI</h4>
              {@render midiChannelRow()}
            </div>
          </DockPanel>
        {/if}
      </aside>
    {/if}
  </div>
  <div class="footer cat-section">
    <small class="footer-label"
      >Alloy Controller — Voltage Foundry Modular - ©2026</small
    >
  </div>

  <!-- Reserves scroll space equal to the dock's current height so an open
       drawer cannot sit on top of the last parameter categories (Delay,
       Reverb…).  Measured rather than hardcoded because either drawer can be
       open, and both change height when they are. -->
  <div
    class="dock-spacer"
    style="height: {dockHeight}px"
    aria-hidden="true"
  ></div>

  <!-- Bottom dock — pinned to the viewport so both drawers stay reachable
       without scrolling to the end of the page.  They live in one fixed
       container rather than being individually fixed, so they stack instead
       of overlapping. -->
  <div class="drawer-dock" bind:this={dockEl}>
    <!-- Topmost of the three, as the one you play rather than read. Toggled by
         `utility.keyboard`, which the panel toolbar's Keyboard button also
         drives — the tab and the button are two handles on one flag. -->
    <div class="kbd-drawer">
      <button class="kbd-tab" onclick={() => toggleUtility("keyboard")}>
        Keyboard {utility.keyboard ? "▼" : "▲"}
      </button>
      {#if utility.keyboard}
        <div class="kbd-body">
          <MidiKeyboard />
        </div>
      {/if}
    </div>

    <MidiMonitor />

    <!-- Serial console drawer -->
    <div class="console-drawer" class:open={consoleOpen}>
      <button class="console-tab" onclick={() => (consoleOpen = !consoleOpen)}>
        <span class="console-conn-dot" class:connected={$serial.connected}
        ></span>
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
            placeholder={$serial.connected
              ? "Type a command…"
              : "Not connected"}
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
</div>

<style>
  .app-shell {
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-ui);
    /* Clears the two collapsed drawer tabs pinned at the bottom. */
    padding-bottom: 3.6rem;
  }

  /* ── Panel toolbar ───────────────────────────────────────────────────────
     A thin rule between the connection bar and the control surface. Kept
     visually quiet: it is chrome, and the panel below it is the subject. */
  .panel-bar {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 20px;
    border-bottom: 1px solid var(--hairline);
  }
  .bar-module {
    font-size: 0.7rem;
    font-weight: 600;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    color: var(--copper);
  }
  .bar-spacer {
    flex: 1;
  }
  .bar-group {
    display: flex;
    gap: 4px;
  }
  .bar-btn {
    font: inherit;
    font-size: 0.66rem;
    letter-spacing: 0.09em;
    text-transform: uppercase;
    padding: 3px 11px;
    border: 1px solid var(--hairline-strong);
    border-radius: var(--radius);
    background: transparent;
    color: var(--text-faint);
    cursor: pointer;
    transition:
      color 90ms,
      border-color 90ms,
      background 90ms;
  }
  .bar-btn:hover {
    color: var(--text);
  }
  .bar-btn.on {
    color: var(--copper-bright);
    border-color: var(--copper-deep);
    background: rgba(192, 137, 74, 0.12);
  }

  /* Panel + utility rail. The panel column is `min-width: 0` so it can actually
     give ground — a flex item defaults to min-content, which would have let the
     stage refuse to shrink and pushed the rail off screen instead. */
  .panel-row {
    display: flex;
    align-items: stretch;
    min-width: 0;
  }
  .panel-col {
    flex: 1;
    min-width: 0;
  }

  /* Wide enough for a preset row (index, name, Save, Load, ✕) without an inner
     horizontal scrollbar, but a share of the viewport so a narrow window does
     not hand most of itself to the rail. */
  .dock-rail {
    flex: none;
    width: clamp(300px, 24vw, 400px);
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 0px 12px 12px 0;
    min-width: 0;
    border-left: 1px solid var(--hairline);
  }

  /* Settings window contents. Grouped with headings so the other settings
     that will land here have somewhere obvious to go. */
  .settings-body {
    display: flex;
    flex-direction: column;
    gap: 8px;
    min-width: 230px;
  }
  .settings-group {
    margin: 0;
    font-size: 0.6rem;
    font-weight: 600;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    color: var(--text-faint);
  }
  .dock-spacer {
    flex: none;
    width: 100%;
    transition: height 0.15s ease;
  }
  .module-warning {
    padding: 0.5rem 1rem;
    background: rgba(224, 168, 58, 0.12);
    border-bottom: 1px solid var(--copper-deep);
    color: var(--copper-bright);
    font-size: 0.8rem;
  }
  .module-warning code {
    font-family: monospace;
    color: var(--warn);
  }
  .drawer-dock {
    position: fixed;
    left: 0;
    right: 0;
    bottom: 0;
    z-index: 100;
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
    color: var(--text-dim);
    white-space: nowrap;
  }
  .midi-channel-select {
    background: var(--bg-raised);
    color: var(--text);
    border: 1px solid var(--hairline-strong);
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
    color: var(--text-dim);
    min-width: 3rem;
  }
  /* Keyboard drawer — same shell as the console and monitor drawers, so the
     three tabs read as one stack. */
  .kbd-drawer {
    background: var(--bg-panel);
    border-top: 1px solid var(--hairline);
  }
  .kbd-body {
    padding: 0.6rem 1rem 0.75rem;
  }
  /* Indented past where the other two tabs put their connection dot, so all
     three labels start on the same column. A keyboard has nothing to be
     connected to, so it gets the space rather than a dot.

     Qualified by the drawer to outrank the `padding` shorthand in the shared
     .kbd-tab/.console-tab rule below, which would otherwise reset this. */
  .kbd-drawer .kbd-tab {
    padding-left: calc(1rem + 0.55rem + 0.5rem);
  }

  .console-drawer {
    background: var(--bg-panel);
    border-top: 1px solid var(--hairline);
  }
  .kbd-tab,
  .console-tab {
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
  .console-conn-dot {
    display: inline-block;
    width: 0.55rem;
    height: 0.55rem;
    border-radius: 50%;
    background: var(--hairline-strong);
    flex-shrink: 0;
    transition: background 0.3s;
  }
  .console-conn-dot.connected {
    background: var(--ok);
    box-shadow: 0 0 5px var(--ok);
  }
  .kbd-tab:hover,
  .console-tab:hover {
    background: rgba(192, 137, 74, 0.1);
    color: var(--text-dim);
  }
  .console-body {
    /* Matches the MIDI monitor: capped so both drawers open together still
       leave the page usable on a short screen. */
    height: min(180px, 28vh);
    overflow-y: auto;
    padding: 0.4rem 0.75rem;
    font-family: monospace;
    font-size: 0.75rem;
    color: var(--led-4);
    background: var(--bg-sunken);
  }
  .console-line {
    white-space: pre-wrap;
    word-break: break-all;
    line-height: 1.4;
  }
  .console-empty {
    color: var(--text-faint);
    font-style: italic;
  }
  .console-input-row {
    display: flex;
    gap: 0.4rem;
    padding: 0.4rem 0.75rem;
    background: var(--bg-panel);
    border-top: 1px solid var(--hairline);
  }
  .console-input {
    flex: 1;
    background: var(--bg-sunken);
    border: 1px solid var(--hairline-strong);
    border-radius: 4px;
    color: var(--text);
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
    background: var(--bg-sunken);
    color: var(--text-dim);
    border: 1px solid var(--hairline-strong);
    border-radius: 4px;
    cursor: pointer;
  }
  .console-send:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .console-send:not(:disabled):hover {
    background: rgba(192, 137, 74, 0.14);
  }
</style>
