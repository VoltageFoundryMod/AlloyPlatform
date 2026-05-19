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

  const BLOCKS: Record<string, Block> = {
    Filter: { label: "Filter", color: "#2a4a6a" },
    Chorus: { label: "Chorus", color: "#2a5a3a" },
    Delay: { label: "Delay", color: "#5a4a1a" },
    Reverb: { label: "Reverb", color: "#4a2a5a" },
  };

  const chain = $derived(
    filterPost
      ? delayPost
        ? ["Chorus", "Filter", "Reverb", "Delay"]
        : ["Chorus", "Filter", "Delay", "Reverb"]
      : delayPost
        ? ["Filter", "Chorus", "Reverb", "Delay"]
        : ["Filter", "Chorus", "Delay", "Reverb"],
  );
</script>

<div class="chain">
  {#each chain as name, i}
    <div class="block" style:background={BLOCKS[name].color}>{name}</div>
    {#if i < chain.length - 1}
      <span class="arrow">→</span>
    {/if}
  {/each}
</div>

<style>
  .chain {
    display: flex;
    align-items: center;
    gap: 0.3rem;
    padding: 0.45rem 0.6rem;
    background: #0d0d1a;
    border: 1px solid #2a2a42;
    border-radius: 6px;
    flex-wrap: wrap;
    margin-bottom: 0.5rem;
  }

  .block {
    color: #ccd;
    font-size: 0.68rem;
    font-weight: 600;
    padding: 0.28rem 0.65rem;
    border-radius: 4px;
    letter-spacing: 0.05em;
    text-transform: uppercase;
    transition: background 0.2s;
  }

  .arrow {
    color: #44445a;
    font-size: 0.8rem;
    flex-shrink: 0;
  }
</style>
