<script lang="ts">
  import { midi } from "../lib/midi";

  let baseOctave = $state(4);
  let velocity = $state(100);
  let activeNotes = $state(new Set<number>());

  const baseNote = $derived((baseOctave + 1) * 12);

  const NUM_OCTAVES = 2;
  const BLACK_PAT = [
    false,
    true,
    false,
    true,
    false,
    false,
    true,
    false,
    true,
    false,
    true,
    false,
  ];
  const NOTE_NAMES = [
    "C",
    "C#",
    "D",
    "D#",
    "E",
    "F",
    "F#",
    "G",
    "G#",
    "A",
    "A#",
    "B",
  ];

  interface VKey {
    offset: number;
    semitone: number;
    isBlack: boolean;
    whiteIndex: number;
  }

  const allKeys: VKey[] = (() => {
    const acc: VKey[] = [];
    let wi = 0;
    for (let off = 0; off < NUM_OCTAVES * 12; off++) {
      const st = off % 12,
        blk = BLACK_PAT[st];
      acc.push({ offset: off, semitone: st, isBlack: blk, whiteIndex: wi });
      if (!blk) wi++;
    }
    return acc;
  })();

  const whiteKeys = allKeys.filter((k) => !k.isBlack);
  const blackKeys = allKeys.filter((k) => k.isBlack);
  const whiteW = 100 / whiteKeys.length;

  // Reason-style interlaced layout — A-row covers C through F+1 (1.5 octaves)
  // White: A S D F G H J   K L ; '
  //        C D E F G A B   C+D+E+F+
  // Black: W E   T Y U     O P
  //       C#D#  F#G#A#    C#D#
  const KEY_PAIRS: readonly [string, number][] = [
    ["a", 0],
    ["w", 1],
    ["s", 2],
    ["e", 3],
    ["d", 4],
    ["f", 5],
    ["t", 6],
    ["g", 7],
    ["y", 8],
    ["h", 9],
    ["u", 10],
    ["j", 11],
    ["k", 12],
    ["o", 13],
    ["l", 14],
    ["p", 15],
    [";", 16],
    ["'", 17],
  ] as const;

  const OFFSET_LABEL: Record<number, string> = Object.fromEntries(
    KEY_PAIRS.map(([k, o]) => [o, k.toUpperCase()]),
  );

  const qwertyMap = $derived(
    Object.fromEntries(KEY_PAIRS.map(([k, off]) => [k, baseNote + off])),
  );

  const VEL_STEPS: readonly [string, number][] = [
    ["1", 16],
    ["2", 32],
    ["3", 48],
    ["4", 64],
    ["5", 80],
    ["6", 96],
    ["7", 112],
    ["8", 127],
  ] as const;
  const VEL_MAP = Object.fromEntries(VEL_STEPS);

  function noteOn(note: number) {
    if (activeNotes.has(note)) return;
    activeNotes = new Set([...activeNotes, note]);
    midi.sendNoteOn(note, velocity);
  }
  function noteOff(note: number) {
    if (!activeNotes.has(note)) return;
    const s = new Set(activeNotes);
    s.delete(note);
    activeNotes = s;
    midi.sendNoteOff(note);
  }
  function octaveDown() {
    baseOctave = Math.max(0, baseOctave - 1);
  }
  function octaveUp() {
    baseOctave = Math.min(8, baseOctave + 1);
  }

  function onKeyDown(e: KeyboardEvent) {
    const target = e.target as HTMLElement;
    if (
      target.tagName === "INPUT" ||
      target.tagName === "TEXTAREA" ||
      target.isContentEditable
    )
      return;
    if (e.repeat || e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key.toLowerCase();
    if (k === "z") {
      octaveDown();
      return;
    }
    if (k === "x") {
      octaveUp();
      return;
    }
    if (k in VEL_MAP) {
      velocity = VEL_MAP[k];
      return;
    }
    const note = qwertyMap[k];
    if (note !== undefined) noteOn(note);
  }
  function onKeyUp(e: KeyboardEvent) {
    const target = e.target as HTMLElement;
    if (
      target.tagName === "INPUT" ||
      target.tagName === "TEXTAREA" ||
      target.isContentEditable
    )
      return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const note = qwertyMap[e.key.toLowerCase()];
    if (note !== undefined) noteOff(note);
  }
  function noContext(e: MouseEvent) {
    e.preventDefault();
  }
</script>

<svelte:window onkeydown={onKeyDown} onkeyup={onKeyUp} />

<div class="keyboard-wrap">
  <div class="kbd-toolbar">
    <!-- Velocity preset buttons 1–8 -->
    <div class="vel-presets">
      {#each VEL_STEPS as [k, v]}
        <button
          class="vel-btn"
          class:on={velocity === v}
          onclick={() => (velocity = v)}
          title="Velocity {v} (key {k})">{k}</button
        >
      {/each}
    </div>
    <!-- Velocity fine-control -->
    <div class="vel-ctrl">
      <span class="lbl">Velocity</span>
      <input type="range" min={1} max={127} bind:value={velocity} />
      <span class="val">{velocity}</span>
    </div>
    <!-- Octave: Z/X buttons + display -->
    <div class="oct-ctrl">
      <button class="oct-btn" onclick={octaveDown} title="Octave down (Z)"
        >Z &#9668;</button
      >
      <span class="oct-lbl">C{baseOctave}</span>
      <button class="oct-btn" onclick={octaveUp} title="Octave up (X)"
        >X &#9658;</button
      >
    </div>
    <!-- Panic / Drone -->
    <div class="misc-ctrl">
      <button class="panic-btn" onclick={() => midi.sendPanic()}>Panic</button>
      <button onclick={() => midi.sendSustain(true)}>Drone</button>
    </div>
  </div>

  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="keyboard" oncontextmenu={noContext}>
    <!-- White keys -->
    {#each whiteKeys as vk}
      {@const note = baseNote + vk.offset}
      {@const hint = OFFSET_LABEL[vk.offset]}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <div
        class="key white"
        class:active={activeNotes.has(note)}
        style="width: {whiteW}%"
        onmousedown={() => noteOn(note)}
        onmouseup={() => noteOff(note)}
        onmouseleave={() => noteOff(note)}
        ontouchstart={(e) => {
          e.preventDefault();
          noteOn(note);
        }}
        ontouchend={(e) => {
          e.preventDefault();
          noteOff(note);
        }}
        role="button"
        tabindex="-1"
        aria-label="{NOTE_NAMES[vk.semitone]}{baseOctave +
          Math.floor(vk.offset / 12)}"
      >
        {#if hint}<span class="kbd-hint">{hint}</span>{/if}
        {#if vk.semitone === 0}
          <span class="note-lbl"
            >C{baseOctave + Math.floor(vk.offset / 12)}</span
          >
        {/if}
      </div>
    {/each}
    <!-- Black keys: left = (whiteIndex - 0.35) * whiteW % -->
    {#each blackKeys as vk}
      {@const note = baseNote + vk.offset}
      {@const hint = OFFSET_LABEL[vk.offset]}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <div
        class="key black"
        class:active={activeNotes.has(note)}
        style="left:{(vk.whiteIndex - 0.35) * whiteW}%; width:{whiteW * 0.65}%"
        onmousedown={(e) => {
          e.stopPropagation();
          noteOn(note);
        }}
        onmouseup={(e) => {
          e.stopPropagation();
          noteOff(note);
        }}
        onmouseleave={() => noteOff(note)}
        ontouchstart={(e) => {
          e.preventDefault();
          e.stopPropagation();
          noteOn(note);
        }}
        ontouchend={(e) => {
          e.preventDefault();
          e.stopPropagation();
          noteOff(note);
        }}
        role="button"
        tabindex="-1"
        aria-label="{NOTE_NAMES[vk.semitone]}{baseOctave +
          Math.floor(vk.offset / 12)}"
      >
        {#if hint}<span class="kbd-hint bk">{hint}</span>{/if}
      </div>
    {/each}
  </div>
</div>

<style>
  .keyboard-wrap {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .kbd-toolbar {
    display: flex;
    align-items: center;
    gap: 0.75rem;
    flex-wrap: wrap;
  }

  .vel-presets {
    display: flex;
    gap: 2px;
  }
  .vel-btn {
    width: 22px;
    height: 22px;
    padding: 0;
    font-size: 0.68rem;
    font-weight: 700;
    border-radius: 4px;
    border: 1px solid #444;
    background: #1e1e32;
    color: #777;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .vel-btn:hover {
    background: #2a2a50;
    color: #bbb;
  }
  .vel-btn.on {
    background: #1a4020;
    color: #6fcf6f;
    border-color: #3a7040;
  }

  .vel-ctrl {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }
  .vel-ctrl input[type="range"] {
    width: 90px;
    accent-color: #7cb8ff;
  }
  .lbl {
    font-size: 0.7rem;
    color: #666;
  }
  .val {
    min-width: 2rem;
    font-size: 0.8rem;
    color: #ccc;
    font-variant-numeric: tabular-nums;
  }

  .oct-ctrl {
    display: flex;
    align-items: center;
    gap: 0.4rem;
  }
  .oct-btn {
    font-size: 0.7rem;
    padding: 0.15rem 0.45rem;
    background: #1e1e32;
    color: #aab;
    border: 1px solid #444;
    border-radius: 4px;
    cursor: pointer;
  }
  .oct-btn:hover {
    background: #2a2a50;
  }
  .oct-lbl {
    font-size: 0.9rem;
    font-weight: 700;
    color: #7cb8ff;
    min-width: 2.5rem;
    text-align: center;
    font-variant-numeric: tabular-nums;
  }
  .misc-ctrl {
    display: flex;
    gap: 0.3rem;
    margin-left: auto;
  }
  button {
    font-size: 0.75rem;
    padding: 0.2rem 0.5rem;
    cursor: pointer;
    background: #252540;
    color: #aab;
    border: 1px solid #555;
    border-radius: 4px;
  }
  button:hover {
    background: #3a3a70;
  }
  .panic-btn {
    color: #cf6f6f;
    border-color: #703030;
  }
  .panic-btn:hover {
    background: #3d1a1a;
  }

  .keyboard {
    position: relative;
    display: flex;
    height: 130px;
    user-select: none;
    border-radius: 6px;
    overflow: visible;
    background: #111;
    border: 1px solid #333;
  }
  .key {
    position: relative;
    cursor: pointer;
    border-radius: 0 0 5px 5px;
    box-sizing: border-box;
    transition: background 40ms;
  }

  .key.white {
    height: 100%;
    background: linear-gradient(175deg, #d8d8d8 0%, #f2f2f2 100%);
    border: 1px solid #999;
    border-top: none;
    z-index: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: flex-end;
    padding-bottom: 5px;
    gap: 1px;
  }
  .key.white:hover:not(.active) {
    background: #dde8ff;
  }
  .key.white.active {
    background: #7cb8ff;
  }

  .key.black {
    position: absolute;
    top: 0;
    height: 58%;
    background: linear-gradient(175deg, #2a2a2a 0%, #111 100%);
    border: 1px solid #555;
    border-top: none;
    z-index: 2;
    border-radius: 0 0 4px 4px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: flex-end;
    padding-bottom: 3px;
  }
  .key.black:hover:not(.active) {
    background: linear-gradient(175deg, #484848 0%, #222 100%);
  }
  .key.black.active {
    background: linear-gradient(175deg, #4a8abb 0%, #2a5a8a 100%);
  }

  .kbd-hint {
    font-size: 0.55rem;
    font-weight: 700;
    color: #888;
    line-height: 1;
    background: rgba(255, 255, 255, 0.45);
    border-radius: 2px;
    padding: 1px 2px;
    pointer-events: none;
  }
  .key.active .kbd-hint {
    color: #fff;
    background: rgba(0, 0, 0, 0.25);
  }
  .kbd-hint.bk {
    color: #999;
    background: rgba(255, 255, 255, 0.12);
    font-size: 0.5rem;
  }
  .key.black.active .kbd-hint {
    color: #ddd;
  }

  .note-lbl {
    font-size: 0.5rem;
    color: #888;
    line-height: 1;
    pointer-events: none;
  }
  .key.active .note-lbl {
    color: rgba(255, 255, 255, 0.7);
  }
</style>
