<script lang="ts">
  /**
   * DockPanel — one panel in the right-hand utility rail.
   *
   * Replaces the earlier draggable overlay. Dragging solved the "it covers the
   * controls" problem by making the user move the window; docking solves it by
   * taking the space out of the panel's width instead. PanelView measures the
   * room it is given and zooms to fit, so opening a panel shrinks the control
   * surface rather than hiding part of it — nothing ever needs moving out of
   * the way.
   */
  import type { Snippet } from "svelte";

  let {
    title,
    onclose,
    children,
  }: {
    title: string;
    onclose: () => void;
    children: Snippet;
  } = $props();
</script>

<section class="dock-panel" aria-label={title}>
  <header class="bar">
    <span class="title">{title}</span>
    <button class="close" onclick={onclose} aria-label={`Close ${title}`}
      >✕</button
    >
  </header>
  <div class="body">
    {@render children()}
  </div>
</section>

<style>
  .dock-panel {
    display: flex;
    flex-direction: column;
    min-height: 0;
    border-radius: 8px;
    margin-left: 8px;
    background: var(--bg-panel);
  }

  .bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 6px 8px 6px 12px;
    border-bottom: 1px solid var(--hairline);
    border-radius: 7px 7px 0 0;
    background: var(--bg-raised);
  }

  .title {
    flex: 1;
    font-size: 0.7rem;
    font-weight: 600;
    letter-spacing: 0.14em;
    text-transform: uppercase;
    color: var(--copper);
    white-space: nowrap;
  }

  .close {
    font: inherit;
    font-size: 0.75rem;
    line-height: 1;
    padding: 3px 7px;
    border: 1px solid transparent;
    border-radius: var(--radius);
    background: transparent;
    color: var(--text-faint);
    cursor: pointer;
  }
  .close:hover {
    color: var(--err);
    border-color: rgba(208, 90, 82, 0.45);
    background: rgba(208, 90, 82, 0.12);
  }

  .body {
    padding: 12px 14px;
    overflow: auto;
    min-height: 0;
  }
</style>
