import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import basicSsl from "@vitejs/plugin-basic-ssl";

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
  return {
    plugins: isHost ? [svelte(), basicSsl()] : [svelte()],
  };
});
