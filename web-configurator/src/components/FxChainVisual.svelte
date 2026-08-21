<script lang="ts">
  /**
   * FxChainVisual — displays the current effect signal chain order.
   *
   * Four blocks reorder live based on filterPost / delayPost props:
   *
   *   false/false (default): Filter → Chorus → Delay  → Reverb
   *   true /false          : Chorus → Filter → Delay  → Reverb
   *   false/true           : Filter → Chorus → Reverb → Delay
   *   true /true           : Chorus → Filter → Reverb → Delay
   */
  let {
    filterPost = false,
    delayPost = false,
  }: { filterPost?: boolean; delayPost?: boolean } = $props();

  type Block = { label: string; color: string };

  // One rung of the panel LED strip per block, so a block keeps its colour
  // wherever the reordering puts it and the eye can follow it across a change.
  const BLOCKS: Record<string, Block> = {
    Voice: { label: "Voice", color: "var(--led-3)" },
    Filter: { label: "Filter", color: "var(--led-2)" },
    Chorus: { label: "Chorus", color: "var(--led-4)" },
    Delay: { label: "Delay", color: "var(--led-5)" },
    Reverb: { label: "Reverb", color: "var(--led-1)" },
    Out: { label: "Out", color: "var(--led-6)" },
  };

  const chain = $derived(
    filterPost
      ? delayPost
        ? ["Voice", "Chorus", "Filter", "Reverb", "Delay", "Out"]
        : ["Voice", "Chorus", "Filter", "Delay", "Reverb", "Out"]
      : delayPost
        ? ["Voice", "Filter", "Chorus", "Reverb", "Delay", "Out"]
        : ["Voice", "Filter", "Chorus", "Delay", "Reverb", "Out"],
  );
</script>

<div class="chain">
  {#each chain as name, i}
    <div class="block" style:--block={BLOCKS[name].color}>{name}</div>
    {#if i < chain.length - 1}
      <span class="arrow">→</span>
    {/if}
  {/each}
</div>

<style>
  .chain {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 0.3rem;
    padding: 0.75rem 0.3rem;
    background: var(--bg-sunken);
    border: 1px solid var(--hairline);
    border-radius: 6px;
    flex-wrap: wrap;
    margin-bottom: 0.5rem;
  }

  /* Outlined rather than filled: four solid chips would out-shout the knobs
     around them, and the chain is context, not a control. */
  .block {
    color: var(--block);
    font-size: 0.63rem;
    font-weight: 600;
    padding: 0.2rem 0.5rem;
    border: 1px solid var(--block);
    border-radius: 4px;
    letter-spacing: 0.07em;
    text-transform: uppercase;
    background: color-mix(in srgb, var(--block) 12%, transparent);
    transition:
      color 0.2s,
      border-color 0.2s;
  }

  .arrow {
    color: var(--copper-deep);
    font-size: 0.75rem;
    flex-shrink: 0;
  }
</style>
