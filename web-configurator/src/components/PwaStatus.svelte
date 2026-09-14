<script lang="ts">
  /**
   * PwaStatus — install, offline and update affordances for the web build.
   *
   * Four states, at most one of which is ever on screen:
   *
   *   ⓵ **iOS browser** — a standing notice that this page cannot reach a
   *      module and the app is the way in. Not dismissible, because it is not
   *      an announcement, it is the answer to "why is everything greyed out".
   *   ⓶ **Update ready** — a new build is precached and waiting. The user
   *      chooses the moment; see the `registerType: "prompt"` note in
   *      vite.config.ts for why this is not automatic.
   *   ⓷ **Offline ready** — fired once, the first time the precache completes.
   *      Auto-dismisses; it is a reassurance, not a task.
   *   ⓸ **Install tip** — the browser has offered `beforeinstallprompt`. A
   *      transient nudge rather than a standing bar: it appears a moment after
   *      the page settles, goes away on its own, and remembers being refused.
   *      See the timing notes on the constants below.
   *
   * Inside the native shell this renders nothing at all and registers no
   * service worker: the assets are already local, there is no install to offer
   * and no update channel that is not the App Store.
   */
  import { onMount } from "svelte";
  import { registerSW } from "virtual:pwa-register";
  import {
    isIosBrowser,
    isNativeShell,
    isStandalone,
    requestPersistentStorage,
    installTipAllowed,
    noteInstallTipShown,
    dismissInstallTipForever,
    IOS_APP_STORE_URL,
  } from "../lib/platform";

  /** Let the panel render and the connection bar settle before interrupting. */
  const TIP_DELAY_MS = 2500;
  /** Long enough to read two lines and reach for the mouse, not a flash. */
  const TIP_VISIBLE_MS = 12000;
  /** After the pointer leaves again — they have seen it; do not re-read it. */
  const TIP_GRACE_MS = 4000;

  let offlineReady = $state(false);
  let updateReady = $state(false);
  let installEvent = $state<any>(null);
  let dismissed = $state(false);

  /** The tip is past its delay and not yet hidden. Gates rendering with
   *  `installEvent`, so the bar appears only once both are true. */
  let tipVisible = $state(false);
  /** Reflects the checkbox. Persisted the moment it is ticked. */
  let tipNever = $state(false);

  const native = isNativeShell();
  const iosBrowser = isIosBrowser();

  let updateSW: ((reload?: boolean) => Promise<void>) | null = null;

  let showTimer: ReturnType<typeof setTimeout> | null = null;
  let hideTimer: ReturnType<typeof setTimeout> | null = null;

  function clearTimers() {
    if (showTimer) clearTimeout(showTimer);
    if (hideTimer) clearTimeout(hideTimer);
    showTimer = hideTimer = null;
  }

  /** Hide for this visit. The persistent refusal is the checkbox, not this. */
  function hideTip() {
    clearTimers();
    tipVisible = false;
  }

  /**
   * Hold the auto-hide open while the pointer or keyboard focus is inside.
   *
   * Without this the "Don't show again" checkbox is close to unusable: the bar
   * would vanish partway through reading it, and a control that disappears
   * while being aimed at is worse than no control.
   */
  function holdTip() {
    if (hideTimer) {
      clearTimeout(hideTimer);
      hideTimer = null;
    }
  }

  function releaseTip() {
    if (!tipVisible) return;
    holdTip();
    hideTimer = setTimeout(hideTip, TIP_GRACE_MS);
  }

  function onNeverChange(e: Event) {
    tipNever = (e.currentTarget as HTMLInputElement).checked;
    // Written immediately rather than on dismiss: whatever happens next — the
    // timer firing, a reload, the tab closing — the choice is already recorded.
    if (tipNever) dismissInstallTipForever();
  }

  onMount(() => {
    if (native) return;

    // Presets are in localStorage and this is the cheap insurance against an
    // eviction sweep. Fire and forget — the result changes nothing we do.
    void requestPersistentStorage();

    updateSW = registerSW({
      onOfflineReady() {
        offlineReady = true;
        setTimeout(() => (offlineReady = false), 6000);
      },
      onNeedRefresh() {
        updateReady = true;
      },
    });

    // ⚠ The `beforeinstallprompt` listener lives in index.html, not here.
    // Chrome fires it once, as soon as the install criteria are met, and that
    // is routinely before this bundle has executed — a listener added in
    // onMount misses it and the tip never appears. The inline script stashes
    // the event on `window` and re-announces it as `alloy:installprompt`, so
    // this works whether we mounted before or after it fired.
    //
    // ⚠ It never fires when the app is already installed, and never on iOS (no
    // browser there implements it). So "already installed" is handled by the
    // browser declining to offer, by `isStandalone()`, and by `appinstalled`
    // below — three independent guards, because the first two are advisory:
    // `display-mode: standalone` is false in a plain tab of an installed app.
    const offer = () => {
      const pending = (window as any).__alloyInstallPrompt;
      if (!pending) return;
      installEvent = pending;

      if (isStandalone() || !installTipAllowed()) return;
      // The event can fire more than once in a session. Without this the second
      // one orphans the first timer, which then fires against a stale closure
      // and re-opens a tip the user has already dismissed.
      if (showTimer || tipVisible) return;

      showTimer = setTimeout(() => {
        showTimer = null;
        tipVisible = true;
        hideTimer = setTimeout(hideTip, TIP_VISIBLE_MS);
      }, TIP_DELAY_MS);
    };

    // Fired on installation from anywhere — our button, or the browser's own
    // menu. Either way there is nothing left to offer.
    const onInstalled = () => {
      installEvent = null;
      hideTip();
    };

    // Already fired before we mounted — the common case on a warm cache.
    offer();

    window.addEventListener("alloy:installprompt", offer);
    window.addEventListener("appinstalled", onInstalled);
    return () => {
      window.removeEventListener("alloy:installprompt", offer);
      window.removeEventListener("appinstalled", onInstalled);
      clearTimers();
    };
  });

  async function install() {
    if (!installEvent) return;
    hideTip();
    installEvent.prompt();
    await installEvent.userChoice;
    // Single-use by spec — the browser will fire a fresh event if the user
    // declines and later becomes eligible again. Clear the stash too, or a
    // later `offer()` would hand back an event that has already been consumed.
    installEvent = null;
    (window as any).__alloyInstallPrompt = null;
  }

  const showInstall = $derived(
    !native && !iosBrowser && tipVisible && installEvent !== null,
  );

  /**
   * Count a showing only once the tip is genuinely on screen.
   *
   * ⚠ Not at the point `tipVisible` is set. The four states share one slot
   * through an `{:else if}` chain, and the install tip is last — so an update
   * or offline-ready bar occupying that slot hides it completely. Counting on
   * the timer would then spend one of the user's three chances on a tip that
   * was never drawn, and after three such visits the offer would be gone
   * without ever having been made.
   */
  let counted = false;
  $effect(() => {
    if (showInstall && !counted) {
      counted = true;
      noteInstallTipShown();
    }
  });
