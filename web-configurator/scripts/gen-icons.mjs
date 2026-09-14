/**
 * Every icon the Controller ships, from one logo — M80.
 *
 * `make web-icons` runs this and nothing else. It owns the ordering, which
 * matters: step ⓷ repairs output from step ⓶, and step ⓸ consumes masters
 * written in step ⓵.
 *
 *   ⓵ Rasterise `public/AlloyFlux_Logo.svg` into the 1024² icon and 2732²
 *     splash masters that `@capacitor/assets` takes as its source.
 *   ⓶ Run `@vite-pwa/assets-generator` for the PWA set (favicon, apple-touch,
 *     the manifest icons). Configured by `pwa-assets.config.ts`.
 *   ⓷ Flatten the PWA icons it left transparent — see the warning below.
 *   ⓸ Run `@capacitor/assets` for the ~97 iOS and Android launcher sizes.
 *   ⓹ Delete the two files `@capacitor/assets` writes that we must not keep.
 *
 * Output is committed; this is not part of a normal build. Both generators pull
 * sharp, which has to compile, and a fresh clone's first `make web` has no
 * business failing on that.
 *
 * ⚠ **The Alloy mark is pale mint line-art and needs a dark ground under it.**
 * On white it is very nearly invisible — the stock apple-touch-icon came out a
 * blank tile. Everything here composites onto `BG`, and that is the single
 * reason this file is more than two generator invocations.
 */

import { execFileSync } from "node:child_process";
import { mkdirSync, rmSync, writeFileSync } from "node:fs";
import { createRequire } from "node:module";
import { dirname, join } from "node:path";
import sharp from "sharp";
import ico from "sharp-ico";

const SRC = "public/AlloyFlux_Logo.svg";
const BG_HEX = "#131518"; // --bg in app.css
const BG = { r: 0x13, g: 0x15, b: 0x18, alpha: 1 };
const TRANSPARENT = { r: 0, g: 0, b: 0, alpha: 0 };

const require = createRequire(import.meta.url);

/** Resolve a dependency's `bin` entry so it can be run under this same node. */
function binOf(pkg, bin) {
  const manifest = `${pkg}/package.json`;
  const entry = require(manifest).bin;
  return join(
    dirname(require.resolve(manifest)),
    typeof entry === "string" ? entry : entry[bin],
  );
}

/** Run a package's CLI in-process-tree, with no shell. */
function run(pkg, bin, args) {
  // Deliberately not npx. Two Windows reasons: `execFileSync` cannot spawn a
  // `.cmd` shim without a shell, and turning the shell on would make `#131518`
  // a comment the moment it reached anything sh-like. No shell, no quoting.
  execFileSync(process.execPath, [binOf(pkg, bin), ...args], {
    stdio: "inherit",
  });
}

/**
 * Smallest density that rasterises SRC to at least `target` pixels a side.
 *
 * ⚠ Not a constant, because the right value depends on how the SVG states its
 * own size and there is no way to know that from here. A unitless
 * `width="626"` and a `width="165mm"` with the same viewBox rasterise to wildly
 * different pixel counts at the same DPI — AlloyFlux_Logo.svg is in millimetres
 * and blows past sharp's input-pixel limit at the density the old VFM mark
 * needed. The relationship is not even linear in DPI, so this probes a ladder
 * rather than computing a multiplier, and stops at whatever last succeeded.
 */
async function densityFor(target) {
  let best = 96;
  for (const d of [96, 150, 200, 300, 450, 600, 900]) {
    try {
      const m = await sharp(SRC, { density: d }).metadata();
      best = d;
      if (Math.max(m.width, m.height) >= target) return d;
    } catch {
      return best; // hit the pixel limit; the previous rung is the best we get
    }
  }
  return best;
}

/** Render the logo centred on the panel ground at `size`, occupying `logo`. */
async function render(out, size, logo) {
  const pad = Math.round((size - logo) / 2);
  const image = await sharp(SRC, { density: await densityFor(logo) })
    .resize(logo, logo, { fit: "contain", background: TRANSPARENT })
    .extend({ top: pad, bottom: pad, left: pad, right: pad, background: BG })
    .flatten({ background: BG })
    .png()
    .toFile(out);
  console.log(`  ${out}  ${image.width}x${image.height}`);
}

// ── ⓵ masters ───────────────────────────────────────────────────────────────
mkdirSync("assets", { recursive: true });
console.log("Rasterising masters:");
// 72% of the canvas. iOS and Android both mask the icon to a rounded shape and
// Android crops it further for adaptive icons, so the mark needs to sit well
// inside the square or the launcher clips its corners.
await render("assets/icon.png", 1024, 736);
// Displayed at the device's own aspect, cropped from the centre — so the logo
// must be small enough to survive the narrowest crop.
await render("assets/splash.png", 2732, 600);

// ── ⓶ PWA set ───────────────────────────────────────────────────────────────
console.log("\nGenerating PWA icons:");
run("@vite-pwa/assets-generator", "pwa-assets-generator", []);

// ── ⓷ repair ────────────────────────────────────────────────────────────────
//
// ⚠ `generateTransparentAsset` hardcodes `{r:0,g:0,b:0,alpha:0}` as its canvas
// and ignores `resizeOptions.background` entirely — only the maskable and apple
// paths honour it. So the `purpose: any` icons and the favicon come out
// transparent however `pwa-assets.config.ts` is written, and a pale mint mark
// on a light tab strip or a light taskbar is invisible. There is no config
// option for this; flattening afterwards is the fix.
//
// Read to a buffer first: sharp cannot write to the file it is reading.
console.log("\nFlattening transparent PWA icons onto the panel ground:");
for (const f of [
  "public/pwa-64x64.png",
  "public/pwa-192x192.png",
  "public/pwa-512x512.png",
]) {
  const buf = await sharp(f).flatten({ background: BG }).png().toBuffer();
  writeFileSync(f, buf);
  console.log(`  ${f}`);
}

// favicon.ico is encoded straight from the same transparent source, so it needs
// the same treatment. 48px is what the generator's preset asks for.
const favicon = await sharp("public/pwa-64x64.png")
  .resize(48, 48, { fit: "contain", background: BG })
  .flatten({ background: BG })
  .png()
  .toBuffer();
writeFileSync("public/favicon.ico", ico.encode([favicon]));
console.log("  public/favicon.ico");

// ── ⓸ native launcher sets ──────────────────────────────────────────────────
console.log("\nGenerating native asset sets:");
run("@capacitor/assets", "capacitor-assets", [
  "generate",
  "--iconBackgroundColor", BG_HEX,
  "--iconBackgroundColorDark", BG_HEX,
  "--splashBackgroundColor", BG_HEX,
  "--splashBackgroundColorDark", BG_HEX,
]);

// ── ⓹ strays ────────────────────────────────────────────────────────────────
//
// ⚠ `@capacitor/assets` writes two things we must delete. `icons/*.webp` at the
// project root is merely unreferenced, since vite-plugin-pwa generates the web
// manifest. The other is actively harmful: a *partial*
// `public/manifest.webmanifest` holding two colour keys. Anything in `public/`
// is copied verbatim into `dist/`, so it lands on top of the real manifest and
// the PWA then installs with no name and no icons.
for (const stray of ["icons", "public/manifest.webmanifest"]) {
  rmSync(stray, { recursive: true, force: true });
}
console.log("\nRemoved @capacitor/assets strays (icons/, public/manifest.webmanifest)");
