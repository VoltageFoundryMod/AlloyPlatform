<script lang="ts">
  /**
   * ScaleKeys — one octave showing which semitones the quantiser lets through.
   *
   * Read-only, deliberately. The firmware selects a scale by `ScaleId` (see
   * scale_quantizer.h) and there is no parameter carrying an arbitrary
   * twelve-bit mask, so a click here would have nothing to send: the panel
   * would say "custom" while the module went on quantising to whatever preset
   * scale was last chosen. Making the keys editable needs a firmware parameter
   * first — until then this reflects the Quantize selector and nothing else.
   */
  import { NOTE_NAMES, IS_BLACK_KEY, scaleNotes } from "../../lib/scales";

  let {
    scaleCC = 0,
    label = undefined,
  }: {
    /** CC value of the `scale` parameter — i.e. the ScaleId. */
    scaleCC?: number;
    /** Name of the current scale, shown beside the keys. */
    label?: string;
  } = $props();

  const notes = $derived(scaleNotes(scaleCC));
  const count = $derived(notes.filter(Boolean).length);

  // Chromatic means the quantiser is bypassed rather than "every note is in
  // the scale", and saying so is more useful than lighting all twelve keys.
  const bypassed = $derived(count === 12);

  /** Spoken description, since the keys themselves are decorative spans. */
  const ariaText = $derived(
    bypassed
      ? "Quantiser off — all twelve semitones pass through"
      : `${label ?? "Scale"}: ${notes
          .flatMap((on, i) => (on ? [NOTE_NAMES[i]] : []))
          .join(", ")}`,
  );
</script>

<div class="scale-keys" class:bypassed>
  <div class="keys" role="img" aria-label={ariaText}>
    {#each notes as inScale, semitone}
      <span
        class="key"
        class:black={IS_BLACK_KEY[semitone]}
        class:on={inScale}
        title={NOTE_NAMES[semitone]}
      ></span>
    {/each}
  </div>
  <div class="caption">
    {#if bypassed}
      <span class="off">quantiser off</span>
    {:else}
      <span class="count">{count} notes</span>
    {/if}
  </div>
</div>

<style>
  .scale-keys {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 4px;
  }

  /* A miniature octave: twelve equal slots, the black keys drawn shorter and
     narrower so the shape reads as a keyboard rather than a bar chart. */
  .keys {
    display: flex;
    align-items: flex-start;
    gap: 2px;
    padding: 5px 6px;
    border: 1px solid var(--hairline);
    border-radius: 5px;
    background: var(--bg-sunken);
  }

  .key {
    width: 9px;
    height: 26px;
    border-radius: 0 0 2px 2px;
    background: var(--bg-raised);
    box-shadow: inset 0 0 0 1px var(--hairline-strong);
    transition:
      background 120ms,
      box-shadow 120ms;
  }
  .key.black {
    width: 7px;
    height: 16px;
    background: var(--bg-sunken);
  }

  .key.on {
    background: var(--copper);
    box-shadow:
      0 0 5px var(--copper-glow),
      inset 0 0 0 1px rgba(255, 255, 255, 0.25);
  }
  .key.black.on {
    background: var(--copper-deep);
  }

  /* Bypassed: all twelve are lit, which would otherwise read as a very busy
     scale. Dim the whole strip so it reads as "not doing anything". */
  .bypassed .key.on {
    background: var(--hairline-strong);
    box-shadow: inset 0 0 0 1px var(--hairline-strong);
  }

  .caption {
    font-size: 0.62rem;
    letter-spacing: 0.06em;
    text-transform: uppercase;
    color: var(--text-faint);
  }
  .count {
    color: var(--copper-deep);
  }
</style>