</script>

{#if iosBrowser}
  <!-- ⓵ -->
  <div class="pwa-bar pwa-notice" role="status">
    <strong>Safari can't reach your module.</strong>
    <span>
      Apple's browser engine has no Bluetooth, MIDI or serial access, and every
      browser on iOS uses it — so switching browsers won't help. The Alloy
      Controller app connects natively.
    </span>
    {#if IOS_APP_STORE_URL}
      <a class="pwa-btn pwa-btn-accent" href={IOS_APP_STORE_URL}>Get the app</a>
    {/if}
  </div>
{:else if updateReady}
  <!-- ⓶ -->
  <div class="pwa-bar" role="status">
    <span>A new version of the Controller is ready.</span>
    <button class="pwa-btn pwa-btn-accent" onclick={() => updateSW?.(true)}>
      Reload
    </button>
    <button class="pwa-btn" onclick={() => (updateReady = false)}>Later</button>
  </div>
{:else if offlineReady && !dismissed}
  <!-- ⓷ -->
  <div class="pwa-bar" role="status">
    <span>Ready to work offline — this page no longer needs a network.</span>
    <button class="pwa-btn" onclick={() => (dismissed = true)}>Dismiss</button>
  </div>
{:else if showInstall}
  <!-- ⓸ — see holdTip() for why the pointer and focus handlers are here. -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="pwa-bar pwa-tip"
    role="status"
    onmouseenter={holdTip}
    onmouseleave={releaseTip}
    onfocusin={holdTip}
    onfocusout={releaseTip}
  >
    <span class="tip-text">
      <strong>Install for offline use.</strong>
      Runs in its own window and keeps working with no network.
    </span>
    <button class="pwa-btn pwa-btn-accent" onclick={install}>Install</button>
    <label class="tip-never">
      <input type="checkbox" checked={tipNever} onchange={onNeverChange} />
      Don't show again
    </label>
    <button class="pwa-btn pwa-btn-close" onclick={hideTip} aria-label="Dismiss">
      ×
    </button>
  </div>
{/if}

<style>
  /* Bottom-anchored rather than top: the panel is the subject of the page and
     a bar above it pushes every control down by its own height on load. */
  .pwa-bar {
    position: fixed;
    left: 50%;
    bottom: 1rem;
    transform: translateX(-50%);
    z-index: 50;
    display: flex;
    align-items: center;
    gap: 0.6rem;
    max-width: min(46rem, calc(100vw - 2rem));
    padding: 0.55rem 0.85rem;
    background: var(--bg-raised);
    border: 1px solid var(--hairline-strong);
    border-radius: 0.5rem;
    box-shadow: 0 0.5rem 1.5rem rgba(0, 0, 0, 0.45);
    font-size: 0.82rem;
    color: var(--text);
  }
  /* The iOS case is the one that wraps — it is two sentences, not a label. */
  .pwa-notice {
    flex-wrap: wrap;
    line-height: 1.45;
  }
  .pwa-notice strong {
    color: var(--copper);
  }
  /* The tip arrives on its own several seconds in, so it slides rather than
     appearing — an element that materialises under the pointer reads as a
     glitch. Short and eased out, so it does not feel like it is asking to be
     waited for. */
  .pwa-tip {
    animation: tip-in 220ms ease-out;
  }
  @keyframes tip-in {
    from {
      opacity: 0;
      transform: translate(-50%, 0.75rem);
    }
    to {
      opacity: 1;
      transform: translate(-50%, 0);
    }
  }
  /* Someone who has asked for less motion has asked for exactly this. */
  @media (prefers-reduced-motion: reduce) {
    .pwa-tip {
      animation: none;
    }
  }
  .pwa-tip strong {
    color: var(--copper);
    font-weight: 600;
  }
  .tip-text {
    line-height: 1.4;
  }
  .tip-never {
    display: flex;
    align-items: center;
    gap: 0.35rem;
    flex: none;
    color: var(--text-faint);
    font-size: 0.76rem;
    white-space: nowrap;
    cursor: pointer;
  }
  .tip-never input {
    accent-color: var(--copper);
    cursor: pointer;
    margin: 0;
  }
  .tip-never:hover {
    color: var(--text);
  }
  /* Square-ish and quiet: it is the least important control in the bar, and
     the auto-hide means most people will never need it. */
  .pwa-btn-close {
    padding: 0.15rem 0.45rem;
    background: transparent;
    border-color: transparent;
    color: var(--text-faint);
    font-size: 1rem;
    line-height: 1;
  }
  .pwa-btn-close:hover {
    background: var(--bg-sunken);
    color: var(--text);
  }
  .pwa-btn {
    flex: none;
    padding: 0.3rem 0.7rem;
    background: var(--bg-panel);
    border: 1px solid var(--hairline-strong);
    border-radius: 0.3rem;
    color: var(--text);
    font: inherit;
    text-decoration: none;
    cursor: pointer;
  }
  .pwa-btn:hover {
    background: var(--bg-sunken);
  }
  .pwa-btn-accent {
    background: rgba(192, 137, 74, 0.16);
    border-color: rgba(192, 137, 74, 0.45);
  }
  .pwa-btn-accent:hover {
    background: rgba(192, 137, 74, 0.26);
  }
  /* Matches the single focus ring ConnectionBar established for the app. The
     checkbox is included: it is the only way to refuse the tip permanently, so
     it has to be reachable by keyboard as well as pointer. */
  .pwa-btn:focus-visible,
  .tip-never input:focus-visible {
    outline: 2px solid var(--copper);
    outline-offset: 2px;
  }
  @media (max-width: 30rem) {
    .pwa-bar {
      flex-wrap: wrap;
    }
  }
</style>
