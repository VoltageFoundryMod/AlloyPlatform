<script lang="ts">
  /**
   * MidiKeyboard — 2-octave on-screen keyboard (C3–B4).
   * Sends Note On / Note Off via the MIDI singleton.
   */
  import { midi } from "../lib/midi";

  // C3 = MIDI note 48
  const START_NOTE = 48;
  const NUM_OCTAVES = 2;

  interface Key {
    note: number;
    label: string;
    isBlack: boolean;
    /** Position within a 7-white-key octave (0-6) to calculate left offset for black keys */
    whiteIndex: number;
  }

  const BLACK_PATTERN = [false, true, false, true, false, false, true, false, true, false, true, false];
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

  function buildKeys(): Key[] {
    const keys: Key[] = [];
    let whiteIndex = 0;
    for (let i = 0; i < NUM_OCTAVES * 12; i++) {
      const note = START_NOTE + i;
      const semitone = i % 12;
      const octave = Math.floor(note / 12) - 1;
      const isBlack = BLACK_PATTERN[semitone];
      keys.push({ note, label: NOTE_NAMES[semitone] + octave, isBlack, whiteIndex });
      if (!isBlack) whiteIndex++;
    }
    return keys;
  }

  const keys = buildKeys();
  const whiteKeys = keys.filter((k) => !k.isBlack);
  const blackKeys = keys.filter((k) => k.isBlack);

  // Track which notes are active (held)
  let activeNotes = $state(new Set<number>());

  let velocity = $state(100);

  function noteOn(note: number) {
    if (activeNotes.has(note)) return;
    activeNotes.add(note);
    activeNotes = new Set(activeNotes);
    midi.sendNoteOn(note, velocity);
  }

  function noteOff(note: number) {
    activeNotes.delete(note);
    activeNotes = new Set(activeNotes);
    midi.sendNoteOff(note);
  }

  // Keyboard events
  function onKeyDown(e: KeyboardEvent) {
    if (e.repeat) return;
    const note = qwertyMap[e.key.toLowerCase()];
    if (note !== undefined) noteOn(note);
  }
  function onKeyUp(e: KeyboardEvent) {
    const note = qwertyMap[e.key.toLowerCase()];
    if (note !== undefined) noteOff(note);
  }

  // QWERTY mapping: z-row = white keys from C3, s-row = black keys
  const qwertyMap: Record<string, number> = {
    z: 48, s: 49, x: 50, d: 51, c: 52, v: 53, g: 54, b: 55,
    h: 56, n: 57, j: 58, m: 59,
    q: 60, "2": 61, w: 62, "3": 63, e: 64, r: 65, "5": 66, t: 67,
    "6": 68, y: 69, "7": 70, u: 71,
  };

  // Prevent right-click context on keys
  function noContext(e: MouseEvent) { e.preventDefault(); }

  // White key width in %
  const whiteW = 100 / whiteKeys.length;
</script>

<svelte:window onkeydown={onKeyDown} onkeyup={onKeyUp} />

<div class="keyboard-wrap">
  <div class="vel-row">
    <label for="vel-slider">Velocity</label>
    <input id="vel-slider" type="range" min={1} max={127} bind:value={velocity} />
    <span>{velocity}</span>
    <button onclick={() => midi.sendPanic()} class="panic-btn">Panic</button>
    <button onclick={() => midi.sendSustain(true)}>Sustain ↓</button>
    <button onclick={() => midi.sendSustain(false)}>Sustain ↑</button>
  </div>

  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="keyboard"
    style="--white-count: {whiteKeys.length}; --white-w: {whiteW}%"
    oncontextmenu={noContext}
  >
    <!-- White keys -->
    {#each whiteKeys as key}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <div
        class="key white"
        class:active={activeNotes.has(key.note)}
        style="width: {whiteW}%"
        onmousedown={() => noteOn(key.note)}
        onmouseup={() => noteOff(key.note)}
        onmouseleave={() => noteOff(key.note)}
        ontouchstart={(e) => { e.preventDefault(); noteOn(key.note); }}
        ontouchend={(e) => { e.preventDefault(); noteOff(key.note); }}
        role="button"
        tabindex="-1"
        aria-label={key.label}
      >
        <span class="key-label">{key.label}</span>
      </div>
    {/each}

    <!-- Black keys — absolutely positioned -->
    {#each blackKeys as key}
      <!-- Determine left offset: find the white key to the left -->
      {@const leftWhiteIdx = whiteKeys.filter((w) => w.note < key.note).length}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <div
        class="key black"
        class:active={activeNotes.has(key.note)}
        style="left: calc({leftWhiteIdx * whiteW}% + {whiteW * 0.6}%)"
        onmousedown={(e) => { e.stopPropagation(); noteOn(key.note); }}
        onmouseup={(e) => { e.stopPropagation(); noteOff(key.note); }}
        onmouseleave={() => noteOff(key.note)}
        ontouchstart={(e) => { e.preventDefault(); e.stopPropagation(); noteOn(key.note); }}
        ontouchend={(e) => { e.preventDefault(); e.stopPropagation(); noteOff(key.note); }}
        role="button"
        tabindex="-1"
        aria-label={key.label}
      ></div>
    {/each}
  </div>
</div>

<style>
  .keyboard-wrap {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .vel-row {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    font-size: 0.8rem;
    color: #aaa;
  }
  .vel-row input[type="range"] { width: 100px; accent-color: #7cb8ff; }
  .vel-row span { min-width: 2rem; font-variant-numeric: tabular-nums; }
  button { font-size: 0.75rem; padding: 0.2rem 0.5rem; cursor: pointer;
    background: #252540; color: #aab; border: 1px solid #555; border-radius: 4px; }
  button:hover { background: #3a3a70; }
  .panic-btn { color: #cf6f6f; border-color: #703030; }
  .panic-btn:hover { background: #3d1a1a; }

  .keyboard {
    position: relative;
    display: flex;
    height: 120px;
    user-select: none;
    border-radius: 6px;
    overflow: visible;
    background: #111;
    border: 1px solid #333;
  }

  .key {
    position: relative;
    cursor: pointer;
    border-radius: 0 0 4px 4px;
    box-sizing: border-box;
    transition: background 60ms;
    flex-shrink: 0;
  }
  .key.white {
    height: 100%;
    background: #e8e8e8;
    border: 1px solid #888;
    border-top: none;
    display: flex;
    align-items: flex-end;
    justify-content: center;
    padding-bottom: 4px;
    z-index: 1;
  }
  .key.white.active  { background: #7cb8ff; }
  .key.black {
    position: absolute;
    top: 0;
    width: calc(var(--white-w) * 0.7);
    height: 60%;
    background: #1a1a1a;
    border: 1px solid #555;
    z-index: 2;
    border-radius: 0 0 3px 3px;
  }
  .key.black.active  { background: #3a6a9f; }

  .key-label {
    font-size: 0.6rem;
    color: #555;
    pointer-events: none;
  }
</style>
