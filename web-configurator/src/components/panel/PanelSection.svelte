<script lang="ts">
  /**
   * PanelSection — one functional group, drawn the way the silkscreen draws
   * one: a thin copper outline with the title set into the top edge, never a
   * filled card. Everything inside sits directly on the panel ground.
   *
   * Width comes from `span` on the stage's 12-column grid. That only works
   * because the stage is a fixed pixel width that PanelView scales to the
   * window — on a fluid grid the same spans produced sections that were mostly
   * empty outline on a wide monitor.
   */
  import type { Snippet } from "svelte";

  let {
    title,
    span = 3,
    dialBox = 88,
    iconRow = false,
    stretch = false,
    children,
  }: {
    title: string;
    /** Width in columns of the stage's 12-column grid. */
    span?: number;
    /** Match the tallest section in the band instead of hugging content. */
    stretch?: boolean;
    /**
     * Reserve the glyph-strip row on every knob here, because at least one of
     * them has glyphs. Without the reservation that knob alone is taller and
     * its readout falls below the rest of the row.
     */
    iconRow?: boolean;
    /**
     * Height of every dial slot inside this section, in px — the diameter of
     * its largest knob. Knobs centre themselves in that slot, so a row of
     * mixed sizes shares one centre line and one readout baseline instead of
     * stepping down as the knobs get smaller.
     */
    dialBox?: number;
    children: Snippet;
  } = $props();
</script>

<section
  class="section"
  class:stretch
  style:--span={span}
  style:--dial-box={`${dialBox}px`}
  style:--icon-row={iconRow ? "20px" : "0px"}
>
  <h3 class="title">{title}</h3>
  <div class="body">
    {@render children()}
  </div>
</section>

<style>
  .section {
    grid-column: span var(--span);
    position: relative;
    display: flex;
    flex-direction: column;
    min-width: 0;
    padding: 18px 16px 14px;
    border: 1px solid var(--copper-dim);
    border-radius: 8px;
  }

  /* Opt in to matching the band's tallest section. The grid sets
     `align-items: start` so this is per section rather than per row — see
     PanelSectionDef.stretch for why it is not the default. */
  .section.stretch {
    align-self: stretch;
  }

  /* The title interrupts the top rule rather than sitting above it — the same
   * move the panel makes, and what keeps a screen of these from reading as a
   * stack of cards. */
  .title {
    position: absolute;
    top: 0;
    left: 12px;
    transform: translateY(-50%);
    padding: 0 7px;
    background: var(--bg);
    font-size: 0.7rem;
    font-weight: 600;
    letter-spacing: 0.16em;
    text-transform: uppercase;
    color: var(--copper);
    white-space: nowrap;
  }

  /* Centred, so whatever room a section has spare is split evenly either side
   * of its controls instead of collecting in one dead strip down the right. */
  .body {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-start;
    justify-content: center;
    align-content: center;
    gap: 14px 16px;
    min-width: 0;
    height: 100%;
  }
</style>
