<script lang="ts" module>
  /**
   * The panel is composed once at its module's design width (see
   * PanelLayout.stageWidth) and then scaled to whatever the window is, rather
   * than reflowing. That is the whole point: a layout that reflows has to look
   * right at every width, which in practice means it looks deliberate at none —
   * sections either stretch into half-empty outlines on a wide monitor or wrap
   * into a column stacked down the left. Zooming a fixed composition keeps the
   * proportions the layout was tuned for.
   */

  /** Never shrink past this; below it the page scrolls instead. */
  const MIN_SCALE = 0.62;
  /** Nor grow past it — knobs the size of coasters help nobody. */
  const MAX_SCALE = 1.3;
  /**
   * Vertical room below the stage to leave for the footer and the two collapsed
   * drawer tabs, which are pinned to the bottom of the viewport.
   */
  const BOTTOM_RESERVE = 96;
</script>

<script lang="ts">
  /**
   * PanelView — the module as a single screen of controls.
   *
   * Replaces the scrolling list of range sliders with the layout in
   * lib/panelLayout.ts, drawn in the panel's own visual language. It owns none
   * of the state: values, refs and hints all belong to App.svelte, which is
   * what lets the panel and the older list view be two renderings of one patch
   * rather than two copies of it.
   */
  import type { CCParam } from "../lib/paramMap";
  import { layoutFor, stageWidthFor } from "../lib/panelLayout";
  import Knob from "./panel/Knob.svelte";
  import PanelSelect from "./panel/PanelSelect.svelte";
  import PanelSection from "./panel/PanelSection.svelte";
  import FxChainVisual from "./FxChainVisual.svelte";
  import EnvelopeGraph from "./EnvelopeGraph.svelte";
  import ScaleKeys from "./panel/ScaleKeys.svelte";

  let {
    moduleId,
    map,
    paramValues = $bindable(),
    selectValues = $bindable(),
    sliderRefs,
    selectRefs,
    sliderHints = {},
    sliderDisplays = {},
    disabledParams = new Set<string>(),
  }: {
    moduleId: string;
    map: CCParam[];
    /** Float values keyed by CC — slider-type params only. */
    paramValues: Record<number, number>;
    /** Raw CC values keyed by CC — select-type params only. */
    selectValues: Record<number, number>;
    /** Written by bind:this so App can push inbound CC into each control. */
    sliderRefs: Record<number, { applyCC: (v: number) => void }>;
    selectRefs: Record<number, { applyCC: (v: number) => void }>;
    /** Mode-contextual hints and readouts, keyed by param name. */
    sliderHints?: Record<string, string | undefined>;
    sliderDisplays?: Record<string, string | undefined>;
    /** Param names the current mode ignores — drawn greyed and inert. */
    disabledParams?: Set<string>;
  } = $props();

  const sections = $derived(layoutFor(moduleId, map));
  // Design width is per module — see PanelLayout.stageWidth.
  const stageW = $derived(stageWidthFor(moduleId));

  // ── Zoom to fit ────────────────────────────────────────────────────────────
  // The stage is laid out at stageW and transform-scaled. A transform does not
  // affect layout, so the wrapper is also given the scaled height explicitly —
  // without that the page would reserve the unscaled height and leave a gap
  // below the panel (or clip it, when scaled up).
  let frameEl = $state<HTMLElement | null>(null);
  let stageEl = $state<HTMLElement | null>(null);
  let scale = $state(1);
  let stageHeight = $state(0);

  $effect(() => {
    if (!frameEl || !stageEl) return;
    const frame = frameEl;
    const stage = stageEl;

    const measure = () => {
      // offsetHeight is the *layout* height, which a transform does not
      // affect — so this stays the stage's natural size however it is scaled,
      // and the fit below cannot oscillate.
      const natural = stage.offsetHeight;
      stageHeight = natural;

      const availW = frame.clientWidth;
      // Height left between the top of the stage and the pinned bottom dock.
      const availH =
        window.innerHeight - frame.getBoundingClientRect().top - BOTTOM_RESERVE;
      if (availW <= 0 || natural <= 0) return;

      // Fit both axes, not just width. Fitting width alone is what pushed the
      // effects band off the bottom of the screen on a wide monitor — the
      // panel got larger exactly when there was no more vertical room for it.
      const fit = Math.min(availW / stageW, availH / natural);
      scale = Math.min(MAX_SCALE, Math.max(MIN_SCALE, fit));
    };

    measure();
    // The frame tracks the window width; the stage's own height changes when a
    // section grows (a mode switch revealing a control, a legend wrapping).
    const ro = new ResizeObserver(measure);
    ro.observe(frame);
    ro.observe(stage);
    // Window height is not covered by either observer — the frame's height is
    // something this effect sets, so it cannot also be what it measures.
    window.addEventListener("resize", measure);
    return () => {
      ro.disconnect();
      window.removeEventListener("resize", measure);
    };
  });

  // The inline diagrams need a handful of specific parameters. Resolving them
  // by name rather than by CC number is what keeps them module-safe: CC 92 is
  // AlloyFlux's COLOR and Alloy Coil's revdecay, and the old code selecting on
  // the raw number showed one module's semantics under the other's controls.
  const ccOf = $derived(new Map(map.map((p) => [p.name, p.cc] as const)));

  /** Current float value of a named slider param, or `fallback`. */
  function val(name: string, fallback: number): number {
    const cc = ccOf.get(name);
    return cc === undefined ? fallback : (paramValues[cc] ?? fallback);
  }

  /** True when a named select param sits in the upper half of its CC range. */
  function isHigh(name: string): boolean {
    const cc = ccOf.get(name);
    return cc === undefined ? false : (selectValues[cc] ?? 0) >= 64;
  }

  // Name of the selected scale, read out of the parameter's own option list so
  // the indicator cannot disagree with the dropdown beside it.
  const scaleLabel = $derived.by(() => {
    const param = map.find((p) => p.name === "scale");
    const cc = param?.cc;
    if (!param?.options || cc === undefined) return undefined;
    const v = selectValues[cc] ?? 0;
    return param.options.find((o) => v >= o.ccMin && v <= o.ccMax)?.label;
  });
