import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import basicSsl from "@vitejs/plugin-basic-ssl";
import { VitePWA } from "vite-plugin-pwa";

/**
 * HTTPS, but only for `make web-host` — the phone-testing path.
 *
 * Web Bluetooth, Web MIDI and Web Serial are all secure-context APIs. Over
 * plain HTTP on a LAN IP the browser does not refuse them, it does not
 * *define* them, so every feature-detect in the Controller reports "not
 * supported" on an Android Chrome that supports them perfectly well.
 * basic-ssl's self-signed certificate is enough to make the origin secure —
 * accept the interstitial once on the phone.
 *
 * `make web-dev` is left alone deliberately: localhost is already a secure
 * context, so a self-signed cert there buys nothing and adds a warning to
 * click through on every session.
 *
 * ⚠ `server.https` is deliberately NOT set here. basicSsl() sets it itself,
 * and Vite types it as `https.ServerOptions` rather than a boolean — so
 * `https: isHost` runs fine but fails `tsc -p tsconfig.node.json`, which is
 * half of `make web-check`.
 */
export default defineConfig(() => {
  const isHost = process.argv.includes("--host");

  /**
   * Where the built site will be served from.
   *
   * ⚠ **Must stay `/` for everything except GitHub Pages.** Capacitor serves
   * the same `dist/` from the root of `capacitor://localhost`, so a build with
   * a sub-path base produces an app whose every asset 404s — a white screen,
   * with nothing in the UI to say why. `make app-sync` therefore never sets
   * this, and CI sets it only for the Pages job.
   *
   * Pages publishes a project site at `/<repo>/`, so the deploy workflow passes
   * `BASE_URL=/AlloyFlux/`. A trailing slash is required by both Vite and the
   * service worker scope below; the normalisation is here rather than in the
   * workflow so the rule lives with the thing that depends on it.
   */
  const rawBase = process.env.BASE_URL ?? "/";
  const base = rawBase.endsWith("/") ? rawBase : `${rawBase}/`;

  return {
    base,
    plugins: [
      svelte(),
      ...(isHost ? [basicSsl()] : []),

      /**
       * Offline support — Milestone 80.
       *
       * The Controller is a pure static bundle talking to hardware over a
       * radio: there is no backend, no API and nothing to be stale about, so
       * precaching the whole build is both the simplest strategy and the
       * correct one. Once the page has been opened once, it works with no
       * network at all — which is the normal case for a module on a stage or
       * in a room with no wifi.
       *
       * This build output is also what Capacitor wraps for the iOS and Android
       * apps, so the same `dist/` serves three targets. The service worker is
       * inert inside the native shells (the assets are already local) and
       * `PwaStatus.svelte` declines to register it there.
       */
      VitePWA({
        // `prompt`, not `autoUpdate`. An automatic reload is fine for a blog
        // and hostile here: it would drop a live GATT link and lose unsaved
        // panel state mid-session, potentially mid-performance. The user gets
        // a button and decides when.
        registerType: "prompt",
        injectRegister: null, // PwaStatus.svelte registers, so it can guard.

        // Dev is deliberately untouched, for the same reason the self-signed
        // cert is limited to `--host`: a service worker caching a dev build is
        // a source of "why is my edit not showing up" and buys nothing.
        devOptions: { enabled: false },

        // No `includeAssets`: everything in public/ is copied into dist/, and
        // the globPatterns below already precache every svg, png and ico there.
        // Listing files here as well only risks the list going stale — it
        // named VFM_Logo_Stripped.svg long after the UI stopped using it.

        manifest: {
          name: "Alloy Controller",
          short_name: "Alloy",
          description:
            "Editor and controller for Alloy Flux and Alloy Coil synthesiser " +
            "modules, over USB MIDI, serial or Bluetooth LE.",
          // Matches --bg in app.css. The browser paints this behind the page
          // during launch, so a mismatch shows as a flash of the wrong colour.
          theme_color: "#131518",
          background_color: "#131518",
          display: "standalone",
          orientation: "any",
          // Both follow `base`, and both must. A manifest claiming `scope: "/"`
          // while the app lives at `/AlloyFlux/` is out-of-scope by its own
          // declaration: Chrome refuses to install it, and the install button
          // in PwaStatus.svelte simply never appears — with no error anywhere.
          start_url: base,
          scope: base,
          categories: ["music", "utilities"],
          icons: [
            { src: "pwa-64x64.png", sizes: "64x64", type: "image/png" },
            { src: "pwa-192x192.png", sizes: "192x192", type: "image/png" },
            { src: "pwa-512x512.png", sizes: "512x512", type: "image/png" },
            {
              src: "maskable-icon-512x512.png",
              sizes: "512x512",
              type: "image/png",
              purpose: "maskable",
            },
          ],
        },

        workbox: {
          globPatterns: ["**/*.{js,css,html,svg,png,ico,woff,woff2}"],
          // The panel SVGs and the Svelte bundle together clear the 2 MiB
          // default comfortably; raising the ceiling keeps a large chunk from
          // being silently dropped from the precache, which would show up only
          // as a mysterious network fetch when offline.
          maximumFileSizeToCacheInBytes: 6 * 1024 * 1024,
          cleanupOutdatedCaches: true,
        },
      }),
    ],
  };
});
