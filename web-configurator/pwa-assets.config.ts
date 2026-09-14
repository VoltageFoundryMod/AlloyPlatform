import { defineConfig } from "@vite-pwa/assets-generator/config";

/**
 * PWA icon generation.
 *
 * Run with `make web-icons` (or `npm run icons`) — **not** on every build. The
 * outputs are committed, because rasterising needs `sharp`, and a native
 * dependency that must compile is exactly the wrong thing to put on the path of
 * `make web`. Re-run it only when the logo changes.
 *
 * ⚠ **Every background here is deliberate; do not fall back to the stock
 * preset.** `minimal2023Preset` renders the transparent sizes on transparent
 * and the maskable and apple ones on **white**, and the Alloy mark is pale mint
 * line-art — on white it is very nearly invisible, and the apple-touch-icon in
 * particular came out as a blank tile. The generator builds each asset by
 * creating a canvas of `resizeOptions.background` and compositing the logo onto
 * it, so naming the panel ground here is what gives the mark something to sit
 * against. It also means the source can stay an SVG with its own transparency.
 *
 * The paddings are retuned for the same reason the backgrounds are. The stock
 * 0.3 on maskable and apple is sized for a square wordmark; ours is a circular
 * badge, which reads as small and adrift once a launcher's own circular mask
 * crops it further.
 */

// --bg in app.css. Matches theme_color/background_color in vite.config.ts and
// backgroundColor in capacitor.config.ts — all four should move together.
const BG = "#131518";

export default defineConfig({
  // The anvil badge, not the VFM wordmark: it is square (165×165 — the VFM mark
  // is 626×584), it is the platform's mark rather than the company's, and as
  // line-art it stays legible at 64px where a wordmark would not.
  //
  // Outputs land next to the source image, which is why this points into
  // public/ rather than at a master under assets/.
  images: ["public/AlloyFlux_Logo.svg"],

  preset: {
    // favicon.ico plus the icons the web manifest lists as `purpose: any`.
    // Near full-bleed: these are shown at 16–64px in a tab strip, where any
    // margin is wasted pixels.
    transparent: {
      sizes: [64, 192, 512],
      favicons: [[48, "favicon.ico"]],
      padding: 0.05,
      resizeOptions: { fit: "contain", background: BG },
    },
    // `purpose: maskable`. Android crops this to whatever shape the launcher
    // uses — circle, squircle, teardrop — guaranteeing only the central 80%.
    // 0.2 keeps the badge inside that circle without shrinking it to a dot.
    maskable: {
      sizes: [512],
      padding: 0.2,
      resizeOptions: { fit: "contain", background: BG },
    },
    // The iOS home-screen icon, for a PWA added from Safari. iOS applies a
    // gentle squircle mask and nothing else, so this sits much closer to the
    // edge than the maskable one.
    apple: {
      sizes: [180],
      padding: 0.1,
      resizeOptions: { fit: "contain", background: BG },
    },
  },
});