</script>

<div class="panel">
  <div class="frame" bind:this={frameEl}>
    <!-- Carries the *scaled* size as real layout, which the transformed stage
         inside it cannot: a transform never changes an element's layout box,
         so the stage still measures stageW wide at any zoom. Left to itself
         that overflowed the frame on every window narrower than the design
         width and put a horizontal scrollbar under content that visibly fit.
         With the box sized correctly, `margin: auto` also centres it, so the
         hand-rolled offset this used to need is gone. -->
    <div
      class="stage-box"
      style:width={`${stageW * scale}px`}
      style:height={`${stageHeight * scale}px`}
    >
      <div
        class="stage"
        bind:this={stageEl}
        style:width={`${stageW}px`}
        style:transform={`scale(${scale})`}
      >
        <div class="grid">
          {#each sections as section (section.title)}
            <PanelSection
              title={section.title}
              span={section.span}
              dialBox={section.dialBox}
              iconRow={section.iconRow}
              stretch={section.stretch}
            >
              {#if section.visual === "fxchain"}
                <div class="visual">
                  <FxChainVisual
                    filterPost={isHigh("fxfilterpos")}
                    delayPost={isHigh("fxdelaypos")}
                  />
                </div>
              {:else if section.visual === "scale"}
                <div class="visual scale">
                  <ScaleKeys
                    scaleCC={selectValues[ccOf.get("scale") ?? -1] ?? 0}
                    label={scaleLabel}
                  />
                </div>
              {:else if section.visual === "envelope"}
                <div class="visual">
                  <EnvelopeGraph
                    isAdsr={isHigh("envtype")}
                    attack={val("adsrattack", 0.05)}
                    decay={val("adsrdecay", 0.1)}
                    sustain={val("adsrsustain", 0.8)}
                    release={val("adsrrelease", 0.3)}
                    curve={val("curve", 0.5)}
                    curveTime={val("curvetime", 1.0)}
                  />
                </div>
              {/if}

              {#each section.items as { param, control } (param.cc)}
                {#if control.breakBefore}
                  <div class="break"></div>
                {/if}
                {#if param.type === "select"}
                  <PanelSelect
                    {param}
                    label={control.label}
                    variant={control.control === "knob"
                      ? undefined
                      : control.control}
                    initial={selectValues[param.cc]}
                    bind:this={selectRefs[param.cc]}
                    onchange={(v) => (selectValues[param.cc] = v)}
                  />
                {:else}
                  <Knob
                    {param}
                    label={control.label}
                    sub={control.sub}
                    size={control.size}
                    icons={control.icons}
                    disabled={disabledParams.has(param.name)}
                    bind:value={paramValues[param.cc]}
                    bind:this={sliderRefs[param.cc]}
                    hint={sliderHints[param.name]}
                    displayOverride={sliderDisplays[param.name]}
                  />
                {/if}
              {/each}
            </PanelSection>
          {/each}
        </div>
      </div>
    </div>
  </div>
</div>

<style>
  /* Contour texture, the flowing topographic lines silkscreened behind the
   * controls on the panel. Low-contrast on purpose — at full strength it
   * competes with the arcs, and the arcs are the thing you read. */
  .panel {
    position: relative;
    padding: 26px 20px 20px;
    background-image: radial-gradient(
        ellipse 120% 80% at 50% -10%,
        rgba(192, 137, 74, 0.05),
        transparent 60%
      ),
      repeating-linear-gradient(
        104deg,
        transparent 0 13px,
        rgba(192, 137, 74, 0.028) 13px 14px
      );
  }

  .frame {
    /* Only ever scrolls once the stage has hit MIN_SCALE and still does not
       fit — above that the scale absorbs the difference and .stage-box is
       narrower than the frame, so no scrollbar appears. */
    overflow-x: auto;
    overflow-y: hidden;
    margin: 1px 1px;
  }

  .stage-box {
    position: relative;
    margin: 0 auto;
  }

  .stage {
    /* Taken out of flow so it cannot push .stage-box, whose size is the scaled
       one set above; top-left origin so the scale is predictable. */
    position: absolute;
    top: 0;
    left: 0;
    transform-origin: top left;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(12, minmax(0, 1fr));
    gap: 24px 18px;
    /* Room for the section titles, which straddle the top rule and so stick
       out above their own box. The frame below clips vertically to keep the
       horizontal scroll honest, and without this the top band's legends lost
       their upper half to it. */
    padding-top: 10px;

    /* Sections take their natural height rather than stretching to the tallest
       in the row. Stretching kept the outlines tidy but left small groups —
       Voice, FX Chain, Chorus — as three controls adrift in a tall empty box,
       which reads as something missing rather than as alignment. */
    align-items: start;
  }

  /* Forces the next control onto a new line inside a section. */
  .break {
    flex-basis: 100%;
    height: 0;
    margin: 0;
  }

  /* Capped rather than full-bleed: the envelope drawing is 216 units wide and
     stretching it across a six-column section flattened the curve into a line
     with a lot of empty grid either side of it. */
  .visual {
    flex-basis: 100%;
    max-width: 450px;
    min-width: 0;
    margin-bottom: 2px;
  }

  /* The scale strip is a small fixed-size graphic, not a diagram that wants
     the section width. */
  .visual.scale {
    max-width: none;
    display: flex;
    justify-content: center;
  }
</style>
